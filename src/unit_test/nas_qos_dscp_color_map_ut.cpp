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

#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <assert.h>

#include "cps_api_events.h"
#include "cps_api_key.h"
#include "cps_api_operation.h"
#include "cps_api_object.h"
#include "cps_api_errors.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"

#include "gtest/gtest.h"

#include "dell-base-qos.h"

static uint64_t test_map_id;

using namespace std;


bool nas_qos_dscp_color_map_create_test() {

    printf("starting nas_qos_dscp_color_map_create_test\n");

    cps_api_transaction_params_t tran;
    if (cps_api_transaction_init(&tran) != cps_api_ret_code_OK)
        return false;

    cps_api_object_t obj = cps_api_object_create();
    if (obj == NULL)
        return false;

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_DSCP_TO_COLOR_MAP_OBJ,
            cps_api_qualifier_TARGET);

    if (cps_api_create(&tran, obj) != cps_api_ret_code_OK) {
        cout << "CPS API CREATE FAILED" <<endl;
        return false;
    }

    if (cps_api_commit(&tran) != cps_api_ret_code_OK) {
        cout << "CPS API commit failed" <<endl;
        return false;
    }

    //Upon success, store the returned dscp_color_map id for future retrieval
    cps_api_object_t recvd_obj = cps_api_object_list_get(tran.prev,0);
    cps_api_object_attr_t map_id_attr = cps_api_get_key_data(recvd_obj, BASE_QOS_DSCP_TO_COLOR_MAP_ID);

    if (map_id_attr == NULL) {
        cout << "Key dscp_color_map id not returned\n";
        return false;
    }

    test_map_id = cps_api_object_attr_data_u64(map_id_attr);

    cout << " NAS returns dscp_color_map Id: "<< test_map_id  << endl;

    if (cps_api_transaction_close(&tran) != cps_api_ret_code_OK) {
        cout << "CPS API transaction closed" << endl;
        return false;
    }

    return true;
}

bool nas_qos_dscp_color_map_get_test(uint64_t map_id) {
    printf("starting nas_qos_dscp_color_map_get_test\n");

    cps_api_get_params_t gp;
    if (cps_api_get_request_init(&gp) != cps_api_ret_code_OK)
        return false;


    cps_api_object_t obj = cps_api_object_list_create_obj_and_append(gp.filters);
    if (obj == NULL)
        return false;

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_DSCP_TO_COLOR_MAP_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ID,
            cps_api_object_ATTR_T_U64,
            &map_id, sizeof(uint64_t));

    bool rc = false;

    if (cps_api_get(&gp) == cps_api_ret_code_OK) {
        // Map id exists, no other info at this level
        cout << "dscp to tc map " << test_map_id << "is created" << endl;
        rc = true;
    }
    else {
        cout << "dscp to color map " << test_map_id << "is not created" << endl;
    }

    cps_api_get_request_close(&gp);
    return rc;
}

bool nas_qos_dscp_color_map_entry_get_test(uint64_t map_id, uint8_t dscp) {
    printf("starting nas_qos_dscp_color_map_get_test\n");

    cps_api_get_params_t gp;
    if (cps_api_get_request_init(&gp) != cps_api_ret_code_OK)
        return false;


    cps_api_object_t obj = cps_api_object_list_create_obj_and_append(gp.filters);
    if (obj == NULL)
        return false;

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj), BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ID,
            cps_api_object_ATTR_T_U64,
            &map_id, sizeof(uint64_t));

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY_DSCP,
            cps_api_object_ATTR_T_BIN,
            &dscp, sizeof(uint8_t));

    bool rc = false;

    if (cps_api_get(&gp) == cps_api_ret_code_OK) {
        size_t ix = 0;
        size_t mx = cps_api_object_list_size(gp.list);

        for (; ix < mx; ++ix) {
            cps_api_object_t obj = cps_api_object_list_get(gp.list, ix);
            cps_api_object_attr_t color_attr = cps_api_object_attr_get(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY_COLOR);
            if (color_attr)
                printf("dscp: %u, color: %u\n", dscp, cps_api_object_attr_data_u32(color_attr));

        }

        rc = true;
    }

    cps_api_get_request_close(&gp);
    return rc;
}

bool nas_qos_dscp_color_map_delete_test(uint64_t map_id) {


    printf("starting nas_qos_dscp_color_map_delete_test\n");

    cps_api_transaction_params_t tran;
    if (cps_api_transaction_init(&tran) != cps_api_ret_code_OK)
        return false;

    cps_api_object_t obj = cps_api_object_create();
    if (obj == NULL)
        return false;

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_QOS_DSCP_TO_COLOR_MAP_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ID,
            cps_api_object_ATTR_T_U64,
            &map_id, sizeof(uint64_t));

    if (cps_api_delete(&tran, obj) != cps_api_ret_code_OK) {
        cout << "CPS API DELETE FAILED" <<endl;
        return false;
    }

    if (cps_api_commit(&tran) != cps_api_ret_code_OK) {
        cout << "CPS API commit failed" <<endl;
        return false;
    }

    if (cps_api_transaction_close(&tran) != cps_api_ret_code_OK) {
        cout << "CPS API transaction closed" << endl;
        return false;
    }

    return true;
}

bool nas_qos_dscp_color_map_entry_create_test(uint64_t map_id, uint8_t dscp,
        uint16_t color) {

    printf("starting nas_qos_dscp_color_map_entry_create_test\n");

    cps_api_transaction_params_t tran;
    if (cps_api_transaction_init(&tran) != cps_api_ret_code_OK)
        return false;

    cps_api_object_t obj = cps_api_object_create();
    if (obj == NULL)
        return false;

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj), BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ID,
            cps_api_object_ATTR_T_U64,
            &map_id, sizeof(uint64_t));

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY_DSCP,
            cps_api_object_ATTR_T_BIN,
            &dscp, sizeof(uint8_t));

    cps_api_object_attr_add_u32(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY_COLOR, color);


    if (cps_api_create(&tran, obj) != cps_api_ret_code_OK) {
        cout << "CPS API CREATE FAILED" <<endl;
        return false;
    }

    if (cps_api_commit(&tran) != cps_api_ret_code_OK) {
        cout << "CPS API commit failed" <<endl;
        return false;
    }

    //Upon success, store the returned dscp_color_map id for future retrieval
    printf ("creating dscp_color_map entry: dscp %u\n", dscp );

    if (cps_api_transaction_close(&tran) != cps_api_ret_code_OK) {
        cout << "CPS API transaction closed" << endl;
        return false;
    }

    return true;
}


bool nas_qos_dscp_color_map_set_test(uint64_t map_id, uint8_t dscp,
        uint16_t color) {
    cps_api_transaction_params_t trans;

    printf("starting nas_qos_dscp_color_map_set_test\n");

    if (cps_api_transaction_init(&trans)!=cps_api_ret_code_OK) return false;

    cps_api_object_t obj = cps_api_object_create();
    if (obj == NULL)
        return false;

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj), BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ID,
            cps_api_object_ATTR_T_U64,
            &map_id, sizeof(uint64_t));
    // Set is operable only on dscp-tc mapping entries
    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY_DSCP,
            cps_api_object_ATTR_T_BIN,
            &dscp, sizeof(uint8_t));

    cps_api_object_attr_add_u32(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY_COLOR, color);

    if (cps_api_set(&trans,obj)!=cps_api_ret_code_OK) return false;

    if (cps_api_commit(&trans)!=cps_api_ret_code_OK) return false;
    cps_api_transaction_close(&trans);

    return true;
}

bool nas_qos_dscp_color_map_entry_delete_test(uint64_t map_id, uint8_t dscp) {

    printf("starting nas_qos_dscp_color_map_entry_delete_test\n");

    cps_api_transaction_params_t tran;
    if (cps_api_transaction_init(&tran) != cps_api_ret_code_OK)
        return false;

    cps_api_object_t obj = cps_api_object_create();
    if (obj == NULL)
        return false;

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj), BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ID,
            cps_api_object_ATTR_T_U64,
            &map_id, sizeof(uint64_t));

    cps_api_set_key_data(obj, BASE_QOS_DSCP_TO_COLOR_MAP_ENTRY_DSCP,
            cps_api_object_ATTR_T_BIN,
            &dscp, sizeof(uint8_t));


    if (cps_api_delete(&tran, obj) != cps_api_ret_code_OK) {
        cout << "CPS API CREATE FAILED" <<endl;
        return false;
    }

    if (cps_api_commit(&tran) != cps_api_ret_code_OK) {
        cout << "CPS API commit failed" <<endl;
        return false;
    }

    //Upon success, store the returned dscp_color_map id for future retrieval
    printf ("deleting dscp_color_map entry: dscp %u\n", dscp );

    if (cps_api_transaction_close(&tran) != cps_api_ret_code_OK) {
        cout << "CPS API transaction closed" << endl;
        return false;
    }

    return true;
}



TEST(cps_api_events, init) {
    // basic test for map
    ASSERT_TRUE(nas_qos_dscp_color_map_create_test());
    ASSERT_TRUE(nas_qos_dscp_color_map_get_test(test_map_id));
    ASSERT_TRUE(nas_qos_dscp_color_map_delete_test(test_map_id));

    // test for map entries create/modify
    ASSERT_TRUE(nas_qos_dscp_color_map_create_test());
    ASSERT_TRUE(nas_qos_dscp_color_map_entry_create_test(test_map_id, 0, 0));
    ASSERT_TRUE(nas_qos_dscp_color_map_entry_get_test(test_map_id, 0));
    ASSERT_TRUE(nas_qos_dscp_color_map_entry_create_test(test_map_id, 4, 0));
    ASSERT_TRUE(nas_qos_dscp_color_map_entry_get_test(test_map_id, 4));
    // set
    ASSERT_TRUE(nas_qos_dscp_color_map_set_test(test_map_id, 4, 1));
    ASSERT_TRUE(nas_qos_dscp_color_map_entry_get_test(test_map_id, 4));

    // delete entries
    ASSERT_TRUE(nas_qos_dscp_color_map_entry_delete_test(test_map_id, 4));
    ASSERT_TRUE(nas_qos_dscp_color_map_entry_delete_test(test_map_id, 0));
    ASSERT_TRUE(nas_qos_dscp_color_map_delete_test(test_map_id));

}


/****** README
 *
 * This test expects a proper switch config at: /etc/opx/switch.xml
 * at the switch boot up time so that NAS-QOS can load the switch
 * configuration before running this set of unit test cases.
 *
 * (currently DN_SWITCH_CFG is set to /etc/opx/switch.xml)
 *
 *  * All test cases require a proper switch ID exists in the config.
 *
 ********** Start of a sample /etc/opx/switch.xml *****

<?xml version="1.0" encoding="UTF-8"?>
<switch_config>
<switch_topology switch_ids="0,1" />
<switch id="0" npus="0,1,2,3,4" />
<switch id="1" npus="5,6" />
</switch_config>

***************** End of the file ********************/


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
