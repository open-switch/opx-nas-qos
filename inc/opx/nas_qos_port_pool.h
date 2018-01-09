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

#ifndef _NAS_QOS_PORT_POOL_H_
#define _NAS_QOS_PORT_POOL_H_

#include "std_type_defs.h"
#include "ds_common_types.h" // npu_id_t
#include "nas_base_obj.h"
#include "nas_ndi_common.h"
#include "nas_ndi_obj_id_table.h"

class nas_qos_switch;

typedef struct nas_qos_port_pool_key_t {
    hal_ifindex_t port_id;
    nas_obj_id_t  pool_id;

    bool operator<(const nas_qos_port_pool_key_t key) const {
        if (port_id != key.port_id)
            return (port_id < key.port_id);

        return (pool_id < key.pool_id);
    }

} nas_qos_port_pool_key_t;

typedef struct qos_port_pool_struct_t {
    nas_obj_id_t     wred_id;

    // stats: TODO
} nas_qos_port_pool_struct_t;

class nas_qos_port_pool : public nas::base_obj_t
{
    // keys
    nas_qos_port_pool_key_t key;

    // attributes
    nas_qos_port_pool_struct_t cfg;

    // cached info
    ndi_port_t      ndi_port_id; // derived from nas_queue_key.port_id, i.e. ifIndex

    // List of mapped NDI IDs one for each NPU
    // managed by this NAS component
    nas::ndi_obj_id_table_t        _ndi_obj_ids;

public:
    nas_qos_port_pool(nas_qos_switch* p_switch, hal_ifindex_t port_id, nas_obj_id_t pool_id);

    nas_qos_switch& get_switch();

    void        set_ndi_port_id(npu_id_t npu_id, npu_port_t npu_port_id);
    ndi_port_t     get_ndi_port_id() {return ndi_port_id;}

    hal_ifindex_t get_port_id() const { return key.port_id; }
    void set_port_id(hal_ifindex_t port) { key.port_id = port; }

    nas_obj_id_t get_pool_id() const { return key.pool_id; }
    void set_pool_id(nas_obj_id_t id) { key.pool_id = id; }

    nas_obj_id_t get_wred_profile_id() const { return cfg.wred_id; }
    void set_wred_profile_id(nas_obj_id_t id) { cfg.wred_id = id; }


    /// Overriding base object virtual functions
    virtual const char* name () const override { return "QOS PORT POOL";}

    /////// Override for object specific behavior ////////
    // Commit newly created object
    virtual void        commit_create (bool rolling_back);

    virtual void* alloc_fill_ndi_obj (nas::mem_alloc_helper_t& m) override;
    virtual bool push_create_obj_to_npu (npu_id_t npu_id,
                                         void* ndi_obj) override;

    virtual bool push_delete_obj_to_npu (npu_id_t npu_id) override;

    virtual bool is_leaf_attr (nas_attr_id_t attr_id);
    virtual bool push_leaf_attr_to_npu (nas_attr_id_t attr_id,
                                        npu_id_t npu_id) override;
    virtual e_event_log_types_enums ev_log_mod_id () const override {return ev_log_t_QOS;}
    virtual const char* ev_log_mod_name () const override {return "QOS";}

    ndi_obj_id_t      ndi_obj_id () const;
    void set_ndi_obj_id (ndi_obj_id_t obj_id);
    void reset_ndi_obj_id ();
} ;

inline ndi_obj_id_t nas_qos_port_pool::ndi_obj_id () const
{
    return (_ndi_obj_ids.at (ndi_port_id.npu_id));
}

inline void nas_qos_port_pool::set_ndi_obj_id (ndi_obj_id_t id)
{
    _ndi_obj_ids[ndi_port_id.npu_id] = id;
}

inline void nas_qos_port_pool::reset_ndi_obj_id ()
{
    _ndi_obj_ids[ndi_port_id.npu_id] = 0;
}

inline void nas_qos_port_pool::set_ndi_port_id(npu_id_t npu_id, npu_port_t npu_port_id)
{
    ndi_port_id.npu_id = npu_id;
    ndi_port_id.npu_port = npu_port_id;
}


#endif
