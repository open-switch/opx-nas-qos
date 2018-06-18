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
 * \file   nas_qos_wred.cpp
 * \brief  NAS QOS WRED Object
 * \date   05-2015
 * \author
 */

#include "event_log.h"
#include "std_assert.h"
#include "nas_qos_wred.h"
#include "dell-base-qos.h"
#include "nas_qos_switch.h"

nas_qos_wred::nas_qos_wred (nas_qos_switch* p_switch)
           : base_obj_t (p_switch)
{
    memset(&cfg, 0, sizeof(cfg));
    wred_id = 0;
}

const nas_qos_switch& nas_qos_wred::get_switch()
{
    return static_cast<const nas_qos_switch&> (base_obj_t::get_switch());
}


void nas_qos_wred::commit_create (bool rolling_back)

{
    base_obj_t::commit_create(rolling_back);
}

void* nas_qos_wred::alloc_fill_ndi_obj (nas::mem_alloc_helper_t& m)
{
    // NAS Qos wred does not allocate memory to save the incoming tentative attributes
    return this;
}

t_std_error nas_qos_wred::get_cfg_value_by_attr_id(nas_attr_id_t attr_id, uint64_t &value) const
{
    switch (attr_id) {
    case BASE_QOS_WRED_PROFILE_GREEN_ENABLE:
        value = get_g_enable();
        break;

    case BASE_QOS_WRED_PROFILE_GREEN_MIN_THRESHOLD:
        value = get_g_min();
        break;

    case BASE_QOS_WRED_PROFILE_GREEN_MAX_THRESHOLD:
        value = get_g_max();
        break;

    case BASE_QOS_WRED_PROFILE_GREEN_DROP_PROBABILITY:
        value = get_g_drop_prob();
        break;

    case BASE_QOS_WRED_PROFILE_YELLOW_ENABLE:
        value = get_y_enable();
        break;

    case BASE_QOS_WRED_PROFILE_YELLOW_MIN_THRESHOLD:
        value = get_y_min();
        break;

    case BASE_QOS_WRED_PROFILE_YELLOW_MAX_THRESHOLD:
        value = get_y_max();
        break;

    case BASE_QOS_WRED_PROFILE_YELLOW_DROP_PROBABILITY:
        value = get_y_drop_prob();
        break;

    case BASE_QOS_WRED_PROFILE_RED_ENABLE:
        value = get_r_enable();
        break;

    case BASE_QOS_WRED_PROFILE_RED_MIN_THRESHOLD:
        value = get_r_min();
        break;

    case BASE_QOS_WRED_PROFILE_RED_MAX_THRESHOLD:
        value = get_r_max();
        break;

    case BASE_QOS_WRED_PROFILE_RED_DROP_PROBABILITY:
        value = get_r_drop_prob();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_GREEN_MIN_THRESHOLD:
        value = get_ecn_g_min();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_GREEN_MAX_THRESHOLD:
        value = get_ecn_g_max();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_GREEN_PROBABILITY:
        value = get_ecn_g_prob();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_YELLOW_MIN_THRESHOLD:
        value = get_ecn_y_min();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_YELLOW_MAX_THRESHOLD:
        value = get_ecn_y_max();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_YELLOW_PROBABILITY:
        value = get_ecn_y_prob();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_RED_MIN_THRESHOLD:
        value = get_ecn_r_min();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_RED_MAX_THRESHOLD:
        value = get_ecn_r_max();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_RED_PROBABILITY:
        value = get_ecn_r_prob();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MIN_THRESHOLD:
        value = get_ecn_c_min();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MAX_THRESHOLD:
        value = get_ecn_c_max();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_PROBABILITY:
        value = get_ecn_c_prob();
        break;

    case BASE_QOS_WRED_PROFILE_WEIGHT:
        value = get_weight();
        break;

    // TO be deprecated
    case BASE_QOS_WRED_PROFILE_ECN_ENABLE:
        value = get_ecn_enable();
        break;

    case BASE_QOS_WRED_PROFILE_ECN_MARK:
        value = get_ecn_mark();
        break;

    default:
        EV_LOGGING(QOS, ERR, "NAS-QOS", "Not configurable attr id %d", attr_id);
        return STD_ERR(QOS, FAIL, 0);
    }

    return STD_ERR_OK;
}

bool nas_qos_wred::push_create_obj_to_npu (npu_id_t npu_id,
                                     void* ndi_obj)
{
    ndi_obj_id_t ndi_wred_id;
    t_std_error rc;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Creating obj on NPU %d", npu_id);

    nas_qos_wred * nas_qos_wred_p = static_cast<nas_qos_wred*> (ndi_obj);

    // form attr_list
    std::vector<nas_attribute_t> nas_attr_list;
    nas_attribute_t nas_attr;
    std::vector<uint64_t> val_list;
    uint64_t val;
    uint32_t count = 0;

    for (auto attr_id: _set_attributes) {

        // npu-id-list is not handled here
        if (attr_id == BASE_QOS_WRED_PROFILE_NPU_ID_LIST)
            continue;

        if (nas_qos_wred_p->get_cfg_value_by_attr_id(attr_id, val) != STD_ERR_OK)
            continue;

        // temporary store the actual nas_attr.data
        val_list.push_back(val);

        nas_attr.id = attr_id;
        nas_attr.len = sizeof(val);
        // nas_attr.data (pointer) will be filled after val_list is no longer changed
        nas_attr_list.push_back(nas_attr);

        count++;
    }

    // update nas_attr.data pointers to the actual storage
    for (uint32_t idx=0; idx < count; idx++) {
        nas_attr_list[idx].data = (void *)&(val_list[idx]);
    }

    if ((rc = ndi_qos_create_wred_ecn_profile (npu_id,
                                   nas_attr_list.size(),
                                   &nas_attr_list[0],
                                   &ndi_wred_id))
            != STD_ERR_OK)
    {
        throw nas::base_exception {rc, __PRETTY_FUNCTION__,
            "NDI QoS WRED Create Failed"};
    }
    // Cache the new WRED ID generated by NDI
    set_ndi_obj_id(npu_id, ndi_wred_id);

    return true;

}


bool nas_qos_wred::push_delete_obj_to_npu (npu_id_t npu_id)
{
    t_std_error rc;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Deleting obj on NPU %d", npu_id);

    if ((rc = ndi_qos_delete_wred_ecn_profile(npu_id, ndi_obj_id(npu_id)))
        != STD_ERR_OK)
    {
        throw nas::base_exception {rc, __PRETTY_FUNCTION__,
            "NDI WRED Delete Failed"};
    }

    return true;
}

bool nas_qos_wred::is_leaf_attr (nas_attr_id_t attr_id)
{
    // Table of function pointers to handle modify of Qos wred
    // attributes.
    static const std::unordered_map <nas_attr_id_t,
                                     bool,
                                     std::hash<int>>
        _leaf_attr_map =
    {
        // modifiable objects
        {BASE_QOS_WRED_PROFILE_GREEN_ENABLE,            true},
        {BASE_QOS_WRED_PROFILE_GREEN_MIN_THRESHOLD,     true},
        {BASE_QOS_WRED_PROFILE_GREEN_MAX_THRESHOLD,     true},
        {BASE_QOS_WRED_PROFILE_GREEN_DROP_PROBABILITY,  true},
        {BASE_QOS_WRED_PROFILE_YELLOW_ENABLE,           true},
        {BASE_QOS_WRED_PROFILE_YELLOW_MIN_THRESHOLD,    true},
        {BASE_QOS_WRED_PROFILE_YELLOW_MAX_THRESHOLD,    true},
        {BASE_QOS_WRED_PROFILE_YELLOW_DROP_PROBABILITY, true},
        {BASE_QOS_WRED_PROFILE_RED_ENABLE,              true},
        {BASE_QOS_WRED_PROFILE_RED_MIN_THRESHOLD,       true},
        {BASE_QOS_WRED_PROFILE_RED_MAX_THRESHOLD,       true},
        {BASE_QOS_WRED_PROFILE_RED_DROP_PROBABILITY,    true},

        {BASE_QOS_WRED_PROFILE_ECN_GREEN_MIN_THRESHOLD,     true},
        {BASE_QOS_WRED_PROFILE_ECN_GREEN_MAX_THRESHOLD,     true},
        {BASE_QOS_WRED_PROFILE_ECN_GREEN_PROBABILITY,       true},
        {BASE_QOS_WRED_PROFILE_ECN_YELLOW_MIN_THRESHOLD,    true},
        {BASE_QOS_WRED_PROFILE_ECN_YELLOW_MAX_THRESHOLD,    true},
        {BASE_QOS_WRED_PROFILE_ECN_YELLOW_PROBABILITY,      true},
        {BASE_QOS_WRED_PROFILE_ECN_RED_MIN_THRESHOLD,       true},
        {BASE_QOS_WRED_PROFILE_ECN_RED_MAX_THRESHOLD,       true},
        {BASE_QOS_WRED_PROFILE_ECN_RED_PROBABILITY,         true},
        {BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MIN_THRESHOLD, true},
        {BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_MAX_THRESHOLD, true},
        {BASE_QOS_WRED_PROFILE_ECN_COLOR_UNAWARE_PROBABILITY,   true},

        {BASE_QOS_WRED_PROFILE_WEIGHT,        true},
        {BASE_QOS_WRED_PROFILE_ECN_ENABLE,    true},
        {BASE_QOS_WRED_PROFILE_ECN_MARK,      true},
        {BASE_QOS_WRED_PROFILE_NPU_ID_LIST,   true},

        //The NPU ID list attribute is handled by the base object itself.
    };

    return (_leaf_attr_map.at(attr_id));
}

bool nas_qos_wred::push_leaf_attr_to_npu (nas_attr_id_t attr_id,
                                           npu_id_t npu_id)
{
    t_std_error rc = STD_ERR_OK;

    EV_LOGGING(QOS, DEBUG, "QOS", "Modifying npu: %d, attr_id %lu",
                    npu_id, attr_id);

    if (attr_id == BASE_QOS_WRED_PROFILE_NPU_ID_LIST)
        // handled separately, not here
        return true;

    nas_attribute_t nas_attr;
    nas_attr.id = attr_id;
    uint64_t val;
    if (get_cfg_value_by_attr_id(attr_id, val) != STD_ERR_OK)
        return true;

    nas_attr.data = (void *)&val;
    nas_attr.len = sizeof(val);

    rc = ndi_qos_set_wred_ecn_profile_attr(npu_id,
                               ndi_obj_id(npu_id),
                               nas_attr);
    if (rc != STD_ERR_OK) {
        throw nas::base_exception {rc, __PRETTY_FUNCTION__,
            "NDI attribute Set Failed"};
    }

    return true;
}

