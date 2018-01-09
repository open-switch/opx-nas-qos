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
 * \file   nas_qos_scheduler_group.h
 * \brief  NAS QOS SCHEDULER_GROUP object
 * \date   05-2015
 * \author
 */

#ifndef _NAS_QOS_SCHEDULER_GROUP_H_
#define _NAS_QOS_SCHEDULER_GROUP_H_

#include <unordered_map>
#include <algorithm>


#include "std_type_defs.h"
#include "ds_common_types.h" // npu_id_t
#include "nas_base_obj.h"
#include "nas_ndi_qos.h"
#include "nas_ndi_common.h"

class nas_qos_switch;

typedef struct nas_qos_scheduler_group_struct{
    uint32_t         max_child;
    hal_ifindex_t    port_id;
    uint32_t         level;
    nas_obj_id_t     scheduler_profile_id;
    nas_obj_id_t     parent;
} nas_qos_scheduler_group_struct_t;

class nas_qos_scheduler_group : public nas::base_obj_t
{

    // keys
    nas_obj_id_t scheduler_group_id;

    // attributes
    nas_qos_scheduler_group_struct_t cfg;

    // cached info
    ndi_port_t   ndi_port_id; // derived from cfg.port_id, i.e. ifIndex

    ndi_obj_id_t ndi_scheduler_group_id;

    std::vector<nas_obj_id_t> child_list;


public:

    nas_qos_scheduler_group (nas_qos_switch* p_switch);

    const nas_qos_switch& get_switch() ;

    ndi_port_t get_ndi_port_id() const {return ndi_port_id;}
    ndi_obj_id_t ndi_obj_id() const {return ndi_scheduler_group_id;}

    nas_obj_id_t get_scheduler_group_id() const {return scheduler_group_id;}
    void    set_scheduler_group_id(nas_obj_id_t id) {scheduler_group_id = id;}

    bool    is_scheduler_group_attached() {
        return ((cfg.level == 0) || (cfg.parent != NDI_QOS_NULL_OBJECT_ID));
    }

    uint32_t    get_max_child()  const  {return cfg.max_child;}
    void set_max_child(uint32_t val);

    nas_obj_id_t get_parent() const {return cfg.parent;}
    void set_parent(nas_obj_id_t val);

    hal_ifindex_t    get_port_id() const {return cfg.port_id;}
    t_std_error set_port_id(hal_ifindex_t idx);

    uint32_t        get_level() const {return cfg.level;}
    void set_level(uint32_t val);

    nas_obj_id_t    get_scheduler_profile_id() const {return cfg.scheduler_profile_id;}
    void set_scheduler_profile_id(nas_obj_id_t id);

    uint32_t         get_child_count() { return child_list.size();}

    nas_obj_id_t     get_child_id(uint32_t idx) {return child_list[idx];}

    void      add_child(nas_obj_id_t child_id) {child_list.push_back(child_id);}
    void      del_child(nas_obj_id_t child_id) {
        child_list.erase(std::remove(child_list.begin(), child_list.end(), child_id),
                         child_list.end());
    }

    void             clear_child(void) {child_list.clear();}

    /// Overriding base object virtual functions
    virtual const char* name () const override { return "QOS SCHEDULER_GROUP";}

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

    ndi_obj_id_t      ndi_obj_id (npu_id_t npu_id) const;
    void set_ndi_obj_id (npu_id_t npu_id,
                         ndi_obj_id_t obj_id);
    void reset_ndi_obj_id (npu_id_t npu_id);

} ;

inline ndi_obj_id_t nas_qos_scheduler_group::ndi_obj_id (npu_id_t npu_id) const
{
    if (is_created_in_ndi())
        return (ndi_scheduler_group_id);
    else
        return NDI_QOS_NULL_OBJECT_ID;
}

inline void nas_qos_scheduler_group::set_ndi_obj_id (npu_id_t npu_id,
                                           ndi_obj_id_t id)
{
    ndi_scheduler_group_id = id;
}

inline void nas_qos_scheduler_group::reset_ndi_obj_id (npu_id_t npu_id)
{
    ndi_scheduler_group_id = 0;
}

inline void nas_qos_scheduler_group::set_level(uint32_t val)
{
    mark_attr_dirty(BASE_QOS_SCHEDULER_GROUP_LEVEL);
    cfg.level = val;
}

inline void nas_qos_scheduler_group::set_max_child(uint32_t val)
{
    mark_attr_dirty(BASE_QOS_SCHEDULER_GROUP_MAX_CHILD);
    cfg.max_child = val;
}

inline void nas_qos_scheduler_group::set_parent(nas_obj_id_t val)
{
    mark_attr_dirty(BASE_QOS_SCHEDULER_GROUP_PARENT);
    cfg.parent = val;
}


inline void nas_qos_scheduler_group::set_scheduler_profile_id(nas_obj_id_t id)
{
    mark_attr_dirty(BASE_QOS_SCHEDULER_GROUP_SCHEDULER_PROFILE_ID);
    cfg.scheduler_profile_id = id;
}

#endif
