/*!
 *  \file device_profile_reset.c
 *
 *  \brief
 *  Implementation of device profile reset feature.
 *
 *  \author
 *  Texas Instruments Incorporated
 *
 *  \copyright
 *  Copyright (C) 2025 Texas Instruments Incorporated
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdbool.h>
#include <stdint.h>

#if defined (SOC_AM243X) || defined (SOC_AM64X)
#include "drivers/pruicss/g_v0/pruicss.h"
#elif defined (SOC_AM263PX) || defined (SOC_AM261X)
#include "drivers/pruicss/m_v0/pruicss.h"
#else
#error "Undefined SOC"
#endif

#include "osal.h"

#include "EI_API.h"
#include "EI_API_def.h"

#include "cust_drivers.h"

#include "drivers/drivers.h"

#include "cfg_example.h"

#include "device_profiles/common/device_profile_cip_codes.h"
#include "device_profiles/common/device_profile_nvm.h"
#include "device_profiles/common/device_profile_cfg.h"
#include "device_profiles/common/device_profile_reset.h"

#include "device_profiles/device_profile_intf.h"


const DEVICE_PROFILE_CFG_AdapterData_t  DEVICE_PROFILE_RESET_adapterFactoryDefaultValues =
{
    /* Time Sync Object - Attribute 1 */
    .ptpEnable               = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TIMESYNC_PTP_ENABLE,

    /* Time Sync Object - Attribute 13 */
    .portEnable              = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TIMESYNC_PORT_ENABLE,

    /* Time Sync Object - Attribute 14 */
    .portLogAnnounceInterval = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TIMESYNC_LOG_ANNOUNCE_INTERVAL,

    /* Time Sync Object - Attribute 15 */
    .portLogSyncInterval     = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TIMESYNC_LOG_SYNC_INTERVAL,

    /* Time Sync Object - Attribute 18 */
    .domainNumber            = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TIMESYNC_DOMAIN_NUMBER,

    /* Time Sync Object - Attribute 23 */
    .aUserDescription        = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TIMESYNC_USER_DESCRIPTION,


    /* Quality of Service Object - Attribute 1 */
    .qosParameter.Q_Tag_Enable     = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_QOS_802_1_TAG_ENABLE,

    /* Quality of Service Object - Attribute 2 */
    .qosParameter.DSCP_PTP_Event   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_QOS_DSCP_PTP_EVENT,

    /* Quality of Service Object - Attribute 3 */
    .qosParameter.DSCP_PTP_General = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_QOS_DSCP_PTP_GENERAL,

    /* Quality of Service Object - Attribute 4 */
    .qosParameter.DSCP_Urgent      = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_QOS_DSCP_PTP_URGENT,

    /* Quality of Service Object - Attribute 5 */
    .qosParameter.DSCP_Scheduled   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_QOS_DSCP_PTP_SCHEDULED,

    /* Quality of Service Object - Attribute 6 */
    .qosParameter.DSCP_High        = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_QOS_DSCP_HIGH,

    /* Quality of Service Object - Attribute 7 */
    .qosParameter.DSCP_Low         = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_QOS_DSCP_LOW,

    /* Quality of Service Object - Attribute 8 */
    .qosParameter.DSCP_Explicit    = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_QOS_DSCP_EXPLICIT,


    /* TCP/IP Object - Attribute 3 */
    .configurationMethod = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_CFG_CONTROL,

    /* TCP/IP Object - Attribute 5 */
    .ipAddr      = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_IP_ADDRESS,
    .ipNwMask    = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_NW_MASK,
    .ipGateway   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_GW_ADDRESS,
    .nameServer1 = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_SERVER_NAME_1,
    .nameServer2 = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_SERVER_NAME_2,
    .aDomainName = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_DOMAIN_NAME,

    /* TCP/IP Object - Attribute 6 */
    .aHostName   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_HOST_NAME,

    /* TCP/IP Object - Attribute 8 */
    .ttlValue = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_TTL_VALUE,

    /* TCP/IP Object - Attribute 9 */
    .mcastConfig.allocControl   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_MCAST_ALLOC_CONTROL,
    .mcastConfig.reserved       = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_MCAST_RESERVED,
    .mcastConfig.numMcast       = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_MCAST_NUM_CAST,
    .mcastConfig.mcastStartAddr = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_MCAST_START_ADDRESS,

    /* TCP/IP Object - Attribute 10 */
    .acdActive = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_ACD_ENABLE,

    /* TCP/IP Object - Attribute 11 */
    .acdState = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_ACD_STATE,
    .aAcdAddr = { CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_ACD_REMOTE_MAC },
    .aAcdHdr  = { CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_ACD_ARP_PDU },

    /* TCP/IP Object - Attribute 12 */
    .quickConnectEnabled = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_QUICK_CONNECT_ENABLE,

    /* TCP/IP Object - Attribute 13 */
    .encapInactTimeout = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_TCPIP_ENCAP_TIMEOUT,

    /* Ethernet Link Object - Attribute 6 - Port 1 */
    .intfConfig[0].bit.ETHIntfAutoNeg = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_ETHLINK_CONTROL_BITS_AUTONEG,
    .intfConfig[0].bit.ETHIntfFDuplex = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_ETHLINK_CONTROL_BITS_FORCED_DUPLEX,
    .intfConfig[0].bit.ETHIntf100MB   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_ETHLINK_FORCED_SPEED,

    /* Ethernet Link Object - Attribute 6 - Port 2 */
    .intfConfig[1].bit.ETHIntfAutoNeg = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_ETHLINK_CONTROL_BITS_AUTONEG,
    .intfConfig[1].bit.ETHIntfFDuplex = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_ETHLINK_CONTROL_BITS_FORCED_DUPLEX,
    .intfConfig[1].bit.ETHIntf100MB   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_ETHLINK_FORCED_SPEED,

    /* Ethernet Link Object - Attribute 9 - Port 1 */
    .intfConfig[0].bit.ETHIntfActive  = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_ETHLINK_ADMIN_STATE_ENABLE,

    /* Ethernet Link Object - Attribute 9 - Port 2 */
    .intfConfig[1].bit.ETHIntfActive  = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_ETHLINK_ADMIN_STATE_ENABLE,

    /* LLDP Management Object - Attribute 1 */
    .lldpParameter.enableArrayLength   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_LLDP_ENABLE_ARRAY_LENGTH,
    .lldpParameter.enableArray.allBits = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_LLDP_ENABLE_ARRAY_BITS,

    /* LLDP Management Object - Attribute 2 */
    .lldpParameter.msgTxInterval       = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_LLDP_MSG_TX_INTERVAL,

    /* LLDP Management Object - Attribute 3 */
    .lldpParameter.msgTxHold           = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_LLDP_MSG_TX_HOLD,

#ifdef SDK_VARIANT_PREMIUM
    /* DLR Object - Attribute 4 */
    .superEnable      = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_DLR_RING_SUPERVISOR_ENABLE,
    .supPrecedence    = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_DLR_RING_SUPERVISOR_PRECEDENCE,
    .beaconInterval   = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_BEACON_INTERVAL,
    .beaconTimeout    = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_BEACON_TIMEOUT,
    .vLanId           = CFG_DEVICE_PROFILE_RESET_FACTORY_DEFAULT_DLR_VLAN_ID,
#endif
};

static int16_t  DEVICE_PROFILE_RESET_serviceFlag_s;

static bool     DEVICE_PROFILE_RESET_isRequired_s = false;
static uint32_t DEVICE_PROFILE_RESET_time_s       = 0;

bool DEVICE_PROFILE_RESET_init (EI_API_ADP_T *pAdapter)
{
    bool ret = true;

    return ret;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Performs a factory reset.
 *
 *  \details
 *  Performs a factory reset
 *  - With serviceFlag == 1: restore default data
 *  - With serviceFlag == 2: restore default data, except communication link attributes.
 *  - Other values:          do nothing
 *
 */
bool DEVICE_PROFILE_RESET_execute(int16_t serviceFlag)
{

    bool ret = false;

    switch (serviceFlag)
    {
        case 1:
        {
            // Restore default data.
            DEVICE_PROFILE_CFG_setDefault();
            break;
        }
        case 2:
        {
            // Restore default data except communication link attributes
            DEVICE_PROFILE_CFG_setDefaultWithoutComm();
            break;
        }
        default:
        {
            return ret;
        }
    }

    return DEVICE_PROFILE_CFG_write(true);
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Callback for reset service of class 0x01.
 *
 *  \details
 *  Callback for reset service of class 0x01. Sets the timestamp for a delayed reset.
 */
void DEVICE_PROFILE_RESET_service (EI_API_CIP_NODE_T *pCipNode, uint16_t classId, uint16_t instanceId, uint16_t attrId, EI_API_CIP_ESc_t serviceCode, int16_t serviceFlag)
{
    if (serviceFlag == 1 || serviceFlag == 2)
    {
        // Reset with parameter 1 or 2 means factory reset.
        DEVICE_PROFILE_RESET_execute(serviceFlag);
    }

    DEVICE_PROFILE_RESET_time_s        = OSAL_getMsTick();
    DEVICE_PROFILE_RESET_isRequired_s  = true;
    DEVICE_PROFILE_RESET_serviceFlag_s = serviceFlag;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Checks if a reset is required.
 *
 *  \details
 *  Checks if a reset is required and 2 seconds are expired.
 +
 */
int16_t DEVICE_PROFILE_RESET_getServiceFlag(void)
{
    uint32_t actTime = OSAL_getMsTick();
    uint32_t difTime = 0;

    if (DEVICE_PROFILE_RESET_isRequired_s)
    {
        // Wait 2 seconds for reset:
        if (actTime < DEVICE_PROFILE_RESET_time_s)
        {
            difTime = (0xFFFFFFFF - DEVICE_PROFILE_RESET_time_s) + actTime;
        }
        else
        {
            difTime = actTime - DEVICE_PROFILE_RESET_time_s;
        }

        if ( (difTime > 2000) &&
             (false  == DEVICE_PROFILE_NVM_isWritePending()) )
        {
            DEVICE_PROFILE_RESET_isRequired_s = false;
            return DEVICE_PROFILE_RESET_serviceFlag_s;
        }
    }
    return -1;
}
