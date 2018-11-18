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
 * \file   nas_qos_cps_scheduler_group.cpp
 * \brief  NAS qos scheduler_group related CPS API routines
 * \date   05-2015
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

#include "nas_qos_common.h"
#include "nas_qos_switch_list.h"
#include "nas_qos_cps.h"
#include "dell-base-qos.h"
#include "nas_qos_scheduler_group.h"
#include "nas_switch.h"
#include "hal_if_mapping.h"
#include "nas_qos_cps_queue.h"
#include "nas_qos_cps_scheduler_group.h"
#include "nas_if_utils.h"

/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_scheduler_group &scheduler_group);
static cps_api_return_code_t nas_qos_store_prev_attr(cps_api_object_t obj,
                                                const nas::attr_set_t attr_set,
                                                const nas_qos_scheduler_group &scheduler_group);
static nas_qos_scheduler_group * nas_qos_cps_get_scheduler_group(uint_t switch_id,
                                           nas_obj_id_t scheduler_group_id);
static cps_api_return_code_t nas_qos_cps_api_scheduler_group_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_scheduler_group_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_scheduler_group_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);

static std_mutex_lock_create_static_init_rec(scheduler_group_mutex);

#define MAX_SG_CHILD_NUM 64
#define DEFAULT_LOWEST_ID 0xFF

static uint8_t get_lowest_child_index(nas_qos_switch * p_switch, npu_id_t npu_id,
                    uint32_t child_level, ndi_obj_id_t *child_list, uint32_t count)
{
    uint32_t i;
    uint8_t lowest_id = DEFAULT_LOWEST_ID;

    for (i = 0; i< count; i++) {
        // search queue object
        nas_qos_queue * queue = p_switch->get_queue_by_id(child_list[i]);
        if (queue != NULL) {
            if (queue->get_local_queue_id() < lowest_id)
                lowest_id = queue->get_local_queue_id();
            continue;
        }

        // search SG object
        nas_qos_scheduler_group *sg_p = p_switch->get_scheduler_group_by_id(npu_id, child_list[i]);
        if (sg_p != NULL) {
            uint64_t sg_id = sg_p->get_scheduler_group_id();
            if (!IS_SG_ID_AUTO_FORMED(sg_id))
                continue;

            if (NAS_QOS_GET_SG_LOCAL_INDEX(sg_id) < lowest_id)
                lowest_id = NAS_QOS_GET_SG_LOCAL_INDEX(sg_id);

            continue;
        }
    }

    return lowest_id;
}

// This is used only during initialization to create a local SG node based on SAI default init
static t_std_error create_port_scheduler_group(hal_ifindex_t port_id,
                                    ndi_port_t ndi_port_id,
                                    ndi_obj_id_t ndi_sg_id,
                                    const ndi_qos_scheduler_group_struct_t& sg_info,
                                    nas_obj_id_t& nas_sg_id)
{

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-SG",
                     "switch_id of ifindex: %u cannot be found/created",
                     port_id);
        return NAS_QOS_E_FAIL;
    }

    try {
        // create the scheduler-group and add it to switch

        // Use specific SG_ID while reading from SAI default
        uint8_t local_sg_index = get_lowest_child_index(p_switch, ndi_port_id.npu_id,
                            sg_info.level+1, sg_info.child_list, sg_info.child_count);
        if (local_sg_index == DEFAULT_LOWEST_ID) {
            EV_LOGGING(QOS, DEBUG, "QOS", "No valid lowest child index. Assigning random index");

            // use randomly generated index
            nas_sg_id = p_switch->alloc_scheduler_group_id();
        }
        else {
            nas_sg_id = NAS_QOS_FORMAT_SG_ID(port_id, sg_info.level, local_sg_index);
        }

        EV_LOGGING(QOS, DEBUG, "QOS",
                    "Allocate nas-sg-id 0x%016lX for port_id %d, level %d, local_sg_index %d",
                    nas_sg_id, port_id, sg_info.level, local_sg_index);
        nas_qos_scheduler_group sg(p_switch);

        sg.set_scheduler_group_id(nas_sg_id);
        sg.add_npu(ndi_port_id.npu_id);
        sg.set_port_id(port_id);
        sg.set_ndi_obj_id(ndi_port_id.npu_id, ndi_sg_id);
        sg.set_level(sg_info.level);
        sg.set_max_child(sg_info.max_child);
        sg.clear_all_dirty_flags();
        sg.mark_ndi_created();

        // sg.parent and sg.child_list will be constructed later after
        // all queues and SG nodes are read from SAI

        p_switch->add_scheduler_group(sg);

    }
    catch (...) {
        return NAS_QOS_E_FAIL;
    }

    return STD_ERR_OK;
}

static void nas_qos_port_scheduler_group_disassociation(hal_ifindex_t ifindex, ndi_port_t ndi_port_id)
{

    EV_LOGGING(QOS, NOTICE, "QOS-SG", "Disassociation ifindex %d from npu %d/port %d",
            ifindex, ndi_port_id.npu_id, ndi_port_id.npu_port);

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-SG",
                     "switch_id cannot be found with npu_id %d",
                     ndi_port_id.npu_id);
        return ;
    }

    // get NAS-initialized SG at Queue's parent level on ifindex
    std::vector<nas_qos_scheduler_group *> sg_list;
    p_switch->get_port_scheduler_groups(ifindex,
                                    p_switch->max_sched_group_level - 1, //0-based
                                    sg_list);

    // Clear data from the node for future re-init
    // Mark the node non-ndi-created
    for (auto sg : sg_list) {
        sg->reset_ndi_obj_id(ndi_port_id.npu_id);
        sg->clear_all_dirty_flags();
        sg->reset_npus();
        sg->mark_ndi_removed();
    }
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
void nas_qos_port_scheduler_group_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd)
{
    if (isAdd == false) {
        return nas_qos_port_scheduler_group_disassociation(ifindex, ndi_port_id);
    }

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-SG",
                     "switch_id cannot be found with npu_id %d",
                     ndi_port_id.npu_id);
        return ;
    }

    // get NAS-initialized SG at Queue's parent level on ifindex
    std::vector<nas_qos_scheduler_group *> sg_list;
    p_switch->get_port_scheduler_groups(ifindex,
                                    p_switch->max_sched_group_level - 1, //0-based
                                    sg_list);
    uint_t sg_num = sg_list.size();

    EV_LOGGING(QOS, NOTICE, "QOS-SG", "Association ifindex %d to npu port %d",
                ifindex, ndi_port_id.npu_port);

    // get ndi-port SG ids from NPU
    /* get the number of SG per port */
    uint_t sg_count = ndi_qos_get_number_of_scheduler_groups(ndi_port_id);
    if (sg_count == 0) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "No scheduler-group for npu_id %u, npu_port_id %u ",
                     ndi_port_id.npu_id, ndi_port_id.npu_port);
        return ;
    }

    /* get the list of ndi_scheduler_group_id list of the port */
    std::vector<ndi_obj_id_t> ndi_sg_id_list(sg_count);
    if (ndi_qos_get_scheduler_group_id_list(ndi_port_id, sg_count, &ndi_sg_id_list[0]) !=
            sg_count) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "Fail to retrieve all scheduler-groups of npu_id %u, npu_port_id %u ",
                     ndi_port_id.npu_id, ndi_port_id.npu_port);
        return ;
    }

    // update NAS-SG with NPU info and push the DB to NPU
    for (uint_t idx = 0; idx < sg_count; idx++) {

        ndi_qos_scheduler_group_struct_t sg_info = {0};
        nas_attr_id_t attr_list[] = {
            BASE_QOS_SCHEDULER_GROUP_CHILD_COUNT,
            BASE_QOS_SCHEDULER_GROUP_CHILD_LIST,
            BASE_QOS_SCHEDULER_GROUP_PORT_ID,
            BASE_QOS_SCHEDULER_GROUP_LEVEL,
            BASE_QOS_SCHEDULER_GROUP_MAX_CHILD,
            BASE_QOS_SCHEDULER_GROUP_SCHEDULER_PROFILE_ID,
            BASE_QOS_SCHEDULER_GROUP_PARENT
        };
        t_std_error rc;

        ndi_obj_id_t ndi_sg_id = ndi_sg_id_list[idx];
        ndi_obj_id_t child_id_list[MAX_SG_CHILD_NUM];
        sg_info.child_count = sizeof(child_id_list) / sizeof(child_id_list[0]);
        sg_info.child_list = child_id_list;

        rc = ndi_qos_get_scheduler_group(ndi_port_id.npu_id, ndi_sg_id,
                                         attr_list, sizeof(attr_list)/sizeof(attr_list[0]),
                                         &sg_info);
        if (rc != STD_ERR_OK) {
            EV_LOGGING(QOS, NOTICE, "QOS",
                         "Failed to get attributes of scheduler-group %lu", ndi_sg_id);
            continue;
        }

        if (sg_info.level != p_switch->max_sched_group_level - 1)
            continue; // only queue's parent SG is initialized in NAS

        // locate the corresponding NAS SG
        uint8_t local_sg_index = get_lowest_child_index(p_switch,
                                    ndi_port_id.npu_id,
                                    sg_info.level+1,
                                    sg_info.child_list,
                                    sg_info.child_count);

        for (uint_t j=0; j< sg_num; j++) {
            nas_qos_scheduler_group * sg = sg_list[j];
            nas_obj_id_t nas_sg_id = sg->get_scheduler_group_id();

            if (NAS_QOS_GET_SG_LEVEL(nas_sg_id) != sg_info.level ||
                NAS_QOS_GET_SG_LOCAL_INDEX(nas_sg_id) != local_sg_index)
                continue;

            // Found a match; update NPU mapping
            sg->add_npu(ndi_port_id.npu_id);
            sg->set_ndi_obj_id(ndi_port_id.npu_id, ndi_sg_id);
            sg->set_max_child(sg_info.max_child);
            sg->clear_all_dirty_flags();
            sg->mark_ndi_created();


            // push DB to NPU
            cps_api_object_guard _og(cps_api_object_create());
            if(!_og.valid()){
                EV_LOGGING(QOS,ERR,"QOS-DB-GET","Failed to create object for db get");
                return;
            }

            cps_api_key_from_attr_with_qual(cps_api_object_key(_og.get()),
                    BASE_QOS_SCHEDULER_GROUP_OBJ,
                    cps_api_qualifier_TARGET);
            cps_api_set_key_data(_og.get(), BASE_QOS_SCHEDULER_GROUP_ID,
                    cps_api_object_ATTR_T_U64,
                    &nas_sg_id, sizeof(uint64_t));

            cps_api_object_list_guard lst(cps_api_object_list_create());
            if (cps_api_db_get(_og.get(),lst.get())==cps_api_ret_code_OK) {
                size_t len = cps_api_object_list_size(lst.get());

                for (uint_t idx = 0; idx < len; idx++){
                    cps_api_object_t db_obj = cps_api_object_list_get(lst.get(),idx);

                    cps_api_key_set_attr(cps_api_object_key(db_obj), cps_api_oper_SET);

                    // push the DB to NPU
                    nas_qos_cps_api_scheduler_group_set(db_obj, NULL);

                    EV_LOGGING(QOS, NOTICE,"QOS-DB", "One Scheduler Group DB record written to NPU");

                }
            }

            break;
        } // end of NAS-SG id matching search
    } // end of ndi-sg-id list walk
}

static t_std_error create_port_scheduler_group_list(const std::vector<ndi_obj_id_t> ndi_sg_id_list,
                                                    hal_ifindex_t ifindex,
                                                    ndi_port_t ndi_port_id,
                                                    parent_map_t& parent_map)
{
    ndi_qos_scheduler_group_struct_t sg_info;
    ndi_obj_id_t child_id_list[MAX_SG_CHILD_NUM];
    nas_attr_id_t attr_list[] = {
        BASE_QOS_SCHEDULER_GROUP_CHILD_COUNT,
        BASE_QOS_SCHEDULER_GROUP_CHILD_LIST,
        BASE_QOS_SCHEDULER_GROUP_PORT_ID,
        BASE_QOS_SCHEDULER_GROUP_LEVEL,
        BASE_QOS_SCHEDULER_GROUP_MAX_CHILD,
        BASE_QOS_SCHEDULER_GROUP_SCHEDULER_PROFILE_ID,
        BASE_QOS_SCHEDULER_GROUP_PARENT
    };
    t_std_error rc;
    nas_obj_id_t nas_sg_id;

    for (auto it= ndi_sg_id_list.rbegin(); it != ndi_sg_id_list.rend(); ++it) {
        ndi_obj_id_t ndi_sg_id = *it;

        EV_LOGGING(QOS, DEBUG, "QOS",
                        "SG id 0x%016lx is being fetched for port %d",
                        ndi_sg_id, ifindex);

        memset(&sg_info, 0, sizeof(sg_info));
        sg_info.child_count = sizeof(child_id_list) / sizeof(child_id_list[0]);
        sg_info.child_list = child_id_list;
        rc = ndi_qos_get_scheduler_group(ndi_port_id.npu_id, ndi_sg_id,
                                         attr_list, sizeof(attr_list)/sizeof(attr_list[0]),
                                         &sg_info);
        if (rc != STD_ERR_OK) {
            EV_LOGGING(QOS, NOTICE, "QOS",
                         "Failed to get attributes of scheduler-group %lu", ndi_sg_id);
            continue;
        }

        rc = create_port_scheduler_group(ifindex, ndi_port_id, ndi_sg_id, sg_info, nas_sg_id);
        if (rc != STD_ERR_OK) {
            EV_LOGGING(QOS, NOTICE, "QOS",
                         "Failed to create scheduler-group object");
            continue;
        }
        EV_LOGGING(QOS, DEBUG, "QOS",
                     "Created scheduler-group object: ndi_id 0x%lx nas_id 0x%lx level %d, parent_ndi_id 0x%lx",
                     ndi_sg_id, nas_sg_id, sg_info.level, sg_info.parent);

        parent_map[nas_sg_id] = sg_info.parent;
    }

    return STD_ERR_OK;
}

static t_std_error setup_scheduler_group_hierarchy(npu_id_t npu_id,
                                                   const parent_map_t& parent_map)
{
    nas_obj_id_t nas_child_id, nas_parent_id;
    ndi_obj_id_t ndi_parent_id;

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "switch_id of npu_id: %u cannot be found/created",
                     npu_id);
        return NAS_QOS_E_FAIL;
    }

    for (auto& it: parent_map) {
        nas_child_id = it.first;
        ndi_parent_id = it.second;

        nas_qos_scheduler_group *parent_sg_p = p_switch->get_scheduler_group_by_id(npu_id, ndi_parent_id);

        if (parent_sg_p == NULL) {
            nas_parent_id = 0;
        }
        else {
            nas_parent_id = parent_sg_p->get_scheduler_group_id();
        }

        if (p_switch->is_queue_id_obj(nas_child_id)) {
            nas_qos_queue *queue = p_switch->get_queue(nas_child_id);
            if (!queue) {
                EV_LOGGING(QOS, ERR, "QOS",
                         "Couldn't find queue object for id %lu", nas_child_id);
                return NAS_QOS_E_FAIL;
            }

            if (nas_parent_id == 0) {
                EV_LOGGING(QOS, NOTICE, "QOS",
                    "Queue missing a parent scheduler-group object, ndi_obj_id 0x%lx", ndi_parent_id);
            }

            queue->set_parent(nas_parent_id);
            queue->clear_all_dirty_flags();
        }
        else {
            nas_qos_scheduler_group *sg_p = p_switch->get_scheduler_group(nas_child_id);
            if (!sg_p) {
                EV_LOGGING(QOS, ERR, "QOS",
                             "Couldn't find scheduler-group object for id %lu", nas_child_id);
                return NAS_QOS_E_FAIL;
            }

            if (nas_parent_id == 0 && sg_p->get_level() != 0) {
                EV_LOGGING(QOS, NOTICE, "QOS",
                        "Level %d Scheduler Group missing a parent scheduler-group object, ndi_obj_id 0x%lx",
                          sg_p->get_level(), ndi_parent_id);
            }

            // update nas-assigned parent-id
            sg_p->set_parent(nas_parent_id);
            sg_p->clear_all_dirty_flags();
        }


        if (parent_sg_p) {
            // update parent's child-list with nas-ids.
            parent_sg_p->add_child(nas_child_id);

            // clear the dirty flag because this information is read from SAI, not going to SAI
            parent_sg_p->clear_all_dirty_flags();
        }
    }


    return STD_ERR_OK;
}

t_std_error nas_qos_port_scheduler_group_init_vp(hal_ifindex_t port_id)
{
    // Initialize the L2 SGs only and link them with the Queues.
    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "switch_id of ifindex: %u cannot be found/created",
                     port_id);
        return NAS_QOS_E_FAIL;
    }

    // Create a place holder for parent SG node of queues
    uint_t max_sg_level_id = p_switch->max_sched_group_level - 1; // id: 0-based
    for (uint_t idx = 0; idx < p_switch->ucast_queues_per_port; idx++) {
        try {
            nas_obj_id_t nas_sg_id = NAS_QOS_FORMAT_SG_ID(port_id, max_sg_level_id, idx);

            EV_LOGGING(QOS, NOTICE, "QOS",
                        "Allocate nas-sg-id 0x%016lX for VP port_id %d, level %d, local_sg_index %d",
                        nas_sg_id, port_id, max_sg_level_id, idx);
            nas_qos_scheduler_group sg(p_switch);

            sg.set_scheduler_group_id(nas_sg_id);
            sg.add_npu(0);
            sg.set_port_id(port_id);
            sg.set_level(max_sg_level_id);
            sg.clear_all_dirty_flags();

            // child-list is not updated for now

            p_switch->add_scheduler_group(sg);

            // Do not add to DB until some attribute is modified.
        }
        catch (...) {
            return NAS_QOS_E_FAIL;
        }
    }

    // Skip creating upper-level SG nodes

    return STD_ERR_OK;

}

t_std_error nas_qos_port_hqos_init(hal_ifindex_t ifindex, ndi_port_t ndi_port_id)
{

    parent_map_t parent_map;

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "switch_id of npu_id: %u cannot be found/created",
                     ndi_port_id.npu_id);
        return NAS_QOS_E_FAIL;
    }

    if (p_switch->port_queue_is_initialized(ifindex) ||
        p_switch->port_sg_is_initialized(ifindex)) {
        // neither should be initialized already.
        // We need to initialize both at the same time to set up HQoS tree.
        EV_LOGGING(QOS, ERR, "QOS",
                "Unexpected: queue or SG is initialized on port %u", ifindex);
        return NAS_QOS_E_FAIL;
    }

    // init physical and VP port queues
    if (nas_qos_port_queue_init(ifindex, ndi_port_id, parent_map) != STD_ERR_OK) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "Failed to initialize queue for port %d", ifindex);
        return NAS_QOS_E_FAIL;
    }

    // init VP SGs
    if (nas_is_virtual_port(ifindex))
        return nas_qos_port_scheduler_group_init_vp(ifindex);

    /* get the number of SG per port */
    uint_t sg_count = ndi_qos_get_number_of_scheduler_groups(ndi_port_id);
    if (sg_count == 0) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "No scheduler-group for npu_id %u, npu_port_id %u ",
                     ndi_port_id.npu_id, ndi_port_id.npu_port);
        return STD_ERR_OK;
    }

    /* get the list of ndi_scheduler_group_id list of the port */
    std::vector<ndi_obj_id_t> ndi_sg_id_list(sg_count);
    if (ndi_qos_get_scheduler_group_id_list(ndi_port_id, sg_count, &ndi_sg_id_list[0]) !=
            sg_count) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "Fail to retrieve all scheduler-groups of npu_id %u, npu_port_id %u ",
                     ndi_port_id.npu_id, ndi_port_id.npu_port);
        return NAS_QOS_E_FAIL;
    }

    EV_LOGGING(QOS, DEBUG, "QOS",
                "Port %d has %d SG ids of all levels.",
                ifindex, sg_count);

    t_std_error rc;
    rc = create_port_scheduler_group_list(ndi_sg_id_list, ifindex, ndi_port_id,
                                          parent_map);
    if (rc != STD_ERR_OK) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "Failed to create scheduler-groups for default hierarchy");
        return rc;
    }

    rc = setup_scheduler_group_hierarchy(ndi_port_id.npu_id, parent_map);
    if (rc != STD_ERR_OK) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "Failed to setup scheduler-group hierarchy");
        return rc;
    }

    return STD_ERR_OK;
}

/**
  * This function provides NAS-QoS SCHEDULER_GROUP CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_scheduler_group_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&scheduler_group_mutex);

    switch (op) {
    case cps_api_oper_CREATE:
        EV_LOGGING(QOS, NOTICE, "QOS",
                "WARNING: Scheduler Group CREATE will be deprecated. Avoid using it now.");

        return nas_qos_cps_api_scheduler_group_create(obj, param->prev);

    case cps_api_oper_SET:
        return nas_qos_cps_api_scheduler_group_set(obj, param->prev);

    case cps_api_oper_DELETE:
        EV_LOGGING(QOS, NOTICE, "QOS",
                "WARNING: Scheduler Group DELETE will be deprecated. Avoid using it now.");
        return nas_qos_cps_api_scheduler_group_delete(obj, param->prev);

    default:
        return NAS_QOS_E_UNSUPPORTED;
    }
}


/**
  * This function provides NAS-QoS SCHEDULER_GROUP CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_scheduler_group_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t sg_attr = cps_api_get_key_data(obj, BASE_QOS_SCHEDULER_GROUP_ID);
    uint32_t port_id = 0, level = 0;
    bool have_port = false, have_level = false;
    if (!sg_attr) {
        cps_api_object_it_t it;
        cps_api_object_it_begin(obj, &it);
        for ( ; cps_api_object_it_valid(&it); cps_api_object_it_next(&it)) {
            cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
            if (id == BASE_QOS_SCHEDULER_GROUP_PORT_ID) {
                port_id = cps_api_object_attr_data_u32(it.attr);
                have_port = true;
            } else if (id == BASE_QOS_SCHEDULER_GROUP_LEVEL) {
                level = cps_api_object_attr_data_u32(it.attr);
                have_level = true;
            }
        }
    }

    if (!sg_attr && !have_port) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                     "Invalid input attributes for reading");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint_t switch_id = 0;
    nas_obj_id_t scheduler_group_id = (sg_attr? cps_api_object_attr_data_u64(sg_attr): 0);

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, scheduler_group id 0x%016lX\n",
                    switch_id, scheduler_group_id);

    std_mutex_simple_lock_guard p_m(&scheduler_group_mutex);

    nas_qos_switch * p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    std::vector<nas_qos_scheduler_group *> sg_list;
    if (sg_attr) {
        nas_qos_scheduler_group *scheduler_group = p_switch->get_scheduler_group(scheduler_group_id);
        if (scheduler_group == NULL) {
            EV_LOGGING(QOS, INFO, "NAS-QOS",
                         "Could not find scheduler group with ID %lu", scheduler_group_id);
            return NAS_QOS_E_FAIL;
        }
        sg_list.push_back(scheduler_group);
    } else {
        int match_level = have_level ? (int)level : -1;
        p_switch->get_port_scheduler_groups(port_id, match_level, sg_list);
    }

    /* fill in data */
    cps_api_object_t ret_obj;

    for (auto scheduler_group: sg_list) {

        ret_obj = cps_api_object_list_create_obj_and_append (param->list);
        if (ret_obj == NULL) {
            EV_LOGGING(QOS, INFO, "NAS-QOS",
                         "Failed to create cps object");
            return cps_api_ret_code_ERR;
        }

        cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),BASE_QOS_SCHEDULER_GROUP_OBJ,
                cps_api_qualifier_TARGET);
        cps_api_set_key_data(ret_obj, BASE_QOS_SCHEDULER_GROUP_SWITCH_ID,
                cps_api_object_ATTR_T_U32,
                &switch_id, sizeof(uint32_t));
        scheduler_group_id = scheduler_group->get_scheduler_group_id();
        cps_api_set_key_data(ret_obj, BASE_QOS_SCHEDULER_GROUP_ID,
                cps_api_object_ATTR_T_U64,
                &scheduler_group_id, sizeof(uint64_t));

        cps_api_object_attr_add_u32(ret_obj,
                                    BASE_QOS_SCHEDULER_GROUP_CHILD_COUNT,
                                    scheduler_group->get_child_count());
        for (uint32_t idx = 0; idx < scheduler_group->get_child_count(); idx++) {
            cps_api_object_attr_add_u64(ret_obj,
                                        BASE_QOS_SCHEDULER_GROUP_CHILD_LIST,
                                        scheduler_group->get_child_id(idx));
        }

        cps_api_object_attr_add_u32(ret_obj, BASE_QOS_SCHEDULER_GROUP_PORT_ID,
                                    scheduler_group->get_port_id());
        cps_api_object_attr_add_u32(ret_obj, BASE_QOS_SCHEDULER_GROUP_LEVEL,
                                    scheduler_group->get_level());
        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_SCHEDULER_GROUP_SCHEDULER_PROFILE_ID,
                                    scheduler_group->get_scheduler_profile_id());
        cps_api_object_attr_add_u32(ret_obj, BASE_QOS_SCHEDULER_GROUP_MAX_CHILD,
                                    scheduler_group->get_max_child());
        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_SCHEDULER_GROUP_PARENT,
                                    scheduler_group->get_parent());
    }

    return cps_api_ret_code_OK;
}


/**
  * This function provides NAS-QoS SCHEDULER_GROUP CPS API rollback function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_scheduler_group_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->prev,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&scheduler_group_mutex);

    if (op == cps_api_oper_CREATE) {
        nas_qos_cps_api_scheduler_group_delete(obj, NULL);
    }

    if (op == cps_api_oper_SET) {
        nas_qos_cps_api_scheduler_group_set(obj, NULL);
    }

    if (op == cps_api_oper_DELETE) {
        nas_qos_cps_api_scheduler_group_create(obj, NULL);
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_scheduler_group_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{
    nas_obj_id_t scheduler_group_id = 0;

    uint32_t switch_id = 0;

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    nas_qos_scheduler_group scheduler_group(p_switch);

    if (nas_qos_cps_parse_attr(obj, scheduler_group) != cps_api_ret_code_OK)
        return NAS_QOS_E_FAIL;

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    try {
        scheduler_group_id = p_switch->alloc_scheduler_group_id();

        scheduler_group.set_scheduler_group_id(scheduler_group_id);

        scheduler_group.commit_create(sav_obj? false: true);

        p_switch->add_scheduler_group(scheduler_group);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Created new scheduler_group %lu\n",
                     scheduler_group.get_scheduler_group_id());

        if (scheduler_group.dirty_attr_list().contains(BASE_QOS_SCHEDULER_GROUP_PARENT)) {
            nas_obj_id_t new_parent_id = scheduler_group.get_parent();
            nas_qos_notify_parent_child_change(switch_id, new_parent_id, scheduler_group_id, true);
        }

        // update obj with new scheduler_group-id key
        cps_api_set_key_data(obj, BASE_QOS_SCHEDULER_GROUP_ID,
                cps_api_object_ATTR_T_U64,
                &scheduler_group_id, sizeof(uint64_t));

        // save for rollback if caller requests it.
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (!tmp_obj) {
                return NAS_QOS_E_FAIL;
            }
            cps_api_object_clone(tmp_obj, obj);

        }

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS SCHEDULER_GROUP Create error code: %d ",
                    e.err_code);
        if (scheduler_group_id)
            p_switch->release_scheduler_group_id(scheduler_group_id);

        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS SCHEDULER_GROUP Create Unexpected error code");
        if (scheduler_group_id)
            p_switch->release_scheduler_group_id(scheduler_group_id);

        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_scheduler_group_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{
    cps_api_object_attr_t sg_attr = cps_api_get_key_data(obj, BASE_QOS_SCHEDULER_GROUP_ID);

    if (sg_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                     "switch id and scheduler group id not specified in the message\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint_t switch_id = 0;
    nas_obj_id_t scheduler_group_id = cps_api_object_attr_data_u64(sg_attr);

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modify switch id %u, scheduler_group id %lu\n",
                    switch_id, scheduler_group_id);

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "switch_id: %u cannot be found/created",
                     switch_id);
        return NAS_QOS_E_FAIL;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_scheduler_group *scheduler_group_p = p_switch->get_scheduler_group(scheduler_group_id);
    if (scheduler_group_p == NULL) {
        return NAS_QOS_E_FAIL;
    }

    /* make a local copy of the existing scheduler_group */
    nas_qos_scheduler_group scheduler_group(*scheduler_group_p);

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modify switch id %u, scheduler_group id %lu . After copy\n",
                    switch_id, scheduler_group_id);

    cps_api_return_code_t rc = cps_api_ret_code_OK;
    if ((rc = nas_qos_cps_parse_attr(obj, scheduler_group)) != cps_api_ret_code_OK)
        return rc;

    if (scheduler_group.get_level() == 0 &&
        scheduler_group.dirty_attr_list().contains(BASE_QOS_SCHEDULER_GROUP_SCHEDULER_PROFILE_ID)) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                " Scheduler Group Level 0 node does not accept Scheduler Profile ID; "
                " Use Port Egress Object to configure Scheduler Profile!\n");
        return NAS_QOS_E_UNSUPPORTED;
    }

    if (scheduler_group.dirty_attr_list().contains(BASE_QOS_SCHEDULER_GROUP_MAX_CHILD)) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                " Scheduler Group Max Child cannot be set after creation\n");
        return NAS_QOS_E_UNSUPPORTED;
    }

    try {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modifying scheduler_group %lu attr on port %d \n",
                     scheduler_group.get_scheduler_group_id(), scheduler_group.get_port_id());

        if (!nas_is_virtual_port(scheduler_group.get_port_id()) &&
            scheduler_group_p->is_created_in_ndi()) {
            nas::attr_set_t modified_attr_list = scheduler_group.commit_modify(*scheduler_group_p, (sav_obj? false: true));

            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "done with commit_modify \n");

            if (scheduler_group.dirty_attr_list().contains(BASE_QOS_SCHEDULER_GROUP_PARENT)) {
                nas_obj_id_t old_parent_id = scheduler_group_p->get_parent();
                nas_obj_id_t new_parent_id = scheduler_group.get_parent();

                nas_qos_notify_parent_child_change(switch_id, old_parent_id, scheduler_group_id, false);
                nas_qos_notify_parent_child_change(switch_id, new_parent_id, scheduler_group_id, true);

            }

            // set attribute with full copy
            // save rollback info if caller requests it.
            // use modified attr list, current scheduler_group value
            if (sav_obj) {
                cps_api_object_t tmp_obj;
                tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
                if (!tmp_obj) {
                    return cps_api_ret_code_ERR;
                }
                nas_qos_store_prev_attr(tmp_obj, modified_attr_list, *scheduler_group_p);
           }
        }

        // update the local cache with newly set values
        *scheduler_group_p = scheduler_group;

        // update DB
        if (cps_api_db_commit_one(cps_api_oper_SET, obj, nullptr, false) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Fail to store SG update to DB");
        }

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS SCHEDULER_GROUP Attr Modify error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS SCHEDULER_GROUP Modify Unexpected error code");
        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_cps_api_scheduler_group_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{
    cps_api_object_attr_t sg_attr = cps_api_get_key_data(obj, BASE_QOS_SCHEDULER_GROUP_ID);

    if (sg_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", " scheduler group id not specified in the message\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint_t switch_id = 0;
    nas_obj_id_t scheduler_group_id = cps_api_object_attr_data_u64(sg_attr);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", " switch: %u not found\n",
                     switch_id);
        return NAS_QOS_E_FAIL;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_scheduler_group *scheduler_group_p = p_switch->get_scheduler_group(scheduler_group_id);
    if (scheduler_group_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", " scheduler_group id: %lu not found\n",
                     scheduler_group_id);

        return NAS_QOS_E_FAIL;
    }

    if (scheduler_group_p->get_child_count() > 0) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", " scheduler_group id: %lu cannot be deleted because it is being referenced.\n",
                     scheduler_group_id);

        return NAS_QOS_E_FAIL;
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Deleting scheduler_group %lu on switch: %u\n",
                 scheduler_group_p->get_scheduler_group_id(), p_switch->id());


    // delete
    try {
        scheduler_group_p->commit_delete(sav_obj? false: true);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Saving deleted scheduler_group %lu\n",
                     scheduler_group_p->get_scheduler_group_id());

        nas_obj_id_t old_parent_id = scheduler_group_p->get_parent();
        nas_qos_notify_parent_child_change(switch_id, old_parent_id, scheduler_group_id, false);

        // save current scheduler_group config for rollback if caller requests it.
        // use existing set_mask, existing config
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (!tmp_obj) {
                return cps_api_ret_code_ERR;
            }
            nas_qos_store_prev_attr(tmp_obj, scheduler_group_p->set_attr_list(), *scheduler_group_p);
        }

        p_switch->remove_scheduler_group(scheduler_group_p->get_scheduler_group_id());

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS SCHEDULER_GROUP Delete error code: %d ",
                    e.err_code);
        return e.err_code;
    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS SCHEDULER_GROUP Delete: Unexpected error");
        return NAS_QOS_E_FAIL;
    }


    return cps_api_ret_code_OK;
}


/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_scheduler_group &scheduler_group)
{
    uint_t val;
    uint64_t val64;
    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch (id) {
        case BASE_QOS_SCHEDULER_GROUP_SWITCH_ID:
        case BASE_QOS_SCHEDULER_GROUP_ID:
            break; // These are for part of the keys

        case BASE_QOS_SCHEDULER_GROUP_CHILD_COUNT:
        case BASE_QOS_SCHEDULER_GROUP_CHILD_LIST:
            // non-configurable.
            break;


        case BASE_QOS_SCHEDULER_GROUP_PORT_ID:
            val = cps_api_object_attr_data_u32(it.attr);
            if (scheduler_group.set_port_id(val) != STD_ERR_OK)
                return NAS_QOS_E_FAIL;
            break;

        case BASE_QOS_SCHEDULER_GROUP_LEVEL:
            val = cps_api_object_attr_data_u32(it.attr);
            scheduler_group.set_level(val);
            break;

        case BASE_QOS_SCHEDULER_GROUP_SCHEDULER_PROFILE_ID:
            val64 = cps_api_object_attr_data_u64(it.attr);
            scheduler_group.set_scheduler_profile_id(val64);
            break;

        case BASE_QOS_SCHEDULER_GROUP_MAX_CHILD:
            val = cps_api_object_attr_data_u32(it.attr);
            scheduler_group.set_max_child(val);
            break;

        case BASE_QOS_SCHEDULER_GROUP_PARENT:
            val64 = cps_api_object_attr_data_u64(it.attr);
            scheduler_group.set_parent(val64);
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
                                                    const nas_qos_scheduler_group &scheduler_group)
{
    // filling in the keys
    uint32_t switch_id = scheduler_group.switch_id();
    nas_obj_id_t sg_id = scheduler_group.get_scheduler_group_id();
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_SCHEDULER_GROUP_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(obj, BASE_QOS_SCHEDULER_GROUP_SWITCH_ID,
            cps_api_object_ATTR_T_U32,
            &switch_id, sizeof(uint32_t));
    cps_api_set_key_data(obj, BASE_QOS_SCHEDULER_GROUP_ID,
            cps_api_object_ATTR_T_U64,
            &sg_id, sizeof(uint64_t));


    for (auto attr_id: attr_set) {
        switch (attr_id) {
        case BASE_QOS_SCHEDULER_GROUP_ID:
            /* key */
            break;

        case BASE_QOS_SCHEDULER_GROUP_CHILD_COUNT:
        case BASE_QOS_SCHEDULER_GROUP_CHILD_LIST:
            /* Read only attributes, no need to save */
            break;

        case BASE_QOS_SCHEDULER_GROUP_PORT_ID:
            cps_api_object_attr_add_u32(obj, attr_id,
                    scheduler_group.get_level());
            break;

        case BASE_QOS_SCHEDULER_GROUP_SCHEDULER_PROFILE_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                    scheduler_group.get_scheduler_profile_id());
            break;

        case BASE_QOS_SCHEDULER_GROUP_MAX_CHILD:
            cps_api_object_attr_add_u32(obj, attr_id,
                    scheduler_group.get_max_child());
            break;

        case BASE_QOS_SCHEDULER_GROUP_PARENT:
            cps_api_object_attr_add_u64(obj, attr_id,
                    scheduler_group.get_parent());
            break;

        default:
            break;
        }
    }

    return cps_api_ret_code_OK;
}

static nas_qos_scheduler_group * nas_qos_cps_get_scheduler_group(uint_t switch_id,
                                           nas_obj_id_t scheduler_group_id)
{

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NULL;

    nas_qos_scheduler_group *scheduler_group_p = p_switch->get_scheduler_group(scheduler_group_id);

    return scheduler_group_p;
}


void nas_qos_notify_parent_child_change(uint_t switch_id, nas_obj_id_t parent_id, nas_obj_id_t child_id, bool add)
{
    nas_qos_scheduler_group * parent_p = nas_qos_cps_get_scheduler_group(switch_id, parent_id);

    if (parent_p != NULL) {
        if (add)
            parent_p->add_child(child_id);
        else
            parent_p->del_child(child_id);
    }
}
