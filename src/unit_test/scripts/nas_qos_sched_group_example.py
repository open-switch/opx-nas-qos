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

### This unit test case is strictly used for unit testing purpose.
### Once it is run, the queue and scheduler group on the port may be altered.
### To get back to the default HQoS setting, remember to reboot the box.

import cps_utils
import cps
import sys
import nas_qos
import nas_os_if_utils
import ifindex_utils


def get_port_queue_id_list(port_id, queue_type):
    cps_data_list = []
    ret_data_list = []

    if queue_type == 'ALL':
        queue_type = None

    attr_list = {
        'type': queue_type,
        'queue-number': None,
        'port-id': port_id,
    }
    queue_obj = nas_qos.QueueCPSObj(map_of_attr=attr_list)
    ret = cps.get([queue_obj.data()], cps_data_list)

    if ret == False:
        print 'Failed to get queue list'
        return None

    print '#### Queue list Show ####'
    print '-' * 36
    print '%-16s %-10s %s' % ('id', 'type', 'number')
    print '-' * 36
    for cps_data in cps_data_list:
        m = nas_qos.QueueCPSObj(cps_data=cps_data)
        queue_id = m.extract_id()
        type_val = m.extract_attr('type')
        local_num = m.extract_attr('queue-number')
        if queue_type is None or queue_type == type_val:
            print '%-16x %-10s %s' % (queue_id, type_val, local_num)
            ret_data_list.append(queue_id)

    return ret_data_list


def scheduler_group_create_example(attr_list):
    sg_obj = nas_qos.SchedGroupCPSObj(map_of_attr=attr_list)
    upd = ('create', sg_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print 'Scheduler Group creation failed'
        return None

    sg_obj = nas_qos.SchedGroupCPSObj(cps_data=ret_cps_data[0])
    sg_id = sg_obj.extract_id()
    print 'Successfully installed Scheduler Group id = ', sg_id

    return sg_id


def scheduler_group_get_example(port_id, sg_id=None, level=None):
    return_data_list = []
    attr_list = {
        'port-id': port_id,
        'level': level,
        'id': sg_id,
    }
    sg_id_list = []
    sg_obj = nas_qos.SchedGroupCPSObj(map_of_attr=attr_list)
    ret = cps.get([sg_obj.data()], return_data_list)
    if ret:
        print '#### Scheduler Group Show ####'
        for cps_ret_data in return_data_list:
            m = nas_qos.SchedGroupCPSObj(cps_data=cps_ret_data)
            m.print_obj()
            sg_id_list.append(m.extract_attr('id'))
    else:
        print 'Error in get'

    print sg_id_list
    return sg_id_list


def scheduler_group_delete_by_id(sg_id):
    attr_list = {
        'id':sg_id,
    }
    sg_obj = nas_qos.SchedGroupCPSObj(map_of_attr=attr_list)
    upd = ('delete', sg_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print 'Scheduler Group delete failed'
        return None

    print 'Successfully deleted Scheduler Group id = ', sg_id
    return ret_cps_data


def scheduler_group_delete_by_level(port_id, level):
    return_data_list = []
    attr_list = {
        'port-id': port_id,
        'level': level,
    }
    sg_obj = nas_qos.SchedGroupCPSObj(map_of_attr=attr_list)
    ret = cps.get([sg_obj.data()], return_data_list)
    if ret != True:
        print 'Error in get'
        return False
    for cps_ret_data in return_data_list:
        print cps_ret_data
        m = nas_qos.SchedGroupCPSObj(cps_data=cps_ret_data)
        sg_id = m.extract_id()
        sched_id = m.extract_attr('scheduler-profile-id')
        if level != 0 and sched_id is not None and sched_id != 0:
            sched_id = 0
        else:
            sched_id = None
        ret_sg_id = scheduler_group_modify_example(sg_id, sched_id)
        if ret_sg_id is None:
            print 'Falied to modify scheduler-group %d' % sg_id
            return False

        ret = scheduler_group_delete_by_id(sg_id)
        if ret is None:
            print 'Failed to delete the scheduler-group %d' % sg_id
            return False

    print 'Successfully delete scheduler-groups of port %s level %d' % (
        port_name, level)
    return True


def queue_create_example(port_id, queue_type, queue_number, parent_sg):
    attr_list = {
        'port-id': port_id,
        'type': queue_type,
        'queue-number': queue_number,
        'parent': parent_sg,
    }
    print port_id, queue_type, queue_number, parent_sg

    q_obj = nas_qos.QueueCPSObj(map_of_attr = attr_list)
    upd = ('create', q_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print "Queue creation failed"
        return None

    print 'Return = ', ret_cps_data
    q_obj = nas_qos.QueueCPSObj(cps_data=ret_cps_data[0])
    q_id = q_obj.extract_id()
    print 'Successfully created queue id %-16x' % q_id

    return q_id

def queue_delete_example(port_id,  queue_type=None, queue_number=None):
    return_data_list = []

    attr_list = {
        'port-id': port_id,
        'type': queue_type,
        'queue-number': queue_number,
    }
    queue_obj = nas_qos.QueueCPSObj(map_of_attr=attr_list)
    ret = cps.get([queue_obj.data()], return_data_list)

    if ret:
        for cps_ret_data in return_data_list:
            m = nas_qos.QueueCPSObj(cps_data=cps_ret_data)
            q_id = m.extract_attr('id')
            upd = ('delete', m.data())
            ret_cps_data = cps_utils.CPSTransaction([upd]).commit()
            if ret_cps_data == False:
                print "Queue delete failed"
                return False
            else:
                print "Queue %-16x is deleted" % q_id

    return True

def scheduler_group_delete_all_example(port_id):

    if scheduler_group_delete_by_level(port_id, 2) == False:
        return False
    if scheduler_group_delete_by_level(port_id, 1) == False:
        return False
    if scheduler_group_delete_by_level(port_id, 0) == False:
        return False
    print 'Successfully deleted all scheduler-groups of port %d' % port_id
    return True


def scheduler_group_modify_example(sg_id, sched_id=None):
    attr_list = {
        'id': sg_id,
        'scheduler-profile-id': sched_id,
    }
    sg_obj = nas_qos.SchedGroupCPSObj(map_of_attr = attr_list)
    upd = ('set', sg_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()
    if ret_cps_data == False:
        print 'Scheduler Group modification failed'
        return None
    m = nas_qos.SchedGroupCPSObj(cps_data=ret_cps_data[0])
    sg_id = m.extract_id()
    print 'Successfully modified Scheduler Group id = ', sg_id
    return sg_id


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

    sched_obj = nas_qos.SchedulerCPSObj(cps_data=ret_cps_data[0])
    sched_id = sched_obj.extract_id()
    print 'Successfully installed Scheduler profile id = ', sched_id

    return sched_id


def scheduler_profile_get_example(port_name, level=None):
    return_data_list = []

    sched_obj = nas_qos.SchedulerCPSObj(port_name=port_name, level=level)
    ret = cps.get([sched_obj.data()], return_data_list)

    if ret:
        print '#### Scheduler Profile Show ####'
        for cps_ret_data in return_data_list:
            m = nas_qos.SchedulerCPSObj(cps_data=cps_ret_data)
            m.print_obj()
    else:
        print 'Error in get'


def scheduler_profile_delete_example(sched_id):
    sched_obj = nas_qos.SchedulerCPSObj(sched_id=sched_id)
    upd = ('delete', sched_obj.data())
    ret_cps_data = cps_utils.CPSTransaction([upd]).commit()

    if ret_cps_data == False:
        print 'Scheduler profile delete failed'
        return None

    print 'Successfully deleted Scheduler id = ', sched_id

    return ret_cps_data


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

if __name__ == '__main__':
    if len(sys.argv) >= 2:
        port_name = sys.argv[1]
    else:
        port_name = get_first_phy_port()
    if port_name is None:
        print 'Could not find front port'
        exit()
    print 'Using port %s' % port_name

    port_id = ifindex_utils.if_nametoindex(port_name)

    # Delete all queues
    if queue_delete_example(port_id) == False:
        exit()
    print '####After deleting all queues on port %s' % port_name
    get_port_queue_id_list(port_id, 'ALL')

    # Creating all queues and attach to L2 SG
    l2_sg_id_list = scheduler_group_get_example(port_id, None, 2)

    queue_create_example(port_id, 'UCAST', 1, l2_sg_id_list[0])
    print '####After creating UCAST queue on port %s' % port_name
    get_port_queue_id_list(port_id, 'ALL')

    queue_create_example(port_id, 'MULTICAST', 1, l2_sg_id_list[0])
    print '####After creating MCAST queue on port %s' % port_name
    get_port_queue_id_list(port_id, 'ALL')

    queue_delete_example(port_id, 'UCAST', 1)
    print '####After deleting UCAST queue on port %s' % port_name
    get_port_queue_id_list(port_id, 'ALL')

    queue_delete_example(port_id, 'MULTICAST', 1)
    print '####After deleting MCAST queue on port %s' % port_name
    get_port_queue_id_list(port_id, 'ALL')

    print 'DONE queue testing'

    # Scheduler-group testing
    l0_sg_id_list = scheduler_group_get_example(port_id, None, 0)


    # Delete all existing scheduler-groups
    ret = scheduler_group_delete_all_example(port_id)
    if ret == False:
        exit()


    # Create scheduler profile
    sched_id_l0 = scheduler_profile_create_example(
        'WRR', 50, 100, 100, 500, 100)
    if sched_id_l0 is None:
        exit()
    sched_id_l1 = scheduler_profile_create_example(
        'WRR', 30, 50, 100, 200, 100)
    if sched_id_l1 is None:
        exit()
    sched_id_l2 = scheduler_profile_create_example(
        'WDRR', 30, 50, 100, 200, 100)
    if sched_id_l2 is None:
        exit()

    # Create scheduler-group
    # L0
    attr_list = {
        # All mandatory attributes
        'port-id': port_id,
        'level': 0,
        'scheduler-profile-id': sched_id_l0,
        'max-child': 5,
        'parent': 0,
    }
    sg_id_root = scheduler_group_create_example(attr_list)
    if sg_id_root is None:
        exit()

    # L1
    attr_list = {
        # All mandatory attributes
        'port-id': port_id,
        'level': 1,
        'scheduler-profile-id': sched_id_l1,
        'max-child': 2,
        'parent': sg_id_root,
    }
    sg_id_l1 = scheduler_group_create_example(attr_list)
    if sg_id_l1 is None:
        exit()

    # L2
    attr_list = {
        # All mandatory attributes
        'port-id': port_id,
        'level': 2,
        'scheduler-profile-id': sched_id_l2,
        'max-child': 2,
        'parent': sg_id_l1,
    }
    sg_id_l2 = scheduler_group_create_example(attr_list)
    if sg_id_l2 is None:
        exit()

    # read the tree out
    scheduler_group_get_example(port_id=port_id)
