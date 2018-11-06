/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/*!
 * \file   nas_qos_cps_priority_group.cpp
 * \brief  NAS qos priority_group related CPS API routines
 * \date   05-2016
 * \author
 */

#include "cps_api_key.h"
#include "cps_api_operation.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"
#include "cps_api_db_interface.h"

#include "event_log_types.h"
#include "event_log.h"
#include "std_error_codes.h"
#include "std_mutex_lock.h"

#include "nas_switch.h"
#include "hal_if_mapping.h"

#include "nas_qos_common.h"
#include "nas_qos_switch_list.h"
#include "nas_qos_cps.h"
#include "dell-base-qos.h"
#include "nas_qos_priority_group.h"
#include "nas_if_utils.h"

#include <vector>

/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                        nas_qos_priority_group &priority_group);
static cps_api_return_code_t nas_qos_store_prev_attr(cps_api_object_t obj,
                                        const nas::attr_set_t attr_set,
                                        const nas_qos_priority_group &priority_group);
static cps_api_return_code_t nas_qos_cps_api_priority_group_set(
                                cps_api_object_t obj,
                                cps_api_object_t sav_obj);
static std_mutex_lock_create_static_init_rec(priority_group_mutex);
static void nas_qos_port_pg_fetch_from_hw(ndi_port_t ndi_port_id,
        ndi_obj_id_t ndi_priority_group_id,
        nas_qos_priority_group * port_pg);
static t_std_error nas_qos_port_priority_group_init_vp(hal_ifindex_t port_id);

/**
  * This function provides NAS-QoS priority_group CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_priority_group_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&priority_group_mutex);

    switch (op) {
    case cps_api_oper_CREATE:
    case cps_api_oper_DELETE:
        return NAS_QOS_E_FAIL; //not supported

    case cps_api_oper_SET:
        return nas_qos_cps_api_priority_group_set(obj, param->prev);

    default:
        return NAS_QOS_E_UNSUPPORTED;
    }
}

static cps_api_return_code_t nas_qos_cps_get_priority_group_info(
                                cps_api_get_params_t * param,
                                uint32_t switch_id, uint_t port_id,
                                bool match_local_id, uint_t local_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    uint_t count = p_switch->get_number_of_port_priority_groups(port_id);

    if  (count == 0) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "switch id %u, port id %u has no priority_groups\n",
                switch_id, port_id);

        return NAS_QOS_E_FAIL;
    }

    std::vector<nas_qos_priority_group *> pg_list(count);
    p_switch->get_port_priority_groups(port_id, count, &pg_list[0]);

    /* fill in data */
    cps_api_object_t ret_obj;

    for (uint_t i = 0; i < count; i++ ) {
        nas_qos_priority_group *priority_group = pg_list[i];

        // filter out unwanted priority_groups

        if (match_local_id && (priority_group->get_local_id() != local_id))
            continue;


        ret_obj = cps_api_object_list_create_obj_and_append(param->list);
        if (ret_obj == NULL) {
            return cps_api_ret_code_ERR;
        }

        cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
                BASE_QOS_PRIORITY_GROUP_OBJ,
                cps_api_qualifier_TARGET);
        uint32_t val_port = priority_group->get_port_id();
        uint8_t  val_local_id = priority_group->get_local_id();

        cps_api_set_key_data(ret_obj, BASE_QOS_PRIORITY_GROUP_PORT_ID,
                cps_api_object_ATTR_T_U32,
                &val_port, sizeof(uint32_t));
        cps_api_set_key_data(ret_obj, BASE_QOS_PRIORITY_GROUP_LOCAL_ID,
                cps_api_object_ATTR_T_BIN,
                &val_local_id, sizeof(uint8_t));

        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PRIORITY_GROUP_ID,
                priority_group->get_priority_group_id());

        // User configured objects
        cps_api_object_attr_add_u64(ret_obj,
                BASE_QOS_PRIORITY_GROUP_BUFFER_PROFILE_ID,
                priority_group->get_buffer_profile());

        // MMU indexes (1-based in NAS)
        for (uint_t idx = 0; idx < priority_group->get_shadow_pg_count(); idx++) {
            uint_t nas_mmu_idx = idx + 1;
            if (priority_group->get_shadow_pg_id(nas_mmu_idx) != NDI_QOS_NULL_OBJECT_ID)
                cps_api_object_attr_add_u32(ret_obj, BASE_QOS_PRIORITY_GROUP_MMU_INDEX_LIST, nas_mmu_idx);
        }
    }

    return cps_api_ret_code_OK;
}

/**
  * This function provides NAS-QoS priority_group CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_priority_group_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_PORT_ID);
    cps_api_object_attr_t local_id_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_LOCAL_ID);

    uint_t switch_id = 0;

    if (port_id_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Port Id must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint_t port_id = cps_api_object_attr_data_u32(port_id_attr);

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    bool local_id_specified = false;
    uint8_t local_id = 0;
    if (local_id_attr) {
        local_id = *(uint8_t *)cps_api_object_attr_data_bin(local_id_attr);
        local_id_specified = true;
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, port_id id %u\n",
                    switch_id, port_id);

    std_mutex_simple_lock_guard p_m(&priority_group_mutex);

    return nas_qos_cps_get_priority_group_info(param, switch_id, port_id,
                                            local_id_specified, local_id);
}




/**
  * This function provides NAS-QoS priority_group CPS API rollback function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_priority_group_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->prev,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&priority_group_mutex);

    if (op == cps_api_oper_SET) {
        nas_qos_cps_api_priority_group_set(obj, NULL);
    }

    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_cps_api_priority_group_set(
                                cps_api_object_t obj,
                                cps_api_object_t sav_obj)
{
    cps_api_object_t tmp_obj;

    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_PORT_ID);
    cps_api_object_attr_t local_id_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_LOCAL_ID);

    if (port_id_attr == NULL ||
        local_id_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Key incomplete in the message\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint32_t switch_id = 0;
    uint_t port_id = cps_api_object_attr_data_u32(port_id_attr);
    uint8_t local_id = *(uint8_t *)cps_api_object_attr_data_bin(local_id_attr);

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Modify switch id %u, port id %u,  local_id %u \n",
            switch_id, port_id,  local_id);


    nas_qos_priority_group_key_t key;
    key.port_id = port_id;
    key.local_id = local_id;

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "Switch %u not found\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_priority_group * priority_group_p = p_switch->get_priority_group(key);
    if (priority_group_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "priority_group not found in switch id %u\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    /* make a local copy of the existing priority_group */
    nas_qos_priority_group priority_group(*priority_group_p);

    cps_api_return_code_t rc = cps_api_ret_code_OK;
    if ((rc = nas_qos_cps_parse_attr(obj, priority_group)) != cps_api_ret_code_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Invalid information in the packet");
        return rc;
    }


    try {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Modifying switch id %u, port id %u priority_group info \n",
                switch_id, port_id);

        if (!nas_is_virtual_port(port_id)) {
            nas::attr_set_t modified_attr_list = priority_group.commit_modify(
                                        *priority_group_p, (sav_obj? false: true));


            // set attribute with full copy
            // save rollback info if caller requests it.
            // use modified attr list, current priority_group value
            if (sav_obj) {
                tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
                if (!tmp_obj) {
                    return cps_api_ret_code_ERR;
                }
                nas_qos_store_prev_attr(tmp_obj, modified_attr_list, *priority_group_p);
            }
        }

        // update the local cache with newly set values
        *priority_group_p = priority_group;

        // update DB
        if (cps_api_db_commit_one(cps_api_oper_SET, obj, nullptr, false) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Fail to store PG update to DB");
        }

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS priority_group Attr Modify error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS priority_group Modify Unexpected error code");
        return NAS_QOS_E_FAIL;
    }


    return cps_api_ret_code_OK;
}

/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_priority_group &priority_group)
{
    uint64_t val;
    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch (id) {
        case BASE_QOS_PRIORITY_GROUP_PORT_ID:
        case BASE_QOS_PRIORITY_GROUP_LOCAL_ID:
        case BASE_QOS_PRIORITY_GROUP_ID:
            break; // These are not settable from cps

        case BASE_QOS_PRIORITY_GROUP_BUFFER_PROFILE_ID:
            val = cps_api_object_attr_data_u64(it.attr);
            priority_group.set_buffer_profile(val);
            break;

        case CPS_API_ATTR_RESERVE_RANGE_END:
            // skip keys
            break;

        default:
            EV_LOGGING(QOS, NOTICE, "QOS", "Unrecognized option: %lu", id);
            return NAS_QOS_E_UNSUPPORTED;
        }
    }


    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_store_prev_attr(cps_api_object_t obj,
                                const nas::attr_set_t attr_set,
                                const nas_qos_priority_group &priority_group)
{
    // filling in the keys
    uint32_t val_port = priority_group.get_port_id();
    uint8_t local_id = priority_group.get_local_id();
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_PRIORITY_GROUP_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_PRIORITY_GROUP_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &val_port, sizeof(uint32_t));
    cps_api_set_key_data(obj, BASE_QOS_PRIORITY_GROUP_LOCAL_ID,
            cps_api_object_ATTR_T_BIN,
            &local_id, sizeof(uint8_t));


    for (auto attr_id: attr_set) {
        switch (attr_id) {
        case BASE_QOS_PRIORITY_GROUP_PORT_ID:
        case BASE_QOS_PRIORITY_GROUP_LOCAL_ID:
        case BASE_QOS_PRIORITY_GROUP_ID:
            /* non-settable attr     */
            break;

        case BASE_QOS_PRIORITY_GROUP_BUFFER_PROFILE_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                    priority_group.get_buffer_profile());
            break;


        default:
            break;
        }
    }

    return cps_api_ret_code_OK;
}


// create per-port, per-priority_group instance
static t_std_error create_port_priority_group(hal_ifindex_t port_id,
                                    ndi_port_t ndi_port_id,
                                    uint8_t local_id,
                                    ndi_obj_id_t ndi_priority_group_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "switch_id of ifindex: %u cannot be found/created",
                     port_id);
        return NAS_QOS_E_FAIL;
    }

    try {
        // create the priority_group and add the priority_group to switch
        nas_obj_id_t priority_group_id = p_switch->alloc_priority_group_id();
        nas_qos_priority_group_key_t key;
        key.port_id = port_id;
        key.local_id = local_id;
        nas_qos_priority_group pg (p_switch, key);

        pg.set_priority_group_id(priority_group_id);

        // get hw initial settings
        nas_qos_port_pg_fetch_from_hw(ndi_port_id, ndi_priority_group_id, &pg);

        EV_LOGGING(QOS, DEBUG, "QOS",
                     "NAS priority_group_id 0x%016lX is allocated for priority_group:"
                     "local_pg_id %u, ndi_priority_group_id 0x%016lX",
                     priority_group_id, local_id, ndi_priority_group_id);
        p_switch->add_priority_group(pg);

    }
    catch (...) {
        return NAS_QOS_E_FAIL;
    }

    return STD_ERR_OK;

}

static void nas_qos_port_pg_get_shadow_pg(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_pg_id,
                                nas_qos_priority_group *pg)
{
    static bool number_of_mmu_known = false;
    static uint_t number_of_mmu = 0;

    // clear up first
    pg->reset_shadow_pg_ids();

    // Get from SAI
    if (number_of_mmu_known == false) {

        number_of_mmu = ndi_qos_get_shadow_priority_group_list(
                            ndi_port_id.npu_id,
                            ndi_pg_id,
                            0, NULL);

        number_of_mmu_known = true;
    }

    if (number_of_mmu == 0)
        return;

    uint count = number_of_mmu;
    std::vector<ndi_obj_id_t> shadow_pg_list(count);

    if (ndi_qos_get_shadow_priority_group_list(
                            ndi_port_id.npu_id,
                            ndi_pg_id,
                            shadow_pg_list.size(),
                            &shadow_pg_list[0]) != count) {
        EV_LOGGING(QOS, ERR, "QOS-Q",
                "Shadow pgs get failed on npu_port %d, ndi_q_id 0x%016lx",
                ndi_port_id.npu_port, ndi_pg_id);
        return;
    }

    // Populate local cache
    for (uint_t i = 0; i< count; i++)
        pg->add_shadow_pg_id(shadow_pg_list[i]);

}


static void nas_qos_port_pg_fetch_from_hw(ndi_port_t ndi_port_id,
        ndi_obj_id_t ndi_priority_group_id,
        nas_qos_priority_group * port_pg)
{
    ndi_qos_priority_group_attribute_t ndi_pg = {0};

    if (ndi_qos_get_priority_group_attribute(ndi_port_id,
                                        ndi_priority_group_id,
                                        &ndi_pg)
            != STD_ERR_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Some Attribute is not supported by NDI for reading\n");
    }

    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return ;
    }

    port_pg->set_buffer_profile(p_switch->ndi2nas_buffer_profile_id(ndi_pg.buffer_profile, ndi_port_id.npu_id));

    port_pg->add_npu(ndi_port_id.npu_id);
    port_pg->set_ndi_port_id(ndi_port_id.npu_id, ndi_port_id.npu_port);
    port_pg->set_ndi_obj_id(ndi_priority_group_id);
    port_pg->mark_ndi_created();

    // get the shadow pg list
    nas_qos_port_pg_get_shadow_pg(ndi_port_id, ndi_priority_group_id, port_pg);


}

/*
 * This function handles the ifindex to NPU-Port association,
 * pushing the saved DB configuration to npu.
 * @Param ifindex
 * @Param ndi_port
 * @Param isAdd: true if establishing a physical port association
 *               false if dissolve the virtual port to physical port association
 * @Return
 */
void nas_qos_port_priority_group_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd)
{
    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-PG",
                     "switch_id cannot be found with npu_id %d",
                     ndi_port_id.npu_id);
        return ;
    }

    /* get NAS-initialized number of PGs on ifindex */
    uint_t pg_num = p_switch->get_number_of_port_priority_groups(ifindex);
    std::vector<nas_qos_priority_group *> pg_list(pg_num);
    p_switch->get_port_priority_groups(ifindex, pg_num, &pg_list[0]);


    if (isAdd == false) {
        EV_LOGGING(QOS, NOTICE, "QOS-PG", "Disassociation ifindex %d", ifindex);

        // clear up all pg's of ifindex and re-init new ones ; will load DB for VP config
        p_switch->delete_pg_by_ifindex(ifindex);
        nas_qos_port_priority_group_init_vp(ifindex);
    }
    else {
        EV_LOGGING(QOS, NOTICE, "QOS-PG", "Association ifindex %d to npu port %d",
                ifindex, ndi_port_id.npu_port);

        /* get ndi_pg_id list */
        /* get the number of priority_groups per port */
        uint_t no_of_priority_group = ndi_qos_get_number_of_priority_groups(ndi_port_id);
        if (no_of_priority_group == 0) {
            EV_LOGGING(QOS, INFO, "QOS",
                         "No priority_groups for npu_id %u, npu_port_id %u ",
                         ndi_port_id.npu_id, ndi_port_id.npu_port);
            return;
        }

        if (pg_num != no_of_priority_group) {
            EV_LOGGING(QOS, ERR, "QOS",
                    "Number of PG initialized %d is not equal to number of ndi PG %d",
                    pg_num, no_of_priority_group);
            return;
        }

        /* get the list of ndi_priority_group id list */
        std::vector<ndi_obj_id_t> ndi_priority_group_id_list(no_of_priority_group);
        if (ndi_qos_get_priority_group_id_list(ndi_port_id, no_of_priority_group,
                                               &ndi_priority_group_id_list[0]) !=
                no_of_priority_group) {
            EV_LOGGING(QOS, NOTICE, "QOS",
                         "Fail to retrieve all priority_groups of npu_id %u, npu_port_id %u ",
                         ndi_port_id.npu_id, ndi_port_id.npu_port);
            return;
        }

        // update npu port association and read the hardware initial settings
        for (uint_t i = 0; i< pg_num; i++) {
            nas_qos_port_pg_fetch_from_hw(ndi_port_id, ndi_priority_group_id_list[i], pg_list[i]);
        }
    }

    // push DB to NPU
    cps_api_object_guard _og(cps_api_object_create());
    if(!_og.valid()){
        EV_LOGGING(QOS,ERR,"QOS-DB-GET","Failed to create object for db get");
        return;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(_og.get()),
            BASE_QOS_PRIORITY_GROUP_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(_og.get(), BASE_QOS_PRIORITY_GROUP_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &ifindex, sizeof(uint32_t));
    cps_api_object_list_guard lst(cps_api_object_list_create());
    if (cps_api_db_get(_og.get(),lst.get())==cps_api_ret_code_OK) {
        size_t len = cps_api_object_list_size(lst.get());

        for (uint_t idx = 0; idx < len; idx++){
            cps_api_object_t db_obj = cps_api_object_list_get(lst.get(),idx);

            cps_api_key_set_attr(cps_api_object_key(db_obj), cps_api_oper_SET);

            // push the DB to NPU
            nas_qos_cps_api_priority_group_set(db_obj, NULL);

            EV_LOGGING(QOS, NOTICE,"QOS-DB",
                    "One Port Priority Group DB record on port %d written to NPU",
                    ifindex);

        }
    }
}

static t_std_error nas_qos_port_priority_group_init_vp(hal_ifindex_t port_id)
{

    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return NAS_QOS_E_FAIL;
    }

    /* 8 dot1p priority */
    for (uint_t idx = 0; idx < 8; idx++) {

        try {
            // create the priority_group and add the priority_group to switch
            nas_obj_id_t priority_group_id = p_switch->alloc_priority_group_id();
            nas_qos_priority_group_key_t key;
            key.port_id = port_id;
            key.local_id = idx;
            nas_qos_priority_group pg (p_switch, key);

            pg.set_priority_group_id(priority_group_id);

            EV_LOGGING(QOS, DEBUG, "QOS",
                         "NAS priority_group_id 0x%016lX is allocated for priority_group:"
                         "local_pg_id %u, on VP %d",
                         priority_group_id, idx, port_id);
           p_switch->add_priority_group(pg);

        }
        catch (...) {
            return NAS_QOS_E_FAIL;
        }

    }

    return STD_ERR_OK;
}



/* This function initializes the priority_groups of a port
 * @Return standard error code
 */
t_std_error nas_qos_port_priority_group_init(hal_ifindex_t ifindex, ndi_port_t ndi_port_id)
{

    if (nas_is_virtual_port(ifindex))
        return nas_qos_port_priority_group_init_vp(ifindex);

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-PG",
                     "switch_id of npu_id: %u cannot be found/created",
                     ndi_port_id.npu_id);
        return NAS_QOS_E_FAIL;
    }

    /* get the number of priority_groups per port */
    uint_t no_of_priority_group = ndi_qos_get_number_of_priority_groups(ndi_port_id);
    if (no_of_priority_group == 0) {
        EV_LOGGING(QOS, INFO, "QOS",
                     "No priority_groups for npu_id %u, npu_port_id %u ",
                     ndi_port_id.npu_id, ndi_port_id.npu_port);
        return STD_ERR_OK;
    }

    /* get the list of ndi_priority_group id list */
    std::vector<ndi_obj_id_t> ndi_priority_group_id_list(no_of_priority_group);
    if (ndi_qos_get_priority_group_id_list(ndi_port_id, no_of_priority_group,
                                           &ndi_priority_group_id_list[0]) !=
            no_of_priority_group) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "Fail to retrieve all priority_groups of npu_id %u, npu_port_id %u ",
                     ndi_port_id.npu_id, ndi_port_id.npu_port);
        return NAS_QOS_E_FAIL;
    }

    /* Create priority_groups with nas_priority_group_key     */
    for (uint_t idx = 0; idx < no_of_priority_group; idx++) {

        // Internally create NAS priority_group nodes and add to NAS QOS
        if (create_port_priority_group(ifindex, ndi_port_id, idx,
                    ndi_priority_group_id_list[idx]) != STD_ERR_OK) {
            EV_LOGGING(QOS, NOTICE, "QOS",
                         "Not able to create ifindex %u, local_id %u",
                         ifindex, idx);
            return NAS_QOS_E_FAIL;
        }
    }

    return STD_ERR_OK;

}

static uint64_t get_stats_by_type(const nas_qos_priority_group_stat_counter_t *p,
                                BASE_QOS_PRIORITY_GROUP_STAT_t id)
{
    switch (id) {
    case BASE_QOS_PRIORITY_GROUP_STAT_PACKETS:
        return (p->packets);
    case BASE_QOS_PRIORITY_GROUP_STAT_BYTES:
        return (p->bytes);
    case BASE_QOS_PRIORITY_GROUP_STAT_CURRENT_OCCUPANCY_BYTES:
        return (p->current_occupancy_bytes);
    case BASE_QOS_PRIORITY_GROUP_STAT_WATERMARK_BYTES:
        return (p->watermark_bytes);
    case BASE_QOS_PRIORITY_GROUP_STAT_SHARED_CURRENT_OCCUPANCY_BYTES:
        return (p->shared_current_occupancy_bytes);
    case BASE_QOS_PRIORITY_GROUP_STAT_SHARED_WATERMARK_BYTES:
        return (p->shared_watermark_bytes);
    case BASE_QOS_PRIORITY_GROUP_STAT_XOFF_ROOM_CURRENT_OCCUPANCY_BYTES:
        return (p->xoff_room_current_occupancy_bytes);
    case BASE_QOS_PRIORITY_GROUP_STAT_XOFF_ROOM_WATERMARK_BYTES:
        return (p->xoff_room_watermark_bytes);
    default:
        return 0;
    }
}


/**
  * This function provides NAS-QoS priority_group stats CPS API read function
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_priority_group_stat_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{

    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_STAT_PORT_ID);
    cps_api_object_attr_t local_id_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_STAT_LOCAL_ID);

    if (port_id_attr == NULL ||
        local_id_attr == NULL ) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Incomplete key: port-id, priority_group local id must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    uint32_t switch_id = 0;
    nas_qos_priority_group_key_t key;
    key.port_id = cps_api_object_attr_data_u32(port_id_attr);
    key.local_id = *(uint8_t *)cps_api_object_attr_data_bin(local_id_attr);

    cps_api_object_attr_t mmu_index_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_STAT_MMU_INDEX);
    uint_t nas_mmu_index = (mmu_index_attr? cps_api_object_attr_data_u32(mmu_index_attr): 0);

    if (nas_is_virtual_port(key.port_id))
        return NAS_QOS_E_FAIL;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, port_id %u priority_group local id %u stat\n",
            switch_id, key.port_id, key.local_id);

    std_mutex_simple_lock_guard p_m(&priority_group_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_priority_group * priority_group_p = p_switch->get_priority_group(key);
    if (priority_group_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Priority Group not found");
        return NAS_QOS_E_FAIL;
    }

    nas_qos_priority_group_stat_counter_t stats = {0};
    std::vector<BASE_QOS_PRIORITY_GROUP_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch (id) {
        case BASE_QOS_PRIORITY_GROUP_STAT_PORT_ID:
        case BASE_QOS_PRIORITY_GROUP_STAT_LOCAL_ID:
            break;

        case BASE_QOS_PRIORITY_GROUP_STAT_PACKETS:
        case BASE_QOS_PRIORITY_GROUP_STAT_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_CURRENT_OCCUPANCY_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_WATERMARK_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_SHARED_CURRENT_OCCUPANCY_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_SHARED_WATERMARK_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_XOFF_ROOM_CURRENT_OCCUPANCY_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_XOFF_ROOM_WATERMARK_BYTES:
            counter_ids.push_back((BASE_QOS_PRIORITY_GROUP_STAT_t)id);
            break;

        default:
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown priority_group STAT flag: %lu, ignored", id);
            break;
        }
    }

    if (counter_ids.size() == 0) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Port PG stats get without any valid counter ids");
        return NAS_QOS_E_FAIL;
    }

    ndi_obj_id_t ndi_pg_id = priority_group_p->ndi_obj_id();
    if (nas_mmu_index != 0)
        ndi_pg_id = priority_group_p->get_shadow_pg_id(nas_mmu_index);

    if (ndi_qos_get_priority_group_stats(priority_group_p->get_ndi_port_id(),
                                ndi_pg_id,
                                &counter_ids[0],
                                counter_ids.size(),
                                &stats) != STD_ERR_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Priority Group stats get failed");
        return NAS_QOS_E_FAIL;
    }

    // return stats objects to cps-app
    cps_api_object_t ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL) {
        return cps_api_ret_code_ERR;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_PRIORITY_GROUP_STAT_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_PRIORITY_GROUP_STAT_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &(key.port_id), sizeof(uint32_t));
    cps_api_set_key_data(ret_obj, BASE_QOS_PRIORITY_GROUP_STAT_LOCAL_ID,
            cps_api_object_ATTR_T_BIN,
            &(key.local_id), sizeof(uint8_t));

    uint64_t val64;
    for (uint_t i=0; i< counter_ids.size(); i++) {
        BASE_QOS_PRIORITY_GROUP_STAT_t id = counter_ids[i];
        val64 = get_stats_by_type(&stats, id);
        cps_api_object_attr_add_u64(ret_obj, id, val64);
    }

    return  cps_api_ret_code_OK;

}


/**
  * This function provides NAS-QoS priority_group stats CPS API clear function
  * To clear the priority_group stats, set relevant counters to zero
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_priority_group_stat_clear (void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    if (op != cps_api_oper_SET)
        return NAS_QOS_E_UNSUPPORTED;

    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_STAT_PORT_ID);
    cps_api_object_attr_t local_id_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_STAT_LOCAL_ID);

    if (port_id_attr == NULL ||
        local_id_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Incomplete key: port-id, priority_group local id must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    uint32_t switch_id = 0;
    nas_qos_priority_group_key_t key;
    key.port_id = cps_api_object_attr_data_u32(port_id_attr);
    key.local_id = *(uint8_t *)cps_api_object_attr_data_bin(local_id_attr);

    cps_api_object_attr_t mmu_index_attr = cps_api_get_key_data(obj, BASE_QOS_PRIORITY_GROUP_STAT_MMU_INDEX);
    uint_t nas_mmu_index = (mmu_index_attr? cps_api_object_attr_data_u32(mmu_index_attr): 0);

    if (nas_is_virtual_port(key.port_id))
        return NAS_QOS_E_FAIL;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, port_id %u priority_group local_id %u stat\n",
            switch_id, key.port_id,  key.local_id);

    std_mutex_simple_lock_guard p_m(&priority_group_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_priority_group * priority_group_p = p_switch->get_priority_group(key);
    if (priority_group_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Priority Group not found");
        return NAS_QOS_E_FAIL;
    }

    std::vector<BASE_QOS_PRIORITY_GROUP_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch (id) {
        case BASE_QOS_PRIORITY_GROUP_STAT_PORT_ID:
        case BASE_QOS_PRIORITY_GROUP_STAT_LOCAL_ID:
            break;

        case BASE_QOS_PRIORITY_GROUP_STAT_PACKETS:
        case BASE_QOS_PRIORITY_GROUP_STAT_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_WATERMARK_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_SHARED_WATERMARK_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_XOFF_ROOM_WATERMARK_BYTES:
            counter_ids.push_back((BASE_QOS_PRIORITY_GROUP_STAT_t)id);
            break;

        case BASE_QOS_PRIORITY_GROUP_STAT_CURRENT_OCCUPANCY_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_SHARED_CURRENT_OCCUPANCY_BYTES:
        case BASE_QOS_PRIORITY_GROUP_STAT_XOFF_ROOM_CURRENT_OCCUPANCY_BYTES:
            // READ-only
            break;

        default:
            EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                    "Unknown priority_group STAT flag: %lu, ignored", id);
            break;
        }
    }

    if (counter_ids.size() == 0) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Port PG stats clear without any valid counter ids");
        return NAS_QOS_E_FAIL;
    }

    ndi_obj_id_t ndi_pg_id = priority_group_p->ndi_obj_id();
    if (nas_mmu_index != 0)
        ndi_pg_id = priority_group_p->get_shadow_pg_id(nas_mmu_index);

    if (ndi_qos_clear_priority_group_stats(priority_group_p->get_ndi_port_id(),
                                ndi_pg_id,
                                &counter_ids[0],
                                counter_ids.size()) != STD_ERR_OK) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Priority Group stats clear failed");
        return NAS_QOS_E_FAIL;
    }


    return  cps_api_ret_code_OK;

}
