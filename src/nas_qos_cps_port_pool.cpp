/*
 * Copyright (c) 2017 Dell Inc.
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
#include "cps_api_key.h"
#include "dell-base-qos.h"
#include "nas_qos_port_pool.h"
#include "nas_if_utils.h"
#include <vector>

static std_mutex_lock_create_static_init_rec(port_pool_mutex);
static cps_api_return_code_t nas_qos_cps_api_port_pool_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_port_pool_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_port_pool_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t _append_one_port_pool(cps_api_get_params_t * param,
                                        uint_t switch_id,
                                        nas_qos_port_pool *port_pool);
static nas_qos_port_pool * nas_qos_cps_get_port_pool(uint_t switch_id,
                                           uint_t port_id,
                                           nas_obj_id_t pool_id);

static cps_api_return_code_t nas_qos_cps_parse_attr(cps_api_object_t obj,
                                                nas_qos_port_pool& port_pool)
{
    uint64_t lval;
    cps_api_object_it_t it;

    cps_api_object_it_begin(obj, &it);
    for ( ; cps_api_object_it_valid(&it); cps_api_object_it_next(&it)) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch(id) {
        case BASE_QOS_PORT_POOL_PORT_ID:
        case BASE_QOS_PORT_POOL_BUFFER_POOL_ID:
            break;
        case BASE_QOS_PORT_POOL_WRED_PROFILE_ID:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_pool.mark_attr_dirty(id);
            port_pool.set_wred_profile_id(lval);
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
                                                    const nas_qos_port_pool& port_pool)
{
    // filling in the keys
    hal_ifindex_t port_id = port_pool.get_port_id();
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_PORT_POOL_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_PORT_POOL_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &port_id, sizeof(uint32_t));


    nas_obj_id_t pool_id = port_pool.get_pool_id();
    cps_api_set_key_data(obj, BASE_QOS_PORT_POOL_BUFFER_POOL_ID,
            cps_api_object_ATTR_T_U64,
            &pool_id, sizeof(uint64_t));

    for (auto attr_id: attr_set) {
        switch (attr_id) {
        case BASE_QOS_PORT_POOL_PORT_ID:
        case BASE_QOS_PORT_POOL_BUFFER_POOL_ID:
            /* key */
            break;
        case BASE_QOS_PORT_POOL_WRED_PROFILE_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    port_pool.get_wred_profile_id());
            break;
        default:
            break;
        }
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t port_pool_get_key(cps_api_object_t obj,
                                   uint_t& switch_id, uint_t& port_id,
                                   nas_obj_id_t& pool_id)
{
    cps_api_object_attr_t attr;

    switch_id = 0;

    attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_PORT_ID);
    if (attr == NULL) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "port id not specified in message\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    port_id = cps_api_object_attr_data_u32(attr);

    attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_BUFFER_POOL_ID);
    if (attr == NULL) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "pool id not specified in message\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    pool_id = cps_api_object_attr_data_u64(attr);

    return cps_api_ret_code_OK;
}


/*
  * This function provides NAS-QoS PORT-POOL CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_pool_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&port_pool_mutex);

    switch (op) {
    case cps_api_oper_CREATE:
        return nas_qos_cps_api_port_pool_create(obj, param->prev);

    case cps_api_oper_DELETE:
        return nas_qos_cps_api_port_pool_delete(obj, param->prev);

    case cps_api_oper_SET:
        return nas_qos_cps_api_port_pool_set(obj, param->prev);

    default:
        return NAS_QOS_E_UNSUPPORTED;
    }
}

/*
  * This function provides NAS-QoS PORT-POOL CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_pool_read(void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_PORT_ID);

    if (port_id_attr == NULL) {
        EV_LOGGING(QOS, WARNING, "NAS-QOS", "Reading port-pool-member, a valid port_id must be provided to limit"
                "the number of output.\n");

        return cps_api_ret_code_ERR;
    }

    uint_t port_id = cps_api_object_attr_data_u32(port_id_attr);

    cps_api_object_attr_t pool_id_attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_BUFFER_POOL_ID);

    nas_obj_id_t pool_id = (pool_id_attr? cps_api_object_attr_data_u64(pool_id_attr): 0);

    uint_t switch_id = 0;
    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, port_id %u, buffer_pool id %lu\n",
                    switch_id, port_id, pool_id);

    std_mutex_simple_lock_guard p_m(&port_pool_mutex);

    nas_qos_switch * p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    cps_api_return_code_t rc = cps_api_ret_code_ERR;
    nas_qos_port_pool *port_pool;
    if (pool_id_attr) {
        port_pool = p_switch->get_port_pool(port_id, pool_id);
        if (port_pool == NULL)
            return NAS_QOS_E_FAIL;

        rc = _append_one_port_pool(param, switch_id, port_pool);
    }
    else {
        for (port_pool_iter_t it = p_switch->get_port_pool_it_begin();
                it != p_switch->get_port_pool_it_end();
                it++) {

            port_pool = &it->second;
            if (port_pool->get_port_id() != (int)port_id)
                continue;

            rc = _append_one_port_pool(param, switch_id, port_pool);
            if (rc != cps_api_ret_code_OK)
                return rc;
        }
    }

    return rc;
}

static cps_api_return_code_t _append_one_port_pool(cps_api_get_params_t * param,
                                        uint_t switch_id,
                                        nas_qos_port_pool *port_pool)
{
    uint_t port_id = port_pool->get_port_id();
    nas_obj_id_t pool_id = port_pool->get_pool_id();

    /* fill in data */
    cps_api_object_t ret_obj;

    ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL){
        return cps_api_ret_code_ERR;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_PORT_POOL_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_PORT_POOL_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &port_id, sizeof(uint32_t));
    cps_api_set_key_data(ret_obj, BASE_QOS_PORT_POOL_BUFFER_POOL_ID,
            cps_api_object_ATTR_T_U64,
            &pool_id, sizeof(uint64_t));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_POOL_WRED_PROFILE_ID,
            port_pool->get_wred_profile_id());

    return cps_api_ret_code_OK;
}

/*
  * This function provides NAS-QoS PORT-POOL CPS API rollback function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_pool_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->prev,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&port_pool_mutex);

    if (op == cps_api_oper_CREATE) {
        nas_qos_cps_api_port_pool_delete(obj, NULL);
    }

    if (op == cps_api_oper_SET) {
        nas_qos_cps_api_port_pool_set(obj, NULL);
    }

    if (op == cps_api_oper_DELETE) {
        nas_qos_cps_api_port_pool_create(obj, NULL);
    }

    return cps_api_ret_code_OK;

}


static cps_api_return_code_t nas_qos_cps_api_port_pool_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{

    uint_t switch_id = 0;
    uint_t port_id = 0;
    nas_obj_id_t pool_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    if ((rc = port_pool_get_key(obj, switch_id, port_id, pool_id)) != cps_api_ret_code_OK) {
        return rc;
    }

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;


    nas_qos_port_pool port_pool(p_switch, port_id, pool_id);

    interface_ctrl_t intf_ctrl;
    if (nas_qos_get_port_intf(port_id, &intf_ctrl) != true) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "Cannot find NPU id for ifIndex: %d",
                      port_id);
        return NAS_QOS_E_FAIL;
    }
    port_pool.set_ndi_port_id(intf_ctrl.npu_id, intf_ctrl.port_id);

    if ((rc = nas_qos_cps_parse_attr(obj, port_pool)) != cps_api_ret_code_OK)
        return rc;

    try {
        if (!nas_is_virtual_port(port_id)) {
            port_pool.commit_create(sav_obj? false: true);
        }

        p_switch->add_port_pool(port_pool);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Created new port_pool\n");

        // update obj with new port_pool-id attr and key
        cps_api_set_key_data(obj, BASE_QOS_PORT_POOL_PORT_ID,
                cps_api_object_ATTR_T_U32,
                &port_id, sizeof(uint32_t));

        cps_api_set_key_data(obj, BASE_QOS_PORT_POOL_BUFFER_POOL_ID,
                cps_api_object_ATTR_T_U64,
                &pool_id, sizeof(uint64_t));

        // save for rollback if caller requests it.
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (tmp_obj == NULL) {
                 return cps_api_ret_code_ERR;
            }
            cps_api_object_clone(tmp_obj, obj);
        }

        // update DB
        if (cps_api_db_commit_one(cps_api_oper_CREATE, obj, nullptr, false) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Fail to store port pool update to DB");
        }

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Create error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Create Unexpected error code");
        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_port_pool_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{

    uint_t switch_id = 0;
    uint_t port_id = 0;
    nas_obj_id_t pool_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

   if ((rc = port_pool_get_key(obj, switch_id, port_id, pool_id))
            !=    cps_api_ret_code_OK)
        return rc;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modify switch id %u, port_id %u, pool id %lu\n",
                    switch_id, port_id, pool_id);

    nas_qos_port_pool * port_pool_p = nas_qos_cps_get_port_pool(switch_id, port_id, pool_id);
    if (port_pool_p == NULL) {
        return NAS_QOS_E_FAIL;
    }

    /* make a local copy of the existing port_pool */
    nas_qos_port_pool port_pool(*port_pool_p);

    if ((rc = nas_qos_cps_parse_attr(obj, port_pool)) != cps_api_ret_code_OK)
        return rc;

    try {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modifying port_pool: port_id attr \n");

        if (!nas_is_virtual_port(port_id) &&
            port_pool_p->is_created_in_ndi()) {
            nas::attr_set_t modified_attr_list = port_pool.commit_modify(
                                            *port_pool_p, (sav_obj? false: true));

            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "done with commit_modify \n");

            // set attribute with full copy
            // save rollback info if caller requests it.
            // use modified attr list, current port_pool value
            if (sav_obj) {
                cps_api_object_t tmp_obj;
                tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
                if (tmp_obj == NULL) {
                    return cps_api_ret_code_ERR;
                }

                nas_qos_store_prev_attr(tmp_obj, modified_attr_list, *port_pool_p);
            }
        }

        // update the local cache with newly set values
        *port_pool_p = port_pool;

        // update DB
        if (cps_api_db_commit_one(cps_api_oper_SET, obj, nullptr, false) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Fail to store port pool update to DB");
        }

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Attr Modify error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Modify Unexpected error code");
        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_cps_api_port_pool_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{
    uint_t switch_id = 0;
    uint_t port_id = 0;
    nas_obj_id_t pool_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    if ((rc = port_pool_get_key(obj, switch_id, port_id, pool_id))
            !=    cps_api_ret_code_OK)
        return rc;


    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", " switch: %u not found\n",
                     switch_id);
           return NAS_QOS_E_FAIL;
    }

    nas_qos_port_pool *port_pool_p = p_switch->get_port_pool(port_id, pool_id);
    if (port_pool_p == NULL) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", " port_pool not found with port_id %u, pool_id %lu\n",
                     port_id, pool_id);

        return NAS_QOS_E_FAIL;
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Deleting port_pool on switch: %u\n", p_switch->id());


    // delete
    try {
        if (!nas_is_virtual_port(port_id)) {
            port_pool_p->commit_delete(sav_obj? false: true);
        }

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Saving deleted port_pool\n");

         // save current port_pool config for rollback if caller requests it.
        // use existing set_mask, existing config
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (tmp_obj == NULL) {
                return cps_api_ret_code_ERR;
            }
            nas_qos_store_prev_attr(tmp_obj, port_pool_p->set_attr_list(), *port_pool_p);
        }

        p_switch->remove_port_pool(port_id, pool_id);

        // update DB
        if (cps_api_db_commit_one(cps_api_oper_DELETE, obj, nullptr, false) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Fail to store port pool update to DB");
        }

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Delete error code: %d ",
                    e.err_code);
        return e.err_code;
    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Delete: Unexpected error");
        return NAS_QOS_E_FAIL;
    }


    return cps_api_ret_code_OK;
}

static nas_qos_port_pool * nas_qos_cps_get_port_pool(uint_t switch_id,
                                           uint_t port_id,
                                           nas_obj_id_t pool_id)
{

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NULL;

    nas_qos_port_pool *port_pool_p = p_switch->get_port_pool(port_id, pool_id);

    return port_pool_p;
}


static bool _port_pool_stat_attr_get(nas_attr_id_t attr_id, stat_attr_capability * p_stat_attr)
{

    static const auto &  _port_pool_attr_map =
            * new std::unordered_map<nas_attr_id_t, stat_attr_capability, std::hash<int>>
    {

        {BASE_QOS_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_RED_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_RED_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_CURRENT_OCCUPANCY_BYTES,
                {true, false}},
        {BASE_QOS_PORT_POOL_STAT_WATERMARK_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_SHARED_CURRENT_OCCUPANCY_BYTES,
                {true, false}},
        {BASE_QOS_PORT_POOL_STAT_SHARED_WATERMARK_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_GREEN_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_GREEN_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_YELLOW_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_YELLOW_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_RED_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_RED_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_PORT_POOL_STAT_WRED_ECN_MARKED_BYTES,
                {true, true}},

       };

    try {
        *p_stat_attr = _port_pool_attr_map.at(attr_id);
    }
    catch (...) {
        return false;
    }
    return true;
}



/**
  * This function provides NAS-QoS port_pool stats CPS API read function
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_pool_stat_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{

    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_STAT_PORT_ID);
    cps_api_object_attr_t pool_id_attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_STAT_BUFFER_POOL_ID);

    if (port_id_attr == NULL ||
        pool_id_attr == NULL ) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Incomplete key: port-id, pool-id must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    uint32_t switch_id = 0;
    nas_qos_port_pool_key_t key;
    key.port_id = cps_api_object_attr_data_u32(port_id_attr);
    key.pool_id = cps_api_object_attr_data_u64(pool_id_attr);

    if (nas_is_virtual_port(key.port_id))
        return NAS_QOS_E_FAIL;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, port_id %u pool_id %lu stat\n",
            switch_id, key.port_id, key.pool_id);

    std_mutex_simple_lock_guard p_m(&port_pool_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_port_pool * port_pool_p = p_switch->get_port_pool(key.port_id, key.pool_id);
    if (port_pool_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Port Pool not found");
        return NAS_QOS_E_FAIL;
    }

    std::vector<BASE_QOS_PORT_POOL_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        if (id == BASE_QOS_PORT_POOL_STAT_PORT_ID ||
            id == BASE_QOS_PORT_POOL_STAT_BUFFER_POOL_ID)
            continue; //key

        stat_attr_capability stat_attr;
        if (_port_pool_stat_attr_get(id, &stat_attr) != true) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown port_pool STAT flag: %lu, ignored", id);
            continue;
        }

        if (stat_attr.read_ok) {
            counter_ids.push_back((BASE_QOS_PORT_POOL_STAT_t)id);
        }
    }

    if (counter_ids.size() == 0) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "port pool stats get without any valid counter ids");
        return NAS_QOS_E_FAIL;
    }

    std::vector<uint64_t> counters(counter_ids.size());

    ndi_port_t ndi_port = port_pool_p->get_ndi_port_id();
    if (ndi_qos_get_port_pool_statistics(ndi_port,
                                port_pool_p->ndi_obj_id(),
                                &counter_ids[0],
                                counter_ids.size(),
                                &counters[0]) != STD_ERR_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "port pool stats get failed");
        return NAS_QOS_E_FAIL;
    }

    // return stats objects to cps-app
    cps_api_object_t ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL) {
        return cps_api_ret_code_ERR;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_PORT_POOL_STAT_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_PORT_POOL_STAT_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &(key.port_id), sizeof(uint32_t));
    cps_api_set_key_data(ret_obj, BASE_QOS_PORT_POOL_STAT_BUFFER_POOL_ID,
            cps_api_object_ATTR_T_U64,
            &(key.pool_id), sizeof(uint64_t));

    for (uint_t i=0; i< counter_ids.size(); i++) {
        cps_api_object_attr_add_u64(ret_obj, counter_ids[i], counters[i]);
    }

    return  cps_api_ret_code_OK;

}


/**
  * This function provides NAS-QoS port_pool stats CPS API clear function
  * User can use this function to clear the port_pool stats by setting relevant counters to zero
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_pool_stat_clear (void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    if (op != cps_api_oper_SET)
        return NAS_QOS_E_UNSUPPORTED;

    cps_api_object_attr_t port_id_attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_STAT_PORT_ID);
    cps_api_object_attr_t pool_id_attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_STAT_BUFFER_POOL_ID);

    if (port_id_attr == NULL ||
        pool_id_attr == NULL ) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Incomplete key: port-id, pool-id must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint32_t switch_id = 0;
    nas_qos_port_pool_key_t key;
    key.port_id = cps_api_object_attr_data_u32(port_id_attr);
    key.pool_id = cps_api_object_attr_data_u64(pool_id_attr);

    if (nas_is_virtual_port(key.port_id))
        return NAS_QOS_E_FAIL;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, port_id %u pool_id %lu stat\n",
            switch_id, key.port_id, key.pool_id);

    std_mutex_simple_lock_guard p_m(&port_pool_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_port_pool * port_pool_p = p_switch->get_port_pool(key.port_id, key.pool_id);
    if (port_pool_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "port pool not found");
        return NAS_QOS_E_FAIL;
    }

    std::vector<BASE_QOS_PORT_POOL_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        if (id == BASE_QOS_PORT_POOL_STAT_PORT_ID ||
            id == BASE_QOS_PORT_POOL_STAT_BUFFER_POOL_ID)
            continue; //key

        stat_attr_capability stat_attr;
        if (_port_pool_stat_attr_get(id, &stat_attr) != true) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown port_pool STAT flag: %lu, ignored", id);
            continue;
        }

        if (stat_attr.write_ok) {
            counter_ids.push_back((BASE_QOS_PORT_POOL_STAT_t)id);
        }
    }

    if (counter_ids.size() == 0) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "port pool stats clear without any valid counter ids");
        return NAS_QOS_E_FAIL;
    }

    ndi_port_t ndi_port = port_pool_p->get_ndi_port_id();
    if (ndi_qos_clear_port_pool_stats(ndi_port,
                                port_pool_p->ndi_obj_id(),
                                &counter_ids[0],
                                counter_ids.size()) != STD_ERR_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "port pool stats clear failed");
        return NAS_QOS_E_FAIL;
    }


    return  cps_api_ret_code_OK;

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
void nas_qos_port_pool_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd)
{

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-Port-Pool",
                     "switch_id cannot be found with npu_id %d",
                     ndi_port_id.npu_id);
        return ;
    }

    std::vector<nas_qos_port_pool *> port_pool_list;

    // Find the matching port pools under ifindex
    port_pool_iter_t it = p_switch->get_port_pool_it_begin();
    for ( ; it != p_switch->get_port_pool_it_end(); it++) {
        if (it->second.get_port_id() == ifindex) {
            port_pool_list.push_back(&(it->second));
        }
    }

    if (isAdd == false) {
        EV_LOGGING(QOS, NOTICE, "QOS-Port-Pool", "Disassociation ifindex %d from port %d",
                    ifindex, ndi_port_id.npu_port);

        // Remove Port Pools from NPU only
        for (auto port_pool: port_pool_list) {
            port_pool->push_delete_obj_to_npu(ndi_port_id.npu_id);
            port_pool->reset_ndi_obj_id();
            port_pool->clear_all_dirty_flags();
            port_pool->reset_npus();
            port_pool->mark_ndi_removed();
        }

        return;
    }
    else {
        EV_LOGGING(QOS, NOTICE, "QOS-Port-Pool", "Association ifindex %d to npu port %d",
                ifindex, ndi_port_id.npu_port);

        // update npu mapping
        for (auto port_pool: port_pool_list) {
            port_pool->set_ndi_port_id(ndi_port_id.npu_id, ndi_port_id.npu_port);
            port_pool->add_npu(ndi_port_id.npu_id);
            port_pool->mark_ndi_created();
        }

        // read DB and push to NPU
        cps_api_object_guard _og(cps_api_object_create());
        if(!_og.valid()){
            EV_LOGGING(QOS,ERR,"QOS-DB-GET","Failed to create object for db get");
            return;
        }

        cps_api_key_from_attr_with_qual(cps_api_object_key(_og.get()),
                BASE_QOS_PORT_POOL_OBJ,
                cps_api_qualifier_TARGET);
        cps_api_set_key_data(_og.get(), BASE_QOS_PORT_POOL_PORT_ID,
                cps_api_object_ATTR_T_U32,
                &ifindex, sizeof(uint32_t));
        cps_api_object_list_guard lst(cps_api_object_list_create());
        if (cps_api_db_get(_og.get(),lst.get())==cps_api_ret_code_OK) {
            size_t len = cps_api_object_list_size(lst.get());

            for (uint idx=0; idx< len; idx++) {

                cps_api_object_t db_obj = cps_api_object_list_get(lst.get(), idx);

                cps_api_key_set_attr(cps_api_object_key(db_obj), cps_api_oper_SET);

                // push the DB to NPU
                nas_qos_cps_api_port_pool_set(db_obj, NULL);

                EV_LOGGING(QOS, NOTICE,"QOS-Port-Pool",
                        "One Port Pool DB record for port %d written to NPU",
                        ifindex);
            }
        }
        return;
    }
}

