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

#include "cps_api_key.h"
#include "cps_api_operation.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"
#include "cps_api_db_interface.h"

#include "event_log_types.h"
#include "event_log.h"
#include "std_error_codes.h"
#include "std_mutex_lock.h"

#include "hal_if_mapping.h"
#include "nas_qos_common.h"
#include "nas_qos_switch_list.h"
#include "nas_qos_cps.h"
#include "dell-base-qos.h"
#include "nas_qos_port_egress.h"
#include "nas_if_utils.h"

static std_mutex_lock_create_static_init_rec(port_eg_mutex);
static cps_api_return_code_t nas_qos_cps_api_port_eg_set(
                                            cps_api_object_t obj,
                                            cps_api_object_list_t sav_obj);
static void nas_qos_port_egress_fetch_from_hw(ndi_port_t ndi_port_id,
        nas_qos_port_egress * port_egr);

static cps_api_return_code_t nas_qos_cps_parse_attr(cps_api_object_t obj,
                                                nas_qos_port_egress& port_eg)
{
    uint64_t lval;
    cps_api_object_it_t it;

    cps_api_object_it_begin(obj, &it);
    for ( ; cps_api_object_it_valid(&it); cps_api_object_it_next(&it)) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch(id) {
        case BASE_QOS_PORT_EGRESS_SWITCH_ID:
        case BASE_QOS_PORT_EGRESS_PORT_ID:
            break;
        case BASE_QOS_PORT_EGRESS_BUFFER_LIMIT:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            port_eg.set_buffer_limit(lval);
            break;
        case BASE_QOS_PORT_EGRESS_WRED_PROFILE_ID:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            port_eg.set_wred_profile_id(lval);
            break;
        case BASE_QOS_PORT_EGRESS_SCHEDULER_PROFILE_ID:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            port_eg.set_scheduler_profile_id(lval);
            break;
        case BASE_QOS_PORT_EGRESS_NUM_UNICAST_QUEUE:
        case BASE_QOS_PORT_EGRESS_NUM_MULTICAST_QUEUE:
        case BASE_QOS_PORT_EGRESS_NUM_QUEUE:
        case BASE_QOS_PORT_EGRESS_QUEUE_ID_LIST:
            // READ-Only object
            break;
        case BASE_QOS_PORT_EGRESS_TC_TO_QUEUE_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_TC_TO_QUEUE);
            port_eg.set_tc_to_queue_map(lval);
            break;
        case BASE_QOS_PORT_EGRESS_TC_TO_DOT1P_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_TC_TO_DOT1P);
            port_eg.set_tc_to_dot1p_map(lval);
            break;
        case BASE_QOS_PORT_EGRESS_TC_TO_DSCP_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_TC_TO_DSCP);
            port_eg.set_tc_to_dscp_map(lval);
            break;
        case BASE_QOS_PORT_EGRESS_TC_COLOR_TO_DOT1P_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_TC_COLOR_TO_DOT1P);
            port_eg.set_tc_color_to_dot1p_map(lval);
            break;
        case BASE_QOS_PORT_EGRESS_TC_COLOR_TO_DSCP_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_TC_COLOR_TO_DSCP);
            port_eg.set_tc_color_to_dscp_map(lval);
            break;
        case BASE_QOS_PORT_EGRESS_PFC_PRIORITY_TO_QUEUE_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_PFC_TO_QUEUE);
            port_eg.set_pfc_priority_to_queue_map(lval);
            break;
        case BASE_QOS_PORT_EGRESS_BUFFER_PROFILE_ID_LIST:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_eg.add_buffer_profile_id(lval);
            port_eg.mark_attr_dirty(id);
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
                                                    const nas::attr_set_t& attr_set,
                                                    const nas_qos_port_egress& port_eg)
{
    // filling in the keys
    hal_ifindex_t port_id = port_eg.get_port_id();
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_PORT_EGRESS_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_PORT_EGRESS_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &port_id, sizeof(uint32_t));


    for (auto attr_id: attr_set) {
        switch (attr_id) {
        case BASE_QOS_PORT_EGRESS_PORT_ID:
            /* key */
            break;
        case BASE_QOS_PORT_EGRESS_BUFFER_LIMIT:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    port_eg.get_buffer_limit());
            break;
        case BASE_QOS_PORT_EGRESS_WRED_PROFILE_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    port_eg.get_wred_profile_id());
            break;
        case BASE_QOS_PORT_EGRESS_SCHEDULER_PROFILE_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    port_eg.get_scheduler_profile_id());
            break;
        case BASE_QOS_PORT_EGRESS_NUM_UNICAST_QUEUE:
        case BASE_QOS_PORT_EGRESS_NUM_MULTICAST_QUEUE:
        case BASE_QOS_PORT_EGRESS_NUM_QUEUE:
        case BASE_QOS_PORT_EGRESS_QUEUE_ID_LIST:
            /* READ only */
            break;
        case BASE_QOS_PORT_EGRESS_TC_TO_QUEUE_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_eg.get_tc_to_queue_map()));
            break;
        case BASE_QOS_PORT_EGRESS_TC_TO_DOT1P_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_eg.get_tc_to_dot1p_map()));
            break;
        case BASE_QOS_PORT_EGRESS_TC_TO_DSCP_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_eg.get_tc_to_dscp_map()));
            break;
        case BASE_QOS_PORT_EGRESS_TC_COLOR_TO_DOT1P_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_eg.get_tc_color_to_dot1p_map()));
            break;
        case BASE_QOS_PORT_EGRESS_TC_COLOR_TO_DSCP_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_eg.get_tc_color_to_dscp_map()));
            break;
        case BASE_QOS_PORT_EGRESS_PFC_PRIORITY_TO_QUEUE_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_eg.get_pfc_priority_to_queue_map()));
            break;
        case BASE_QOS_PORT_EGRESS_BUFFER_PROFILE_ID_LIST:
            for (uint_t i= 0; i< port_eg.get_buffer_profile_id_count(); i++) {
                cps_api_object_attr_add_u64(obj, attr_id,
                                 port_eg.get_buffer_profile_id(i));
            }
            break;

        default:
            break;
        }
    }

    return cps_api_ret_code_OK;
}

// Create an empty place holder for VP
static t_std_error nas_qos_port_egress_init_vp(hal_ifindex_t port_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return NAS_QOS_E_FAIL;
    }

    try {
        nas_qos_port_egress port_eg(p_switch, port_id);

        // init vp's q_id_list
        std::vector<nas_obj_id_t> nas_q_id_list;
        uint_t q_id_count = p_switch->get_port_queue_ids(port_id, 0, NULL);
        if (q_id_count) {
            nas_q_id_list.resize(q_id_count);
            (void) (p_switch->get_port_queue_ids(port_id, q_id_count, &nas_q_id_list[0]));
        }
        for (uint_t idx = 0; idx < q_id_count; idx ++) {
            port_eg.add_queue_id(nas_q_id_list[idx]);
        }

        uint_t ucast_queues = p_switch->get_number_of_port_queues_by_type(port_id, BASE_QOS_QUEUE_TYPE_UCAST);
        port_eg.set_num_unicast_queue(ucast_queues);
        uint_t mcast_queues = p_switch->get_number_of_port_queues_by_type(port_id, BASE_QOS_QUEUE_TYPE_MULTICAST);
        port_eg.set_num_multicast_queue(mcast_queues);

        p_switch->add_port_egress(port_eg);


    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                "Exception on creating nas_qos_port_egress object for VP");
        return NAS_QOS_E_FAIL;
    }

    return STD_ERR_OK;
}

/*
 * This function initializes an egress port  node
 * @Return Standard error code
 */
t_std_error nas_qos_port_egress_init(hal_ifindex_t port_id, ndi_port_t ndi_port_id)
{
    if (nas_is_virtual_port(port_id))
        return nas_qos_port_egress_init_vp(port_id);

    EV_LOGGING(QOS, DEBUG, "QOS",
            "Create port egress profile: port %d\n",
            port_id);

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Failed to get switch by npu: %d\n",
                   ndi_port_id.npu_id);
        return NAS_QOS_E_FAIL;
    }

    try {
        nas_qos_port_egress port_eg(p_switch, port_id);

        nas_qos_port_egress_fetch_from_hw(ndi_port_id, &port_eg);

        p_switch->add_port_egress(port_eg);

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                "Exception on creating nas_qos_port_egress object");
        return NAS_QOS_E_FAIL;
    }


    return STD_ERR_OK;
}

static void nas_qos_port_egress_fetch_from_hw(ndi_port_t ndi_port_id,
        nas_qos_port_egress * port_egr)
{
    static const int MAX_QUEUE_ID_NUM = 128;
    static const int MAX_BUFFER_POOL_ID_NUM = 32;

    ndi_obj_id_t queue_id_list[MAX_QUEUE_ID_NUM] = {0};
    ndi_obj_id_t buf_prof_id_list[MAX_BUFFER_POOL_ID_NUM] = {0};

    qos_port_egr_struct_t ndi_info;

    memset(&ndi_info, 0, sizeof(qos_port_egr_struct_t));
    ndi_info.queue_id_list = queue_id_list;
    ndi_info.num_queue_id = MAX_QUEUE_ID_NUM;
    ndi_info.buffer_profile_list = buf_prof_id_list;
    ndi_info.num_buffer_profile = MAX_BUFFER_POOL_ID_NUM;

    BASE_QOS_PORT_EGRESS_t attr_list[] = {
        BASE_QOS_PORT_EGRESS_TC_TO_QUEUE_MAP,
        BASE_QOS_PORT_EGRESS_TC_TO_DOT1P_MAP,
        BASE_QOS_PORT_EGRESS_TC_TO_DSCP_MAP,
        BASE_QOS_PORT_EGRESS_TC_COLOR_TO_DOT1P_MAP,
        BASE_QOS_PORT_EGRESS_TC_COLOR_TO_DSCP_MAP,
        BASE_QOS_PORT_EGRESS_WRED_PROFILE_ID,
        BASE_QOS_PORT_EGRESS_SCHEDULER_PROFILE_ID,
        BASE_QOS_PORT_EGRESS_NUM_QUEUE,
        BASE_QOS_PORT_EGRESS_QUEUE_ID_LIST,
        BASE_QOS_PORT_EGRESS_PFC_PRIORITY_TO_QUEUE_MAP,
        BASE_QOS_PORT_EGRESS_BUFFER_PROFILE_ID_LIST,
    };
    int attr_num = sizeof(attr_list)/sizeof(attr_list[0]);
    for (int idx = 0; idx < attr_num; idx ++) {
        // Get attributes one at a time so that we can skip any unsupported SAI attributes
        // without stopping the rest of the attributes
        int rc = ndi_qos_get_port_egr_profile(ndi_port_id.npu_id,
                                        ndi_port_id.npu_port,
                                        &attr_list[idx], 1,
                                        &ndi_info);
        if (rc != STD_ERR_OK) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                    "Attribute %u is not supported by NDI for reading\n", attr_list[idx]);
            if (attr_list[idx] ==  BASE_QOS_PORT_EGRESS_QUEUE_ID_LIST) {
                // in case of q-query failure, do not return any junk q-id.
                ndi_info.num_queue_id = 0;
            }
            if (attr_list[idx] == BASE_QOS_PORT_EGRESS_BUFFER_PROFILE_ID_LIST) {
                // in case of failure, do not return any junk values
                ndi_info.num_buffer_profile = 0;
            }
        }
    }

    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return ;
    }

    port_egr->set_buffer_limit(ndi_info.buffer_limit);
    port_egr->set_wred_profile_id(p_switch->ndi2nas_wred_id(ndi_info.wred_profile_id, ndi_port_id.npu_id));
    port_egr->set_num_unicast_queue(ndi_info.num_ucast_queue);
    port_egr->set_num_multicast_queue(ndi_info.num_mcast_queue);
    for (uint_t idx = 0; idx < ndi_info.num_queue_id; idx ++) {
        port_egr->add_queue_id(p_switch->ndi2nas_queue_id(ndi_info.queue_id_list[idx]));
    }

    port_egr->set_tc_to_queue_map(p_switch->ndi2nas_map_id(ndi_info.tc_to_queue_map, ndi_port_id.npu_id));
    port_egr->set_tc_to_dot1p_map(p_switch->ndi2nas_map_id(ndi_info.tc_to_dot1p_map, ndi_port_id.npu_id));
    port_egr->set_tc_to_dscp_map(p_switch->ndi2nas_map_id(ndi_info.tc_to_dscp_map, ndi_port_id.npu_id));
    port_egr->set_tc_color_to_dot1p_map(p_switch->ndi2nas_map_id(ndi_info.tc_to_dot1p_map, ndi_port_id.npu_id));
    port_egr->set_tc_color_to_dscp_map(p_switch->ndi2nas_map_id(ndi_info.tc_to_dscp_map, ndi_port_id.npu_id));
    port_egr->set_scheduler_profile_id(p_switch->ndi2nas_scheduler_profile_id(ndi_info.scheduler_profile_id, ndi_port_id.npu_id));
    port_egr->set_pfc_priority_to_queue_map(p_switch->ndi2nas_map_id(ndi_info.pfc_priority_to_queue_map, ndi_port_id.npu_id));
    for (uint_t idx = 0; idx < ndi_info.num_buffer_profile; idx ++) {
        port_egr->add_buffer_profile_id(p_switch->ndi2nas_buffer_profile_id(ndi_info.buffer_profile_list[idx], ndi_port_id.npu_id));
    }

    port_egr->add_npu(ndi_port_id.npu_id);
    port_egr->set_ndi_port_id(ndi_port_id.npu_id, ndi_port_id.npu_port);
    port_egr->mark_ndi_created();

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
void nas_qos_port_egress_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd)
{
    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-EGR",
                     "switch_id cannot be found with npu_id %d",
                     ndi_port_id.npu_id);
        return ;
    }

    if (isAdd == false) {
        EV_LOGGING(QOS, NOTICE, "QOS-EGR", "Disassociation ifindex %d", ifindex);

        // clear up and re-init a new one ; will load DB for VP config
        p_switch->remove_port_egress(ifindex);
        nas_qos_port_egress_init_vp(ifindex);
    }
    else {
        EV_LOGGING(QOS, NOTICE, "QOS-EGR", "Association ifindex %d to npu port %d",
                ifindex, ndi_port_id.npu_port);

        // update npu mapping
        nas_qos_port_egress * port_egr = p_switch->get_port_egress(ifindex);

        // update port_egr with npu-specific readings
        nas_qos_port_egress_fetch_from_hw(ndi_port_id, port_egr);
    }

    // read DB and push to NPU
    cps_api_object_guard _og(cps_api_object_create());
    if(!_og.valid()){
        EV_LOGGING(QOS,ERR,"QOS-DB-GET","Failed to create object for db get");
        return;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(_og.get()),
            BASE_QOS_PORT_EGRESS_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(_og.get(), BASE_QOS_PORT_EGRESS_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &ifindex, sizeof(uint32_t));
    cps_api_object_list_guard lst(cps_api_object_list_create());
    if (cps_api_db_get(_og.get(),lst.get())==cps_api_ret_code_OK) {
        size_t len = cps_api_object_list_size(lst.get());

        if(len){
            // Get should return only one object matching to the interface
            if (len > 1) {
                EV_LOGGING(QOS,WARNING,"QOS-DB-GET",
                        "More than one entry (%lu) with the same index %d in DB",
                        len, ifindex);
            }
            cps_api_object_t db_obj = cps_api_object_list_get(lst.get(),0);

            cps_api_key_set_attr(cps_api_object_key(db_obj), cps_api_oper_SET);

            // push the DB to NPU
            nas_qos_cps_api_port_eg_set(db_obj, NULL);

            EV_LOGGING(QOS, NOTICE,"QOS-DB", "One Egress Port DB record for port %d written to NPU", ifindex);

        }
        else {
            EV_LOGGING(QOS, NOTICE, "QOS-DB-GET",
                    "No DB entry with the same index %d in DB",
                    ifindex);
        }
    }
}


static cps_api_return_code_t get_cps_obj_switch_port_id(cps_api_object_t obj,
                                                uint_t& switch_id, uint_t& port_id)
{
    cps_api_object_attr_t attr;

    switch_id = 0;

    attr = cps_api_get_key_data(obj, BASE_QOS_PORT_EGRESS_PORT_ID);
    if (attr == NULL) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "port id not specified in message\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    port_id = cps_api_object_attr_data_u32(attr);

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_port_eg_set(
                                            cps_api_object_t obj,
                                            cps_api_object_list_t sav_obj)
{
    uint_t switch_id = 0, port_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    if ((rc = get_cps_obj_switch_port_id(obj, switch_id, port_id)) !=
            cps_api_ret_code_OK) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "Failed to get switch and port id");
        return rc;
    }

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modify switch id %u, port id %u\n",
                    switch_id, port_id);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "Switch %u not found\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_port_egress* port_eg_p = p_switch->get_port_egress(port_id);
    if (port_eg_p == NULL) {
        return NAS_QOS_E_FAIL;
    }

    /* make a local copy of the existing port egress */
    nas_qos_port_egress port_eg(*port_eg_p);

    if ((rc = nas_qos_cps_parse_attr(obj, port_eg)) != cps_api_ret_code_OK) {
        return rc;
    }

    try {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modifying port egress %u attr \n",
                     port_eg.get_port_id());

        if (!nas_is_virtual_port(port_id) &&
            port_eg_p->is_created_in_ndi()) {
            nas::attr_set_t modified_attr_list = port_eg.commit_modify(*port_eg_p,
                                                        (sav_obj? false: true));

            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "done with commit_modify \n");


            // set attribute with full copy
            // save rollback info if caller requests it.
            // use modified attr list, current port egress value
            if (sav_obj) {
                cps_api_object_t tmp_obj;
                tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
                if (tmp_obj == NULL) {
                    return cps_api_ret_code_ERR;
                }

                nas_qos_store_prev_attr(tmp_obj, modified_attr_list, *port_eg_p);
            }
        }

        // update the local cache with newly set values
        *port_eg_p = port_eg;

        // update DB
        if (cps_api_db_commit_one(cps_api_oper_SET, obj, nullptr, false) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Fail to store egress port update to DB");
        }

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS PORT EGRESS Attr Modify error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS PORT EGRESS Modify Unexpected error code");
        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}

/*
  * This function provides NAS-QoS PORT-EGRESS CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_egress_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&port_eg_mutex);

    switch (op) {
    case cps_api_oper_CREATE:
    case cps_api_oper_DELETE:
        // Port egress requires no creation or deletion
        return NAS_QOS_E_FAIL;

    case cps_api_oper_SET:
        return nas_qos_cps_api_port_eg_set(obj, param->prev);

    default:
        return NAS_QOS_E_UNSUPPORTED;
    }
}

/*
  * This function provides NAS-QoS PORT-EGRESS CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_egress_read(void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj;
    uint_t switch_id = 0, port_id = 0;
    uint8_t bval;
    uint32_t idx, queue_id_count;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    obj = cps_api_object_list_get(param->filters, ix);
    if (obj == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Object not exist\n");
        return cps_api_ret_code_ERR;
    }
    if ((rc = get_cps_obj_switch_port_id(obj, switch_id, port_id)) !=
            cps_api_ret_code_OK) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "Failed to get switch and port id");
        return rc;
    }

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, port id %u\n",
                 switch_id, port_id);

    std_mutex_simple_lock_guard p_m(&port_eg_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "Switch %u not found\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_port_egress *port_eg = p_switch->get_port_egress(port_id);
    if (port_eg == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Failed to get port egress object, port_id=%d\n",
                   port_id);
        return NAS_QOS_E_FAIL;
    }

    /* fill in data */
    cps_api_object_t ret_obj;

    ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL){
        return cps_api_ret_code_ERR;
    }

    // @todo: handle getting individual attributes.
    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_PORT_EGRESS_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_PORT_EGRESS_SWITCH_ID,
            cps_api_object_ATTR_T_U32,
            &switch_id, sizeof(uint32_t));
    cps_api_set_key_data(ret_obj, BASE_QOS_PORT_EGRESS_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &port_id, sizeof(uint32_t));

    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_BUFFER_LIMIT,
                                port_eg->get_buffer_limit());
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_WRED_PROFILE_ID,
                                port_eg->get_wred_profile_id());
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_SCHEDULER_PROFILE_ID,
                                port_eg->get_scheduler_profile_id());
    bval = port_eg->get_num_unicast_queue();
    cps_api_object_attr_add(ret_obj, BASE_QOS_PORT_EGRESS_NUM_UNICAST_QUEUE, &bval, 1);
    bval = port_eg->get_num_multicast_queue();
    cps_api_object_attr_add(ret_obj, BASE_QOS_PORT_EGRESS_NUM_MULTICAST_QUEUE, &bval, 1);
    bval = port_eg->get_queue_id_count();
    cps_api_object_attr_add(ret_obj, BASE_QOS_PORT_EGRESS_NUM_QUEUE, &bval, 1);
    queue_id_count = port_eg->get_queue_id_count();
    for (idx = 0; idx < queue_id_count; idx ++) {
        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_QUEUE_ID_LIST,
                                    port_eg->get_queue_id(idx));
    }
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_TC_TO_QUEUE_MAP,
            GET_LOCAL_MAP_ID(port_eg->get_tc_to_queue_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_TC_TO_DOT1P_MAP,
            GET_LOCAL_MAP_ID(port_eg->get_tc_to_dot1p_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_TC_TO_DSCP_MAP,
            GET_LOCAL_MAP_ID(port_eg->get_tc_to_dscp_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_TC_COLOR_TO_DOT1P_MAP,
            GET_LOCAL_MAP_ID(port_eg->get_tc_color_to_dot1p_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_TC_COLOR_TO_DSCP_MAP,
            GET_LOCAL_MAP_ID(port_eg->get_tc_color_to_dscp_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_PFC_PRIORITY_TO_QUEUE_MAP,
            GET_LOCAL_MAP_ID(port_eg->get_pfc_priority_to_queue_map()));
    for (idx = 0; idx < port_eg->get_buffer_profile_id_count(); idx++) {
        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_EGRESS_BUFFER_PROFILE_ID_LIST,
                                    port_eg->get_buffer_profile_id(idx));
    }

    return cps_api_ret_code_OK;
}

/*
  * This function provides NAS-QoS PORT-EGRESS CPS API rollback function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_egress_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    if (obj == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Object not exist\n");
        return cps_api_ret_code_ERR;
    }
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&port_eg_mutex);

    if (op == cps_api_oper_SET) {
        nas_qos_cps_api_port_eg_set(obj, NULL);
    }

    // create/delete are not allowed for queue, no roll-back is needed

    return cps_api_ret_code_OK;
}

/* Debugging and unit testing */
void dump_nas_qos_port_egress(nas_switch_id_t switch_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);

    if (p_switch) {
        p_switch->dump_all_port_egr_profile();
    }
}
