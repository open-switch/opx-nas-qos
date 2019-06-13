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
import nas_qos_utils as utl


class PortPoolCPSObj:

    yang_name = 'port-pool'

    type_map = {
        'port-id': ('leaf', 'uint32_t'),
        'buffer-pool-id': ('leaf', 'uint64_t'),
        'wred-profile-id': ('leaf', 'uint64_t'),
    }

    @classmethod
    def get_type_map(cls):
        return cls.type_map

    def __init__(self, cps_data=None, map_of_attr={}):
        if cps_data is not None:
            self.cps_data = cps_data
            return

        self.cps_obj_wr = utl.CPSObjWrp(self.yang_name, self.get_type_map())
        self.cps_data = self.cps_obj_wr.get()
        for key in map_of_attr:
            self.set_attr(key, map_of_attr[key])

    def attrs(self):
        return self.cps_obj_wr.get_attrs()

    def set_attr(self, attr_name, attr_val):
        self.cps_obj_wr.add_leaf_attr(attr_name, attr_val)

    def data(self):
        return self.cps_data

    def print_obj(self):
        """
        Print the contents of the CPS Object in a user friendly format
        """
        print self.data()
        for attr_name in self.get_type_map():
            val = utl.extract_cps_attr(self, self.data(), attr_name)
            if val is not None:
                print attr_name, " " * (30 - len(attr_name)), ": ", str(val)

    def extract_attr(self, attr_name):
        """
        Get value for any attribute from the CPS data returned by Get
        """
        return utl.extract_cps_attr(self, self.cps_data, attr_name)
