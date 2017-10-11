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


static bool nas_qos_if_set_handler(
        cps_api_object_t obj, void *context)
{

    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));
    if (op != cps_api_oper_DELETE)
        return true;

    EV_LOGGING(QOS, ERR, "NAS-QOS", "Getting an interface deletion notification...");

    // Only listen to Interface Delete event
    cps_api_object_attr_t if_index_attr =
        cps_api_get_key_data(obj, DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);

    if (if_index_attr == NULL) {
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Interface Deletion message does not have if-index");
        return true;
    }

    uint32_t ifidx = cps_api_object_attr_data_u32(if_index_attr);

    nas_qos_if_delete_notify(ifidx);

    return true;
}

static t_std_error cps_init ()
{
    cps_api_operation_handle_t       h;
    cps_api_return_code_t            cps_rc;
    cps_api_registration_functions_t f;

    if ((cps_rc = cps_api_operation_subsystem_init(&h,1))
          != cps_api_ret_code_OK) {
        return STD_ERR(QOS, FAIL, cps_rc);
    }

    memset(&f,0,sizeof(f));

    f.handle = h;
    f._read_function = nas_qos_cps_api_read;
    f._write_function = nas_qos_cps_api_write;
    f._rollback_function = nas_qos_cps_api_rollback;

    /* Register all QoS object */
    cps_api_key_init(&f.key,
                     cps_api_qualifier_TARGET,
                     (cps_api_object_category_types_t)cps_api_obj_CAT_BASE_QOS,
                     0, /* register all sub-categories */
                     0);

    if ((cps_rc = cps_api_register(&f)) != cps_api_ret_code_OK) {
        return STD_ERR(QOS, FAIL, cps_rc);
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
