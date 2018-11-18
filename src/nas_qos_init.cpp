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
 * \file   nas_qos_init.cpp
 * \brief  NAS QOS Entry Point
 * \date   02-2015
 * \author
 */

#include "event_log.h"
#include "std_error_codes.h"
#include "nas_qos_cps.h"
#include "dell-base-qos.h"
#include "dell-base-if.h"
#include "cps_class_map.h"
#include "cps_api_events.h"
#include "nas_qos_switch_list.h"
#include "cps_api_object_key.h"
#include "iana-if-type.h"
#include "nas_if_utils.h"
#include "dell-base-if-lag.h"
#include "nas_qos_cps_queue.h"
#include "nas_qos_cps_policer.h"
#include "nas_qos_cps_buffer_pool.h"
#include "nas_qos_cps_buffer_profile.h"
#include "nas_qos_cps_map.h"
#include "nas_qos_cps_port_egress.h"
#include "nas_qos_cps_port_ingress.h"
#include "nas_qos_cps_port_pool.h"
#include "nas_qos_cps_priority_group.h"
#include "nas_qos_cps_scheduler.h"
#include "nas_qos_cps_scheduler_group.h"
#include "nas_qos_cps_wred.h"
#include <map>

static bool nas_qos_if_set_handler(
        cps_api_object_t obj, void *context)
{

    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    // Only listen to Interface Create/Delete event
    cps_api_object_attr_t if_index_attr =
        cps_api_get_key_data(obj, DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);

    if (if_index_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Interface message does not have if-index");
        return true;
    }

    uint32_t ifidx = cps_api_object_attr_data_u32(if_index_attr);

    if (op == cps_api_oper_CREATE) {
        nas_qos_if_create_notify(ifidx);
        return true;
    }

    if (op == cps_api_oper_DELETE) {
        nas_qos_if_delete_notify(ifidx);
        return true;
    }

    if (op == cps_api_oper_SET) {
        nas_int_port_mapping_t port_mapping;
        if(!nas_get_phy_port_mapping_change( obj, &port_mapping)){
            EV_LOGGING(QOS,DEBUG,"NAS-QOS","Interface event is not an "
                    "association/dis-association event, skipped");
            return true;
        }

        cps_api_object_attr_t npu_attr = cps_api_object_attr_get(obj,
                                        BASE_IF_PHY_IF_INTERFACES_INTERFACE_NPU_ID);
        cps_api_object_attr_t port_attr = cps_api_object_attr_get(obj,
                                        BASE_IF_PHY_IF_INTERFACES_INTERFACE_PORT_ID);

        if (npu_attr == nullptr || port_attr == nullptr ) {
            EV_LOGGING(QOS,DEBUG, "NAS-QOS", "Interface object does not have npu/port");
            return true;
        }

        ndi_port_t ndi_port;
        ndi_port.npu_id = cps_api_object_attr_data_u32(npu_attr);
        ndi_port.npu_port = cps_api_object_attr_data_u32(port_attr);

        bool add = (port_mapping == nas_int_phy_port_MAPPED) ? true : false;

        nas_qos_if_set_notify(ifidx, ndi_port, add);
    }

    return true;
}

static t_std_error nas_qos_init_existing_intf(char * if_type, uint8_t sizeof_if_type)
{
    cps_api_object_t obj = NULL, ret_obj = NULL;
    cps_api_get_params_t gp;
    t_std_error rc = STD_ERR_OK;
    cps_api_object_attr_t attr = NULL;
    uint32_t ifindex;

    EV_LOGGING(QOS, NOTICE, "QOS", "Scanning created interface ... ");

    if (cps_api_get_request_init(&gp) != cps_api_ret_code_OK) {
        EV_LOGGING(QOS, ERR, "QOS", "cps_api_get_request_init() failed ");
        return STD_ERR(QOS, NOMEM, 0);
    }

    do {
        obj = cps_api_object_list_create_obj_and_append(gp.filters);
        if (obj == NULL) {
            EV_LOGGING(QOS, ERR, "QOS", "cps_api_object_list_create_obj_and_append () failed");
            rc = STD_ERR(QOS, NOMEM, 0);
            break;
        }
        if (! cps_api_key_from_attr_with_qual(cps_api_object_key(obj),
                    DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_OBJ,
                    cps_api_qualifier_TARGET)) {
            EV_LOGGING(QOS, ERR, "QOS", "cps_api_key_from_attr_with_qual() failed ");
            rc = STD_ERR(QOS, FAIL, 0);
            break;
        }
        if (! cps_api_object_attr_add(obj, IF_INTERFACES_INTERFACE_TYPE,
                                             if_type, sizeof_if_type)){
            EV_LOGGING(QOS, ERR, "QOS", "cps_api_object_attr_add () failed ");
            rc = STD_ERR(QOS, FAIL, 0);
            break;
        }
        if (cps_api_get(&gp) != cps_api_ret_code_OK) {
            EV_LOGGING(QOS, ERR, "QOS", "cps_api_get () failed");
            rc = STD_ERR(QOS, FAIL, 0);
            break;
        }

        size_t obj_num = cps_api_object_list_size(gp.list);
        if (obj_num == 0) {
            EV_LOGGING(QOS, NOTICE, "QOS", "no interface object returned");
        }

        for (size_t id = 0; id < obj_num; id++) {

            ret_obj = cps_api_object_list_get(gp.list, id);
            if (ret_obj == NULL) {
                EV_LOGGING(QOS, ERR, "QOS", "cps_api_object_list_get () failed");
                continue;
            }

            attr = cps_api_get_key_data(ret_obj, DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);
            if (!attr) {
                EV_LOGGING(QOS, ERR, "QOS", "cps_api_get_key_data () failed, no ifindex");
                continue;
            }

            ifindex = cps_api_object_attr_data_u32(attr);

            EV_LOGGING(QOS, NOTICE, "QOS", "ifindex created : %u", ifindex);
            nas_qos_if_create_notify(ifindex);
        }

    }while(0);

    if(cps_api_get_request_close(&gp) != cps_api_ret_code_OK)
    {
        EV_LOGGING(QOS, DEBUG, "QOS", "CPS get request close failed ");
        rc = STD_ERR(QOS, FAIL, 0);
    }
    return rc;

}

static t_std_error cps_init ()
{
    cps_api_operation_handle_t       h;
    cps_api_return_code_t            cps_rc;

    if ((cps_rc = cps_api_operation_subsystem_init(&h,1))
          != cps_api_ret_code_OK) {
        return STD_ERR(QOS, FAIL, cps_rc);
    }

    typedef struct {
        cps_api_return_code_t (*_read_function) (void * context, cps_api_get_params_t * param, size_t key_ix); //!< the read db function
        cps_api_return_code_t (*_write_function)(void * context, cps_api_transaction_params_t * param, size_t index_of_element_being_updated); //!< the set db function
        cps_api_return_code_t (*_rollback_function)(void * context, cps_api_transaction_params_t * param, size_t index_of_element_being_updated); //!< the set db function
        cps_api_qualifier_t cat;
    } qos_cps_init_t;

    std::map<cps_api_attr_id_t, qos_cps_init_t> qos_cps_init_map;

    /* All Stats register with REALTIME  */
    qos_cps_init_map[BASE_QOS_BUFFER_POOL_STAT_OBJ] = {
                                                        nas_qos_cps_api_buffer_pool_stat_read,
                                                        nas_qos_cps_api_buffer_pool_stat_clear,
                                                        NULL,
                                                        cps_api_qualifier_REALTIME
                                                      };

    qos_cps_init_map[BASE_QOS_PRIORITY_GROUP_STAT_OBJ] = {
                                                            nas_qos_cps_api_priority_group_stat_read,
                                                            nas_qos_cps_api_priority_group_stat_clear,
                                                            NULL,
                                                            cps_api_qualifier_REALTIME
                                                         };

    qos_cps_init_map[BASE_QOS_PORT_POOL_STAT_OBJ] = {
                                                      nas_qos_cps_api_port_pool_stat_read,
                                                      nas_qos_cps_api_port_pool_stat_clear,
                                                      NULL,
                                                      cps_api_qualifier_REALTIME
                                                    };

    qos_cps_init_map[BASE_QOS_QUEUE_STAT_OBJ] = {
                                                  nas_qos_cps_api_queue_stat_read,
                                                  nas_qos_cps_api_queue_stat_clear,
                                                  NULL,
                                                  cps_api_qualifier_REALTIME
                                                };
    qos_cps_init_map[BASE_QOS_METER_OBJ] = {
                                                nas_qos_cps_api_policer_read,
                                                nas_qos_cps_api_policer_write,
                                                nas_qos_cps_api_policer_rollback,
                                                cps_api_qualifier_TARGET
                                           };

    qos_cps_init_map[BASE_QOS_QUEUE_OBJ] = {
                                                nas_qos_cps_api_queue_read,
                                                nas_qos_cps_api_queue_write,
                                                nas_qos_cps_api_queue_rollback,
                                                cps_api_qualifier_TARGET
                                           };

    qos_cps_init_map[BASE_QOS_WRED_PROFILE_OBJ] = { nas_qos_cps_api_wred_read,
                                                    nas_qos_cps_api_wred_write,
                                                    nas_qos_cps_api_wred_rollback,
                                                    cps_api_qualifier_TARGET
                                                  };

    qos_cps_init_map[BASE_QOS_SCHEDULER_PROFILE_OBJ] = { nas_qos_cps_api_scheduler_read,
                                                         nas_qos_cps_api_scheduler_write,
                                                         nas_qos_cps_api_scheduler_rollback,
                                                         cps_api_qualifier_TARGET
                                                       };

    qos_cps_init_map[BASE_QOS_SCHEDULER_GROUP_OBJ] = {
                                                        nas_qos_cps_api_scheduler_group_read,
                                                        nas_qos_cps_api_scheduler_group_write,
                                                        nas_qos_cps_api_scheduler_group_rollback,
                                                        cps_api_qualifier_TARGET
                                                     };

    qos_cps_init_map[BASE_QOS_DOT1P_TO_TC_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                       nas_qos_cps_api_map_write,
                                                       nas_qos_cps_api_map_rollback,
                                                       cps_api_qualifier_TARGET
                                                     };

    qos_cps_init_map[BASE_QOS_DOT1P_TO_COLOR_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                          nas_qos_cps_api_map_write,
                                                          nas_qos_cps_api_map_rollback,
                                                          cps_api_qualifier_TARGET
                                                        };

    qos_cps_init_map[BASE_QOS_DOT1P_TO_TC_COLOR_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                             nas_qos_cps_api_map_write,
                                                             nas_qos_cps_api_map_rollback,
                                                             cps_api_qualifier_TARGET
                                                           };

    qos_cps_init_map[BASE_QOS_DSCP_TO_TC_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                      nas_qos_cps_api_map_write,
                                                      nas_qos_cps_api_map_rollback,
                                                      cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_DSCP_TO_COLOR_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                         nas_qos_cps_api_map_write,
                                                         nas_qos_cps_api_map_rollback,
                                                         cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_DSCP_TO_TC_COLOR_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                            nas_qos_cps_api_map_write,
                                                            nas_qos_cps_api_map_rollback,
                                                            cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_TC_TO_DOT1P_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                       nas_qos_cps_api_map_write,
                                                       nas_qos_cps_api_map_rollback,
                                                       cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_TC_TO_DSCP_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                      nas_qos_cps_api_map_write,
                                                      nas_qos_cps_api_map_rollback,
                                                      cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_TC_COLOR_TO_DOT1P_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                             nas_qos_cps_api_map_write,
                                                             nas_qos_cps_api_map_rollback,
                                                             cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_TC_COLOR_TO_DSCP_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                            nas_qos_cps_api_map_write,
                                                            nas_qos_cps_api_map_rollback,
                                                            cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_TC_TO_QUEUE_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                       nas_qos_cps_api_map_write,
                                                       nas_qos_cps_api_map_rollback,
                                                       cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_TC_TO_PRIORITY_GROUP_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                                nas_qos_cps_api_map_write,
                                                                nas_qos_cps_api_map_rollback,
                                                                cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_PRIORITY_GROUP_TO_PFC_PRIORITY_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                                          nas_qos_cps_api_map_write,
                                                                          nas_qos_cps_api_map_rollback,
                                                                          cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_PFC_PRIORITY_TO_QUEUE_MAP_OBJ] = { nas_qos_cps_api_map_read,
                                                                 nas_qos_cps_api_map_write,
                                                                 nas_qos_cps_api_map_rollback,
                                                                 cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_PORT_INGRESS_OBJ] = { nas_qos_cps_api_port_ingress_read,
                                                    nas_qos_cps_api_port_ingress_write,
                                                    nas_qos_cps_api_port_ingress_rollback,
                                                    cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_PORT_EGRESS_OBJ] = { nas_qos_cps_api_port_egress_read,
                                                   nas_qos_cps_api_port_egress_write,
                                                   nas_qos_cps_api_port_egress_rollback,
                                                   cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_BUFFER_POOL_OBJ] = { nas_qos_cps_api_buffer_pool_read,
                                                   nas_qos_cps_api_buffer_pool_write,
                                                   nas_qos_cps_api_buffer_pool_rollback,
                                                   cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_BUFFER_PROFILE_OBJ] = { nas_qos_cps_api_buffer_profile_read,
                                                      nas_qos_cps_api_buffer_profile_write,
                                                      nas_qos_cps_api_buffer_profile_rollback,
                                                      cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_PRIORITY_GROUP_OBJ] = { nas_qos_cps_api_priority_group_read,
                                                      nas_qos_cps_api_priority_group_write,
                                                      nas_qos_cps_api_priority_group_rollback,
                                                      cps_api_qualifier_TARGET};

    qos_cps_init_map[BASE_QOS_PORT_POOL_OBJ] = { nas_qos_cps_api_port_pool_read,
                                                 nas_qos_cps_api_port_pool_write,
                                                 nas_qos_cps_api_port_pool_rollback,
                                                 cps_api_qualifier_TARGET};

    /* This cps_api_obj_CAT_BASE_QOS Registeration will be removed once stats magr implementation over */
    qos_cps_init_map[cps_api_obj_CAT_BASE_QOS] = {
                                                  nas_qos_cps_api_read,
                                                  nas_qos_cps_api_write,
                                                  nas_qos_cps_api_rollback,
                                                  cps_api_qualifier_TARGET
                                                 };

    std::map<cps_api_attr_id_t, qos_cps_init_t>::iterator it;
    qos_cps_init_t qos_cps;
    cps_api_registration_functions_t f;
    /* Register All QoS objects */
    for (it = qos_cps_init_map.begin(); it != qos_cps_init_map.end(); it++ )
    {
        qos_cps = it->second;
        memset(&f,0,sizeof(f));
        f.handle = h;
        f._read_function = qos_cps._read_function;
        f._write_function = qos_cps._write_function;
        f._rollback_function = qos_cps._rollback_function;

        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Resgister Qos %d object with cat %d", it->first, qos_cps.cat);

        if (!cps_api_key_from_attr_with_qual(&f.key, it->first, qos_cps.cat)) {
            EV_LOGGING(QOS, ERR, "NAS-QOS", "Cannot create a key for qos %d object", it->first);
            return STD_ERR(QOS, FAIL, 0);
        } else {
            if ((cps_rc = cps_api_register(&f)) !=cps_api_ret_code_OK) {
                EV_LOGGING(QOS, ERR, "NAS-QOS", "Failed to register callback for qos %d object", it->first);
                return STD_ERR(QOS, FAIL, cps_rc);
            }
        }
    }

    // Register interface creation/deletion event
    cps_api_event_reg_t reg;
    cps_api_key_t key;

    memset(&reg, 0, sizeof(cps_api_event_reg_t));

    if (!cps_api_key_from_attr_with_qual(&key,
            DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_OBJ,
            cps_api_qualifier_OBSERVED)) {
        EV_LOGGING(QOS, ERR, "NAS-QOS", "Cannot create a key for interface event");
        return STD_ERR(QOS, FAIL, 0);
    }

    reg.objects = &key;
    reg.number_of_objects = 1;

    if (cps_api_event_thread_reg(&reg, nas_qos_if_set_handler, NULL)
            != cps_api_ret_code_OK) {
        EV_LOGGING(QOS, ERR, "NAS-QOS", "Cannot register interface operation event");
        return STD_ERR(QOS, FAIL, cps_rc);
    }

    // pick up any existing interfaces that has been created before event subscription
    char *if_type = NULL;
    uint8_t sizeof_if_type = 0;

   // First CPU ports
    if_type = (char *)IF_INTERFACE_TYPE_IANAIFT_IANA_INTERFACE_TYPE_BASE_IF_CPU;
    sizeof_if_type = sizeof(IF_INTERFACE_TYPE_IANAIFT_IANA_INTERFACE_TYPE_BASE_IF_CPU);

    (void)nas_qos_init_existing_intf(if_type, sizeof_if_type);

    // Then Front panel ports
    if_type = (char *)IF_INTERFACE_TYPE_IANAIFT_IANA_INTERFACE_TYPE_IANAIFT_ETHERNETCSMACD;
    sizeof_if_type = sizeof(IF_INTERFACE_TYPE_IANAIFT_IANA_INTERFACE_TYPE_IANAIFT_ETHERNETCSMACD);

    (void)nas_qos_init_existing_intf(if_type, sizeof_if_type);

     return STD_ERR_OK;
}

extern "C" {
/**
 * This function initializes the lower NAS related QoS data structure
 * @Return   Standard Error Code
 */
t_std_error nas_qos_init(void)
{
    t_std_error         rc = STD_ERR_OK;

    EV_LOGGING(QOS, INFO, "NAS-QOS", "Initializing NAS-QOS data structures");

    do {

        if ((rc = cps_init ()) != STD_ERR_OK) {
            break;
        }

    } while (0);

    return rc;
}


}
