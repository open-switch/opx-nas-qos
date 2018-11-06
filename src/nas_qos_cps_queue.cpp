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
 * \file   nas_qos_cps_queue.cpp
 * \brief  NAS qos queue related CPS API routines
 * \date   02-2015
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

#include "dell-base-qos.h"
#include "nas_qos_common.h"
#include "nas_qos_switch_list.h"
#include "nas_qos_cps.h"
#include "nas_qos_queue.h"
#include "nas_ndi_port.h"
#include "nas_qos_cps_queue.h"
#include "nas_qos_cps_scheduler_group.h"
#include "nas_if_utils.h"
#include <vector>

/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_queue &queue);

static cps_api_return_code_t nas_qos_store_prev_attr(cps_api_object_t obj,
                                                const nas::attr_set_t attr_set,
                                                const nas_qos_queue &queue);
static cps_api_return_code_t nas_qos_cps_api_queue_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_queue_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_queue_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);

static std_mutex_lock_create_static_init_rec(queue_mutex);

static t_std_error nas_qos_port_queue_init_vp(hal_ifindex_t port_id);


/**
  * This function provides NAS-QoS queue CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_queue_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&queue_mutex);

    switch (op) {
    case cps_api_oper_CREATE:
        EV_LOGGING(QOS, NOTICE, "QOS",
                "WARNING: Queue CREATE will be deprecated. Avoid using it now.");
        return nas_qos_cps_api_queue_create(obj, param->prev);

    case cps_api_oper_DELETE:
        EV_LOGGING(QOS, NOTICE, "QOS",
                "WARNING: Queue DELETE will be deprecated. Avoid using it now.");
        return nas_qos_cps_api_queue_delete(obj, param->prev);

    case cps_api_oper_SET:
        return nas_qos_cps_api_queue_set(obj, param->prev);

    default:
        return NAS_QOS_E_UNSUPPORTED;
    }
}


static cps_api_return_code_t nas_qos_cps_get_queue_info(
                                cps_api_get_params_t * param,
                                uint32_t switch_id, uint_t port_id,
                                bool match_type, uint_t type,
                                bool match_q_num, uint_t q_num)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    uint_t count = p_switch->get_number_of_port_queues(port_id);

    if  (count == 0) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "switch id %u, port id %u has no queues\n",
                switch_id, port_id);

        return NAS_QOS_E_FAIL;
    }

    std::vector<nas_qos_queue *> q_list(count);
    p_switch->get_port_queues(port_id, count, &q_list[0]);

    /* fill in data */
    cps_api_object_t ret_obj;

    for (uint_t i = 0; i < count; i++ ) {
        nas_qos_queue *queue = q_list[i];

        // filter out unwanted queues
        if (match_type && (queue->get_type() != type))
            continue;

        if (match_q_num && (queue->get_local_queue_id() != q_num))
            continue;


        ret_obj = cps_api_object_list_create_obj_and_append(param->list);
        if (ret_obj == NULL) {
            return cps_api_ret_code_ERR;
        }

        cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
                BASE_QOS_QUEUE_OBJ,
                cps_api_qualifier_TARGET);
        uint32_t val_port = queue->get_port_id();
        uint32_t val_type = queue->get_type();
        uint32_t val_queue_number = queue->get_local_queue_id();

        cps_api_set_key_data(ret_obj, BASE_QOS_QUEUE_PORT_ID,
                cps_api_object_ATTR_T_U32,
                &val_port, sizeof(uint32_t));
        cps_api_set_key_data(ret_obj, BASE_QOS_QUEUE_TYPE,
                cps_api_object_ATTR_T_U32,
                &val_type, sizeof(uint32_t));
        cps_api_set_key_data(ret_obj, BASE_QOS_QUEUE_QUEUE_NUMBER,
                cps_api_object_ATTR_T_U32,
                &val_queue_number, sizeof(uint32_t));

        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_QUEUE_ID, queue->get_queue_id());
        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_QUEUE_PARENT, queue->get_parent());

        // User configured objects
        if (queue->is_wred_id_set())
            cps_api_object_attr_add_u64(ret_obj, BASE_QOS_QUEUE_WRED_ID, queue->get_wred_id());
        if (queue->is_buffer_profile_set())
            cps_api_object_attr_add_u64(ret_obj, BASE_QOS_QUEUE_BUFFER_PROFILE_ID, queue->get_buffer_profile());
        if (queue->is_scheduler_profile_set())
            cps_api_object_attr_add_u64(ret_obj, BASE_QOS_QUEUE_SCHEDULER_PROFILE_ID, queue->get_scheduler_profile());

        queue->opaque_data_to_cps (ret_obj);

        // MMU indexes (1-based in NAS)
        for (uint_t idx = 0; idx < queue->get_shadow_queue_count(); idx++) {
            uint_t nas_mmu_idx = idx + 1;
            if (queue->get_shadow_queue_id(nas_mmu_idx) != NDI_QOS_NULL_OBJECT_ID)
                cps_api_object_attr_add_u32(ret_obj, BASE_QOS_QUEUE_MMU_INDEX_LIST, nas_mmu_idx);
        }
    }

    return cps_api_ret_code_OK;
}


/**
  * This function provides NAS-QoS queue CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_queue_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{

    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_PORT_ID);
    cps_api_object_attr_t queue_type_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_TYPE);
    cps_api_object_attr_t queue_num_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_QUEUE_NUMBER);

    uint32_t switch_id = 0;

    if (port_id_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Port Id must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint_t port_id = cps_api_object_attr_data_u32(port_id_attr);

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    uint_t queue_type= 0;
    bool queue_type_specified = false;
    if (queue_type_attr) {
        queue_type_specified = true;
        queue_type = cps_api_object_attr_data_u32(queue_type_attr);
    }

    uint_t queue_num = 0;
    bool queue_num_specified = false;
    if (queue_num_attr) {
        queue_num_specified = true;
        queue_num = cps_api_object_attr_data_u32(queue_num_attr);
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, port_id %u\n",
            switch_id, port_id);

    std_mutex_simple_lock_guard p_m(&queue_mutex);

    return nas_qos_cps_get_queue_info(param, switch_id, port_id,
                                queue_type_specified, queue_type,
                                queue_num_specified, queue_num);

}

/**
  * This function provides NAS-QoS queue CPS API rollback function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_queue_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->prev,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&queue_mutex);

    if (op == cps_api_oper_CREATE) {
        nas_qos_cps_api_queue_delete(obj, NULL);
    }

    if (op == cps_api_oper_SET) {
        nas_qos_cps_api_queue_set(obj, NULL);
    }

    if (op == cps_api_oper_DELETE) {
        nas_qos_cps_api_queue_create(obj, NULL);
    }


    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_queue_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{
    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_PORT_ID);
    cps_api_object_attr_t queue_type_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_TYPE);
    cps_api_object_attr_t queue_num_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_QUEUE_NUMBER);
    cps_api_object_attr_t parent_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_PARENT);

    if (port_id_attr == NULL ||
        parent_attr == NULL ||
        queue_type_attr == NULL || queue_num_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Missing mandatory attribute in the message\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint32_t switch_id = 0;
    uint_t port_id = cps_api_object_attr_data_u32(port_id_attr);
    uint_t queue_type = cps_api_object_attr_data_u32(queue_type_attr);
    uint_t queue_num = cps_api_object_attr_data_u32(queue_num_attr);

    nas_obj_id_t queue_id = 0;

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    nas_qos_queue_key_t key;
    key.port_id = port_id;
    key.type = (BASE_QOS_QUEUE_TYPE_t)queue_type;
    key.local_queue_id = queue_num;
    nas_qos_queue queue(p_switch, key);

    interface_ctrl_t intf_ctrl;
    if (nas_qos_get_port_intf(port_id, &intf_ctrl) != true) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "Cannot find NPU id for ifIndex: %d",
                      port_id);
        return NAS_QOS_E_FAIL;
    }
    queue.set_ndi_port_id(intf_ctrl.npu_id, intf_ctrl.port_id);

    if (nas_qos_cps_parse_attr(obj, queue) != cps_api_ret_code_OK)
        return NAS_QOS_E_FAIL;

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    try {
        queue_id = p_switch->alloc_queue_id();

        queue.set_queue_id(queue_id);

        queue.commit_create(sav_obj? false: true);

        p_switch->add_queue(queue);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Created new queue %lu\n",
                     queue.get_queue_id());

        if (queue.dirty_attr_list().contains(BASE_QOS_QUEUE_PARENT)) {
            nas_obj_id_t new_parent_id = queue.get_parent();
            nas_qos_notify_parent_child_change(switch_id, new_parent_id, queue_id, true);
        }

        // update obj with new queue-id key
        cps_api_set_key_data(obj, BASE_QOS_QUEUE_ID,
                cps_api_object_ATTR_T_U64,
                &queue_id, sizeof(uint64_t));

        // save for rollback if caller requests it.
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (!tmp_obj) {
                return NAS_QOS_E_FAIL;
            }
            cps_api_object_clone(tmp_obj, obj);

        }

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS QUEUE Create error code: %d ",
                    e.err_code);
        if (queue_id)
            p_switch->release_queue_id(queue_id);

        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS QUEUE Create Unexpected error code");
        if (queue_id)
            p_switch->release_queue_id(queue_id);

        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_queue_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{
    cps_api_object_t tmp_obj;

    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_PORT_ID);
    cps_api_object_attr_t queue_type_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_TYPE);
    cps_api_object_attr_t queue_num_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_QUEUE_NUMBER);

    if (port_id_attr == NULL ||
        queue_type_attr == NULL || queue_num_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Key incomplete in the message\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint32_t switch_id = 0;
    uint_t port_id = cps_api_object_attr_data_u32(port_id_attr);
    uint_t queue_type = cps_api_object_attr_data_u32(queue_type_attr);
    uint_t queue_num = cps_api_object_attr_data_u32(queue_num_attr);

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Modify switch id %u, port id %u, queue_type %u, queue_num %u \n",
            switch_id, port_id, queue_type, queue_num);

    nas_qos_queue_key_t key;
    key.port_id = port_id;
    key.type = (BASE_QOS_QUEUE_TYPE_t)queue_type;
    key.local_queue_id = queue_num;

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "Switch %u not found\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_queue *queue_p = p_switch->get_queue(key);

    if (queue_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "Queue not found in switch id %u\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    /* make a local copy of the existing queue */
    nas_qos_queue queue(*queue_p);

    cps_api_return_code_t rc = cps_api_ret_code_OK;
    if ((rc = nas_qos_cps_parse_attr(obj, queue)) != cps_api_ret_code_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Invalid information in the packet");
        return rc;
    }


    try {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modifying switch id %u, port id %u queue info \n",
                switch_id, port_id);

        if (!nas_is_virtual_port(port_id)) {
            nas::attr_set_t modified_attr_list = queue.commit_modify(*queue_p, (sav_obj? false: true));

            if (queue.dirty_attr_list().contains(BASE_QOS_QUEUE_PARENT)) {
                nas_obj_id_t old_parent_id = queue_p->get_parent();
                nas_obj_id_t new_parent_id = queue.get_parent();
                nas_qos_notify_parent_child_change(switch_id, old_parent_id, queue.get_queue_id(), false);
                nas_qos_notify_parent_child_change(switch_id, new_parent_id, queue.get_queue_id(), true);
            }

            // set attribute with full copy
            // save rollback info if caller requests it.
            // use modified attr list, current queue value
            if (sav_obj) {
                tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
                if (!tmp_obj) {
                    return cps_api_ret_code_ERR;
                }
                nas_qos_store_prev_attr(tmp_obj, modified_attr_list, *queue_p);
            }
        }

        // update the local cache with newly set values
        *queue_p = queue;

        // update DB
        if (cps_api_db_commit_one(cps_api_oper_SET, obj, nullptr, false) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Fail to store queue update to DB");
        }

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS queue Attr Modify error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS queue Modify Unexpected error code");
        return NAS_QOS_E_FAIL;
    }


    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_queue_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)

{
    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_PORT_ID);
    cps_api_object_attr_t queue_type_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_TYPE);
    cps_api_object_attr_t queue_num_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_QUEUE_NUMBER);

    if (port_id_attr == NULL ||
        queue_type_attr == NULL || queue_num_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Key incomplete in the message\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint32_t switch_id = 0;
    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    uint_t port_id = cps_api_object_attr_data_u32(port_id_attr);
    uint_t queue_type = cps_api_object_attr_data_u32(queue_type_attr);
    uint_t queue_num = cps_api_object_attr_data_u32(queue_num_attr);

    nas_qos_queue_key_t key;
    key.port_id = port_id;
    key.type = (BASE_QOS_QUEUE_TYPE_t)queue_type;
    key.local_queue_id = queue_num;

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_queue * queue_p = p_switch->get_queue(key);
    if (queue_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "Queue not found in switch id %u\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Deleting queue %lu on switch: %u\n",
                 queue_p->get_queue_id(), switch_id);

    // delete
    try {
        queue_p->commit_delete(sav_obj? false: true);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Saving deleted queue %lu\n",
                     queue_p->get_queue_id());

        nas_obj_id_t old_parent_id = queue_p->get_parent();
        nas_qos_notify_parent_child_change(switch_id, old_parent_id, queue_p->get_queue_id(), false);

        // save current queue config for rollback if caller requests it.
        // use existing set_mask, existing config
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (!tmp_obj) {
                return cps_api_ret_code_ERR;
            }
            nas_qos_store_prev_attr(tmp_obj, queue_p->set_attr_list(), *queue_p);
        }

        p_switch->remove_queue(key);

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS QUEUE Delete error code: %d ",
                    e.err_code);
        return e.err_code;
    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS QUEUE Delete: Unexpected error");
        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}



/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_queue &queue)
{
    uint64_t val64;
    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch (id) {
        case BASE_QOS_QUEUE_SWITCH_ID:
        case BASE_QOS_QUEUE_PORT_ID:
        case BASE_QOS_QUEUE_TYPE:
        case BASE_QOS_QUEUE_QUEUE_NUMBER:
        case BASE_QOS_QUEUE_ID:
        case BASE_QOS_QUEUE_MMU_INDEX_LIST:
            break; // These are not settable attr

        case BASE_QOS_QUEUE_PARENT:
            val64 = cps_api_object_attr_data_u64(it.attr);
            queue.set_parent((nas_obj_id_t)val64);
            break;

        case BASE_QOS_QUEUE_WRED_ID:
            val64 = cps_api_object_attr_data_u64(it.attr);
            queue.set_wred_id((nas_obj_id_t)val64);
            break;


        case BASE_QOS_QUEUE_BUFFER_PROFILE_ID:
            val64 = cps_api_object_attr_data_u64(it.attr);
            queue.set_buffer_profile((nas_obj_id_t)val64);
            break;

        case BASE_QOS_QUEUE_SCHEDULER_PROFILE_ID:
            val64 = cps_api_object_attr_data_u64(it.attr);
            queue.set_scheduler_profile((nas_obj_id_t)val64);
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
                                                    const nas_qos_queue &queue)
{
    // filling in the keys
    uint32_t switch_id = queue.switch_id();
    uint32_t val_port = queue.get_port_id();
    uint32_t val_type = queue.get_type();
    uint32_t val_queue_number = queue.get_local_queue_id();

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_QUEUE_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(obj, BASE_QOS_QUEUE_SWITCH_ID,
            cps_api_object_ATTR_T_U32,
            &switch_id, sizeof(uint32_t));
    cps_api_set_key_data(obj, BASE_QOS_QUEUE_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &val_port, sizeof(uint32_t));
    cps_api_set_key_data(obj, BASE_QOS_QUEUE_TYPE,
            cps_api_object_ATTR_T_U32,
            &val_type, sizeof(uint32_t));
    cps_api_set_key_data(obj, BASE_QOS_QUEUE_QUEUE_NUMBER,
            cps_api_object_ATTR_T_U32,
            &val_queue_number, sizeof(uint32_t));

    for (auto attr_id: attr_set) {
        switch (attr_id) {
        case BASE_QOS_QUEUE_PORT_ID:
        case BASE_QOS_QUEUE_ID:
        case BASE_QOS_QUEUE_TYPE:
            break; // These are not settable attr

        case BASE_QOS_QUEUE_PARENT:
            cps_api_object_attr_add_u64(obj, attr_id,
                                        queue.get_parent());
            break;

        case BASE_QOS_QUEUE_WRED_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                                        queue.get_wred_id());
            break;

        case BASE_QOS_QUEUE_BUFFER_PROFILE_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                                        queue.get_buffer_profile());
            break;

        case BASE_QOS_QUEUE_SCHEDULER_PROFILE_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                                        queue.get_scheduler_profile());
            break;

        default:
            break;
        }
    }

    return cps_api_ret_code_OK;
}

static void nas_qos_port_queue_get_shadow_queues(ndi_port_t ndi_port_id,
                                ndi_obj_id_t ndi_queue_id,
                                nas_qos_queue *q)
{
    static bool number_of_mmu_known = false;
    static uint_t number_of_mmu = 0;

    // clear up first
    q->reset_shadow_queue_ids();

    // Get from SAI
    if (number_of_mmu_known == false) {
        number_of_mmu = ndi_qos_get_shadow_queue_list(
                                ndi_port_id.npu_id,
                                ndi_queue_id,
                                0, NULL);
        number_of_mmu_known = true;
    }

    if (number_of_mmu == 0)
        return;

    uint_t count = number_of_mmu;
    std::vector<ndi_obj_id_t> shadow_q_list(count);

    if (ndi_qos_get_shadow_queue_list(
                            ndi_port_id.npu_id,
                            ndi_queue_id,
                            shadow_q_list.size(),
                            &shadow_q_list[0]) != count) {
        EV_LOGGING(QOS, ERR, "QOS-Q",
                "Shadow queues get failed on npu_port %d, ndi_q_id 0x%016lx",
                ndi_port_id.npu_port, ndi_queue_id);
        return;
    }

    // Populate local cache
    for (uint_t i = 0; i< count; i++)
        q->add_shadow_queue_id(shadow_q_list[i]);

}


static void nas_qos_port_queue_init_from_hw_info(ndi_port_t ndi_port_id,
            const ndi_qos_queue_struct_t & ndi_queue_info,
            ndi_obj_id_t ndi_obj_id,
            nas_qos_queue *q)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return ;
    }

    q->set_buffer_profile(p_switch->ndi2nas_buffer_profile_id(
                                ndi_queue_info.buffer_profile,
                                ndi_port_id.npu_id));
    q->set_scheduler_profile(p_switch->ndi2nas_scheduler_profile_id(
                                ndi_queue_info.scheduler_profile,
                                ndi_port_id.npu_id));
    q->set_wred_id(p_switch->ndi2nas_wred_id(
                                ndi_queue_info.wred_id,
                                ndi_port_id.npu_id));
    q->add_npu(ndi_port_id.npu_id);
    q->set_ndi_port_id(ndi_port_id.npu_id, ndi_port_id.npu_port);
    q->set_ndi_obj_id(ndi_obj_id);
    q->mark_ndi_created();

    // get the shadow queue list
    nas_qos_port_queue_get_shadow_queues(ndi_port_id, ndi_obj_id, q);


}

// create per-port, per-queue instance
static t_std_error create_port_queue(hal_ifindex_t port_id,
                                    ndi_port_t ndi_port_id,
                                    uint32_t local_queue_id,
                                    BASE_QOS_QUEUE_TYPE_t type,
                                    ndi_obj_id_t ndi_queue_id,
                                    const  ndi_qos_queue_struct_t & ndi_queue_info,
                                    nas_obj_id_t & nas_queue_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-Q",
                     "switch_id of ifindex: %u cannot be found/created",
                     port_id);
        return NAS_QOS_E_FAIL;
    }

    try {
        // create the queue and add the queue to switch
        nas_obj_id_t queue_id = p_switch->alloc_queue_id();
        nas_qos_queue_key_t key;
        key.port_id = port_id;
        key.type = type;
        key.local_queue_id = local_queue_id;
        nas_qos_queue q (p_switch, key);

        // initial hw settings
        nas_qos_port_queue_init_from_hw_info(ndi_port_id, ndi_queue_info, ndi_queue_id, &q);

        q.set_queue_id(queue_id);

        // return the newly created nas-obj-id
        nas_queue_id = queue_id;

        EV_LOGGING(QOS, DEBUG, "QOS",
                     "Allocate nas-q-id 0x%016lX for queue: type %u, local_qid %u, ndi_queue_id 0x%016lX",
                     queue_id, type, local_queue_id, ndi_queue_id);
        p_switch->add_queue(q);

    }
    catch (...) {
        return NAS_QOS_E_FAIL;
    }

    return STD_ERR_OK;

}

static void nas_qos_port_queue_disassociation(hal_ifindex_t ifindex, ndi_port_t ndi_port_id)
{
    EV_LOGGING(QOS, NOTICE, "QOS-Q", "Disassociation ifindex %d from npu %d/port %d",
                ifindex, ndi_port_id.npu_id, ndi_port_id.npu_port);

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-Q",
                     "switch_id cannot be found with npu_id %d",
                     ndi_port_id.npu_id);
        return ;
    }

    // get NAS-initialized queues on ifindex
    uint_t queue_num = p_switch->get_number_of_port_queues(ifindex);
    std::vector<nas_qos_queue *> q_list(queue_num);
    p_switch->get_port_queues(ifindex, queue_num, &q_list[0]);

    // Clear data from the node for future re-init
    // Mark the node non-ndi-created
    for (auto q: q_list) {
        q->set_buffer_profile(NAS_QOS_NULL_OBJECT_ID);
        q->set_scheduler_profile(NAS_QOS_NULL_OBJECT_ID);
        q->set_wred_id(NAS_QOS_NULL_OBJECT_ID);
        q->reset_ndi_obj_id();
        q->clear_all_dirty_flags();
        q->reset_npus();
        q->mark_ndi_removed();
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
void nas_qos_port_queue_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd)
{
    if (isAdd == false) {
        return nas_qos_port_queue_disassociation(ifindex, ndi_port_id);
    }

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-Q",
                     "switch_id cannot be found with npu_id %d",
                     ndi_port_id.npu_id);
        return ;
    }

    // get NAS-initialized queues on ifindex
    uint_t queue_num = p_switch->get_number_of_port_queues(ifindex);
    std::vector<nas_qos_queue *> q_list(queue_num);
    p_switch->get_port_queues(ifindex, queue_num, &q_list[0]);


    EV_LOGGING(QOS, NOTICE, "QOS-Q", "Association ifindex %d to npu port %d",
            ifindex, ndi_port_id.npu_port);


    // get ndi-port queue ids from NPU
    /* get the max number of queues allowed per port */
    uint_t max_num_of_queue = ndi_qos_get_number_of_queues(ndi_port_id);
    if (max_num_of_queue == 0) {
        EV_LOGGING(QOS, INFO, "QOS",
                     "No queues for npu_id %u, npu_port_id %u ",
                     ndi_port_id.npu_id, ndi_port_id.npu_port);
        return;
    }

    /* get the list of ndi_queue ids */
    std::vector<ndi_obj_id_t> ndi_queue_id_list(max_num_of_queue);
    uint_t num_of_queue_retrieved = ndi_qos_get_queue_id_list(ndi_port_id, max_num_of_queue, &ndi_queue_id_list[0]);


    // update NAS-queue with NPU info
    if (queue_num != num_of_queue_retrieved) {
        EV_LOGGING(QOS, ERR, "QOS",
                "Number of queues initialized %d is not equal to number of ndi queues %d",
                queue_num, num_of_queue_retrieved);
        return;
    }

    // update NAS-queue to physical queue mapping
    for (uint_t idx = 0; idx < num_of_queue_retrieved; idx++) {

        ndi_qos_queue_struct_t ndi_qos_queue_info;
        memset(&ndi_qos_queue_info, 0, sizeof(ndi_qos_queue_struct_t));

        nas_attr_id_t nas_attr_list[] = {
                BASE_QOS_QUEUE_TYPE,
                BASE_QOS_QUEUE_QUEUE_NUMBER,
                BASE_QOS_QUEUE_BUFFER_PROFILE_ID,
                BASE_QOS_QUEUE_SCHEDULER_PROFILE_ID,
                BASE_QOS_QUEUE_WRED_ID,
                BASE_QOS_QUEUE_PARENT,

        };
        ndi_qos_get_queue(ndi_port_id.npu_id,
                          ndi_queue_id_list[idx],
                          nas_attr_list,
                          sizeof(nas_attr_list)/sizeof(nas_attr_id_t),
                          &ndi_qos_queue_info);

        // TODO: This may be changed as SAI is changing indexing scheme at present.
        uint_t local_queue_number = ndi_qos_queue_info.queue_index;
        bool found = false;

        // search for the corresponding NAS-q structure on new ifindex
        // and populate the new npu-info
        for (uint_t j=0; j< queue_num; j++) {
            nas_qos_queue * q = q_list[j];
            if (q->get_type() == ndi_qos_queue_info.type &&
                q->get_local_queue_id() == local_queue_number) {
                found = true;

                // initial hw settings
                nas_qos_port_queue_init_from_hw_info(ndi_port_id, ndi_qos_queue_info,
                                                    ndi_queue_id_list[idx], q);

                break;
            }
        }

        if (!found) {
            EV_LOGGING(QOS,ERR,"QOS-Q-Init",
                        "Cannot find nas queue on ifindex %d matching ndi q_type %d, q_num %d",
                        ifindex, ndi_qos_queue_info.type, local_queue_number);
            continue;
        }
    }

    // push DB to NPU
    cps_api_object_guard _og(cps_api_object_create());
    if(!_og.valid()){
        EV_LOGGING(QOS,ERR,"QOS-DB-GET","Failed to create object for db get");
        return;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(_og.get()),
            BASE_QOS_QUEUE_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(_og.get(), BASE_QOS_QUEUE_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &ifindex, sizeof(uint32_t));
    // use partial key is sufficient

    cps_api_object_list_guard lst(cps_api_object_list_create());
    if (cps_api_db_get(_og.get(),lst.get())==cps_api_ret_code_OK) {
        size_t len = cps_api_object_list_size(lst.get());

        for (uint_t idx = 0; idx < len; idx++){
            cps_api_object_t db_obj = cps_api_object_list_get(lst.get(),idx);

            cps_api_key_set_attr(cps_api_object_key(db_obj), cps_api_oper_SET);

            // push the DB to NPU
            nas_qos_cps_api_queue_set(db_obj, NULL);

            EV_LOGGING(QOS, NOTICE,"QOS-DB-Q",
                    "One Port Queue DB record of port %u written to NPU",
                    ifindex);

        }
    }
}


static t_std_error nas_qos_port_queue_init_vp(hal_ifindex_t port_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return NAS_QOS_E_FAIL;
    }

    for (uint_t idx = 0; idx < p_switch->total_queues_per_port; idx++) {
        try {
            // create the queue and add the queue to switch
            nas_obj_id_t queue_id = p_switch->alloc_queue_id();
            nas_qos_queue_key_t key;
            key.port_id = port_id;
            key.type = (idx < p_switch->ucast_queues_per_port?
                        BASE_QOS_QUEUE_TYPE_UCAST: BASE_QOS_QUEUE_TYPE_MULTICAST);
            key.local_queue_id = (idx < p_switch->ucast_queues_per_port?
                        idx: idx - p_switch->ucast_queues_per_port);
            nas_qos_queue q (p_switch, key);

            q.set_queue_id(queue_id);

            // For VP, queue's parent id is pre-set
            nas_obj_id_t nas_sg_id = NAS_QOS_FORMAT_SG_ID(port_id,
                                            p_switch->max_sched_group_level - 1,
                                            key.local_queue_id);
            q.set_parent(nas_sg_id);

            EV_LOGGING(QOS, INFO, "QOS",
                         "Allocate nas-q-id 0x%016lX for queue: type %u, local_qid %u, "
                         "on VP %d, parent nas_sg_id 0x%016lX",
                         queue_id, key.type, key.local_queue_id, port_id, nas_sg_id);
            p_switch->add_queue(q);

            // Do not add to DB until some attribute is modified.

        }
        catch (...) {
            return NAS_QOS_E_FAIL;
        }
    }

    return STD_ERR_OK;
}

/* This function initializes the queues of a port
 * @Return standard error code
 */
t_std_error nas_qos_port_queue_init(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, parent_map_t & parent_map)
{

    if (nas_is_virtual_port(ifindex))
        return nas_qos_port_queue_init_vp(ifindex);

    /* get the max number of queues allowed per port */
    uint_t max_num_of_queue = ndi_qos_get_number_of_queues(ndi_port_id);
    if (max_num_of_queue == 0) {
        EV_LOGGING(QOS, INFO, "QOS",
                     "No queues for npu_id %u, npu_port_id %u ",
                     ndi_port_id.npu_id, ndi_port_id.npu_port);
        return STD_ERR_OK;
    }

    /* get the list of ndi_queue id list */
    std::vector<ndi_obj_id_t> ndi_queue_id_list(max_num_of_queue);
    uint_t num_of_queue_retrieved = ndi_qos_get_queue_id_list(ndi_port_id, max_num_of_queue, &ndi_queue_id_list[0]);

    /* retrieve the ndi-queue type and assign the queues to with nas_queue_key */
    ndi_qos_queue_struct_t ndi_qos_queue_info;
    uint_t anycast_queue_id = 0;

    uint32_t local_queue_number = 0;
    BASE_QOS_QUEUE_TYPE_t type = BASE_QOS_QUEUE_TYPE_NONE;

    for (uint_t idx = 0; idx < num_of_queue_retrieved; idx++) {
        memset(&ndi_qos_queue_info, 0, sizeof(ndi_qos_queue_struct_t));

        nas_attr_id_t nas_attr_list[] = {
            BASE_QOS_QUEUE_TYPE,
            BASE_QOS_QUEUE_QUEUE_NUMBER,
            BASE_QOS_QUEUE_BUFFER_PROFILE_ID,
            BASE_QOS_QUEUE_SCHEDULER_PROFILE_ID,
            BASE_QOS_QUEUE_WRED_ID,
            BASE_QOS_QUEUE_PARENT,
        };
        ndi_qos_get_queue(ndi_port_id.npu_id,
                          ndi_queue_id_list[idx],
                          nas_attr_list,
                          sizeof(nas_attr_list)/sizeof(nas_attr_id_t),
                          &ndi_qos_queue_info);
        switch (ndi_qos_queue_info.type) {
        case BASE_QOS_QUEUE_TYPE_UCAST:
            local_queue_number = ndi_qos_queue_info.queue_index;
            type = BASE_QOS_QUEUE_TYPE_UCAST;

            break;

        case BASE_QOS_QUEUE_TYPE_MULTICAST:
            local_queue_number = ndi_qos_queue_info.queue_index;
            type = BASE_QOS_QUEUE_TYPE_MULTICAST;

            break;

        case BASE_QOS_QUEUE_TYPE_NONE:
            local_queue_number = anycast_queue_id;
            type = BASE_QOS_QUEUE_TYPE_NONE;

            anycast_queue_id++;
            break;

        default:
            EV_LOGGING(QOS, INFO, "QOS",
                    "Unknown queue type: %u", ndi_qos_queue_info.type);
            continue;
        }

        // Internally create NAS queue nodes and add to NAS QOS
        nas_obj_id_t nas_queue_id;
        if (create_port_queue(ifindex, ndi_port_id, local_queue_number, type,
                            ndi_queue_id_list[idx], ndi_qos_queue_info, nas_queue_id) != STD_ERR_OK) {
            EV_LOGGING(QOS, NOTICE, "QOS",
                         "Not able to create ifindex %u, local_queue_number %u, type %u",
                         ifindex, local_queue_number, type);
            return NAS_QOS_E_FAIL;
        }

        parent_map[nas_queue_id] = ndi_qos_queue_info.parent;
    }

    return STD_ERR_OK;

}

/* Debugging and unit testing */
void dump_nas_qos_queues(nas_switch_id_t switch_id, hal_ifindex_t port_id)
{
    nas_qos_switch * p_switch = nas_qos_get_switch(switch_id);

    if (p_switch == NULL)
        return;

    p_switch->dump_all_queues(port_id);

}

static bool _queue_stat_attr_get(nas_attr_id_t attr_id, stat_attr_capability * p_stat_attr)
{

    static const auto &  _queue_stat_attr_map =
            * new std::unordered_map<nas_attr_id_t, stat_attr_capability, std::hash<int>>
    {
        {BASE_QOS_QUEUE_STAT_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_GREEN_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_GREEN_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_GREEN_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_GREEN_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_YELLOW_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_YELLOW_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_YELLOW_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_YELLOW_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_RED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_RED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_RED_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_RED_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_GREEN_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_GREEN_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_YELLOW_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_YELLOW_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_RED_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_RED_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_CURRENT_OCCUPANCY_BYTES,
                {true, false}},
        {BASE_QOS_QUEUE_STAT_WATERMARK_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_SHARED_CURRENT_OCCUPANCY_BYTES,
                {true, false}},
        {BASE_QOS_QUEUE_STAT_SHARED_WATERMARK_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_GREEN_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_GREEN_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_YELLOW_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_YELLOW_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_RED_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_RED_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_QUEUE_STAT_WRED_ECN_MARKED_BYTES,
                {true, true}},

       };

    try {
        *p_stat_attr = _queue_stat_attr_map.at(attr_id);
    }
    catch (...) {
        return false;
    }
    return true;
}

/**
  * This function provides NAS-QoS queue stats CPS API read function
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_queue_stat_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{

    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_STAT_PORT_ID);
    cps_api_object_attr_t queue_type_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_STAT_TYPE);
    cps_api_object_attr_t queue_num_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_STAT_QUEUE_NUMBER);

    if (port_id_attr == NULL ||
        queue_type_attr == NULL || queue_num_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Incomplete key: port-id, queue type and queue number must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    uint32_t switch_id = 0;
    nas_qos_queue_key_t key;
    key.port_id = cps_api_object_attr_data_u32(port_id_attr);
    key.type = (BASE_QOS_QUEUE_TYPE_t)cps_api_object_attr_data_u32(queue_type_attr);
    key.local_queue_id = cps_api_object_attr_data_u32(queue_num_attr);

    if (nas_is_virtual_port(key.port_id))
        return NAS_QOS_E_FAIL;

    cps_api_object_attr_t mmu_index_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_STAT_MMU_INDEX);
    uint_t nas_mmu_index = (mmu_index_attr? cps_api_object_attr_data_u32(mmu_index_attr): 0);

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, port_id %u queue type %u, queue_number %u stat\n",
            switch_id, key.port_id, key.type, key.local_queue_id);

    std_mutex_simple_lock_guard p_m(&queue_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_queue * queue_p = p_switch->get_queue(key);
    if (queue_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Queue not found");
        return NAS_QOS_E_FAIL;
    }

    std::vector<BASE_QOS_QUEUE_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        if (id == BASE_QOS_QUEUE_STAT_SWITCH_ID ||
            id == BASE_QOS_QUEUE_STAT_PORT_ID ||
            id == BASE_QOS_QUEUE_STAT_TYPE ||
            id == BASE_QOS_QUEUE_STAT_QUEUE_NUMBER)
            continue; //key

        stat_attr_capability stat_attr;
        if (_queue_stat_attr_get(id, &stat_attr) != true) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown queue STAT flag: %lu, ignored", id);
            continue;
        }

        if (stat_attr.read_ok) {
            counter_ids.push_back((BASE_QOS_QUEUE_STAT_t)id);
        }
    }

    if (counter_ids.size() == 0) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Queue stats get without any counter ids");
        return NAS_QOS_E_FAIL;
    }

    std::vector<uint64_t> counters(counter_ids.size());

    ndi_obj_id_t ndi_queue_id = queue_p->ndi_obj_id();
    if (nas_mmu_index != 0)
        ndi_queue_id = queue_p->get_shadow_queue_id(nas_mmu_index);

    if (ndi_qos_get_queue_statistics(queue_p->get_ndi_port_id(),
                                ndi_queue_id,
                                &counter_ids[0],
                                counter_ids.size(),
                                &counters[0]) != STD_ERR_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Queue stats get failed");
        return NAS_QOS_E_FAIL;
    }

    // return stats objects to cps-app
    cps_api_object_t ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL) {
        return cps_api_ret_code_ERR;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_QUEUE_STAT_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_QUEUE_STAT_SWITCH_ID,
            cps_api_object_ATTR_T_U32,
            &switch_id, sizeof(uint32_t));
    cps_api_set_key_data(ret_obj, BASE_QOS_QUEUE_STAT_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &(key.port_id), sizeof(uint32_t));
    cps_api_set_key_data(ret_obj, BASE_QOS_QUEUE_STAT_TYPE,
            cps_api_object_ATTR_T_U32,
            &(key.type), sizeof(uint32_t));
    cps_api_set_key_data(ret_obj, BASE_QOS_QUEUE_STAT_QUEUE_NUMBER,
            cps_api_object_ATTR_T_U32,
            &(key.local_queue_id), sizeof(uint32_t));

    for (uint_t i=0; i< counter_ids.size(); i++) {
        cps_api_object_attr_add_u64(ret_obj, counter_ids[i], counters[i]);
    }

    return  cps_api_ret_code_OK;

}


/**
  * This function provides NAS-QoS queue stats CPS API clear function
  * User can use this function to clear the queue stats by setting relevant counters to zero
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_queue_stat_clear (void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    if (op != cps_api_oper_SET)
        return NAS_QOS_E_UNSUPPORTED;

    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_STAT_PORT_ID);
    cps_api_object_attr_t queue_type_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_STAT_TYPE);
    cps_api_object_attr_t queue_num_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_STAT_QUEUE_NUMBER);

    if (port_id_attr == NULL ||
        queue_type_attr == NULL || queue_num_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Incomplete key: port-id, queue type and queue number must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    uint32_t switch_id = 0;
    nas_qos_queue_key_t key;
    key.port_id = cps_api_object_attr_data_u32(port_id_attr);
    key.type = (BASE_QOS_QUEUE_TYPE_t)cps_api_object_attr_data_u32(queue_type_attr);
    key.local_queue_id = cps_api_object_attr_data_u32(queue_num_attr);

    cps_api_object_attr_t mmu_index_attr = cps_api_get_key_data(obj, BASE_QOS_QUEUE_STAT_MMU_INDEX);
    uint_t nas_mmu_index = (mmu_index_attr? cps_api_object_attr_data_u32(mmu_index_attr): 0);

    if (nas_is_virtual_port(key.port_id))
        return NAS_QOS_E_FAIL;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, port_id %u queue type %u, queue_number %u stat\n",
            switch_id, key.port_id, key.type, key.local_queue_id);

    std_mutex_simple_lock_guard p_m(&queue_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_queue * queue_p = p_switch->get_queue(key);
    if (queue_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Queue not found");
        return NAS_QOS_E_FAIL;
    }

    std::vector<BASE_QOS_QUEUE_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        if (id == BASE_QOS_QUEUE_STAT_SWITCH_ID ||
            id == BASE_QOS_QUEUE_STAT_PORT_ID ||
            id == BASE_QOS_QUEUE_STAT_TYPE ||
            id == BASE_QOS_QUEUE_STAT_QUEUE_NUMBER)
            continue; //key

        stat_attr_capability stat_attr;
        if (_queue_stat_attr_get(id, &stat_attr) != true) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown queue STAT flag: %lu, ignored", id);
            continue;
        }

        if (stat_attr.write_ok) {
            counter_ids.push_back((BASE_QOS_QUEUE_STAT_t)id);
        }
    }

    if (counter_ids.size() == 0) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Queue stats clear without any valid counter ids");
        return NAS_QOS_E_FAIL;
    }

    ndi_obj_id_t ndi_queue_id = queue_p->ndi_obj_id();
    if (nas_mmu_index != 0)
        ndi_queue_id = queue_p->get_shadow_queue_id(nas_mmu_index);

    if (ndi_qos_clear_queue_stats(queue_p->get_ndi_port_id(),
                                ndi_queue_id,
                                &counter_ids[0],
                                counter_ids.size()) != STD_ERR_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Queue stats clear failed");
        return NAS_QOS_E_FAIL;
    }


    return  cps_api_ret_code_OK;

}
