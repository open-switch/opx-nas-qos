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

#include "event_log.h"
#include "std_assert.h"
#include "nas_qos_common.h"
#include "nas_qos_port_pool.h"
#include "dell-base-qos.h"
#include "nas_ndi_qos.h"
#include "nas_base_obj.h"
#include "nas_qos_switch.h"

nas_qos_port_pool::nas_qos_port_pool (nas_qos_switch* switch_p,
                            hal_ifindex_t port, nas_obj_id_t pool_id)
           : base_obj_t(switch_p)
{
    key.port_id = port;
    key.pool_id = pool_id;
    memset(&cfg, 0, sizeof(cfg));
    ndi_port_id = {0};
}

nas_qos_switch& nas_qos_port_pool::get_switch()
{
    return static_cast<nas_qos_switch &>(base_obj_t::get_switch());
}


void nas_qos_port_pool::commit_create (bool rolling_back)

{
    base_obj_t::commit_create(rolling_back);
}

void* nas_qos_port_pool::alloc_fill_ndi_obj (nas::mem_alloc_helper_t& m)
{
    // NAS Qos port pool does not allocate memory to save the incoming tentative attributes
    return this;
}

bool nas_qos_port_pool::push_create_obj_to_npu (npu_id_t npu_id,
                                                   void* ndi_obj)
{
    ndi_obj_id_t ndi_port_pool_id;
    t_std_error rc = STD_ERR_OK;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Creating obj on NPU %d", npu_id);

    nas_qos_port_pool * nas_qos_port_pool_p = static_cast<nas_qos_port_pool *> (ndi_obj);

    // form attr_list
    std::vector<uint64_t> attr_list;
    // Mandatory keys {Port-id, pool-id} must be manually added during cps-create
    // These attributes are not marked-dirty because cps-set cannot alter them
    // but use them to get ndi-port-pool-id
    attr_list.resize(_set_attributes.len() + 2);

    uint_t num_attr = 0;
    attr_list[num_attr++] = BASE_QOS_PORT_POOL_PORT_ID;
    attr_list[num_attr++] = BASE_QOS_PORT_POOL_BUFFER_POOL_ID;
    for (auto attr_id: _set_attributes) {
        attr_list[num_attr++] = attr_id;
    }

    ndi_qos_port_pool_struct_t  ndi_port_pool = {0};
    ndi_port_pool.ndi_port = nas_qos_port_pool_p->ndi_port_id;

    nas_qos_switch& p_switch = const_cast <nas_qos_switch &> (get_switch());
    ndi_port_pool.ndi_pool_id =
            p_switch.nas2ndi_pool_id(nas_qos_port_pool_p->get_pool_id(), npu_id);

    if (is_attr_dirty(BASE_QOS_PORT_POOL_WRED_PROFILE_ID))
        ndi_port_pool.wred_profile_id =
                p_switch.nas2ndi_wred_profile_id(nas_qos_port_pool_p->cfg.wred_id, npu_id);

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Creating port pool object on NPU %d, port_id %u, ndi_pool_id %u",
            ndi_port_pool.ndi_port.npu_id, ndi_port_pool.ndi_port.npu_port, ndi_port_pool.ndi_pool_id);


    if ((rc = ndi_qos_create_port_pool (npu_id,
                                   &attr_list[0],
                                   num_attr,
                                   &ndi_port_pool,
                                   &ndi_port_pool_id))
            != STD_ERR_OK)
    {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Creating Port Pool id on NPU %d failed!", npu_id);
        throw nas::base_exception {rc, __PRETTY_FUNCTION__,
            "NDI QoS Port Pool Create Failed"};
    }
    // Cache the new port pool ID generated by NDI
    set_ndi_obj_id(ndi_port_pool_id);

    return true;
}

bool nas_qos_port_pool::push_delete_obj_to_npu (npu_id_t npu_id)
{
    t_std_error rc;

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Deleting obj on NPU %d", npu_id);

    if ((rc = ndi_qos_delete_port_pool(npu_id, ndi_obj_id()))
        != STD_ERR_OK)
    {
        throw nas::base_exception {rc, __PRETTY_FUNCTION__,
            "NDI Port Pool Delete Failed"};
    }

    return true;
}

bool nas_qos_port_pool::is_leaf_attr (nas_attr_id_t attr_id)
{
    // Table of function pointers to handle modify of Qos port pool
    // attributes.
    static const std::unordered_map <BASE_QOS_PORT_POOL_t,
                                     bool,
                                     std::hash<int>>
        _leaf_attr_map =
    {
        // modifiable objects
        {BASE_QOS_PORT_POOL_WRED_PROFILE_ID,   true},
    };

    return (_leaf_attr_map.at(static_cast<BASE_QOS_PORT_POOL_t>(attr_id)));
}


bool nas_qos_port_pool::push_leaf_attr_to_npu(nas_attr_id_t attr_id,
                                                 npu_id_t npu_id)
{
    t_std_error rc = STD_ERR_OK;

    EV_LOGGING(QOS, DEBUG, "QOS", "Modifying npu: %d, attr_id %d",
                    npu_id, attr_id);

    ndi_qos_port_pool_struct_t ndi_cfg;
    memset(&ndi_cfg, 0, sizeof(ndi_qos_port_pool_struct_t));
    nas_qos_switch & nas_switch = const_cast<nas_qos_switch &>(get_switch());

    switch (attr_id) {
    case BASE_QOS_PORT_POOL_WRED_PROFILE_ID:
        ndi_cfg.wred_profile_id = nas_switch.nas2ndi_wred_profile_id(cfg.wred_id, npu_id);
        break;
    default:
        STD_ASSERT(0);  //non-modifiable object
    }

    ndi_port_t ndi_port = get_ndi_port_id();
    rc = ndi_qos_set_port_pool_attr(ndi_port.npu_id,
                                ndi_obj_id(),
                                (BASE_QOS_PORT_POOL_t)attr_id,
                                &ndi_cfg);
    if (rc != STD_ERR_OK) {
        throw nas::base_exception {rc, __PRETTY_FUNCTION__,
            "NDI attribute Set Failed"};
    }

    return true;
}
