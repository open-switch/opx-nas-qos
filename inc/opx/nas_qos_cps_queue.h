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
 * \file   nas_qos_cps_queue.h
 * \brief  NAS qos queue CPS API routines
 * \date   02-2015
 * \author
 */
#ifndef _NAS_QOS_CPS_QUEUE_H_
#define _NAS_QOS_CPS_QUEUE_H_

#include "cps_api_operation.h"

#include <unordered_map>

typedef std::unordered_map<nas_obj_id_t, ndi_obj_id_t> parent_map_t;

/**
  * This function provides NAS-QoS queue CPS API write function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_queue_write(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix);

/**
  * This function provides NAS-QoS queue CPS API read function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_queue_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix);
/**
  * This function provides NAS-QoS queue CPS API rollback function
  * @Param      Standard CPS API params
  * @Return   Standard Error Code
  */
cps_api_return_code_t nas_qos_cps_api_queue_rollback(void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix);

cps_api_return_code_t nas_qos_cps_api_queue_stat_read (void * context,
                                            cps_api_get_params_t * param,
                                            size_t ix);
cps_api_return_code_t nas_qos_cps_api_queue_stat_clear (void * context,
                                            cps_api_transaction_params_t * param,
                                            size_t ix);

t_std_error nas_qos_port_queue_init(hal_ifindex_t ifindex, ndi_port_t ndi_port_id, parent_map_t & parent_map);

#endif
