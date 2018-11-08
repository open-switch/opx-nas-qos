#!/usr/bin/python
# Copyright (c) 2015 Dell Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License. You may obtain
# a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
#
# THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
# LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
# FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
#
# See the Apache Version 2.0 License for specific language governing
# permissions and limitations under the License.

# Before running this test on BASE, do "touch /etc/opx/base_qos_no_init" and reboot

import cps_utils
import cps
import sys
import nas_qos
import nas_qos_buffer_pool_example
import nas_qos_buffer_profile_example
import nas_qos_wred_example
import nas_qos_queue_example

def port_pool_create_example(port_id, pool_id):
    attr_list = {
        'port-id': port_id,
        'buffer-pool-id': pool_id,
    }

    port_pool_obj = nas_qos.PortPoolCPSObj(map_of_attr=attr_list)
    upd = ('create', port_pool_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print "port pool creation failed"
        return None

    print 'Return = ', ret_cps_data
    print "Successfully installed port pool on port: %d pool: %d " % (port_id, pool_id)
    return pool_id


def port_pool_get_example(port_id, pool_id):
    return_data_list = []

    attr_list = {
        'port-id': port_id,
        'buffer-pool-id': pool_id,
    }

    port_pool_obj = nas_qos.PortPoolCPSObj(map_of_attr=attr_list)
    ret = cps.get([port_pool_obj.data()], return_data_list)

    if ret:
        print '#### port pool Profile Show ####'
        for cps_ret_data in return_data_list:
            m = nas_qos.PortPoolCPSObj(cps_data=cps_ret_data)
            m.print_obj()
    else:
        print 'Error in get'


def port_pool_modify_attrs(port_id, pool_id, mod_attr_list):
    attr_list = {
        'port-id': port_id,
        'buffer-pool-id': pool_id,
    }

    port_pool_obj = nas_qos.PortPoolCPSObj(map_of_attr=attr_list)
    for attr in mod_attr_list:
        port_pool_obj.set_attr(attr[0], attr[1])

    upd = ('set', port_pool_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print "port pool modification failed"
    else:
        print "Successfully modified port pool "


def port_pool_modify_example(port_id, pool_id, wred_id):
    mod_attrs = [
        ('wred-profile-id', wred_id),
    ]
    return port_pool_modify_attrs(port_id, pool_id, mod_attrs)


def port_pool_delete_example(port_id, pool_id):
    attr_list = {
        'port-id': port_id,
        'buffer-pool-id': pool_id,
    }

    port_pool_obj = nas_qos.PortPoolCPSObj(map_of_attr=attr_list)
    upd = ('delete', port_pool_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print "port pool delete failed"
    else:
        print "Successfully deleted port pool "

if __name__ == '__main__':
    print "### create EGRESS buffer pool"
    buffer_pool_id = nas_qos_buffer_pool_example.buffer_pool_create_example('EGRESS')
    if buffer_pool_id is None:
        sys.exit(0)

    print "### create buffer profile to use the buffer pool"
    buffer_profile_id = nas_qos_buffer_profile_example.buffer_profile_create_example(buffer_pool_id, pool_type='EGRESS')
    if buffer_profile_id is None:
        sys.exit(0)

    print "### create WRED profile"
    wred_id = nas_qos_wred_example.wred_profile_create_example()
    if wred_id is None:
        sys.exit(0)

    port_id = 17
    pool_id = buffer_pool_id

    print "### set up a queue to use the buffer pool"
    queue_id = nas_qos_queue_example.queue_modify_buffer_profile_example(port_id, 'UCAST', 1, buffer_profile_id)
    if queue_id is None:
        sys.exit(0)

    print "### create Port Buffer Pool for management"
    port_pool_id = port_pool_create_example(port_id, pool_id)
    if port_pool_id is None:
        sys.exit(0)
    port_pool_get_example(port_id, pool_id)


    print "### modify Port Buffer Pool to use new WRED id"
    port_pool_id = port_pool_modify_example(port_id, pool_id, wred_id)
    if port_pool_id is None:
        sys.exit(0)
    port_pool_get_example(port_id, pool_id)
    port_pool_delete_example(port_id, pool_id)

    nas_qos_buffer_profile_example.buffer_profile_delete_example(buffer_profile_id)
    nas_qos_buffer_pool_example.buffer_pool_delete_example(buffer_pool_id)
    nas_qos_wred_example.wred_profile_delete_example(wred_id)

