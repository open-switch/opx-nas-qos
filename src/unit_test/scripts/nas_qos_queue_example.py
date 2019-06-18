#!/usr/bin/python
# Copyright (c) 2019 Dell Inc.
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
import cps_utils
import cps
import nas_qos
import nas_qos_buffer_pool_example
import nas_qos_buffer_profile_example


import sys
import nas_os_if_utils
import ifindex_utils

def get_first_phy_port():
    ret_data_list = nas_os_if_utils.nas_os_if_list()
    if not ret_data_list:
        return None
    name_list = []
    for ret_data in ret_data_list:
        cps_obj = cps_utils.CPSObject(obj=ret_data)
        port_name = cps_obj.get_attr_data('if/interfaces/interface/name')
        name_list.append(port_name)
    name_list.sort()
    return name_list[0]


def queue_get_example(port_id, queue_type, queue_number):
    return_data_list = []

    attr_list = {
        'type': queue_type,
        'queue-number': queue_number,
        'port-id': port_id,
    }
    queue_obj = nas_qos.QueueCPSObj(map_of_attr=attr_list)
    ret = cps.get([queue_obj.data()], return_data_list)

    if ret:
        print '#### Queue Show ####'
        for cps_ret_data in return_data_list:
            m = nas_qos.QueueCPSObj(cps_data=cps_ret_data)
            m.print_obj()
    else:
        print "Error in Get"


def queue_modify_wred_example (port_id, queue_type, queue_number, wred_id):
    attr_list = {
        'type': queue_type,
        'queue-number': queue_number,
        'port-id': port_id,
        'wred-id': wred_id,
    }
    m = nas_qos.QueueCPSObj (map_of_attr=attr_list)

    upd = ('set', m.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print "Queue modify Failed"
        return None

    print 'Return = ', ret_cps_data
    m = nas_qos.QueueCPSObj(cps_data=ret_cps_data[0])
    port_id = m.extract_attr('port-id')
    queue_number = m.extract_attr('queue-number')
    print "Successfully modified Queue of Port %d Number %d" % (port_id, queue_number)

    return ret_cps_data


def wred_profile_create_example():
    attr_list = {
        'green-enable': 1,
        'green-min-threshold': 20,
        'green-max-threshold': 80,
        'green-drop-probability': 10,
        'yellow-enable': 1,
        'yellow-min-threshold': 40,
        'yellow-max-threshold': 60,
        'yellow-drop-probability': 30,
        'weight': 8,
        'ecn-mark': 'ALL',
        'npu-id-list': [0],
    }

    wred_obj = nas_qos.WredCPSObj(map_of_attr=attr_list)
    upd = ('create', wred_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print "WRED profile creation failed"
        return None

    print 'Return = ', ret_cps_data
    wred_obj = nas_qos.WredCPSObj(cps_data=ret_cps_data[0])
    wred_id = wred_obj.extract_attr('id')
    print "Successfully installed WRED id = ", wred_id

    return wred_id


def wred_profile_delete_example(wred_id):
    wred_obj = nas_qos.WredCPSObj(wred_id=wred_id)
    upd = ('delete', wred_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()
    if ret_cps_data == False:
        print 'WRED profile delete failed'
        return None

    print 'Return = ', ret_cps_data
    print 'Successfully deleted WREd id = ', wred_id

    return ret_cps_data


def scheduler_profile_create_example(
        algo, weight, min_rate, min_burst, max_rate, max_burst):
    attr_list = {
        'algorithm': algo,
        'weight': weight,
        'meter-type': 'PACKET',
        'min-rate': min_rate,
        'min-burst': min_burst,
        'max-rate': min_rate,
        'max-burst': max_burst,
        'npu-id-list': [0],
    }
    sched_obj = nas_qos.SchedulerCPSObj(map_of_attr=attr_list)
    upd = ('create', sched_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()
    if ret_cps_data == False:
        print 'Scheduler profile creation failed'
        return None

    print 'Return = ', ret_cps_data
    sched_obj = nas_qos.SchedulerCPSObj(cps_data=ret_cps_data[0])
    sched_id = sched_obj.extract_id()
    print 'Successfully installed Scheduler profile id = ', sched_id

    return sched_id


def scheduler_profile_delete_example(sched_id):
    sched_obj = nas_qos.SchedulerCPSObj(sched_id=sched_id)
    upd = ('delete', sched_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print 'Scheduler profile delete failed'
        return None

    print 'Return = ', ret_cps_data
    print 'Successfully deleted Scheduler id = ', sched_id

    return ret_cps_data


def queue_modify_scheduler_example(
        port_id, queue_type, queue_number, sched_id):
    attr_list = {
        'type': queue_type,
        'queue-number': queue_number,
        'port-id': port_id,
        'scheduler-profile-id': sched_id,
    }
    queue_obj = nas_qos.QueueCPSObj(map_of_attr=attr_list)

    upd = ('set', queue_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print 'Queue scheduler profile modification failed'
        return None

    print 'Return = ', ret_cps_data
    m = nas_qos.QueueCPSObj(cps_data=ret_cps_data[0])
    port_id = m.extract_attr('port-id')
    queue_number = m.extract_attr('queue-number')
    print "Successfully modified Queue of Port %d Number %d" % (port_id, queue_number)

    return ret_cps_data

def queue_modify_buffer_profile_example(
        port_id, queue_type, queue_number, buf_prof_id):
    attr_list = {
        'type': queue_type,
        'queue-number': queue_number,
        'port-id': port_id,
        'buffer-profile-id': buf_prof_id,
    }
    queue_obj = nas_qos.QueueCPSObj(map_of_attr=attr_list)

    upd = ('set', queue_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print 'Queue buffer profile modification failed'
        return None

    print 'Return = ', ret_cps_data
    m = nas_qos.QueueCPSObj(cps_data=ret_cps_data[0])
    port_id = m.extract_attr('port-id')
    queue_number = m.extract_attr('queue-number')
    print "Successfully modified Queue of Port %d Number %d" % (port_id, queue_number)

    return ret_cps_data

if __name__ == '__main__':
    if len(sys.argv) >= 2:
        port_name = sys.argv[1]
    else:
        port_name = get_first_phy_port()

    port_id = ifindex_utils.if_nametoindex(port_name)

    print '### Show all queues of port %d ###' % port_id
    queue_get_example(port_id, None, None)

    print '### Show all unicast queues of port %d ###' % port_id
    queue_get_example(port_id, 'UCAST', None)

    print '### Show unicast queue 1 of port %d ###' % port_id
    queue_get_example(port_id, 'UCAST', 1)

    wred_id = wred_profile_create_example()
    if wred_id is None:
        print 'Failed to create WRED profile'
        exit()

    sched_id = scheduler_profile_create_example(
        'WRR', 50, 10000, 200, 50000, 200)
    if sched_id is None:
        print 'Failed to create scheduler profile'
        exit()

    buffer_pool_id = nas_qos_buffer_pool_example.buffer_pool_create_example('EGRESS')
    if buffer_pool_id is None:
        print 'Failed to create a buffer pool'
        exit()

    buffer_profile_id = nas_qos_buffer_profile_example.buffer_profile_create_example(buffer_pool_id, 'EGRESS')
    if buffer_profile_id is None:
	print 'Failed to create a buffer profile'
        exit()


    queue_id = queue_modify_wred_example(port_id, 'UCAST', 1, wred_id)
    if queue_id is None:
        exit()
    queue_id = queue_modify_scheduler_example(port_id, 'UCAST', 1, sched_id)
    if queue_id is None:
        exit()
    queue_id = queue_modify_buffer_profile_example(port_id, 'UCAST', 1, buffer_profile_id)
    if queue_id is None:
        exit()

    print '### Show unicast queue 1 after WRED and Scheduler setup ###'
    queue_get_example(port_id, 'UCAST', 1)

    print '### Clean-up WRED and Scheduler profiles from queue ###'
    queue_id = queue_modify_wred_example(port_id, 'UCAST', 1, 0)
    queue_id = queue_modify_scheduler_example(port_id, 'UCAST', 1, 0)
    queue_id = queue_modify_buffer_profile_example(port_id, 'UCAST', 1, 0)

    print '### Remove WRED and Scheduler profiles ###'
    wred_profile_delete_example(wred_id)
    scheduler_profile_delete_example(sched_id)
    nas_qos_buffer_profile_example.buffer_profile_delete_example(buffer_profile_id)
    nas_qos_buffer_pool_example.buffer_pool_delete_example(buffer_pool_id)


