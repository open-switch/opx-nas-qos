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
 * \file   nas_qos_wred.h
 * \brief  NAS QOS WRED object
 * \date   05-2015
 * \author
 */

#ifndef _NAS_QOS_WRED_H_
#define _NAS_QOS_WRED_H_

#include <unordered_map>

#include "std_type_defs.h"
#include "ds_common_types.h" // npu_id_t
#include "nas_base_obj.h"
#include "nas_ndi_common.h"
#include "nas_ndi_qos.h"

class nas_qos_switch;

typedef struct nas_qos_wred_struct{
    bool    g_enable;        // WRED enabled/disabled on green traffic
    uint_t  g_min;          // WRED min threshold for green traffic
    uint_t  g_max;          // WRED max threshold for green traffic
    uint_t  g_drop_prob;    // Green traffic drop probability when min-threshold is crossed

    bool    y_enable;       // WRED enabled/disabled on yellow traffic
    uint_t  y_min;          // WRED min threshold for yellow traffic
    uint_t  y_max;          // WRED max threshold for yellow traffic
    uint_t  y_drop_prob;    // Yellow traffic drop probability when min-threshold is crossed

    bool    r_enable;       // WRED enabled/disabled on red traffic
    uint_t  r_min;          // WRED min threshold for red traffic
    uint_t  r_max;          // WRED max threshold for red traffic
    uint_t  r_drop_prob;    // Red traffic drop probability when min-threshold is crossed

    uint_t  ecn_g_min;      // ECN min threshold for green traffic
    uint_t  ecn_g_max;      // ECN max threshold for green traffic
    uint_t  ecn_g_prob;     // Green traffic ECN-mark probability when min-threshold is crossed

    uint_t  ecn_y_min;      // ECN min threshold for yellow traffic
    uint_t  ecn_y_max;      // ECN max threshold for yellow traffic
    uint_t  ecn_y_prob;     // Yellow traffic ECN-mark probability when min-threshold is crossed

    uint_t  ecn_r_min;      // ECN min threshold for red traffic
    uint_t  ecn_r_max;      // ECN max threshold for red traffic
    uint_t  ecn_r_prob;     // Red traffic ECN-mark probability when min-threshold is crossed

    uint_t  ecn_c_min;      // ECN min threshold for colorless traffic
    uint_t  ecn_c_max;      // ECN max threshold for colorless traffic
    uint_t  ecn_c_prob;     // Colorless traffic ECN-mark probability when min-threshold is crossed

    uint_t  weight;         // Weight factor to calculate the average queue size based on historic average
    BASE_QOS_ECN_MARK_MODE_t ecn_mark;          // ECN marking mode
}nas_qos_wred_struct_t;


class nas_qos_wred : public nas::base_obj_t
{

    // keys
    nas_obj_id_t wred_id;

    // attributes
    nas_qos_wred_struct_t cfg;

    ///// Typedefs /////
    typedef std::unordered_map <npu_id_t, ndi_obj_id_t> ndi_obj_id_map_t;

    // List of mapped NDI IDs one for each NPU
    // managed by this NAS component
    ndi_obj_id_map_t          _ndi_obj_ids;

public:

    nas_qos_wred (nas_qos_switch* p_switch);

    const nas_qos_switch& get_switch() ;

    nas_obj_id_t get_wred_id() const {return wred_id;}
    void    set_wred_id(nas_obj_id_t id) {wred_id = id;}

    bool    get_g_enable()  const  {return cfg.g_enable;}
    void    set_g_enable(bool val) {cfg.g_enable = val;}

    uint_t  get_g_min()  const  {return cfg.g_min;}
    void    set_g_min(uint_t val) {cfg.g_min = val;}

    uint_t  get_g_max()  const  {return cfg.g_max;}
    void    set_g_max(uint_t val) {cfg.g_max = val;}

    uint_t  get_g_drop_prob() const   {return cfg.g_drop_prob;}
    void    set_g_drop_prob(uint_t val) {cfg.g_drop_prob = val;}

    bool    get_y_enable() const   {return cfg.y_enable;}
    void    set_y_enable(bool val) {cfg.y_enable = val;}

    uint_t  get_y_min() const   {return cfg.y_min;}
    void    set_y_min(uint_t val) {cfg.y_min = val;}

    uint_t  get_y_max()  const  {return cfg.y_max;}
    void    set_y_max(uint_t val) {cfg.y_max = val;}

    uint_t  get_y_drop_prob() const   {return cfg.y_drop_prob;}
    void    set_y_drop_prob(uint_t val) {cfg.y_drop_prob = val;}

    bool    get_r_enable()  const  {return cfg.r_enable;}
    void    set_r_enable(bool val) {cfg.r_enable = val;}

    uint_t  get_r_min() const   {return cfg.r_min;}
    void    set_r_min(uint_t val) {cfg.r_min = val;}

    uint_t  get_r_max() const   {return cfg.r_max;}
    void    set_r_max(uint_t val) {cfg.r_max = val;}

    uint_t  get_r_drop_prob()  const  {return cfg.r_drop_prob;}
    void    set_r_drop_prob(uint_t val) {cfg.r_drop_prob = val;}

    uint_t  get_ecn_g_min()  const  {return cfg.ecn_g_min;}
    void    set_ecn_g_min(uint_t val) {cfg.ecn_g_min = val;}

    uint_t  get_ecn_g_max()  const  {return cfg.ecn_g_max;}
    void    set_ecn_g_max(uint_t val) {cfg.ecn_g_max = val;}

    uint_t  get_ecn_g_prob() const   {return cfg.ecn_g_prob;}
    void    set_ecn_g_prob(uint_t val) {cfg.ecn_g_prob = val;}

    uint_t  get_ecn_y_min() const   {return cfg.ecn_y_min;}
    void    set_ecn_y_min(uint_t val) {cfg.ecn_y_min = val;}

    uint_t  get_ecn_y_max()  const  {return cfg.ecn_y_max;}
    void    set_ecn_y_max(uint_t val) {cfg.ecn_y_max = val;}

    uint_t  get_ecn_y_prob() const   {return cfg.ecn_y_prob;}
    void    set_ecn_y_prob(uint_t val) {cfg.ecn_y_prob = val;}

    uint_t  get_ecn_r_min() const   {return cfg.ecn_r_min;}
    void    set_ecn_r_min(uint_t val) {cfg.ecn_r_min = val;}

    uint_t  get_ecn_r_max() const   {return cfg.ecn_r_max;}
    void    set_ecn_r_max(uint_t val) {cfg.ecn_r_max = val;}

    uint_t  get_ecn_r_prob()  const  {return cfg.ecn_r_prob;}
    void    set_ecn_r_prob(uint_t val) {cfg.ecn_r_prob = val;}

    uint_t  get_ecn_c_min() const   {return cfg.ecn_c_min;}
    void    set_ecn_c_min(uint_t val) {cfg.ecn_c_min = val;}

    uint_t  get_ecn_c_max() const   {return cfg.ecn_c_max;}
    void    set_ecn_c_max(uint_t val) {cfg.ecn_c_max = val;}

    uint_t  get_ecn_c_prob()  const  {return cfg.ecn_c_prob;}
    void    set_ecn_c_prob(uint_t val) {cfg.ecn_c_prob = val;}

    uint_t  get_weight() const {return cfg.weight;}
    void    set_weight(uint_t val) {cfg.weight = val;}

    // To be deprecated
    bool    get_ecn_enable()  const  {
                return (cfg.ecn_mark == BASE_QOS_ECN_MARK_MODE_ALL?
                        true: false);
            }
    void    set_ecn_enable(bool val) {
                cfg.ecn_mark = (val? BASE_QOS_ECN_MARK_MODE_ALL:
                                     BASE_QOS_ECN_MARK_MODE_NONE);
            }

    BASE_QOS_ECN_MARK_MODE_t get_ecn_mark() const {return cfg.ecn_mark;}
    void    set_ecn_mark(BASE_QOS_ECN_MARK_MODE_t val) {cfg.ecn_mark = val;}


    /// Overriding base object virtual functions
    virtual const char* name () const override { return "QOS WRED";}

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

    t_std_error get_cfg_value_by_attr_id(nas_attr_id_t attr_id, uint64_t &value) const;

} ;

inline ndi_obj_id_t nas_qos_wred::ndi_obj_id (npu_id_t npu_id) const
{
    return (_ndi_obj_ids.at (npu_id));
}

inline void nas_qos_wred::set_ndi_obj_id (npu_id_t npu_id,
                                           ndi_obj_id_t id)
{
    // Will overwrite or insert a new element
    // if npu_id is not already present.
    _ndi_obj_ids [npu_id] = id;
}

inline void nas_qos_wred::reset_ndi_obj_id (npu_id_t npu_id)
{
    _ndi_obj_ids.erase (npu_id);
}


#endif
