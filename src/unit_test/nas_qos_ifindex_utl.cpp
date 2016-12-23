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


#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <assert.h>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <list>

#include "cps_api_events.h"
#include "cps_api_key.h"
#include "cps_api_operation.h"
#include "cps_api_object.h"
#include "cps_api_errors.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"
#include "dell-base-qos.h"
#include "dell-base-if.h"
#include "iana-if-type.h"

using namespace std;

typedef unordered_map<string, uint32_t> if_info_map_t;

static bool nas_qos_get_intf_list(if_info_map_t& if_list, const char * if_type)
{
    cps_api_get_params_t gp;
    if (cps_api_get_request_init(&gp) != cps_api_ret_code_OK) {
        return false;
    }

    cps_api_object_t obj = cps_api_object_list_create_obj_and_append(gp.filters);
    if (obj == NULL) {
        return false;
    }
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj), DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_object_attr_add(obj, IF_INTERFACES_INTERFACE_TYPE,
        (const char *)if_type,
        strlen(if_type));


    if (cps_api_get(&gp) == cps_api_ret_code_OK) {
        cps_api_object_attr_t attr;
        uint32_t ifindex;
        char *type;
        char *ifname;
        size_t obj_num = cps_api_object_list_size(gp.list);
        for (size_t id = 0; id < obj_num; id ++) {
            obj = cps_api_object_list_get(gp.list, id);
            attr = cps_api_get_key_data(obj, DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);
            if (!attr) {
                continue;
            }
            ifindex = cps_api_object_attr_data_u32(attr);
            attr = cps_api_object_attr_get(obj, IF_INTERFACES_INTERFACE_NAME);
            if (!attr) {
                continue;
            }
            ifname = (char *)cps_api_object_attr_data_bin(attr);
            attr = cps_api_object_attr_get(obj, IF_INTERFACES_INTERFACE_TYPE);
            if (!attr) {
                continue;
            }
            type = (char *)cps_api_object_attr_data_bin(attr);
            if (strcmp(if_type, type)) {
                continue;
            }
            if_list.insert(make_pair(ifname, ifindex));
        }
    }

    return true;
}

bool nas_qos_get_if_index(const char *if_name, const char *if_type, uint32_t& if_index)
{
    cps_api_get_params_t gp;
    if (cps_api_get_request_init(&gp) != cps_api_ret_code_OK) {
        return false;
    }

    cps_api_object_t obj = cps_api_object_list_create_obj_and_append(gp.filters);
    if (obj == NULL) {
        return false;
    }
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj), DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_object_attr_add(obj, IF_INTERFACES_INTERFACE_TYPE,
        (const char *)if_type,
        strlen(if_type));

    cps_api_object_attr_add(obj, IF_INTERFACES_INTERFACE_NAME,
    		(const char *)if_name,
    		strlen(if_name));

    if (cps_api_get(&gp) == cps_api_ret_code_OK) {
        cps_api_object_attr_t attr;
        size_t obj_num = cps_api_object_list_size(gp.list);
        for (size_t id = 0; id < obj_num; id ++) {
            obj = cps_api_object_list_get(gp.list, id);
            attr = cps_api_get_key_data(obj, DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);
            if (!attr) {
                continue;
            }
            else {
            	if_index = cps_api_object_attr_data_u32(attr);
            	return true;
            }
        }
    }

    return true;
}

bool nas_qos_get_first_phy_port(char *if_name, uint_t name_len,
                                uint32_t& if_index)
{
    if_info_map_t if_list;
    if (!nas_qos_get_intf_list(if_list,
    		IF_INTERFACE_TYPE_IANAIFT_IANA_INTERFACE_TYPE_IANAIFT_ETHERNETCSMACD)) {
        printf("failed to get interface list\n");
        return false;
    }
    if (if_list.size() == 0) {
        printf("no physical port found\n");
        return false;
    }
    list<string> key_list;
    for (auto& if_info: if_list) {
        key_list.push_back(if_info.first);
    }
    key_list.sort();
    string name = key_list.front();
    if (if_name && name_len > 0) {
        strncpy(if_name, name.c_str(), name_len);
        if_name[name_len - 1] = '\0';
    }
    if_index = if_list[name];

    return true;
}
