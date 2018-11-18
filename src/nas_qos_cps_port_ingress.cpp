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

#include "cps_api_key.h"
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
#include "dell-base-qos.h"
#include "nas_qos_port_ingress.h"
#include "nas_if_utils.h"

static std_mutex_lock_create_static_init_rec(port_ing_mutex);
static cps_api_return_code_t nas_qos_cps_api_port_ing_set(
                                            cps_api_object_t obj,
                                            cps_api_object_list_t sav_obj);
static void nas_qos_port_ingress_fetch_from_hw(ndi_port_t ndi_port_id,
        nas_qos_port_ingress * port_ing);

static cps_api_return_code_t nas_qos_cps_parse_attr(cps_api_object_t obj,
                                                nas_qos_port_ingress& port_ing)
{
    uint_t val;
    uint64_t lval;
    cps_api_object_it_t it;
    bool first_time = true;

    cps_api_object_it_begin(obj, &it);
    for ( ; cps_api_object_it_valid(&it); cps_api_object_it_next(&it)) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        switch(id) {
        case BASE_QOS_PORT_INGRESS_SWITCH_ID:
        case BASE_QOS_PORT_INGRESS_PORT_ID:
            break;
        case BASE_QOS_PORT_INGRESS_DEFAULT_TRAFFIC_CLASS:
            val = cps_api_object_attr_data_u32(it.attr);
            port_ing.mark_attr_dirty(id);
            port_ing.set_default_traffic_class(val);
            break;
        case BASE_QOS_PORT_INGRESS_DOT1P_TO_TC_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_DOT1P_TO_TC);
            port_ing.set_dot1p_to_tc_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_DOT1P_TO_COLOR_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_DOT1P_TO_COLOR);
            port_ing.set_dot1p_to_color_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_DOT1P_TO_TC_COLOR_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_DOT1P_TO_TC_COLOR);
            port_ing.set_dot1p_to_tc_color_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_DSCP_TO_TC_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_DSCP_TO_TC);
            port_ing.set_dscp_to_tc_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_DSCP_TO_COLOR_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_DSCP_TO_COLOR);
            port_ing.set_dscp_to_color_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_DSCP_TO_TC_COLOR_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_DSCP_TO_TC_COLOR);
            port_ing.set_dscp_to_tc_color_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_TC_TO_QUEUE_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_TC_TO_QUEUE);
            port_ing.set_tc_to_queue_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_FLOW_CONTROL:
            val = cps_api_object_attr_data_u32(it.attr);
            port_ing.mark_attr_dirty(id);
            port_ing.set_flow_control(val);
            break;
        case BASE_QOS_PORT_INGRESS_POLICER_ID:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            port_ing.set_policer_id(lval);
            break;
        case BASE_QOS_PORT_INGRESS_FLOOD_STORM_CONTROL:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            port_ing.set_flood_storm_control(lval);
            break;
        case BASE_QOS_PORT_INGRESS_BROADCAST_STORM_CONTROL:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            port_ing.set_broadcast_storm_control(lval);
            break;
        case BASE_QOS_PORT_INGRESS_MULTICAST_STORM_CONTROL:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            port_ing.set_multicast_storm_control(lval);
            break;
        case BASE_QOS_PORT_INGRESS_TC_TO_PRIORITY_GROUP_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_TC_TO_PG);
            port_ing.set_tc_to_priority_group_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_TO_PFC_PRIORITY_MAP:
            lval = cps_api_object_attr_data_u64(it.attr);
            port_ing.mark_attr_dirty(id);
            lval = ENCODE_LOCAL_MAP_ID_AND_TYPE(lval, NDI_QOS_MAP_PG_TO_PFC);
            port_ing.set_priority_group_to_pfc_priority_map(lval);
            break;
        case BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_NUMBER:
        case BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_ID_LIST:
            break; //non-settable
        case BASE_QOS_PORT_INGRESS_PER_PRIORITY_FLOW_CONTROL:
            {
                uint8_t val8 = *(uint8_t *)cps_api_object_attr_data_bin(it.attr);
                port_ing.mark_attr_dirty(id);
                port_ing.set_per_priority_flow_control(val8);
            }
            break;
        case BASE_QOS_PORT_INGRESS_BUFFER_PROFILE_ID_LIST:
            if (first_time) {
                port_ing.clear_buf_prof_id();
                first_time = false;
            }
            lval = cps_api_object_attr_data_u64(it.attr);
            if (lval)
                port_ing.add_buffer_profile_id(lval);
            port_ing.mark_attr_dirty(id);
            break;

        case CPS_API_ATTR_RESERVE_RANGE_END:
            // skip keys
            break;

        default:
            EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                    "Unrecognized option: %lu", id);
            return NAS_QOS_E_UNSUPPORTED;
        }
    }

    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_qos_store_prev_attr(cps_api_object_t obj,
                                                    const nas::attr_set_t& attr_set,
                                                    const nas_qos_port_ingress& port_ing)
{
    // filling in the keys
    hal_ifindex_t port_id = port_ing.get_port_id();
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_PORT_INGRESS_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_PORT_INGRESS_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &port_id, sizeof(uint32_t));


    uint8_t bit_vec;

    for (auto attr_id: attr_set) {
        switch (attr_id) {
        case BASE_QOS_PORT_INGRESS_PORT_ID:
            /* key  */
            break;
        case BASE_QOS_PORT_INGRESS_DEFAULT_TRAFFIC_CLASS:
            cps_api_object_attr_add_u32(obj, attr_id,
                                    port_ing.get_default_traffic_class());
            break;
        case BASE_QOS_PORT_INGRESS_DOT1P_TO_TC_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_dot1p_to_tc_map()));
            break;
        case BASE_QOS_PORT_INGRESS_DOT1P_TO_COLOR_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_dot1p_to_color_map()));
            break;
        case BASE_QOS_PORT_INGRESS_DOT1P_TO_TC_COLOR_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_dot1p_to_tc_color_map()));
            break;
        case BASE_QOS_PORT_INGRESS_DSCP_TO_TC_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_dscp_to_tc_map()));
            break;
        case BASE_QOS_PORT_INGRESS_DSCP_TO_COLOR_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_dscp_to_color_map()));
            break;
        case BASE_QOS_PORT_INGRESS_DSCP_TO_TC_COLOR_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_dscp_to_tc_color_map()));
            break;
        case BASE_QOS_PORT_INGRESS_TC_TO_QUEUE_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_tc_to_queue_map()));
            break;
        case BASE_QOS_PORT_INGRESS_FLOW_CONTROL:
            cps_api_object_attr_add_u32(obj, attr_id,
                                    (uint32_t)port_ing.get_flow_control());
            break;
        case BASE_QOS_PORT_INGRESS_POLICER_ID:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    port_ing.get_policer_id());
            break;
        case BASE_QOS_PORT_INGRESS_FLOOD_STORM_CONTROL:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    port_ing.get_flood_storm_control());
            break;
        case BASE_QOS_PORT_INGRESS_BROADCAST_STORM_CONTROL:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    port_ing.get_broadcast_storm_control());
            break;
        case BASE_QOS_PORT_INGRESS_MULTICAST_STORM_CONTROL:
            cps_api_object_attr_add_u64(obj, attr_id,
                                    port_ing.get_multicast_storm_control());
            break;
        case BASE_QOS_PORT_INGRESS_TC_TO_PRIORITY_GROUP_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_tc_to_priority_group_map()));
            break;
        case BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_TO_PFC_PRIORITY_MAP:
            cps_api_object_attr_add_u64(obj, attr_id,
                    GET_LOCAL_MAP_ID(port_ing.get_priority_group_to_pfc_priority_map()));
            break;

        case BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_NUMBER:
        case BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_ID_LIST:
            break; //non-settable
        case BASE_QOS_PORT_INGRESS_PER_PRIORITY_FLOW_CONTROL:
            bit_vec = port_ing.get_per_priority_flow_control();
            cps_api_object_attr_add(obj, attr_id,
                                    &bit_vec, sizeof(uint8_t));
            break;
        case BASE_QOS_PORT_INGRESS_BUFFER_PROFILE_ID_LIST:
            for (uint_t i= 0; i< port_ing.get_buffer_profile_id_count(); i++) {
                cps_api_object_attr_add_u64(obj, attr_id,
                                 port_ing.get_buffer_profile_id(i));
            }
            break;

        default:
            break;
        }
    }

    return cps_api_ret_code_OK;
}

static t_std_error nas_qos_port_ingress_init_vp(hal_ifindex_t port_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return NAS_QOS_E_FAIL;
    }

    try {
        nas_qos_port_ingress port_ing(p_switch, port_id);

        // init vp's pg_id_list
        std::vector<nas_obj_id_t> nas_pg_id_list;
        uint_t pg_id_count = p_switch->get_port_pg_ids(port_id, 0, NULL);
        if (pg_id_count) {
            nas_pg_id_list.resize(pg_id_count);
            (void) (p_switch->get_port_pg_ids(port_id, pg_id_count, &nas_pg_id_list[0]));
        }
        for (uint_t idx = 0; idx < pg_id_count; idx ++) {
            port_ing.add_priority_group_id(nas_pg_id_list[idx]);
        }

        p_switch->add_port_ingress(port_ing);


    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                "Exception on creating nas_qos_port_ingress object for VP");
        return NAS_QOS_E_FAIL;
    }

    return STD_ERR_OK;
}

/*
 * This function initializes an ingress port node
 * @Return standard error code
 */
t_std_error nas_qos_port_ingress_init(hal_ifindex_t port_id, ndi_port_t ndi_port_id)
{
    if (nas_is_virtual_port(port_id))
        return nas_qos_port_ingress_init_vp(port_id);

    EV_LOGGING(QOS, DEBUG, "NAS-QOS",
            "Init port ingress profile: port %d\n",
            port_id);

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-ING",
                     "switch_id of npu_id: %u cannot be found/created",
                     ndi_port_id.npu_id);
        return NAS_QOS_E_FAIL;
    }

    try {
        nas_qos_port_ingress port_ing(p_switch, port_id);

        nas_qos_port_ingress_fetch_from_hw(ndi_port_id, &port_ing);

        p_switch->add_port_ingress(port_ing);

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                "Exception on creating nas_qos_port_ingress object");
        return NAS_QOS_E_FAIL;
    }

    return STD_ERR_OK;
}

static void nas_qos_port_ingress_fetch_from_hw(ndi_port_t ndi_port_id,
        nas_qos_port_ingress * port_ing)
{
    static const int MAX_PRIORITY_GROUP_ID_NUM = 32;
    static const int MAX_BUFFER_POOL_ID_NUM = 32;
    ndi_obj_id_t pg_id_list[MAX_PRIORITY_GROUP_ID_NUM] = {0};
    ndi_obj_id_t buf_prof_id_list[MAX_BUFFER_POOL_ID_NUM] = {0};
    BASE_QOS_PORT_INGRESS_t attr_list[] = {
        BASE_QOS_PORT_INGRESS_DEFAULT_TRAFFIC_CLASS,
        BASE_QOS_PORT_INGRESS_DOT1P_TO_TC_MAP,
        BASE_QOS_PORT_INGRESS_DOT1P_TO_COLOR_MAP,
        BASE_QOS_PORT_INGRESS_DOT1P_TO_TC_COLOR_MAP,
        BASE_QOS_PORT_INGRESS_DSCP_TO_TC_MAP,
        BASE_QOS_PORT_INGRESS_DSCP_TO_COLOR_MAP,
        BASE_QOS_PORT_INGRESS_DSCP_TO_TC_COLOR_MAP,
        BASE_QOS_PORT_INGRESS_TC_TO_QUEUE_MAP,
        BASE_QOS_PORT_INGRESS_FLOW_CONTROL,
        BASE_QOS_PORT_INGRESS_POLICER_ID,
        BASE_QOS_PORT_INGRESS_FLOOD_STORM_CONTROL,
        BASE_QOS_PORT_INGRESS_BROADCAST_STORM_CONTROL,
        BASE_QOS_PORT_INGRESS_MULTICAST_STORM_CONTROL,
        BASE_QOS_PORT_INGRESS_TC_TO_PRIORITY_GROUP_MAP,
        BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_TO_PFC_PRIORITY_MAP,
        BASE_QOS_PORT_INGRESS_PER_PRIORITY_FLOW_CONTROL,
        BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_NUMBER,
        BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_ID_LIST,
        BASE_QOS_PORT_INGRESS_BUFFER_PROFILE_ID_LIST,
    };
    qos_port_ing_struct_t ndi_info;


    memset(&ndi_info, 0, sizeof(qos_port_ing_struct_t));
    ndi_info.priority_group_id_list = pg_id_list;
    ndi_info.num_priority_group_id = MAX_PRIORITY_GROUP_ID_NUM;
    ndi_info.buffer_profile_list = buf_prof_id_list;
    ndi_info.num_buffer_profile = MAX_BUFFER_POOL_ID_NUM;
    int attr_num = sizeof(attr_list)/sizeof(attr_list[0]);
    for (int idx = 0; idx < attr_num; idx ++) {
        int rc = ndi_qos_get_port_ing_profile(ndi_port_id.npu_id,
                                            ndi_port_id.npu_port,
                                            &attr_list[idx], 1,
                                            &ndi_info);
        if (rc != STD_ERR_OK) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                    "Attribute %u is not supported by NDI for reading\n", attr_list[idx]);
            if (attr_list[idx] == BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_ID_LIST) {
                // in case of query failure, do not return any empty values
                ndi_info.num_priority_group_id = 0;
            }
            if (attr_list[idx] == BASE_QOS_PORT_INGRESS_BUFFER_PROFILE_ID_LIST) {
                ndi_info.num_buffer_profile = 0;
            }
        }
    }

    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return ;
    }

    port_ing->set_default_traffic_class(ndi_info.default_tc);
    port_ing->set_dot1p_to_tc_map(p_switch->ndi2nas_map_id(ndi_info.dot1p_to_tc_map, ndi_port_id.npu_id));
    port_ing->set_dot1p_to_color_map(p_switch->ndi2nas_map_id(ndi_info.dot1p_to_color_map, ndi_port_id.npu_id));
    port_ing->set_dot1p_to_tc_color_map(p_switch->ndi2nas_map_id(ndi_info.dot1p_to_tc_color_map, ndi_port_id.npu_id));
    port_ing->set_dscp_to_tc_map(p_switch->ndi2nas_map_id(ndi_info.dscp_to_tc_map, ndi_port_id.npu_id));
    port_ing->set_dscp_to_color_map(p_switch->ndi2nas_map_id(ndi_info.dscp_to_color_map, ndi_port_id.npu_id));
    port_ing->set_dscp_to_tc_color_map(p_switch->ndi2nas_map_id(ndi_info.dscp_to_tc_color_map, ndi_port_id.npu_id));
    port_ing->set_tc_to_queue_map(p_switch->ndi2nas_map_id(ndi_info.tc_to_queue_map, ndi_port_id.npu_id));
    port_ing->set_flow_control(ndi_info.flow_control);
    port_ing->set_policer_id(p_switch->ndi2nas_policer_id(ndi_info.policer_id, ndi_port_id.npu_id));
    port_ing->set_flood_storm_control(p_switch->ndi2nas_policer_id(ndi_info.flood_storm_control, ndi_port_id.npu_id));
    port_ing->set_broadcast_storm_control(p_switch->ndi2nas_policer_id(ndi_info.bcast_storm_control, ndi_port_id.npu_id));
    port_ing->set_multicast_storm_control(p_switch->ndi2nas_policer_id(ndi_info.mcast_storm_control, ndi_port_id.npu_id));
    port_ing->set_tc_to_priority_group_map(p_switch->ndi2nas_map_id(ndi_info.tc_to_priority_group_map, ndi_port_id.npu_id));
    port_ing->set_priority_group_to_pfc_priority_map(p_switch->ndi2nas_map_id(ndi_info.priority_group_to_pfc_priority_map, ndi_port_id.npu_id));
    port_ing->set_per_priority_flow_control(ndi_info.per_priority_flow_control);
    port_ing->clear_priority_group_id();
    for (uint_t idx = 0; idx < ndi_info.num_priority_group_id; idx ++) {
        port_ing->add_priority_group_id(p_switch->ndi2nas_priority_group_id(ndi_info.priority_group_id_list[idx]));
    }
    port_ing->clear_buf_prof_id();
    for (uint_t idx = 0; idx < ndi_info.num_buffer_profile; idx ++) {
        port_ing->add_buffer_profile_id(p_switch->ndi2nas_buffer_profile_id(ndi_info.buffer_profile_list[idx], ndi_port_id.npu_id));
    }
    port_ing->add_npu(ndi_port_id.npu_id);
    port_ing->set_ndi_port_id(ndi_port_id.npu_id, ndi_port_id.npu_port);
    port_ing->mark_ndi_created();

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
void nas_qos_port_ingress_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd)
{
    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-ING",
                     "switch_id cannot be found with npu_id %d",
                     ndi_port_id.npu_id);
        return ;
    }

    if (isAdd == false) {
        EV_LOGGING(QOS, NOTICE, "QOS-ING", "Disassociation ifindex %d", ifindex);

        // clear up and re-init a new one ; will load DB for VP config
        p_switch->remove_port_ingress(ifindex);
        nas_qos_port_ingress_init_vp(ifindex);
    }
    else {
        EV_LOGGING(QOS, NOTICE, "QOS-ING", "Association ifindex %d to npu port %d",
                ifindex, ndi_port_id.npu_port);

        // update npu mapping
        nas_qos_port_ingress * port_ing = p_switch->get_port_ingress(ifindex);

        // update port_ing with npu-specific readings
        nas_qos_port_ingress_fetch_from_hw(ndi_port_id, port_ing);
    }

    // read DB and push to NPU
    cps_api_object_guard _og(cps_api_object_create());
    if(!_og.valid()){
        EV_LOGGING(QOS,ERR,"QOS-DB-GET","Failed to create object for db get");
        return;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(_og.get()),
            BASE_QOS_PORT_INGRESS_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(_og.get(), BASE_QOS_PORT_INGRESS_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &ifindex, sizeof(uint32_t));
    cps_api_object_list_guard lst(cps_api_object_list_create());
    if (cps_api_db_get(_og.get(),lst.get())==cps_api_ret_code_OK) {
        size_t len = cps_api_object_list_size(lst.get());

        if(len){
            // Get should return only one object matching to the interface
            if (len > 1) {
                EV_LOGGING(QOS,WARNING,"QOS-DB-GET",
                        "More than one entry (%lu) with the same index %d in DB",
                        len, ifindex);
            }
            cps_api_object_t db_obj = cps_api_object_list_get(lst.get(),0);

            cps_api_key_set_attr(cps_api_object_key(db_obj), cps_api_oper_SET);

            // push the DB to NPU
            nas_qos_cps_api_port_ing_set(db_obj, NULL);

            EV_LOGGING(QOS, NOTICE,"QOS-DB", "One Ingress Port DB record for port %d written to NPU", ifindex);

        }
        else {
            EV_LOGGING(QOS, NOTICE, "QOS-DB-GET",
                    "No DB entry with the same index %d in DB",
                    ifindex);
        }
    }
}

static cps_api_return_code_t get_cps_obj_switch_port_id(cps_api_object_t obj,
                                                uint_t& switch_id, uint_t& port_id)
{
    cps_api_object_attr_t attr;

    switch_id = 0;

    attr = cps_api_get_key_data(obj, BASE_QOS_PORT_INGRESS_PORT_ID);
    if (attr == NULL) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "port id not specified in message\n");
        return NAS_QOS_E_MISSING_KEY;
    }
    port_id = cps_api_object_attr_data_u32(attr);

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_qos_cps_api_port_ing_set(
                                            cps_api_object_t obj,
                                            cps_api_object_list_t sav_obj)
{
    uint_t switch_id = 0, port_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    if ((rc = get_cps_obj_switch_port_id(obj, switch_id, port_id)) !=
            cps_api_ret_code_OK) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "Failed to get switch and port id");
        return rc;
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modify switch id %u, port id %u\n",
                    switch_id, port_id);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "Switch %u not found\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_port_ingress* port_ing_p = p_switch->get_port_ingress(port_id);
    if (port_ing_p == NULL) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "Could not find port ingress object for switch %d port %d\n",
                switch_id, port_id);
        return NAS_QOS_E_FAIL;
    }

    /* make a local copy of the existing port ingress */
    nas_qos_port_ingress port_ing(*port_ing_p);

    if ((rc = nas_qos_cps_parse_attr(obj, port_ing)) != cps_api_ret_code_OK) {
        return rc;
    }

    try {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Modifying port ingress %u attr \n",
                     port_ing.get_port_id());

        if (!nas_is_virtual_port(port_id) &&
            port_ing_p->is_created_in_ndi()) {
            nas::attr_set_t modified_attr_list = port_ing.commit_modify(*port_ing_p,
                                                        (sav_obj? false: true));

            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "done with commit_modify \n");


            // set attribute with full copy
            // save rollback info if caller requests it.
            // use modified attr list, current port ingress value
            if (sav_obj) {
                cps_api_object_t tmp_obj;
                tmp_obj = cps_api_object_list_create_obj_and_append(sav_obj);
                if (tmp_obj == NULL) {
                    return cps_api_ret_code_ERR;
                }

                nas_qos_store_prev_attr(tmp_obj, modified_attr_list, *port_ing_p);

           }
        }

        // update the local cache with newly set values
        *port_ing_p = port_ing;

        // update DB
        if (cps_api_db_commit_one(cps_api_oper_SET, obj, nullptr, false) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Fail to store ingress port update to DB");
        }

    } catch (nas::base_exception& e) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                    "NAS PORT INGRESS Attr Modify error code: %d ",
                    e.err_code);
        return e.err_code;

    } catch (...) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                    "NAS PORT INGRESS Modify Unexpected error code");
        return NAS_QOS_E_FAIL;
    }

    return cps_api_ret_code_OK;
}

/*
  * This function provides NAS-QoS PORT-INGRESS CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_ingress_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    if (obj == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Object not exist\n");
        return cps_api_ret_code_ERR;
    }
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&port_ing_mutex);

    switch (op) {
    case cps_api_oper_CREATE:
    case cps_api_oper_DELETE:
        // Port ingress requires no creation or deletion
        return NAS_QOS_E_FAIL;
    case cps_api_oper_SET:
        return nas_qos_cps_api_port_ing_set(obj, param->prev);

    default:
        return NAS_QOS_E_UNSUPPORTED;
    }
}

/*
  * This function provides NAS-QoS PORT-INGRESS CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_ingress_read(void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj;
    uint_t switch_id = 0, port_id = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    obj = cps_api_object_list_get(param->filters, ix);
    if (obj == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Object not exist\n");
        return NAS_QOS_E_FAIL;
    }
    if ((rc = get_cps_obj_switch_port_id(obj, switch_id, port_id)) !=
            cps_api_ret_code_OK) {
        EV_LOGGING(QOS, INFO, "NAS-QOS",
                "Failed to get switch and port id");
        return rc;
    }

    if (!nas_qos_port_is_initialized(switch_id, port_id)) {
        nas_qos_if_create_notify(port_id);
    }

    EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Read switch id %u, port id %u\n",
                 switch_id, port_id);

    std_mutex_simple_lock_guard p_m(&port_ing_mutex);

    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS",
                        "Switch %u not found\n",
                        switch_id);
        return NAS_QOS_E_FAIL;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    nas_qos_port_ingress *port_ing = p_switch->get_port_ingress(port_id);
    if (port_ing == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Failed to get port ingress object, port_id=%d\n",
                   port_id);
        return NAS_QOS_E_FAIL;
    }

    /* fill in data */
    cps_api_object_t ret_obj;

    ret_obj = cps_api_object_list_create_obj_and_append(param->list);
    if (ret_obj == NULL){
        return cps_api_ret_code_ERR;
    }

    // @todo: handle getting individual attributes.
    cps_api_key_from_attr_with_qual(cps_api_object_key(ret_obj),
            BASE_QOS_PORT_INGRESS_OBJ,
            cps_api_qualifier_TARGET);
    cps_api_set_key_data(ret_obj, BASE_QOS_PORT_INGRESS_PORT_ID,
            cps_api_object_ATTR_T_U32,
            &port_id, sizeof(uint32_t));
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_PORT_INGRESS_DEFAULT_TRAFFIC_CLASS,
                                port_ing->get_default_traffic_class());
    // convert to local per-type map-id before returning to cps-api user
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_DOT1P_TO_TC_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_dot1p_to_tc_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_DOT1P_TO_COLOR_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_dot1p_to_color_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_DOT1P_TO_TC_COLOR_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_dot1p_to_tc_color_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_DSCP_TO_TC_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_dscp_to_tc_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_DSCP_TO_COLOR_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_dscp_to_color_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_DSCP_TO_TC_COLOR_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_dscp_to_tc_color_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_TC_TO_QUEUE_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_tc_to_queue_map()));
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_PORT_INGRESS_FLOW_CONTROL,
                                port_ing->get_flow_control());
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_POLICER_ID,
                                port_ing->get_policer_id());
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_FLOOD_STORM_CONTROL,
                                port_ing->get_flood_storm_control());
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_BROADCAST_STORM_CONTROL,
                                port_ing->get_broadcast_storm_control());
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_MULTICAST_STORM_CONTROL,
                                port_ing->get_multicast_storm_control());
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_TC_TO_PRIORITY_GROUP_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_tc_to_priority_group_map()));
    cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_TO_PFC_PRIORITY_MAP,
                                GET_LOCAL_MAP_ID(port_ing->get_priority_group_to_pfc_priority_map()));
    cps_api_object_attr_add_u32(ret_obj, BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_NUMBER,
                                port_ing->get_priority_group_id_count());
    uint8_t bit_vec = port_ing->get_per_priority_flow_control();
    cps_api_object_attr_add(ret_obj, BASE_QOS_PORT_INGRESS_PER_PRIORITY_FLOW_CONTROL,
                            &bit_vec, sizeof(uint8_t));
    for (uint_t idx = 0; idx < port_ing->get_priority_group_id_count(); idx++) {
        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_PRIORITY_GROUP_ID_LIST,
                                    port_ing->get_priority_group_id(idx));
    }
    for (uint_t idx = 0; idx < port_ing->get_buffer_profile_id_count(); idx++) {
        cps_api_object_attr_add_u64(ret_obj, BASE_QOS_PORT_INGRESS_BUFFER_PROFILE_ID_LIST,
                                    port_ing->get_buffer_profile_id(idx));
    }
    return cps_api_ret_code_OK;
}

/*
  * This function provides NAS-QoS PORT-INGRESS CPS API rollback function
  * @Param    Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_port_ingress_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    if (obj == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Object not exist\n");
        return cps_api_ret_code_ERR;
    }
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    std_mutex_simple_lock_guard p_m(&port_ing_mutex);

    if (op == cps_api_oper_SET) {
        nas_qos_cps_api_port_ing_set(obj, NULL);
    }

    // create/delete are not allowed for port ingress, no roll-back is needed

    return cps_api_ret_code_OK;
}

/* Debugging and unit testing */
void dump_nas_qos_port_ingress(nas_switch_id_t switch_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);

    if (p_switch) {
        p_switch->dump_all_port_ing_profile();
    }
}
