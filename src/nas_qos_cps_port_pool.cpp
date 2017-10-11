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

#include "cps_api_events.h"
#include "cps_api_operation.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"

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

static uint64_t get_stats_by_type(const nas_qos_port_pool_stat_counter_t *p,
                                BASE_QOS_PORT_POOL_STAT_t id);

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
            EV_LOGGING(QOS, NOTICE, "QOS", "Unrecognized option: %d", id);
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

    uint_t port_id = (port_id_attr? cps_api_object_attr_data_u32(port_id_attr): 0);

    cps_api_object_attr_t pool_id_attr = cps_api_get_key_data(obj, BASE_QOS_PORT_POOL_BUFFER_POOL_ID);

    nas_obj_id_t pool_id = (pool_id_attr? cps_api_object_attr_data_u64(pool_id_attr): 0);

    uint_t switch_id = 0;
    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, port_id %u, buffer_pool id %u\n",
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
        port_pool.commit_create(sav_obj? false: true);

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

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Create error code: %d ",
                    e.err_code);
        return NAS_QOS_E_FAIL;

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

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modify switch id %u, port_id %u, pool id %u\n",
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

        // update the local cache with newly set values
        *port_pool_p = port_pool;

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Attr Modify error code: %d ",
                    e.err_code);
        return NAS_QOS_E_FAIL;

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
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", " port_pool not found with port_id %u, pool_id %u\n",
                     port_id, pool_id);

        return NAS_QOS_E_FAIL;
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Deleting port_pool on switch: %u\n", p_switch->id());


    // delete
    try {
        port_pool_p->commit_delete(sav_obj? false: true);

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

    } catch (nas::base_exception & e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS port_pool Delete error code: %d ",
                    e.err_code);
        return NAS_QOS_E_FAIL;
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

static uint64_t _get_green_discard_dropped_packets (const nas_qos_port_pool_stat_counter_t *p) {
    return p->green_discard_dropped_packets ;
}
static uint64_t _get_green_discard_dropped_bytes (const nas_qos_port_pool_stat_counter_t *p) {
    return p->green_discard_dropped_bytes ;
}
static uint64_t _get_yellow_discard_dropped_packets (const nas_qos_port_pool_stat_counter_t *p) {
    return p->yellow_discard_dropped_packets ;
}
static uint64_t _get_yellow_discard_dropped_bytes (const nas_qos_port_pool_stat_counter_t *p) {
    return p->yellow_discard_dropped_bytes ;
}
static uint64_t _get_red_discard_dropped_packets (const nas_qos_port_pool_stat_counter_t *p) {
    return p->red_discard_dropped_packets ;
}
static uint64_t _get_red_discard_dropped_bytes (const nas_qos_port_pool_stat_counter_t *p) {
    return p->red_discard_dropped_bytes ;
}
static uint64_t _get_discard_dropped_packets (const nas_qos_port_pool_stat_counter_t *p) {
    return p->discard_dropped_packets ;
}
static uint64_t _get_discard_dropped_bytes (const nas_qos_port_pool_stat_counter_t *p) {
    return p->discard_dropped_bytes ;
}
static uint64_t _get_current_occupancy_bytes (const nas_qos_port_pool_stat_counter_t *p) {
    return p->current_occupancy_bytes ;
}
static uint64_t _get_watermark_bytes (const nas_qos_port_pool_stat_counter_t *p) {
    return p->watermark_bytes ;
}
static uint64_t _get_shared_current_occupancy_bytes (const nas_qos_port_pool_stat_counter_t *p) {
    return p->shared_current_occupancy_bytes ;
}
static uint64_t _get_shared_watermark_bytes (const nas_qos_port_pool_stat_counter_t *p) {
    return p->shared_watermark_bytes ;
}

typedef uint64_t (*_stat_get_fn) (const nas_qos_port_pool_stat_counter_t *p);

typedef struct _stat_attr_list_t {
    bool            read_ok;
    bool            write_ok;
    _stat_get_fn    fn;
}_stat_attr_list;

static bool _port_pool_stat_attr_get(nas_attr_id_t attr_id, _stat_attr_list * p_stat_attr)
{

    static const auto &  _port_pool_attr_map =
            * new std::unordered_map<nas_attr_id_t, _stat_attr_list, std::hash<int>>
    {

        {BASE_QOS_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_PACKETS,
                {true, true, _get_green_discard_dropped_packets}},
        {BASE_QOS_PORT_POOL_STAT_GREEN_DISCARD_DROPPED_BYTES,
                {true, true, _get_green_discard_dropped_bytes}},
        {BASE_QOS_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_PACKETS,
                {true, true, _get_yellow_discard_dropped_packets}},
        {BASE_QOS_PORT_POOL_STAT_YELLOW_DISCARD_DROPPED_BYTES,
                {true, true, _get_yellow_discard_dropped_bytes}},
        {BASE_QOS_PORT_POOL_STAT_RED_DISCARD_DROPPED_PACKETS,
                {true, true, _get_red_discard_dropped_packets}},
        {BASE_QOS_PORT_POOL_STAT_RED_DISCARD_DROPPED_BYTES,
                {true, true, _get_red_discard_dropped_bytes}},
        {BASE_QOS_PORT_POOL_STAT_DISCARD_DROPPED_PACKETS,
                {true, true, _get_discard_dropped_packets}},
        {BASE_QOS_PORT_POOL_STAT_DISCARD_DROPPED_BYTES,
                {true, true, _get_discard_dropped_bytes}},
        {BASE_QOS_PORT_POOL_STAT_CURRENT_OCCUPANCY_BYTES,
                {true, false, _get_current_occupancy_bytes}},
        {BASE_QOS_PORT_POOL_STAT_WATERMARK_BYTES,
                {true, true, _get_watermark_bytes}},
        {BASE_QOS_PORT_POOL_STAT_SHARED_CURRENT_OCCUPANCY_BYTES,
                {true, false, _get_shared_current_occupancy_bytes}},
        {BASE_QOS_PORT_POOL_STAT_SHARED_WATERMARK_BYTES,
                {true, true, _get_shared_watermark_bytes}},
       };

    try {
        *p_stat_attr = _port_pool_attr_map.at(attr_id);
    }
    catch (...) {
        return false;
    }
    return true;
}


static uint64_t get_stats_by_type(const nas_qos_port_pool_stat_counter_t *p,
                                BASE_QOS_PORT_POOL_STAT_t id)
{
    _stat_attr_list stat_attr;
    if (_port_pool_stat_attr_get(id, &stat_attr) != true)
        return 0;

    return stat_attr.fn(p);
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

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, port_id %u pool_id %u stat\n",
            switch_id, key.port_id, key.pool_id);

    std_mutex_simple_lock_guard p_m(&port_pool_mutex);

    nas_qos_switch *switch_p = nas_qos_get_switch(switch_id);
    if (switch_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_port_pool * port_pool_p = switch_p->get_port_pool(key.port_id, key.pool_id);
    if (port_pool_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Port Pool not found");
        return NAS_QOS_E_FAIL;
    }

    nas_qos_port_pool_stat_counter_t stats = {0};
    std::vector<BASE_QOS_PORT_POOL_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        if (id == BASE_QOS_PORT_POOL_STAT_PORT_ID ||
            id == BASE_QOS_PORT_POOL_STAT_BUFFER_POOL_ID)
            continue; //key

        _stat_attr_list stat_attr;
        if (_port_pool_stat_attr_get(id, &stat_attr) != true) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown port_pool STAT flag: %u, ignored", id);
            continue;
        }

        if (stat_attr.read_ok) {
            counter_ids.push_back((BASE_QOS_PORT_POOL_STAT_t)id);
        }
    }

    ndi_port_t ndi_port = port_pool_p->get_ndi_port_id();
    if (ndi_qos_get_port_pool_stats(ndi_port,
                                port_pool_p->ndi_obj_id(),
                                &counter_ids[0],
                                counter_ids.size(),
                                &stats) != STD_ERR_OK) {
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

    uint64_t val64;
    for (uint_t i=0; i< counter_ids.size(); i++) {
        BASE_QOS_PORT_POOL_STAT_t id = counter_ids[i];
        val64 = get_stats_by_type(&stats, id);
        cps_api_object_attr_add_u64(ret_obj, id, val64);
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

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, port_id %u pool_id %u stat\n",
            switch_id, key.port_id, key.pool_id);

    std_mutex_simple_lock_guard p_m(&port_pool_mutex);

    nas_qos_switch *switch_p = nas_qos_get_switch(switch_id);
    if (switch_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_port_pool * port_pool_p = switch_p->get_port_pool(key.port_id, key.pool_id);
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

        _stat_attr_list stat_attr;
        if (_port_pool_stat_attr_get(id, &stat_attr) != true) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown port_pool STAT flag: %u, ignored", id);
            continue;
        }

        if (stat_attr.write_ok) {
            counter_ids.push_back((BASE_QOS_PORT_POOL_STAT_t)id);
        }
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
