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
 * \file   nas_qos_cps_buffer_pool.cpp
 * \brief  NAS qos buffer_pool related CPS API routines
 * \date   05-2016
 * \author
 */

#include "cps_api_events.h"
#include "cps_api_operation.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"

#include "event_log_types.h"
#include "event_log.h"
#include "std_error_codes.h"
#include "std_mutex_lock.h"

#include "nas_qos_common.h"
#include "nas_qos_switch_list.h"
#include "nas_qos_cps.h"
#include "cps_api_key.h"
#include "dell-base-qos.h"
#include "nas_qos_buffer_pool.h"

/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_buffer_pool &buffer_pool);
static cps_api_return_code_t nas_qos_store_prev_attr(cps_api_object_t obj,
                                                const nas::attr_set_t attr_set,
                                                const nas_qos_buffer_pool &buffer_pool);
static nas_qos_buffer_pool * nas_qos_cps_get_buffer_pool(uint_t switch_id,
                                           nas_obj_id_t buffer_pool_id);
static cps_api_return_code_t nas_qos_cps_get_switch_and_buffer_pool_id(
                                    cps_api_object_t obj,
                                    uint_t &switch_id,
                                    nas_obj_id_t &buffer_pool_id);
static cps_api_return_code_t nas_qos_cps_api_buffer_pool_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_buffer_pool_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_buffer_pool_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t _append_one_buffer_pool(cps_api_get_params_t * param,
                                        uint_t switch_id,
                                        nas_qos_buffer_pool *buffer_pool,
                                        uint_t nas_mmu_index);

static std_mutex_lock_create_static_init_rec(buffer_pool_mutex);

/**
  * This function provides NAS-QoS buffer_pool CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_buffer_pool_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&buffer_pool_mutex);

    switch (op) {
    case cps_api_oper_CREATE:
        return nas_qos_cps_api_buffer_pool_create(obj, param->prev);

    case cps_api_oper_SET:
        return nas_qos_cps_api_buffer_pool_set(obj, param->prev);

    case cps_api_oper_DELETE:
        return nas_qos_cps_api_buffer_pool_delete(obj, param->prev);

    default:
        return NAS_QOS_E_UNSUPPORTED;
    }
}


/**
  * This function provides NAS-QoS buffer_pool CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_buffer_pool_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t buffer_pool_id_attr = cps_api_get_key_data(obj, BASE_QOS_BUFFER_POOL_ID);

    uint_t switch_id = 0;
    nas_obj_id_t buffer_pool_id = (buffer_pool_id_attr?
                                    cps_api_object_attr_data_u64(buffer_pool_id_attr): 0);

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, buffer_pool id %lu\n",
                    switch_id, buffer_pool_id);

    std_mutex_simple_lock_guard p_m(&buffer_pool_mutex);

    nas_qos_switch * p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    // convert to MMU buffer_pool id if necessary
    cps_api_object_attr_t mmu_index_attr = cps_api_get_key_data(obj, BASE_QOS_BUFFER_POOL_MMU_INDEX);
    uint_t nas_mmu_index = (mmu_index_attr? cps_api_object_attr_data_u32(mmu_index_attr): 0);

    cps_api_return_code_t rc = cps_api_ret_code_ERR;
    nas_qos_buffer_pool *buffer_pool;
    if (buffer_pool_id) {
        buffer_pool = p_switch->get_buffer_pool(buffer_pool_id);
        if (buffer_pool == NULL)
            return NAS_QOS_E_FAIL;

        rc = _append_one_buffer_pool(param, switch_id, buffer_pool, nas_mmu_index);
    }
    else {
        for (buffer_pool_iter_t it = p_switch->get_buffer_pool_it_begin();
                it != p_switch->get_buffer_pool_it_end();
                it++) {

            buffer_pool = &it->second;
            rc = _append_one_buffer_pool(param, switch_id, buffer_pool, nas_mmu_index);
            if (rc != cps_api_ret_code_OK)
                return rc;
        }
    }

    return rc;
}

static cps_api_return_code_t _append_one_buffer_pool(cps_api_get_params_t * param,
                                        uint_t switch_id,
                                        nas_qos_buffer_pool *buffer_pool,
                                        uint_t nas_mmu_index)
{
    nas_obj_id_t buffer_pool_id = buffer_pool->get_buffer_pool_id();

    /* fill in data */
    cps_api_object_t ret_obj;

    ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL){
        return cps_api_ret_code_ERR;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_BUFFER_POOL_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_BUFFER_POOL_ID,
            cps_api_object_ATTR_T_U64,
            &buffer_pool_id, sizeof(uint64_t));
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_BUFFER_POOL_MMU_INDEX, nas_mmu_index);
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_BUFFER_POOL_SHARED_SIZE,
            buffer_pool->get_shared_size(nas_mmu_index));
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_BUFFER_POOL_POOL_TYPE,
            buffer_pool->get_type());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_BUFFER_POOL_SIZE,
            buffer_pool->get_size(nas_mmu_index));
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_BUFFER_POOL_THRESHOLD_MODE,
            buffer_pool->get_threshold_mode());

    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_BUFFER_POOL_XOFF_SIZE,
            buffer_pool->get_xoff_size());

    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_BUFFER_POOL_WRED_PROFILE_ID,
            buffer_pool->get_wred_profile_id());

    return cps_api_ret_code_OK;
}


/**
  * This function provides NAS-QoS buffer_pool CPS API rollback function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_buffer_pool_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->prev,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&buffer_pool_mutex);

    if (op == cps_api_oper_CREATE) {
        nas_qos_cps_api_buffer_pool_delete(obj, NULL);
    }

    if (op == cps_api_oper_SET) {
        nas_qos_cps_api_buffer_pool_set(obj, NULL);
    }

    if (op == cps_api_oper_DELETE) {
        nas_qos_cps_api_buffer_pool_create(obj, NULL);
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_buffer_pool_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{

    uint_t switch_id = 0;
    nas_obj_id_t buffer_pool_id = NAS_QOS_NULL_OBJECT_ID;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

   nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    nas_qos_buffer_pool buffer_pool(p_switch);

    if ((rc = nas_qos_cps_parse_attr(obj, buffer_pool)) != cps_api_ret_code_OK)
        return rc;

    try {
        (void)nas_qos_cps_get_switch_and_buffer_pool_id(obj, switch_id, buffer_pool_id);

        if (buffer_pool_id == NAS_QOS_NULL_OBJECT_ID) {
            buffer_pool_id = p_switch->alloc_buffer_pool_id();
        }
        else {
            // assign user-specified id
            if (p_switch->reserve_buffer_pool_id(buffer_pool_id) != true) {
                EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Buffer pool id is being used. Creation failed");
                return NAS_QOS_E_FAIL;
            }
        }

        buffer_pool.set_buffer_pool_id(buffer_pool_id);

        buffer_pool.commit_create(sav_obj? false: true);

        p_switch->add_buffer_pool(buffer_pool);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Created new buffer_pool %lu\n",
                     buffer_pool.get_buffer_pool_id());

        // update obj with new buffer_pool-id attr and key
        cps_api_set_key_data(obj, BASE_QOS_BUFFER_POOL_ID,
                cps_api_object_ATTR_T_U64,
                &buffer_pool_id, sizeof(uint64_t));

        // save for rollback if caller requests it.
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (tmp_obj == NULL) {
                 return cps_api_ret_code_ERR;
            }
            cps_api_object_clone(tmp_obj, obj);
        }

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS buffer_pool Create error code: %d ",
                    e.err_code);
        if (buffer_pool_id)
            p_switch->release_buffer_pool_id(buffer_pool_id);

        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS buffer_pool Create Unexpected error code");
        if (buffer_pool_id)
            p_switch->release_buffer_pool_id(buffer_pool_id);

        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_buffer_pool_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{

    uint_t switch_id = 0;
    nas_obj_id_t buffer_pool_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    if ((rc = nas_qos_cps_get_switch_and_buffer_pool_id(obj, switch_id, buffer_pool_id))
            !=    cps_api_ret_code_OK)
        return rc;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modify switch id %u, buffer_pool id %lu\n",
                    switch_id, buffer_pool_id);

    nas_qos_buffer_pool * buffer_pool_p = nas_qos_cps_get_buffer_pool(switch_id, buffer_pool_id);
    if (buffer_pool_p == NULL) {
        return NAS_QOS_E_FAIL;
    }

    /* make a local copy of the existing buffer_pool */
    nas_qos_buffer_pool buffer_pool(*buffer_pool_p);

    if ((rc = nas_qos_cps_parse_attr(obj, buffer_pool)) != cps_api_ret_code_OK)
        return rc;

    try {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modifying buffer_pool %lu attr \n",
                     buffer_pool.get_buffer_pool_id());

        nas::attr_set_t modified_attr_list = buffer_pool.commit_modify(
                                        *buffer_pool_p, (sav_obj? false: true));

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "done with commit_modify \n");


        // set attribute with full copy
        // save rollback info if caller requests it.
        // use modified attr list, current buffer_pool value
        if (sav_obj) {
            cps_api_object_t tmp_obj;
            tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (tmp_obj == NULL) {
                return cps_api_ret_code_ERR;
            }

            nas_qos_store_prev_attr(tmp_obj, modified_attr_list, *buffer_pool_p);

       }

        // update the local cache with newly set values
        *buffer_pool_p = buffer_pool;

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS buffer_pool Attr Modify error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS buffer_pool Modify Unexpected error code");
        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_cps_api_buffer_pool_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{
    uint_t switch_id = 0;
    nas_obj_id_t buffer_pool_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    if ((rc = nas_qos_cps_get_switch_and_buffer_pool_id(obj, switch_id, buffer_pool_id))
            !=    cps_api_ret_code_OK)
        return rc;


    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", " switch: %u not found\n",
                     switch_id);
           return NAS_QOS_E_FAIL;
    }

    nas_qos_buffer_pool *buffer_pool_p = p_switch->get_buffer_pool(buffer_pool_id);
    if (buffer_pool_p == NULL) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", " buffer_pool id: %lu not found\n",
                     buffer_pool_id);

        return NAS_QOS_E_FAIL;
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Deleting buffer_pool %lu on switch: %u\n",
                 buffer_pool_p->get_buffer_pool_id(), p_switch->id());


    // delete
    try {
        buffer_pool_p->commit_delete(sav_obj? false: true);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Saving deleted buffer_pool %lu\n",
                     buffer_pool_p->get_buffer_pool_id());

         // save current buffer_pool config for rollback if caller requests it.
        // use existing set_mask, existing config
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (tmp_obj == NULL) {
                return cps_api_ret_code_ERR;
            }
            nas_qos_store_prev_attr(tmp_obj, buffer_pool_p->set_attr_list(), *buffer_pool_p);
        }

        p_switch->remove_buffer_pool(buffer_pool_p->get_buffer_pool_id());

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS buffer_pool Delete error code: %d ",
                    e.err_code);
        return e.err_code;
    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS buffer_pool Delete: Unexpected error");
        return NAS_QOS_E_FAIL;
    }


    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_get_switch_and_buffer_pool_id(
                                    cps_api_object_t obj,
                                    uint_t &switch_id,
                                    nas_obj_id_t &buffer_pool_id)
{
    cps_api_object_attr_t buffer_pool_id_attr = cps_api_get_key_data(obj, BASE_QOS_BUFFER_POOL_ID);

    if (buffer_pool_id_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "QOS", "buffer_pool id not exist in message");
        return NAS_QOS_E_MISSING_KEY;
    }

    switch_id = 0;
    buffer_pool_id = cps_api_object_attr_data_u64(buffer_pool_id_attr);

    return cps_api_ret_code_OK;

}

/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_buffer_pool &buffer_pool)
{
    uint_t val;
    uint64_t lval;
    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch (id) {
        case BASE_QOS_BUFFER_POOL_ID:
        case BASE_QOS_BUFFER_POOL_MMU_INDEX:  //ignored in SET
            break; // These are for part of the keys

        case BASE_QOS_BUFFER_POOL_SHARED_SIZE:
            break; // READ-ONLY attribute

        case BASE_QOS_BUFFER_POOL_POOL_TYPE:
            val = cps_api_object_attr_data_u32(it.attr);
            buffer_pool.mark_attr_dirty(id);
            buffer_pool.set_type((BASE_QOS_BUFFER_POOL_TYPE_t)val);
            break;

        case BASE_QOS_BUFFER_POOL_SIZE:
            val = cps_api_object_attr_data_u32(it.attr);
            buffer_pool.mark_attr_dirty(id);
            buffer_pool.set_size(val);
            break;

        case BASE_QOS_BUFFER_POOL_THRESHOLD_MODE:
            val = cps_api_object_attr_data_u32(it.attr);
            buffer_pool.mark_attr_dirty(id);
            buffer_pool.set_threshold_mode((BASE_QOS_BUFFER_THRESHOLD_MODE_t)val);
            break;

        case BASE_QOS_BUFFER_POOL_XOFF_SIZE:
            val = cps_api_object_attr_data_u32(it.attr);
            buffer_pool.mark_attr_dirty(id);
            buffer_pool.set_xoff_size(val);
            break;

        case BASE_QOS_BUFFER_POOL_WRED_PROFILE_ID:
            lval = cps_api_object_attr_data_u64(it.attr);
            buffer_pool.mark_attr_dirty(id);
            buffer_pool.set_wred_profile_id(lval);
            break;

        case CPS_API_ATTR_RESERVE_RANGE_END:
            // skip keys
            break;

        default:
            EV_LOGGING(QOS, NOTICE, "QOS", "Unrecognized option: %ld", id);
            return NAS_QOS_E_UNSUPPORTED;
        }
    }


    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_store_prev_attr(cps_api_object_t obj,
                                                    const nas::attr_set_t attr_set,
                                                    const nas_qos_buffer_pool &buffer_pool)
{
    // filling in the keys
    nas_obj_id_t buffer_pool_id = buffer_pool.get_buffer_pool_id();
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_BUFFER_POOL_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_BUFFER_POOL_ID,
            cps_api_object_ATTR_T_U64,
            &buffer_pool_id, sizeof(uint64_t));


    for (auto attr_id: attr_set) {
        switch (attr_id) {
        case BASE_QOS_BUFFER_POOL_ID:
            /* key */
            break;

        case BASE_QOS_BUFFER_POOL_SHARED_SIZE:
            // READ-only
            break;

        case BASE_QOS_BUFFER_POOL_POOL_TYPE:
            cps_api_object_attr_add_u32(obj, attr_id, buffer_pool.get_type());
            break;

        case BASE_QOS_BUFFER_POOL_SIZE:
            cps_api_object_attr_add_u32(obj, attr_id,
                    const_cast<nas_qos_buffer_pool&>(buffer_pool).get_size());
            break;

        case BASE_QOS_BUFFER_POOL_THRESHOLD_MODE:
            cps_api_object_attr_add_u32(obj, attr_id, buffer_pool.get_threshold_mode());
            break;

        case BASE_QOS_BUFFER_POOL_XOFF_SIZE:
            cps_api_object_attr_add_u32(obj, attr_id, buffer_pool.get_xoff_size());
            break;

        case BASE_QOS_BUFFER_POOL_WRED_PROFILE_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    buffer_pool.get_wred_profile_id());
            break;

        default:
            break;
        }
    }

    return cps_api_ret_code_OK;
}

static nas_qos_buffer_pool * nas_qos_cps_get_buffer_pool(uint_t switch_id,
                                           nas_obj_id_t buffer_pool_id)
{

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NULL;

    nas_qos_buffer_pool *buffer_pool_p = p_switch->get_buffer_pool(buffer_pool_id);

    return buffer_pool_p;
}


static bool _buffer_pool_stat_attr_get(nas_attr_id_t attr_id, stat_attr_capability * p_stat_attr)
{

    static const auto &  _buffer_pool_attr_map =
            * new std::unordered_map<nas_attr_id_t, stat_attr_capability, std::hash<int>>
    {

        {BASE_QOS_BUFFER_POOL_STAT_GREEN_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_GREEN_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_YELLOW_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_YELLOW_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_RED_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_RED_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_DISCARD_DROPPED_PACKETS,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_DISCARD_DROPPED_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_CURRENT_OCCUPANCY_BYTES,
                {true, false}},
        {BASE_QOS_BUFFER_POOL_STAT_WATERMARK_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_XOFF_HEADROOM_OCCUPANCY_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_XOFF_HEADROOM_WATERMARK_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_GREEN_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_GREEN_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_YELLOW_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_YELLOW_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_RED_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_RED_WRED_ECN_MARKED_BYTES,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_WRED_ECN_MARKED_PACKETS,
                {true, true}},
        {BASE_QOS_BUFFER_POOL_STAT_WRED_ECN_MARKED_BYTES,
                {true, true}},

    };

    try {
        *p_stat_attr = _buffer_pool_attr_map.at(attr_id);
    }
    catch (...) {
        return false;
    }
    return true;
}




/**
  * This function provides NAS-QoS buffer_pool stats CPS API read function
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_buffer_pool_stat_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{

    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t id_attr = cps_api_get_key_data(obj, BASE_QOS_BUFFER_POOL_STAT_ID);

    if (id_attr == NULL) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                "Incomplete key: buffer pool id must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    nas_obj_id_t buffer_pool_id = cps_api_object_attr_data_u64(id_attr);
    uint32_t switch_id = 0;

    cps_api_object_attr_t mmu_index_attr = cps_api_get_key_data(obj, BASE_QOS_BUFFER_POOL_STAT_MMU_INDEX);
    uint_t nas_mmu_index = (mmu_index_attr? cps_api_object_attr_data_u32(mmu_index_attr): 0);


    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, buffer_pool_id %lu stat\n",
            switch_id, buffer_pool_id);

    std_mutex_simple_lock_guard p_m(&buffer_pool_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_buffer_pool * buffer_pool_p = p_switch->get_buffer_pool(buffer_pool_id);
    if (buffer_pool_p == NULL) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "buffer_pool not found");
        return NAS_QOS_E_FAIL;
    }

    std::vector<BASE_QOS_BUFFER_POOL_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        if (id == BASE_QOS_BUFFER_POOL_STAT_ID)
            continue; //key

        stat_attr_capability stat_attr;
        if (_buffer_pool_stat_attr_get(id, &stat_attr) != true) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown buffer_pool STAT flag: %lu, ignored", id);
            continue;
        }

        if (stat_attr.read_ok) {
            counter_ids.push_back((BASE_QOS_BUFFER_POOL_STAT_t)id);
        }
    }

    if (counter_ids.size() == 0) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "buffer pool stats get without any valid counter ids");
        return NAS_QOS_E_FAIL;
    }

    std::vector<uint64_t> counters(counter_ids.size());

    npu_id_t npu_id;
    if (buffer_pool_p->get_first_npu_id(npu_id) == false) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "npu_id not available, buffer_pool stats read failed");
        return NAS_QOS_E_FAIL;
    }

    ndi_obj_id_t ndi_pool_id = buffer_pool_p->ndi_obj_id(npu_id);
    if (nas_mmu_index)
        ndi_pool_id = buffer_pool_p->get_shadow_buffer_pool_id(nas_mmu_index);

    if (ndi_qos_get_buffer_pool_statistics(npu_id,
                                ndi_pool_id,
                                &counter_ids[0],
                                counter_ids.size(),
                                &counters[0]) != STD_ERR_OK) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "buffer_pool stats get failed");
        return NAS_QOS_E_FAIL;
    }

    // return stats objects to cps-app
    cps_api_object_t ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL) {
        return cps_api_ret_code_ERR;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_BUFFER_POOL_STAT_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_BUFFER_POOL_STAT_ID,
            cps_api_object_ATTR_T_U64,
            &buffer_pool_id, sizeof(uint64_t));

    for (uint_t i=0; i< counter_ids.size(); i++) {
        cps_api_object_attr_add_u64(ret_obj, counter_ids[i], counters[i]);
    }

    return  cps_api_ret_code_OK;

}



/**
  * This function provides NAS-QoS buffer_pool stats CPS API clear function
  * User can use this function to clear the buffer_pool stats by setting relevant counters to zero
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_buffer_pool_stat_clear (void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    if (op != cps_api_oper_SET)
        return NAS_QOS_E_UNSUPPORTED;

    cps_api_object_attr_t pool_id_attr = cps_api_get_key_data(obj, BASE_QOS_BUFFER_POOL_STAT_ID);

    if (pool_id_attr == NULL ) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "Incomplete key: pool-id must be specified\n");
        return NAS_QOS_E_MISSING_KEY;
    }

    uint32_t switch_id = 0;
    nas_obj_id_t buffer_pool_id = cps_api_object_attr_data_u64(pool_id_attr);

    cps_api_object_attr_t mmu_index_attr = cps_api_get_key_data(obj, BASE_QOS_BUFFER_POOL_STAT_MMU_INDEX);
    uint_t nas_mmu_index = (mmu_index_attr? cps_api_object_attr_data_u32(mmu_index_attr): 0);

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Read switch id %u, pool_id %lu stat\n",
            switch_id, buffer_pool_id);

    std_mutex_simple_lock_guard p_m(&buffer_pool_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "switch_id %u not found", switch_id);
        return NAS_QOS_E_FAIL;
    }

    nas_qos_buffer_pool * buffer_pool_p = p_switch->get_buffer_pool(buffer_pool_id);
    if (buffer_pool_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "buffer pool not found");
        return NAS_QOS_E_FAIL;
    }

    std::vector<BASE_QOS_BUFFER_POOL_STAT_t> counter_ids;

    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        if (id == BASE_QOS_BUFFER_POOL_STAT_ID)
            continue; //key

        stat_attr_capability stat_attr;
        if (_buffer_pool_stat_attr_get(id, &stat_attr) != true) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Unknown buffer_pool STAT flag: %lu, ignored", id);
            continue;
        }

        if (stat_attr.write_ok) {
            counter_ids.push_back((BASE_QOS_BUFFER_POOL_STAT_t)id);
        }
    }

    if (counter_ids.size() == 0) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "buffer pool stats clear without any valid counter ids");
        return NAS_QOS_E_FAIL;
    }

    npu_id_t npu_id;
    if (buffer_pool_p->get_first_npu_id(npu_id) == false) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                "npu_id not available, buffer_pool stats clear failed");
        return NAS_QOS_E_FAIL;
    }

    ndi_obj_id_t ndi_pool_id = buffer_pool_p->ndi_obj_id(npu_id);
    if (nas_mmu_index)
        ndi_pool_id = buffer_pool_p->get_shadow_buffer_pool_id(nas_mmu_index);

    if (ndi_qos_clear_buffer_pool_stats(npu_id,
                                ndi_pool_id,
                                &counter_ids[0],
                                counter_ids.size()) != STD_ERR_OK) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "buffer pool stats clear failed");
        return NAS_QOS_E_FAIL;
    }


    return  cps_api_ret_code_OK;

}
