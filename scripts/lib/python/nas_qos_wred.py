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
import nas_qos_utils as utl
import bytearray_utils as ba_utils


class WredCPSObj:

    yang_name = 'wred-profile'

    type_map = {
        'switch-id': ('leaf', 'uint32_t'),
        'id': ('leaf', 'uint64_t'),
        'green-enable': ('leaf', 'uint32_t'),
        'green-min-threshold': ('leaf', 'uint32_t'),
        'green-max-threshold': ('leaf', 'uint32_t'),
        'green-drop-probability': ('leaf', 'uint32_t'),
        'yellow-enable': ('leaf', 'uint32_t'),
        'yellow-min-threshold': ('leaf', 'uint32_t'),
        'yellow-max-threshold': ('leaf', 'uint32_t'),
        'yellow-drop-probability': ('leaf', 'uint32_t'),
        'red-enable': ('leaf', 'uint32_t'),
        'red-min-threshold': ('leaf', 'uint32_t'),
        'red-max-threshold': ('leaf', 'uint32_t'),
        'red-drop-probability': ('leaf', 'uint32_t'),
        'weight': ('leaf', 'uint32_t'),
        'ecn-enable': ('leaf', 'uint32_t'),
        'ecn-mark': ('leaf', 'enum', 'base-qos:ecn-mark-mode:'),
        'npu-id-list': ('leaf', 'uint32_t'),
        'ecn-green/min-threshold': ('leaf', 'uint32_t'),
        'ecn-green/max-threshold': ('leaf', 'uint32_t'),
        'ecn-green/probability': ('leaf', 'uint32_t'),
        'ecn-yellow/min-threshold': ('leaf', 'uint32_t'),
        'ecn-yellow/max-threshold': ('leaf', 'uint32_t'),
        'ecn-yellow/probability': ('leaf', 'uint32_t'),
        'ecn-red/min-threshold': ('leaf', 'uint32_t'),
        'ecn-red/max-threshold': ('leaf', 'uint32_t'),
        'ecn-red/probability': ('leaf', 'uint32_t'),
        'ecn-color-unaware/min-threshold': ('leaf', 'uint32_t'),
        'ecn-color-unaware/max-threshold': ('leaf', 'uint32_t'),
        'ecn-color-unaware/probability': ('leaf', 'uint32_t'),

    }

    @classmethod
    def get_type_map(cls):
        return cls.type_map

    def __init__(self, switch_id=0, wred_id=None, cps_data=None,
                 map_of_attr={}):
        if cps_data is not None:
            self.cps_data = cps_data
            return

        self.cps_obj_wr = utl.CPSObjWrp(self.yang_name, self.get_type_map())
        self.cps_data = self.cps_obj_wr.get()
        self.set_attr('switch-id', switch_id)
        if wred_id is not None:
            self.set_attr('id', wred_id)
        for key in map_of_attr:
            if key == 'npu-id-list':
                for npu_id in map_of_attr[key]:
                    self.add_npu_to_list(npu_id)
            else:
                self.set_attr(key, map_of_attr[key])

    def attrs(self):
        return self.cps_obj_wr.get_attrs()

    def set_attr(self, attr_name, attr_val):
        self.cps_obj_wr.add_leaf_attr(attr_name, attr_val)

    def data(self):
        return self.cps_data

    def get_npu_list(self):
        cps_obj = utl.extract_cps_obj(self.data())
        path = utl.name_to_path(self.yang_name, 'npu-id-list')
        if path not in cps_obj:
            return None
        cps_id_list = cps_obj[path]
        npu_id_list = []
        for npu_id in cps_id_list:
            val = ba_utils.from_ba(npu_id, 'uint32_t')
            npu_id_list.append(val)
        return npu_id_list

    def add_npu_to_list(self, npu_id):
        cps_obj = utl.extract_cps_obj(self.data())
        ba = ba_utils.to_ba(npu_id, 'uint32_t')
        if ba is None:
            print 'Failed to convert npu_id to bytearray'
            return False
        path = utl.name_to_path(self.yang_name, 'npu-id-list')
        if path not in cps_obj:
            cps_obj[path] = [ba]
        else:
            cps_obj[path].append(ba)
        return True

    def print_obj(self):
        """
        Print the contents of the CPS Object in a user friendly format
        """
        print self.data()
        for attr_name in self.get_type_map():
            if attr_name == 'npu-id-list':
                val = self.get_npu_list()
            else:
                val = utl.extract_cps_attr(self, self.data(), attr_name)
            if val is not None:
                print attr_name, " " * (30 - len(attr_name)), ": ", str(val)

    def extract_attr(self, attr_name):
        """
        Get value for any attribute from the CPS data returned by Get
        """
        return utl.extract_cps_attr(self, self.cps_data, attr_name)
