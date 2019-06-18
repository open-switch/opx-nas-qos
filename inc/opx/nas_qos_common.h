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
 * \file   nas_qos_common.h
 * \brief  NAS QOS Common macros and typedefs
 * \date   02-2015
 * \author
 */

#ifndef _NAS_QOS_COMMON_H_
#define _NAS_QOS_COMMON_H_

#include "ietf-inet-types.h"
#include "nas_ndi_qos.h"
#include <string>
/** NAS QOS Error Codes */
#define NAS_QOS_E_NONE          (int)STD_ERR_OK
#define NAS_QOS_E_MEM           (int)STD_ERR (QOS, NONMEM, 0)

#define NAS_QOS_E_MISSING_KEY   (int)STD_ERR (QOS, CFG, 1)
#define NAS_QOS_E_MISSING_ATTR  (int)STD_ERR (QOS, CFG, 2)
#define NAS_QOS_E_UNSUPPORTED   (int)STD_ERR (QOS, CFG, 3) // Unsupported attribute
#define NAS_QOS_E_DUPLICATE     (int)STD_ERR (QOS, CFG, 4) // Attribute duplicated in CPS object
#define NAS_QOS_E_ATTR_LEN      (int)STD_ERR (QOS, CFG, 5) // Unexpected attribute length

#define NAS_QOS_E_CREATE_ONLY   (int)STD_ERR (QOS, PARAM, 1) // Modify attempt on create-only attribute
#define NAS_QOS_E_ATTR_VAL      (int)STD_ERR (QOS, PARAM, 2) // Wrong value for attribute
#define NAS_QOS_E_INCONSISTENT  (int)STD_ERR (QOS, PARAM, 3) // Attribute Value inconsistent
                                                             // with other attributes

#define NAS_QOS_E_KEY_VAL       (int)STD_ERR (QOS, NEXIST, 0) // No object with this key

#define NAS_QOS_E_FAIL          (int)STD_ERR (QOS, FAIL, 0) // All other run time failures

#define NAS_QOS_NULL_OBJECT_ID  0

// TYPE MASK prepended to the nas-obj-id to make QUEUE-id and Scheduler-id
// unique across the NAS subsystem
#define NAS_QUEUE_ID_TYPE_MASK              0x1000000000000000UL
#define NAS_SCHEDULER_GROUP_ID_TYPE_MASK    0x2000000000000000UL

#define NAS_SCHEDULER_GROUP_ID_AUTO_FORMED  0x0100000000000000UL
#define IS_SG_ID_AUTO_FORMED(x)             ((x) & NAS_SCHEDULER_GROUP_ID_AUTO_FORMED)

// Format of allocated nas-sg-id:
//  { SG_TYPE_MASK (8-bits) | level (8-bits) | local-sg-index (16-bits) | port_id (32-bit) }
#define NAS_SG_ID_LEVEL_POS             48
#define NAS_SG_ID_LOCAL_SG_INDEX_POS    32
#define NAS_SG_ID_PORT_ID_POS            0

#define NAS_SG_ID_LEVEL_FIELD_MASK          0x00FF000000000000UL
#define NAS_SG_ID_LOCAL_SG_INDEX_FIELD_MASK 0x0000FFFF00000000UL
#define NAS_SG_ID_PORT_ID_FIELD_MASK        0x00000000FFFFFFFFUL

#define NAS_QOS_FORMAT_SG_ID(port_id, level, local_sg_index) \
                (NAS_SCHEDULER_GROUP_ID_TYPE_MASK | \
                 NAS_SCHEDULER_GROUP_ID_AUTO_FORMED | \
                    (((uint64_t)level)          << NAS_SG_ID_LEVEL_POS) | \
                    (((uint64_t)local_sg_index) << NAS_SG_ID_LOCAL_SG_INDEX_POS) | \
                    (((uint64_t)port_id         << NAS_SG_ID_PORT_ID_POS)))

#define NAS_QOS_GET_SG_LEVEL(x)       (((x) & NAS_SG_ID_LEVEL_FIELD_MASK) >> NAS_SG_ID_LEVEL_POS)
#define NAS_QOS_GET_SG_LOCAL_INDEX(x) (((x) & NAS_SG_ID_LOCAL_SG_INDEX_FIELD_MASK) >> NAS_SG_ID_LOCAL_SG_INDEX_POS)
#define NAS_QOS_GET_SG_PORT_ID(x)     (((x) & NAS_SG_ID_PORT_ID_FIELD_MASK) >> NAS_SG_ID_PORT_ID_POS)

typedef ndi_qos_map_type_t nas_qos_map_type_t;

// NAS MAP-ID encoding: Map-type (5-bit) + Per-type Map-id (7-bit)
#define MAX_MAP_ID_PER_TYPE     127     // 0 is reserved, 1..127 are available
#define MAX_MAP_TYPES           32
#define MAP_ID_BIT_MASK         (0x7F)  // Per-type map-id bits
#define MAP_TYPE_POS            7       // starting from 8th bit (ie. bit-7 in 0..7)
#define MAP_TYPE_BIT_MASK       (0x1F << MAP_TYPE_POS)

// Encode map-type and per-type map-id into a unique NAS map-id
#define ENCODE_LOCAL_MAP_ID_AND_TYPE(map_id, map_type) \
            (map_id == 0? NAS_QOS_NULL_OBJECT_ID : \
             (((map_type) << (MAP_TYPE_POS)) | (map_id & MAP_ID_BIT_MASK)))

// Get the per-type map-id, which is used by CPS YANG model application
#define GET_LOCAL_MAP_ID(map_id)         (map_id & MAP_ID_BIT_MASK)
#define GET_LOCAL_MAP_TYPE(map_id)       ((map_id & MAP_TYPE_BIT_MASK) >> MAP_TYPE_POS)
#define MAP_ID_IS_UNSPECIFIED(map_id)    (GET_LOCAL_MAP_ID(map_id) == 0)


// Making complex keys
#define MAKE_KEY(key1, key2)     (((key2) << 16) | (key1))
#define GET_KEY1(key)            (((key).any) & 0xFFFF)
#define GET_KEY2(key)            ((((key).any) >> 16) & 0xFFFF)


typedef union nas_qos_map_entry_key_t {
    /* The following field are selectively filled
     * based on the type of qos map.
     * TC, DSCP, DOT1P, pfc-priority are all casted into uint_t
     * so the map_entry can have uniformed uint_t key.
     */
    // Traffic Class
    uint_t    tc;

    // DSCP value
    uint_t  dscp;

    // dot1p value
    uint_t  dot1p;

    // tc-queue-type combination key
    uint_t  tc_queue_type;

    // tc-color-type combination key
    uint_t  tc_color;

    // PG-PFC combination key: PG-to-PFC-priority map entry key
    uint_t    pg_prio;

    // PFC-queue-type combination key: PFC-priority-to-queue map entry key
    uint_t    prio_queue_type;

    // generic reference to any of the above fields
    uint_t  any;

} nas_qos_map_entry_key_t;

typedef struct nas_qos_map_entry_value_t {
    /* The following field are selectively filled
     * based on the type of qos map.
     */
    // TC and Color are used by dot1p|dscp to TC|Color map
    // Traffic Class
    BASE_CMN_TRAFFIC_CLASS_t tc;

    // Color
    BASE_QOS_PACKET_COLOR_t color;

    // DSCP and dot1p are used by TC to dscp|dot1p map
    // DSCP value
    INET_DSCP_t   dscp;

    // dot1p value
    BASE_CMN_DOT1P_t  dot1p;

    // Queue number
    uint_t     queue_num;

    // local priority group id
    uint_t         pg;

} nas_qos_map_entry_value_t;


typedef struct stat_attr_capability_t {
    bool            read_ok;
    bool            write_ok;
    bool            snapshot_ok;
} stat_attr_capability;


t_std_error nas_qos_port_hqos_init(hal_ifindex_t ifindex, ndi_port_t ndi_port_id);
t_std_error nas_qos_port_priority_group_init(hal_ifindex_t ifindex, ndi_port_t ndi_port_id);
t_std_error nas_qos_port_ingress_init(hal_ifindex_t port_id, ndi_port_t ndi_port_id);
t_std_error nas_qos_port_egress_init(hal_ifindex_t port_id, ndi_port_t ndi_port_id);
void nas_qos_port_ingress_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd);
void nas_qos_port_egress_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd);
void nas_qos_port_priority_group_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd);
void nas_qos_port_queue_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd);
void nas_qos_port_scheduler_group_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd);
void nas_qos_port_pool_association(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, bool isAdd);
bool nas_qos_port_is_initialized(uint32_t switch_id, hal_ifindex_t port_id);
char * nas_qos_fmt_error_code(t_std_error ec);
t_std_error nas_qos_if_name_to_if_index(hal_ifindex_t *if_index, const char *name);
t_std_error nas_qos_get_if_index_to_name(hal_ifindex_t if_index, std::string &name);
#endif
