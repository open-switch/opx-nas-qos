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
 * \file   nas_qos_switch_list.cpp
 * \brief  Managing a cache within QoS module for the list of switches in the system
 * \date   02-2015
 * \author
 */

#include <unordered_map>
#include "event_log.h"
#include "nas_qos_common.h"
#include "nas_qos_switch_list.h"
#include "nas_switch.h"
#include "nas_ndi_switch.h"

typedef std::unordered_map<uint_t, nas_qos_switch *>  switch_list_t;
typedef switch_list_t::iterator switch_iter_t;

static auto & nas_qos_switch_list_cache = *new switch_list_t;


/**
 *  This function gets a switch instance from the cached switch list in QoS
 *  @Param switch_id
 *  @return QoS Switch Object pointer
 */
nas_qos_switch * nas_qos_get_switch(uint_t switch_id)
{
    try {
        /* get from our cache first */
        return nas_qos_switch_list_cache.at(switch_id);

    } catch (std::out_of_range&) {

        EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                     "Creating switch id %d in nas-qos switch_list from inventory", switch_id);

        nas_qos_switch * p_switch = new nas_qos_switch(switch_id);

        // Search the switch information from the config file
        const nas_switch_detail_t* sw =  nas_switch (switch_id);
        if (sw == NULL) {
            // No such switch id in our inventory
            EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Switch id %d does not exist", switch_id);
            delete p_switch;
            return NULL;
        }

        for (size_t count = 0; count < sw->number_of_npus; count++)
            p_switch->add_npu (sw->npus[count]);

        /* init switch-wide queue info using one of the npu ids */
        if (sw->number_of_npus > 0) {
            if (ndi_switch_get_queue_numbers(sw->npus[0],
                    &(p_switch->ucast_queues_per_port), &(p_switch->mcast_queues_per_port),
                    &(p_switch->total_queues_per_port), &(p_switch->cpu_queues))
                    != STD_ERR_OK) {
                EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Failed to get global queue info for switch id %d",
                        switch_id);
                delete p_switch;
                return NULL;
            }
        }

        /* Cache switch-wise HQoS tree level */
        if (ndi_switch_get_max_number_of_scheduler_group_level(sw->npus[0],
                &(p_switch->max_sched_group_level)) != STD_ERR_OK) {
            EV_LOGGING(QOS, DEBUG, "NAS-QOS", "Failed to get MAX number of SG Levels");
            delete p_switch;
            return NULL;
        }
        EV_LOGGING(QOS, DEBUG, "NAS-QOS", "MAX_SG _LEVEL %d ", p_switch->max_sched_group_level);

        if (nas_qos_add_switch(switch_id, p_switch) != STD_ERR_OK) {
            delete p_switch;
            return NULL;
        }
        else {
            EV_LOGGING(QOS, NOTICE, "NAS-QOS",
                         "Successfully created Switch id %d in nas-qos switch_list from inventory", switch_id);
            return nas_qos_switch_list_cache.at(switch_id);
        }
    }

    return NULL;
}

/**
 * This function add a switch instance to QoS switch list
 * @Param switch_id
 * @Param QoS switch instance
 * @return standard error code
 */
t_std_error nas_qos_add_switch (uint_t switch_id, nas_qos_switch* s)
{
    /* Do NOT allow overwrite of existing entry */
    switch_iter_t si = nas_qos_switch_list_cache.find(switch_id);
    nas_qos_switch *p = ((si != nas_qos_switch_list_cache.end())? (si->second): NULL);

    if (p) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Switch id %d exists already in nas-qos switch_list.", switch_id);
        return NAS_BASE_E_DUPLICATE;
    }

    try {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Inserting Switch id %d to nas-qos switch_list", switch_id);
        nas_qos_switch_list_cache.insert(std::make_pair(switch_id, s));
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Successfully inserted Switch id %d to nas-qos switch_list", switch_id);
    }
    catch (...) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Failed to insert a new switch id %u to nas-qos switch_list", switch_id);
        return NAS_BASE_E_FAIL;
    }

    return STD_ERR_OK;
}

/**
 * This function removes a switch instance from QoS switch list
 * @Param switch_id
 * @Return standard error code
 */
t_std_error nas_qos_remove_switch (uint32_t switch_id)
{
    try {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Deleting switch id %u from nas-qos switch_list", switch_id);
        nas_qos_switch_list_cache.erase(switch_id);
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Successfully deleted switch id %u from nas-qos switch_list", switch_id);
    }
    catch (...) {
        EV_LOGGING(QOS, NOTICE, "NAS-QOS", "Failed to delete switch id %u from nas-qos switch_list", switch_id);
        return NAS_BASE_E_FAIL;
    }

    return STD_ERR_OK;
}

/**
 *  This function returns the switch pointer that contains the specified npu_id
 *  @Param npu id
 *  @Return Switch instance pointer if found
 */
nas_qos_switch * nas_qos_get_switch_by_npu(npu_id_t npu_id)
{
    // find the switch instance to which the NPU_id belongs
    nas_switch_id_t switch_id;
    if (nas_find_switch_id_by_npu(npu_id, &switch_id) != true) {
        EV_LOGGING(QOS, INFO, "QOS",
                     "npu_id %u is not in any switch",
                     npu_id);
        return NULL;
    }

    return nas_qos_get_switch(switch_id);
}

/*
 * This function fills in the interface_ctrl_t structure given an ifindex
 * @Param ifindex
 * @Return True if the interface structure is properly filled; False otherwise
 */
bool nas_qos_get_port_intf(uint_t ifindex, interface_ctrl_t *intf_ctrl)
{
    /* get the npu id of the port */
    intf_ctrl->q_type = HAL_INTF_INFO_FROM_IF;
    intf_ctrl->if_index = ifindex;
    intf_ctrl->vrf_id = 0; //default vrf
    if (dn_hal_get_interface_info(intf_ctrl) != STD_ERR_OK) {
        EV_LOGGING(QOS, INFO, "QOS",
                     "Cannot find NPU id for ifIndex: %d",
                        ifindex);
        return false;
    }

    return true;
}

/*
 *  This function cleans up interface-related data structure
 *  within NAS-QoS.
 *  Note: This clean up work does not go further down to SAI level.
 *  SAI automatically cleans up its own QoS-related data structure
 *  upon receiving "interface-deletion" message from NAS-Interface.
 *
 *  @Param ifindex
 *  @return
 *
 */
void nas_qos_if_delete_notify(uint_t ifindex)
{

    EV_LOGGING(QOS, NOTICE, "QOS-if-delete", "ifindex: %d is being deleted", ifindex);

    nas_qos_switch * p_switch = nas_qos_get_switch(0);

    if (p_switch == NULL)
        return;

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    // delete Queues
    p_switch->delete_queue_by_ifindex(ifindex);

    // delete SGs
    p_switch->delete_sg_by_ifindex(ifindex);

    // delete Port-Ingress node
    p_switch->remove_port_ingress(ifindex);

    // delete Port-Egress node
    p_switch->remove_port_egress(ifindex);

    // delete PG
    p_switch->delete_pg_by_ifindex(ifindex);

    // clear init-port-list
    p_switch->del_initialized_port(ifindex);

    return;
}

/*
 *  This function initializes interface-related structure upon interface creation event
 *  within NAS-QoS.
 *
 *  @Param ifindex
 *  @return
 *
 */
void nas_qos_if_create_notify(uint_t ifindex)
{

    EV_LOGGING(QOS, NOTICE, "QOS-if-create",
            "Receiving ifindex: %d creation event or on-demand request", ifindex);

    interface_ctrl_t intf_ctrl;

    if (nas_qos_get_port_intf(ifindex, &intf_ctrl) == false) {
        EV_LOGGING(QOS, NOTICE, "QOS-if-create",
                     "Cannot find ifindex %u in nas-intf. Possibly recently deleted.",
                     ifindex);
        return ;
    }

    ndi_port_t ndi_port_id;
    ndi_port_id.npu_id = intf_ctrl.npu_id;
    ndi_port_id.npu_port = intf_ctrl.port_id;

    nas_qos_switch *p_switch = nas_qos_get_switch_by_npu(ndi_port_id.npu_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS-if-create",
                     "switch_id of npu_id: %u cannot be found/created",
                     ndi_port_id.npu_id);
        return ;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    // initialization order is important!
    // create Queues & SGs
    if (!(p_switch->port_queue_is_initialized(ifindex) &&
          p_switch->port_sg_is_initialized(ifindex)))
        nas_qos_port_hqos_init(ifindex, ndi_port_id);

    // create PG
    if (!(p_switch->port_priority_group_is_initialized(ifindex)))
        nas_qos_port_priority_group_init(ifindex, ndi_port_id);

    // create port-ingress
    if (!(p_switch->port_ing_is_initialized(ifindex)))
        nas_qos_port_ingress_init(ifindex, ndi_port_id);

    // create port-egress
    if (!(p_switch->port_egr_is_initialized(ifindex)))
        nas_qos_port_egress_init(ifindex, ndi_port_id);

    // set init-port-list
    p_switch->add_initialized_port(ifindex);

    return;
}

/*
 *  This function handles interface SET notification
 *  within NAS-QoS.
 *
 *  @Param ifindex
 *  @Param ndi_port
 *  @Param isAdd: True: ifindex is associated to a new ndi_port
 *                False: ifindex is disassociated from the ndi_port
 *  @return
 *
 */
void nas_qos_if_set_notify(uint_t ifindex, ndi_port_t ndi_port_id, bool isAdd)
{

    EV_LOGGING(QOS, NOTICE, "QOS-if-set", "Receiving ifindex: %d %s npu %d, port %d event",
                ifindex, (isAdd? "association to": "disassociation from"),
                ndi_port_id.npu_id, ndi_port_id.npu_port);

    nas_qos_switch *p_switch = nas_qos_get_switch(0);
    if (p_switch == NULL) {
        return ;
    }

    std::lock_guard<std::recursive_mutex> switch_lg(p_switch->mtx);

    // The order is important here!
    if (isAdd) {
        // Port Ingress DB
        nas_qos_port_ingress_association(ifindex, ndi_port_id, isAdd);

        // Port Egress DB
        nas_qos_port_egress_association(ifindex, ndi_port_id, isAdd);

        // Queue DB
        nas_qos_port_queue_association(ifindex, ndi_port_id, isAdd);

        // Scheduler Group DB
        nas_qos_port_scheduler_group_association(ifindex, ndi_port_id, isAdd);

        // Port Priority Group DB
        nas_qos_port_priority_group_association(ifindex, ndi_port_id, isAdd);
    }
    else {
        // Port Priority Group DB
        nas_qos_port_priority_group_association(ifindex, ndi_port_id, isAdd);

        // Queue DB
        nas_qos_port_queue_association(ifindex, ndi_port_id, isAdd);

        // Scheduler Group DB
        nas_qos_port_scheduler_group_association(ifindex, ndi_port_id, isAdd);

        // Port Ingress DB
        nas_qos_port_ingress_association(ifindex, ndi_port_id, isAdd);

        // Port Egress DB
        nas_qos_port_egress_association(ifindex, ndi_port_id, isAdd);
    }

    // Port Pool
    nas_qos_port_pool_association(ifindex, ndi_port_id, isAdd);

    return;
}

/*
 * This function checks whether a port is initiailzed in NAS-QoS module
 * @Param   switch_id
 * @Param   port_id : i.e. ifindex
 * @Return  true if it is initialized; false otherwise
 */
bool nas_qos_port_is_initialized(uint32_t switch_id, hal_ifindex_t port_id)
{
    nas_qos_switch *p_switch = nas_qos_get_switch(switch_id);
    if (p_switch == NULL) {
        EV_LOGGING(QOS, NOTICE, "QOS",
                     "switch_id %u cannot be found/created",
                     switch_id);
        return false;
    }

    return p_switch->port_is_initialized(port_id);
}
