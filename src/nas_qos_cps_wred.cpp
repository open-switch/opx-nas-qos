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
 * \file   nas_qos_cps_wred.cpp
 * \brief  NAS qos wred related CPS API routines
 * \date   05-2015
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
#include "nas_qos_wred.h"

/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_wred &wred);
static cps_api_return_code_t nas_qos_store_prev_attr(cps_api_object_t obj,
                                                const nas::attr_set_t attr_set,
                                                const nas_qos_wred &wred);
static nas_qos_wred * nas_qos_cps_get_wred(uint_t switch_id,
                                           nas_obj_id_t wred_id);
static cps_api_return_code_t nas_qos_cps_get_switch_and_wred_id(
                                    cps_api_object_t obj,
                                    uint_t &switch_id,
                                    nas_obj_id_t &wred_id);
static cps_api_return_code_t nas_qos_cps_api_wred_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_wred_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t nas_qos_cps_api_wred_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj);
static cps_api_return_code_t _append_one_wred(cps_api_get_params_t * param,
                                        uint_t switch_id,
                                        nas_qos_wred *wred);

static std_mutex_lock_create_static_init_rec(wred_mutex);

/**
  * This function provides NAS-QoS WRED CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_wred_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&wred_mutex);

    switch (op) {
    case cps_api_oper_CREATE:
        return nas_qos_cps_api_wred_create(obj, param->prev);

    case cps_api_oper_SET:
        return nas_qos_cps_api_wred_set(obj, param->prev);

    case cps_api_oper_DELETE:
        return nas_qos_cps_api_wred_delete(obj, param->prev);

    default:
        return NAS_QOS_E_UNSUPPORTED;
    }
}


/**
  * This function provides NAS-QoS WRED CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_wred_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);
    cps_api_object_attr_t wred_id_attr = cps_api_get_key_data(obj, BASE_QOS_WRED_PROFILE_ID);

    uint_t switch_id = 0;
    nas_obj_id_t wred_id = (wred_id_attr? cps_api_object_attr_data_u64(wred_id_attr): 0);

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, wred id %lu\n",
                    switch_id, wred_id);

    std_mutex_simple_lock_guard p_m(&wred_mutex);

    nas_qos_switch * p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    cps_api_return_code_t rc = cps_api_ret_code_ERR;
    nas_qos_wred *wred;
    if (wred_id) {
        wred = p_switch->get_wred(wred_id);
        if (wred == NULL)
            return NAS_QOS_E_FAIL;

        rc = _append_one_wred(param, switch_id, wred);
    }
    else {
        for (wred_iter_t it = p_switch->get_wred_it_begin();
                it != p_switch->get_wred_it_end();
                it++) {

            wred = &it->second;
            rc = _append_one_wred(param, switch_id, wred);
            if (rc != cps_api_ret_code_OK)
                return rc;
        }
    }

    return rc;
}

static cps_api_return_code_t _append_one_wred(cps_api_get_params_t * param,
                                        uint_t switch_id,
                                        nas_qos_wred *wred)
{
    nas_obj_id_t wred_id = wred->get_wred_id();

    /* fill in data */
    cps_api_object_t ret_obj;

    ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL){
        return cps_api_ret_code_ERR;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_WRED_PROFILE_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_WRED_PROFILE_SWITCH_ID,
            cps_api_object_ATTR_T_U32,
            &switch_id, sizeof(uint32_t));
    cps_api_set_key_data(ret_obj, BASE_QOS_WRED_PROFILE_ID,
            cps_api_object_ATTR_T_U64,
            &wred_id, sizeof(uint64_t));
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_GREEN_ENABLE, wred->get_g_enable());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_GREEN_MIN_THRESHOLD, wred->get_g_min());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_GREEN_MAX_THRESHOLD, wred->get_g_max());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_GREEN_DROP_PROBABILITY, wred->get_g_drop_prob());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_YELLOW_ENABLE, wred->get_y_enable());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_YELLOW_MIN_THRESHOLD, wred->get_y_min());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_YELLOW_MAX_THRESHOLD, wred->get_y_max());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_YELLOW_DROP_PROBABILITY, wred->get_y_drop_prob());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_RED_ENABLE, wred->get_r_enable());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_RED_MIN_THRESHOLD, wred->get_r_min());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_RED_MAX_THRESHOLD, wred->get_r_max());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_RED_DROP_PROBABILITY, wred->get_r_drop_prob());

    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_GREEN_MIN_THRESHOLD, wred->get_ecn_g_min());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_GREEN_MAX_THRESHOLD, wred->get_ecn_g_max());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_GREEN_PROBABILITY,   wred->get_ecn_g_prob());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_YELLOW_MIN_THRESHOLD, wred->get_ecn_y_min());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_YELLOW_MAX_THRESHOLD, wred->get_ecn_y_max());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_YELLOW_PROBABILITY,   wred->get_ecn_y_prob());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_RED_MIN_THRESHOLD, wred->get_ecn_r_min());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_RED_MAX_THRESHOLD, wred->get_ecn_r_max());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_RED_PROBABILITY,   wred->get_ecn_r_prob());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MIN_THRESHOLD, wred->get_ecn_c_min());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MAX_THRESHOLD, wred->get_ecn_c_max());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_PROBABILITY,   wred->get_ecn_c_prob());

    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_WEIGHT, wred->get_weight());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_ENABLE, wred->get_ecn_enable());
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_ECN_MARK, wred->get_ecn_mark());

    // add the list of NPUs
    for (auto npu_id: wred->npu_list()) {
        cps_api_object_attr_add_u32(ret_obj, BASE_QOS_WRED_PROFILE_NPU_ID_LIST, npu_id);
    }

    return cps_api_ret_code_OK;
}


/**
  * This function provides NAS-QoS WRED CPS API rollback function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_wred_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->prev,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&wred_mutex);

    if (op == cps_api_oper_CREATE) {
        nas_qos_cps_api_wred_delete(obj, NULL);
    }

    if (op == cps_api_oper_SET) {
        nas_qos_cps_api_wred_set(obj, NULL);
    }

    if (op == cps_api_oper_DELETE) {
        nas_qos_cps_api_wred_create(obj, NULL);
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_wred_create(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{

    uint_t switch_id = 0;
    nas_obj_id_t wred_id = NAS_QOS_NULL_OBJECT_ID;

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NAS_QOS_E_FAIL;

    nas_qos_wred wred(p_switch);

    if (nas_qos_cps_parse_attr(obj, wred) != cps_api_ret_code_OK)
        return NAS_QOS_E_FAIL;

    try {
        (void) nas_qos_cps_get_switch_and_wred_id(obj, switch_id, wred_id);

        if (wred_id == NAS_QOS_NULL_OBJECT_ID) {
            wred_id = p_switch->alloc_wred_id();
        }
        else {
               // assign user-specified id
            if (p_switch->reserve_wred_id(wred_id) != true) {
                EV_LOGGING(QOS, DEBUG, "NAS-QOS", "WRED id is being used. Creation failed");
                return NAS_QOS_E_FAIL;
            }
        }

        wred.set_wred_id(wred_id);

        wred.commit_create(sav_obj? false: true);

        p_switch->add_wred(wred);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Created new wred %lu\n",
                     wred.get_wred_id());

        // update obj with new wred-id attr and key
        cps_api_set_key_data(obj, BASE_QOS_WRED_PROFILE_ID,
                cps_api_object_ATTR_T_U64,
                &wred_id, sizeof(uint64_t));

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
                    "NAS WRED Create error code: %d ",
                    e.err_code);
        if (wred_id)
            p_switch->release_wred_id(wred_id);

        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS WRED Create Unexpected error code");
        if (wred_id)
            p_switch->release_wred_id(wred_id);

        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_wred_set(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{

    uint_t switch_id = 0;
    nas_obj_id_t wred_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;
    if ((rc = nas_qos_cps_get_switch_and_wred_id(obj, switch_id, wred_id))
            !=    cps_api_ret_code_OK)
        return rc;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modify switch id %u, wred id %lu\n",
                    switch_id, wred_id);

    nas_qos_wred * wred_p = nas_qos_cps_get_wred(switch_id, wred_id);
    if (wred_p == NULL) {
        return NAS_QOS_E_FAIL;
    }

    /* make a local copy of the existing wred */
    nas_qos_wred wred(*wred_p);

    if ((rc = nas_qos_cps_parse_attr(obj, wred)) != cps_api_ret_code_OK)
        return rc;

    try {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modifying wred %lu attr \n",
                     wred.get_wred_id());

        nas::attr_set_t modified_attr_list = wred.commit_modify(*wred_p, (sav_obj? false: true));

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "done with commit_modify \n");


        // set attribute with full copy
        // save rollback info if caller requests it.
        // use modified attr list, current wred value
        if (sav_obj) {
            cps_api_object_t tmp_obj;
            tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (tmp_obj == NULL) {
                return cps_api_ret_code_ERR;
            }

            nas_qos_store_prev_attr(tmp_obj, modified_attr_list, *wred_p);

       }

        // update the local cache with newly set values
        *wred_p = wred;

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS WRED Attr Modify error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS WRED Modify Unexpected error code");
        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_cps_api_wred_delete(
                                cps_api_object_t obj,
                                cps_api_object_list_t sav_obj)
{
    uint_t switch_id = 0;
    nas_obj_id_t wred_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;
    if ((rc = nas_qos_cps_get_switch_and_wred_id(obj, switch_id, wred_id))
            !=    cps_api_ret_code_OK)
        return rc;


    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", " switch: %u not found\n",
                     switch_id);
           return NAS_QOS_E_FAIL;
    }

    nas_qos_wred *wred_p = p_switch->get_wred(wred_id);
    if (wred_p == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", " wred id: %lu not found\n",
                     wred_id);

        return NAS_QOS_E_FAIL;
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Deleting wred %lu on switch: %u\n",
                 wred_p->get_wred_id(), p_switch->id());


    // delete
    try {
        wred_p->commit_delete(sav_obj? false: true);

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Saving deleted wred %lu\n",
                     wred_p->get_wred_id());

         // save current wred config for rollback if caller requests it.
        // use existing set_mask, existing config
        if (sav_obj) {
            cps_api_object_t tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
            if (tmp_obj == NULL) {
                return cps_api_ret_code_ERR;
            }
            nas_qos_store_prev_attr(tmp_obj, wred_p->set_attr_list(), *wred_p);
        }

        p_switch->remove_wred(wred_p->get_wred_id());

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS WRED Delete error code: %d ",
                    e.err_code);
        return e.err_code;
    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                    "NAS WRED Delete: Unexpected error");
        return NAS_QOS_E_FAIL;
    }


    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_cps_get_switch_and_wred_id(
                                    cps_api_object_t obj,
                                    uint_t &switch_id,
                                    nas_obj_id_t &wred_id)
{
    cps_api_object_attr_t wred_id_attr = cps_api_get_key_data(obj, BASE_QOS_WRED_PROFILE_ID);

    if (wred_id_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "QOS", "wred id not exist in message");
        return NAS_QOS_E_MISSING_KEY;
    }

    switch_id = 0;
    wred_id = cps_api_object_attr_data_u64(wred_id_attr);

    return cps_api_ret_code_OK;

}

/* Parse the attributes */
static cps_api_return_code_t  nas_qos_cps_parse_attr(cps_api_object_t obj,
                                              nas_qos_wred &wred)
{
    uint_t val;
    cps_api_object_it_t it;
    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);

        // skip keys and unprocessed fields
        if (id == BASE_QOS_WRED_PROFILE_SWITCH_ID ||  //keys
            id == BASE_QOS_WRED_PROFILE_ID ||  //keys
            id == CPS_API_ATTR_RESERVE_RANGE_END)
            continue;

        // All processed fields
        if (id == BASE_QOS_WRED_PROFILE_ECN_ENABLE)
            // To be deprecated; use new attribute
            wred.mark_attr_dirty(BASE_QOS_WRED_PROFILE_ECN_MARK);
        else
            wred.mark_attr_dirty(id);
        val = cps_api_object_attr_data_u32(it.attr);

        switch (id) {
        case BASE_QOS_WRED_PROFILE_GREEN_ENABLE:
            wred.set_g_enable((bool)val);
            break;

        case BASE_QOS_WRED_PROFILE_GREEN_MIN_THRESHOLD:
            wred.set_g_min(val);
            break;

        case BASE_QOS_WRED_PROFILE_GREEN_MAX_THRESHOLD:
            wred.set_g_max(val);
            break;

        case BASE_QOS_WRED_PROFILE_GREEN_DROP_PROBABILITY:
            wred.set_g_drop_prob(val);
            break;

        case BASE_QOS_WRED_PROFILE_YELLOW_ENABLE:
            wred.set_y_enable((bool)val);
            break;

        case BASE_QOS_WRED_PROFILE_YELLOW_MIN_THRESHOLD:
            wred.set_y_min(val);
            break;

        case BASE_QOS_WRED_PROFILE_YELLOW_MAX_THRESHOLD:
            wred.set_y_max(val);
            break;

        case BASE_QOS_WRED_PROFILE_YELLOW_DROP_PROBABILITY:
            wred.set_y_drop_prob(val);
            break;

        case BASE_QOS_WRED_PROFILE_RED_ENABLE:
            wred.set_r_enable((bool)val);
            break;

        case BASE_QOS_WRED_PROFILE_RED_MIN_THRESHOLD:
            wred.set_r_min(val);
            break;

        case BASE_QOS_WRED_PROFILE_RED_MAX_THRESHOLD:
            wred.set_r_max(val);
            break;

        case BASE_QOS_WRED_PROFILE_RED_DROP_PROBABILITY:
            wred.set_r_drop_prob(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_GREEN_MIN_THRESHOLD:
            wred.set_ecn_g_min(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_GREEN_MAX_THRESHOLD:
            wred.set_ecn_g_max(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_GREEN_PROBABILITY:
            wred.set_ecn_g_prob(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_YELLOW_MIN_THRESHOLD:
            wred.set_ecn_y_min(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_YELLOW_MAX_THRESHOLD:
            wred.set_ecn_y_max(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_YELLOW_PROBABILITY:
            wred.set_ecn_y_prob(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_RED_MIN_THRESHOLD:
            wred.set_ecn_r_min(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_RED_MAX_THRESHOLD:
            wred.set_ecn_r_max(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_RED_PROBABILITY:
            wred.set_ecn_r_prob(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MIN_THRESHOLD:
            wred.set_ecn_c_min(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MAX_THRESHOLD:
            wred.set_ecn_c_max(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_PROBABILITY:
            wred.set_ecn_c_prob(val);
            break;

        case BASE_QOS_WRED_PROFILE_WEIGHT:
            wred.set_weight(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_ENABLE:
            wred.set_ecn_enable(val);
            break;

        case BASE_QOS_WRED_PROFILE_ECN_MARK:
            wred.set_ecn_mark((BASE_QOS_ECN_MARK_MODE_t)val);
            break;

        case BASE_QOS_WRED_PROFILE_NPU_ID_LIST:
            wred.add_npu((npu_id_t)val);
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
                                                    const nas_qos_wred &wred)
{
    // filling in the keys
    uint32_t switch_id = wred.switch_id();
    nas_obj_id_t wred_id = wred.get_wred_id();
    uint64_t val;

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_WRED_PROFILE_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_WRED_PROFILE_SWITCH_ID,
            cps_api_object_ATTR_T_U32,
            &switch_id, sizeof(uint32_t));
    cps_api_set_key_data(obj, BASE_QOS_WRED_PROFILE_ID,
            cps_api_object_ATTR_T_U64,
            &wred_id, sizeof(uint64_t));


    for (auto attr_id: attr_set) {
        if (attr_id == BASE_QOS_WRED_PROFILE_ID)
            /* key */
            continue;

        if (attr_id == BASE_QOS_WRED_PROFILE_NPU_ID_LIST) {
            for (auto npu_id: wred.npu_list()) {
                cps_api_object_attr_add_u32(obj, attr_id, npu_id);
            }
            continue;
        }

        // all other attributes
        if (wred.get_cfg_value_by_attr_id(attr_id, val) != STD_ERR_OK)
            continue;

        cps_api_object_attr_add_u32(obj, attr_id, (uint32_t)val);
    }

    return cps_api_ret_code_OK;
}

static nas_qos_wred * nas_qos_cps_get_wred(uint_t switch_id,
                                           nas_obj_id_t wred_id)
{

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL)
        return NULL;

    nas_qos_wred *wred_p = p_switch->get_wred(wred_id);

    return wred_p;
}


