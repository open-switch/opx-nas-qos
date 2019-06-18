/*
 * Copyright (c) 2019 Dell Inc.
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
 * \file   nas_qos_priority_group.h
 * \brief  NAS QOS Priority Group object
 * \date   03-2016
 * \author
 */

#ifndef _NAS_QOS_PRIORITY_GROUP_H_
#define _NAS_QOS_PRIORITY_GROUP_H_

#include <unordered_map>

#include "std_type_defs.h"
#include "ds_common_types.h" // npu_id_t
#include "dell-base-qos.h"
#include "nas_base_obj.h"
#include "nas_ndi_qos.h"
#include "nas_ndi_common.h"
#include <string>
#include "nas_qos_common.h"

class nas_qos_switch;

typedef struct nas_qos_priority_group_key_t {
    hal_ifindex_t port_id;
    uint8_t       local_id;

    bool operator<(const nas_qos_priority_group_key_t key) const {
        if (port_id != key.port_id)
            return (port_id < key.port_id);

        return (local_id < key.local_id);
    }
} nas_qos_priority_group_key_t;

class nas_qos_priority_group : public nas::base_obj_t
{

    // keys
    nas_qos_priority_group_key_t key;

    // attributes
    nas_obj_id_t priority_group_id;
    nas_obj_id_t buffer_profile; // buffer profile id to which the priority group is referring

    ///// Typedefs /////
    typedef std::unordered_map <npu_id_t, ndi_obj_id_t> ndi_obj_id_map_t;

    // cached info
    ndi_port_t           ndi_port_id; // derived from nas_queue_key.port_id, i.e. ifIndex

    // List of mapped NDI IDs (only one for each priority group)
    // managed by this NAS component
    ndi_obj_id_map_t          _ndi_obj_ids;

    // list of shadow priority group ids on all different MMUs
    // If the priority group does not exist in a particular MMU,
    // NULL_OBJECT_ID will be stored at that MMU location.
    std::vector<ndi_obj_id_t> _shadow_ndi_obj_id_list;
    std::string if_name;
public:

    nas_qos_priority_group (nas_qos_switch* p_switch, nas_qos_priority_group_key_t key);

    const nas_qos_switch& get_switch() ;

    void        set_ndi_port_id(npu_id_t npu_id, npu_port_t npu_port_id);
    ndi_port_t     get_ndi_port_id() const {return ndi_port_id;}

    nas_qos_priority_group_key_t get_key() const {return key;}
    hal_ifindex_t get_port_id() const {return key.port_id;}
    uint8_t get_local_id() const {return key.local_id;}


    nas_obj_id_t get_priority_group_id() const {return priority_group_id;}
    void    set_priority_group_id(nas_obj_id_t id) {priority_group_id = id;}

    bool is_buffer_profile_set() {return _set_attributes.contains(BASE_QOS_PRIORITY_GROUP_BUFFER_PROFILE_ID);}
    nas_obj_id_t    get_buffer_profile() const {return (nas_obj_id_t)buffer_profile;}
    void            set_buffer_profile(nas_obj_id_t id);

    /// Overriding base object virtual functions
    virtual const char* name () const override { return "QOS priority_group";}

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

    void reset_shadow_pg_ids() {_shadow_ndi_obj_id_list.clear();}
    void add_shadow_pg_id(ndi_obj_id_t id) {_shadow_ndi_obj_id_list.push_back(id);}
    uint_t get_shadow_pg_count() {return _shadow_ndi_obj_id_list.size();}
    ndi_obj_id_t get_shadow_pg_id(uint_t nas_mmu_idx) {
        uint_t ndi_mmu_idx = nas_mmu_idx - 1;
        if (ndi_mmu_idx < _shadow_ndi_obj_id_list.size())
            return _shadow_ndi_obj_id_list[ndi_mmu_idx];
        else
            return NDI_QOS_NULL_OBJECT_ID;
    }
    std::string &get_if_name() { return if_name;}
    void set_if_name() {nas_qos_get_if_index_to_name (key.port_id, if_name);}
} ;

inline ndi_obj_id_t nas_qos_priority_group::ndi_obj_id () const
{
    if (is_created_in_ndi())
        return (_ndi_obj_ids.at (ndi_port_id.npu_id));
    else
        return NDI_QOS_NULL_OBJECT_ID;
}

inline void nas_qos_priority_group::set_ndi_obj_id (ndi_obj_id_t id)
{
    // Will overwrite or insert a new element
    // if npu_id is not already present.
    _ndi_obj_ids [ndi_port_id.npu_id] = id;
}

inline void nas_qos_priority_group::reset_ndi_obj_id ()
{
    _ndi_obj_ids.erase (ndi_port_id.npu_id);
}

inline void nas_qos_priority_group::set_ndi_port_id(npu_id_t npu_id, npu_port_t npu_port_id)
{
    ndi_port_id.npu_id = npu_id;
    ndi_port_id.npu_port = npu_port_id;
}

inline void nas_qos_priority_group::set_buffer_profile(nas_obj_id_t id)
{
    if (buffer_profile == id) {
        return;
    }

    mark_attr_dirty(BASE_QOS_PRIORITY_GROUP_BUFFER_PROFILE_ID);
    buffer_profile = id;
}

#endif
