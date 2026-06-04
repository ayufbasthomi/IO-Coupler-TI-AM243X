/*!
 *  \file generic_device.c
 *
 *  \brief
 *  Generic device profile declarations.
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

#if (!(defined FBTLPROVIDER) || (0 == FBTLPROVIDER)) && (!(defined FBTL_REMOTE) || (0 == FBTL_REMOTE))

#if defined (SOC_AM243X) || defined (SOC_AM64X)
#include "drivers/pruicss/g_v0/pruicss.h"
#elif defined (SOC_AM263PX) || defined (SOC_AM261X)
#include "drivers/pruicss/m_v0/pruicss.h"
#else
#error "Undefined SOC"
#endif

#include "osal.h"
#include "osal_error.h"

#include "EI_API.h"
#include "EI_API_def.h"

#include "cfg_example.h"

#include "cust_drivers.h"

#include "drivers/drivers.h"

#include "device_profiles/common/device_profile_nvm.h"
#include "device_profiles/common/device_profile_cfg.h"

#include "device_profiles/device_profile_intf.h"
#include "device_profiles/device_profile_intf_intern.h"
#include "device_profiles/generic_device/generic_device_cfg.h"
#include "device_profiles/generic_device/generic_device.h"

#include "IOCoupler.h"

#define OFFSET_INDEX_OUTPUT MAX_INPUT_SIZE_BYTES
extern IOCoupler_Device IOCoupler_Devices;
extern uint8_t gInputProcessImage[MAX_INPUT_SIZE_BYTES];   // T -> O (Device → PLC)
extern uint8_t gOutputProcessImage[MAX_OUTPUT_SIZE_BYTES]; // O -> T (PLC → Device)
extern void UpdateInputProcessImage();
extern void UpdateOutputModules();
extern bool isMaster_running(void);
extern uint8_t getNumIOSlave(void);

static uint16_t GENERIC_DEVICE_extendedStatus_s[255] = {0};

static bool GENERIC_DEVICE_init               (EI_API_ADP_T      *pAdapter, EI_API_CIP_NODE_T *pCipNode);
static void GENERIC_DEVICE_run                (EI_API_CIP_NODE_T *pCipNode);
static bool GENERIC_DEVICE_cipSetup           (EI_API_CIP_NODE_T *pCipNode);
static void GENERIC_DEVICE_cipGenerateContent (EI_API_CIP_NODE_T *pCipNode, uint16_t classId, uint16_t instanceId);

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Callback function for ForwardOpen, LargeForwardOpen and ForwardClose.
 *
 *  \details
 *  Callback function which is called when a request for the services
 *  ForwardOpen, LargeForwardOpen and ForwardClose was received.
 *
 *  \ingroup GENERIC_DEVICE_MAIN
 *
 */
EI_API_ADP_SEipStatus_t *GENERIC_DEVICE_cmgrCb(uint32_t serviceCode, EI_API_ADP_UCmgrInfo_u *cmgrInfo)
{
    static EI_API_ADP_SEipStatus_t ret_val= {.gen_status=0,
                                      .extended_status_size=0,
                                      .extended_status_arr=GENERIC_DEVICE_extendedStatus_s};


    switch(serviceCode)
        {
        case 0x54:
            /* For debug purpose - OSAL_printf("Forward open Connection Serial Number: 0x%04x\r\n", cmgrInfo.forwardOpenInfo.conSerialNum); */
            break;
        case 0x5b:
            /* For debug purpose - OSAL_printf("Large forward open Connection Serial Number: 0x%04x\r\n", cmgrInfo.forwardOpenInfo.conSerialNum); */
            break;
        case 0x4e:
            /* For debug purpose - OSAL_printf("Forward close Connection Serial Number: 0x%04x\r\n", cmgrInfo.forwardCloseInfo.conSerialNum); */
            break;
        default:
            OSAL_printf("unknown service code %x\r\n", serviceCode);
        }
    return &ret_val;
}

/*!
 * \brief
 * Registers generic device profile interface functions.
 *
 * \details
 * Called by #DEVICE_PROFILE_INTF_init function of device profile interface.
 * Function needs to be defined in each profile.
 *
 * \remark
 * In time of build process only one device profile can be active.
 * Rest profiles need to be excluded from the build.
 *
 * \param[in]     pIntf    Pointer to device profile interface which needs to be initialized.
 *
 * \return        value as bool.
 * \retval        true     Success.
 * \retval        false    Invalid input parameter defined as NULL.
 *
 * \par Example
 * \code{.c}
 * #include "device_profiles/generic_device/generic_device_cfg.h"
 *
 * uint32_t DEVICE_PROFILE_INTF_init (void)
 * {
 *     return DEVICE_PROFILE_register(&EI_APP_DEVICE_PROFILE_intf_s);
 * }
 * \endcode
 *
 * \see DEVICE_PROFILE_register DEVICE_PROFILE_INTF_cfgInit
 *
 * \ingroup GENERIC_DEVICE_MAIN
 *
 */
uint32_t DEVICE_PROFILE_register (DEVICE_PROFILE_INTF_Interface_t *pIntf)
{
    uint32_t error = DEVICE_PROFILE_INTF_ERR_General;

    if (NULL == pIntf)
    {
        error = DEVICE_PROFILE_INTF_ERR_PtrInvalid;
        goto laError;
    }

    pIntf->task.init = GENERIC_DEVICE_init;
    pIntf->task.run  = GENERIC_DEVICE_run;

    pIntf->cfg.init                  = GENERIC_DEVICE_CFG_init;
    pIntf->cfg.isValid               = GENERIC_DEVICE_CFG_isValid;
    pIntf->cfg.setHeader             = GENERIC_DEVICE_CFG_setHeader;
    pIntf->cfg.apply                 = GENERIC_DEVICE_CFG_apply;
    pIntf->cfg.setDefaultWithoutComm = GENERIC_DEVICE_CFG_setDefaultWithoutComm;
    pIntf->cfg.callback              = GENERIC_DEVICE_CFG_callback;
    pIntf->cfg.getRuntimeData        = GENERIC_DEVICE_CFG_getRuntimeData;
    pIntf->cfg.getFactoryResetData   = GENERIC_DEVICE_CFG_getFactoryResetData;
    pIntf->cfg.getLength             = GENERIC_DEVICE_CFG_getLength;

    error = DEVICE_PROFILE_INTF_ERR_OK;

laError:
    return error;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Basic initialization function.
 *
 *  \details
 *  Creates a new EtherNet/IP&trade; Adapter.<br>
 *  Initializes data structures from non-volatile storage.<br>
 *  Registers stack error handler.<br>
 *  Initializes the Adapter.<br>
 *  Create a CIP&trade; node.<br>
 *
 *  \ingroup GENERIC_DEVICE_MAIN
 */
bool GENERIC_DEVICE_init(EI_API_ADP_T* pAdapter, EI_API_CIP_NODE_T *pCipNode)
{
    bool result = false;

#if (defined BRIDGING_AND_ROUTING) && (BRIDGING_AND_ROUTING==1)
    /* Enable Bridging and Routing feature */
    EI_API_ADP_setBridgingAndRoutingSupported(pAdapter);
#endif

    /* Create vendor specific classes. */
    result = GENERIC_DEVICE_cipSetup(pCipNode);

    uint16_t size;
    EI_API_CIP_getAssemblySize(pCipNode, 100, &size);
    OSAL_printf(" Assembly100 size = %d\r\n", size);
    EI_API_CIP_getAssemblySize(pCipNode, 101, &size);
    OSAL_printf(" Assembly101 size = %d\r\n", size);

    return result;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Cyclically called run function.
 *
 *  \details
 *  Cyclically called run function, handles the EtherNet/IP stack and hardware
 *  specific functionality, such as reading switches, reading inputs, setting outputs
 *  and LEDs.
 *
 *  \ingroup GENERIC_DEVICE_MAIN
 */
void GENERIC_DEVICE_run(EI_API_CIP_NODE_T* pCipNode)
{
    uint32_t errCode = 0;
    
    if(isMaster_running() && getNumIOSlave() > 0){
        for(int attr = 0; attr < MAX_OUTPUT_SIZE_BYTES; attr++)
        {
            errCode = EI_API_CIP_getAttr_byte(pCipNode, 0x0070, 0x0001, attr + CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_OUTPUT_ID, &gOutputProcessImage[attr]);
        }
        /* Write outputs → physical IO */
        UpdateOutputModules();

        /* Read physical IO → process image */
        UpdateInputProcessImage();

        for(int attr = 0; attr < MAX_INPUT_SIZE_BYTES; attr++)
        {
            EI_API_CIP_setAttr_byte(pCipNode, 0x0070, 0x0001, attr + CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_INPUT_ID, gInputProcessImage[attr]);
        }
    }
}

void GENERIC_DEVICE_Create_IO_Map(EI_API_CIP_NODE_T* pCipNode, uint16_t classId, uint16_t instanceId)
{
    // EI_API_CIP_addAssemblyMember(
    //     pCipNode,
    //     CFG_PROFILE_GENERIC_DEVICE_ASSEMBLY_INSTANCE_EXCLUSIVE_OWNER_1,
    //     classId,
    //     instanceId,
    //     CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_OUTPUT_ID
    // );

    // EI_API_CIP_addAssemblyMember(
    //     pCipNode,
    //     CFG_PROFILE_GENERIC_DEVICE_ASSEMBLY_INSTANCE_EXCLUSIVE_OWNER_2,
    //     classId,
    //     instanceId,
    //     CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_INPUT_ID
    // );

    uint32_t errCode = 0;
    
    for (uint16_t i = 0; i < MAX_OUTPUT_SIZE_BYTES; i++)
    {
        uint16_t attrId = CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_OUTPUT_ID + i;

        errCode = EI_API_CIP_addAssemblyMember(
            pCipNode,
            CFG_PROFILE_GENERIC_DEVICE_ASSEMBLY_INSTANCE_EXCLUSIVE_OWNER_1, // 100
            classId,
            instanceId,
            attrId
        );

        if (errCode != 0)
        {
            OSAL_printf("Add OUTPUT member %d failed: 0x%08X\n", i, errCode);
        }
    }

    /* -------- INPUT (T -> O) Assembly 101 -------- */
    for (uint16_t i = 0; i < MAX_INPUT_SIZE_BYTES; i++)
    {
        uint16_t attrId = CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_INPUT_ID + i;

        errCode = EI_API_CIP_addAssemblyMember(
            pCipNode,
            CFG_PROFILE_GENERIC_DEVICE_ASSEMBLY_INSTANCE_EXCLUSIVE_OWNER_2, // 101
            classId,
            instanceId,
            attrId
        );

        if (errCode != 0)
        {
            OSAL_printf("Add INPUT member %d failed: 0x%08X\n", i, errCode);
        }
    }
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Setup the application with classes, instances, attributes, and assemblies.
 *
 *  \details
 *  Setup the application with classes, instances, attributes, and assemblies.<br>
 *  For the assemblies, use instances in the Vendor Specific range of IDs.
 *
 *  \ingroup GENERIC_DEVICE_MAIN
 */
static bool GENERIC_DEVICE_cipSetup(EI_API_CIP_NODE_T* pCipNode)
{
    uint32_t errCode = 0;

    uint16_t classId    = CFG_PROFILE_GENERIC_DEVICE_VENDOR_CLASS_ID;
    uint16_t instanceId = CFG_PROFILE_GENERIC_DEVICE_VENDOR_INSTANCE_ID;

    /* Create new vendor class 0x70 with read and write service and several attributes. */
    GENERIC_DEVICE_cipGenerateContent(pCipNode, classId, instanceId);

#if (defined BRIDGING_AND_ROUTING) && (BRIDGING_AND_ROUTING==1)
    GENERIC_DEVICE_cipRoutingSampleSetup(pCipNode);
    GENERIC_DEVICE_cipUnRoutingSampleSetup(pCipNode);
#endif  /* (defined BRIDGING_AND_ROUTING) && (BRIDGING_AND_ROUTING==1) */

    // Exclusive Owner Output (Inst 100 / 0x64)
    errCode = EI_API_CIP_createAssembly(pCipNode, CFG_PROFILE_GENERIC_DEVICE_ASSEMBLY_INSTANCE_EXCLUSIVE_OWNER_1, EI_API_CIP_eAR_GET_AND_SET);
    // Exclusive Owner Input (Inst 101 / 0x65)
    errCode = EI_API_CIP_createAssembly(pCipNode, CFG_PROFILE_GENERIC_DEVICE_ASSEMBLY_INSTANCE_EXCLUSIVE_OWNER_2, EI_API_CIP_eAR_GET);

    GENERIC_DEVICE_Create_IO_Map(pCipNode, classId, instanceId);

    return (errCode == 0);
}

/*!
*  <!-- Description: -->
*
*  \brief
*  Generates attributes and services for a CIP&trade;class.
*
*  \details
*  Create a CIP class with a Class IDs using the value specified in parameter classId.<br>
*  Generates attributes and services for that class.<br>
*  Adds read and write services.<br>
*
* \ingroup GENERIC_DEVICE_MAIN
*
*/
static void GENERIC_DEVICE_cipGenerateContent(EI_API_CIP_NODE_T* cipNode,
                                              uint16_t classId,
                                              uint16_t instanceId)
{
    EI_API_CIP_SService_t service;
    EI_API_CIP_SAttr_t attr;
    uint32_t errCode;

    /* Create class */
    EI_API_CIP_createClass(cipNode, classId);

    service.getAttrAllResponseCnt = 0;
    service.callback = NULL;

    service.code = EI_API_CIP_eSC_GETATTRSINGLE;
    EI_API_CIP_addClassService(cipNode, classId, &service);

    service.code = EI_API_CIP_eSC_SETATTRSINGLE;
    EI_API_CIP_addClassService(cipNode, classId, &service);

    /* Create instance */
    EI_API_CIP_createInstance(cipNode, classId, instanceId);

    service.code = EI_API_CIP_eSC_GETATTRSINGLE;
    EI_API_CIP_addInstanceService(cipNode, classId, instanceId, &service);

    service.code = EI_API_CIP_eSC_SETATTRSINGLE;
    EI_API_CIP_addInstanceService(cipNode, classId, instanceId, &service);

    uint16_t attribID = CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_OUTPUT_ID;
    for (int i = 0; i < MAX_OUTPUT_SIZE_BYTES; i++)
    {
        EI_API_CIP_SAttr_t attr;
        OSAL_MEMORY_memset(&attr, 0, sizeof(attr));
        attr.id = attribID;
        attr.edt = EI_API_CIP_eEDT_BYTE;
        attr.accessRule = EI_API_CIP_eAR_GET_AND_SET;
        attr.pvValue = &gOutputProcessImage[i];

        EI_API_CIP_addInstanceAttr(cipNode, classId, instanceId, &attr);
        EI_API_CIP_setInstanceAttr(cipNode, classId, instanceId, &attr);

        attribID++;
    }
    
    attribID = CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_INPUT_ID;
    for (int i = 0; i < MAX_INPUT_SIZE_BYTES; i++)
    {
        EI_API_CIP_SAttr_t attr;
        OSAL_MEMORY_memset(&attr, 0, sizeof(attr));
        attr.id = attribID;
        attr.edt = EI_API_CIP_eEDT_BYTE;
        attr.accessRule = EI_API_CIP_eAR_GET;
        attr.pvValue = &gInputProcessImage[i];

        EI_API_CIP_addInstanceAttr(cipNode, classId, instanceId, &attr);
        EI_API_CIP_setInstanceAttr(cipNode, classId, instanceId, &attr);

        attribID++;
    }

    // OSAL_MEMORY_memset(&attr, 0, sizeof(attr));
    // attr.id         = CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_OUTPUT_ID;
    // attr.edt        = EI_API_CIP_eEDT_USINT; 
    // attr.cdt        = EI_API_CIP_eCDT_FARRAY;
    // attr.accessRule = EI_API_CIP_eAR_GET_AND_SET;
    // attr.edtSize    = sizeof(uint8_t);
    // attr.cdtSize    = (uint16_t)MAX_OUTPUT_SIZE_BYTES;
    // attr.pvValue    = &gOutputProcessImage[0];          // Point to the start of the array
    // EI_API_CIP_addClassAttr        (pCipNode, classId, &attr);
    //  EI_API_CIP_setClassAttrFunc    (pCipNode, classId, &attr);
    // errCode = EI_API_CIP_addInstanceAttr(cipNode, classId, instanceId, &attr);
    // if(errCode != 0) {
    //     OSAL_printf("OUTPUT ATTRIB REGISTRATION FAILED: 0x%08X\r\n", errCode);
    // }
    
    // OSAL_MEMORY_memset(&attr, 0, sizeof(attr));
    
    // attr.id         = CFG_PROFILE_GENERIC_DEVICE_VENDOR_ATTRIBUTE_INPUT_ID;
    // attr.edt        = EI_API_CIP_eEDT_USINT;
    // attr.cdt        = EI_API_CIP_eCDT_FARRAY;
    // attr.accessRule = EI_API_CIP_eAR_GET;
    // attr.edtSize    = sizeof(uint8_t);
    // attr.cdtSize    = (uint16_t)MAX_INPUT_SIZE_BYTES;
    // attr.pvValue    = &gInputProcessImage[0];           // Point to the start of the array
    // errCode = EI_API_CIP_addInstanceAttr(cipNode, classId, instanceId, &attr);
    // if(errCode != 0) {
    //     OSAL_printf("INPUT ATTRIB REGISTRATION FAILED: 0x%08X\r\n", errCode);
    // }
}

#if (defined BRIDGING_AND_ROUTING) && (BRIDGING_AND_ROUTING==1)
/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Callback function is triggered when get_attribute_service request is received during explicit messaging.
 *  This function must be shown to the attribute get_callback during setup.
 *
 *  \details
 *
 *
 */
uint32_t GENERIC_DEVICE_CLASS70_GetAttr2Inst1_Link1_cb (EI_API_CIP_NODE_T *pCipNode,
                                                        uint16_t           classId,
                                                        uint16_t           instanceId,
                                                        uint16_t           attrId,
                                                        uint16_t          *pLen,
                                                        void              *pvValue,
                                                        uint16_t           linkAddress)
{
    /* USINT val */
    *pLen = sizeof(uint8_t);

    /* value to be returned as get attr response */
    *(uint8_t*)pvValue = 13;

    return EI_API_eERR_CB_NO_ERROR;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Callback function is triggered when set_attribute_service request is received during explicit messaging.
 *  This function must be shown to the attribute set_callback during setup.
 *
 *  \details
 *
 *
 */
uint32_t GENERIC_DEVICE_CLASS70_SetAttr2Inst1_Link1_cb (EI_API_CIP_NODE_T *pCipNode,
                                                        uint16_t           classId,
                                                        uint16_t           instanceId,
                                                        uint16_t           attrId,
                                                        uint16_t           len,
                                                        void              *pvValue,
                                                        uint16_t           linkAddress)
{
    uint8_t data=*((uint8_t*)pvValue);

    OSALUNREF_PARM(data);

    return EI_API_eERR_CB_NO_ERROR;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Callback function is triggered when get_attribute_service request is received during explicit messaging.
 *  This function must be shown to the attribute get_callback during setup.
 *
 *  \details
 *
 *
 */
uint32_t GENERIC_DEVICE_CLASS70_GetAttr2Inst1_cb (EI_API_CIP_NODE_T *pCipNode,
                                                  uint16_t           classId,
                                                  uint16_t           instanceId,
                                                  uint16_t           attrId,
                                                  uint16_t          *pLen,
                                                  void              *pvValue)
{

    /* USINT val */
    *pLen = sizeof(uint8_t);

    /* Value to be returned as get attr response. */
    *(uint8_t*)pvValue = 13;


    return EI_API_eERR_CB_NO_ERROR;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Callback function is triggered when set_attribute_service request is received during explicit messaging.
 *  This function must be shown to the attribute set_callback during setup.
 *
 *  \details
 *
 *
 */
uint32_t GENERIC_DEVICE_CLASS70_SetAttr2Inst1_cb (EI_API_CIP_NODE_T *pCipNode,
                                                  uint16_t           classId,
                                                  uint16_t           instanceId,
                                                  uint16_t           attrId,
                                                  uint16_t           len,
                                                  void              *pvValue)
{
    uint8_t data=*((uint8_t*)pvValue);

    OSALUNREF_PARM(data);

    return EI_API_eERR_CB_NO_ERROR;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  In order to observe configurationAssemblies data , EI_APP_CFGASSEM_cb was created.
 *
 *  \details
 *
 *
 */
uint32_t GENERIC_DEVICE_CFGASSEM_cb (EI_API_CIP_NODE_T   *pCipNode,
                                     uint16_t             o2t,
                                     uint16_t             t2o,
                                     uint16_t             cfg_inst,
                                     const uint8_t* const pCfgData,
                                     uint16_t             cfgDataSize,
                                     uint16_t             linkaddr)
{
    return EI_API_eERR_CB_NO_ERROR;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Call EI_APP_cipGenerateContentCipRouting function in order to create Assembly and use routable functionality.
 *
 *  \details
 *   A routableInstance has been created by cipGenerateContent. Assemblies attributes are setting in this function.

 */
void GENERIC_DEVICE_cipGenerateContentCipRouting (EI_API_CIP_NODE_T *pCipNode,
                                                  uint16_t           classId,
                                                  uint16_t           instanceId,
                                                  uint8_t            linkAddr)
{
    EI_API_CIP_SService_t service;
    uint16_t i = 0;
    uint16_t attribID = 0x300;
    uint32_t errCode = 0;

    errCode = EI_API_CIP_createRoutableInstance(pCipNode, classId, instanceId, linkAddr);

    if (errCode != EI_API_CIP_eERR_OK)
    {
        OSAL_printf("Failed to create routable instance\n");
    }

    service.getAttrAllResponseCnt = 0;
    service.callback = NULL;

    service.code = EI_API_CIP_eSC_GETATTRSINGLE;
    errCode = EI_API_CIP_addRoutableInstanceService(pCipNode, classId, instanceId, &service, linkAddr);

    if (errCode != EI_API_CIP_eERR_OK)
    {
        OSAL_printf("Failed to add routable instance service\n");
    }

    service.code = EI_API_CIP_eSC_SETATTRSINGLE;
    errCode = EI_API_CIP_addRoutableInstanceService(pCipNode, classId, instanceId, &service, linkAddr);
    if (errCode != EI_API_CIP_eERR_OK)
    {
        OSAL_printf("Failed to add routable instance service\n");
    }

    /* 64 USINT (uint8_t). */
    for (i = 0; i < 64; i++)
    {
        EI_API_CIP_SAttr_t attr;
        memset(&attr, 0, sizeof(attr));
        attr.id = attribID;
        attr.edt = EI_API_CIP_eEDT_USINT;
        attr.accessRule = EI_API_CIP_eAR_GET_AND_SET;
        attr.pvValue = &i;

        errCode = EI_API_CIP_addRoutableInstanceAttr(pCipNode, classId, instanceId, &attr, linkAddr);
        if (errCode != EI_API_CIP_eERR_OK)
        {
           OSAL_printf("Failed to add routable instance service\n");
        }

        attribID++;
    }

    /* 32 UINT (uint16_t). */
    for (i = 0; i < 32; i++)
    {
        EI_API_CIP_SAttr_t attr;
        memset(&attr, 0, sizeof(attr));
        attr.id = attribID;
        attr.edt = EI_API_CIP_eEDT_UINT;
        attr.accessRule = EI_API_CIP_eAR_GET_AND_SET;
        attr.pvValue = &i;

        EI_API_CIP_addRoutableInstanceAttr(pCipNode, classId, instanceId, &attr, linkAddr);

        attribID++;
    }

    /* 16 UDINT (uint32_t). */
    for (i = 0; i < 16; i++)
    {
        EI_API_CIP_SAttr_t attr;
        memset(&attr, 0, sizeof(attr));
        attr.id = attribID;
        attr.edt = EI_API_CIP_eEDT_UDINT;
        attr.accessRule = EI_API_CIP_eAR_GET_AND_SET;
        attr.pvValue = &i;

        EI_API_CIP_addRoutableInstanceAttr(pCipNode, classId, instanceId, &attr, linkAddr);

        attribID++;
    }

    /* 8 ULINT (uint64_t). */
    for (i = 0; i < 8; i++)
    {
        EI_API_CIP_SAttr_t attr;
        memset(&attr, 0, sizeof(attr));
        attr.id = attribID;
        attr.edt = EI_API_CIP_eEDT_ULINT;
        attr.accessRule = EI_API_CIP_eAR_GET_AND_SET;
        attr.pvValue = &i;

        EI_API_CIP_addRoutableInstanceAttr(pCipNode, classId, instanceId, &attr, linkAddr);

        attribID++;
    }
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Initialize the application with instance's, attribute's, service's to be able provide unrouting functionality.
 *  This example shows how to set explicit messaging us.
 *
 *  \details
 *  In the SampleSetup application with instances,attributes callback.
 *  In EI_APP_cipUnRoutingSampleSetup, we must first create an Instance and the Instance services we need.
 *  If you need to Class Instace, You should call EI_API_CIP_addClassAttr() and EI_API_CIP_setClassAttrFunc() as well.
 *
 */
static bool GENERIC_DEVICE_cipUnRoutingSampleSetup (EI_API_CIP_NODE_T *pCipNode)
{
    uint32_t              errCode = 0;
    uint16_t              classId = 0x70;
    uint16_t              instanceId = 6;


    EI_API_CIP_SAttr_t    attr = {0};
    const uint16_t        attr2_val = 2;
    EI_API_CIP_SService_t service = {0};

    /* Creating routable instance 1 which is connected to link addr 1 in the 0x70 class */
    errCode = EI_API_CIP_createInstance(pCipNode, classId, instanceId);

    if (errCode != EI_API_CIP_eERR_OK) {
        OSAL_printf("Failed to create Routable Instance\n");
    }

    /* Set class instance. */
    service.code = EI_API_CIP_eSC_GETATTRSINGLE;
    errCode = EI_API_CIP_addInstanceService(pCipNode, classId, instanceId, &service);

    if (errCode != EI_API_CIP_eERR_OK) {
        OSAL_printf("Failed to create Routable Instance\n");
    }

    service.code = EI_API_CIP_eSC_SETATTRSINGLE;
    errCode = EI_API_CIP_addInstanceService(pCipNode, classId, instanceId, &service);

    if (errCode != EI_API_CIP_eERR_OK) {
        OSAL_printf("Failed to create Routable Instance\n");
    }


    attr.id = 2;
    attr.edt = EI_API_CIP_eEDT_USINT;
    attr.accessRule = EI_API_CIP_eAR_GET_AND_SET;
    attr.get_callback = GENERIC_DEVICE_CLASS70_GetAttr2Inst1_cb;
    attr.set_callback = GENERIC_DEVICE_CLASS70_SetAttr2Inst1_cb;
    attr.pvValue = (void *)&attr2_val;

    errCode = EI_API_CIP_addInstanceAttr(pCipNode, classId, instanceId, &attr);
    if (errCode != EI_API_CIP_eERR_OK) {
        OSAL_printf("Failed to add Routable Instance Attr\n");
    }
    errCode = EI_API_CIP_setInstanceAttrFunc(pCipNode, classId, instanceId, &attr);
    if (errCode != EI_API_CIP_eERR_OK) {
        OSAL_printf("Failed to set Routable Instance attr func\n");
    }

    /* Adding class attribute and function. */
    EI_API_CIP_addClassAttr        (pCipNode, classId, &attr);
    EI_API_CIP_setClassAttrFunc    (pCipNode, classId, &attr);

    return true;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Setup the application with routableinstances, routable attributes callback and routableassemblies..
 *
 *  \details
 *  In the SampleSetup application with routableinstances, routable attributes callback, and routableassemblies.
 *  In the EI_APP_cipRoutingSampleSetup, first of all,setroutinglinkadress function should be called. after setting routable ports.
 *  routable Instance or assembly Instance can be created. Don't forget it, Rotable side is using the routable callback function no longer.
 *
 */
static bool GENERIC_DEVICE_cipRoutingSampleSetup (EI_API_CIP_NODE_T *pCipNode)
{
    uint32_t              errCode = 0;
    uint16_t              classId = 0x10D;
    uint16_t              instanceId = 1;
    uint8_t               linkAddress = 2;
    EI_API_CIP_SAttr_t    attr = {0};
    const uint16_t        attr2_val = 2;
    EI_API_CIP_SService_t service = {0};
    const uint8_t         maxlinkaddr = 8;
    uint8_t               linkaddr = 0;

     /* CIP routing functions */
     errCode = EI_API_CIP_setRoutingMaxLinkAddr(pCipNode, maxlinkaddr);
     if (errCode != EI_API_CIP_eERR_OK) {
                OSAL_printf("Failed to set Max link addr\n");
            }

     errCode= EI_API_CIP_getRoutingMaxLinkAddr(pCipNode, &linkaddr);
     if ((errCode != EI_API_CIP_eERR_OK) || (linkaddr!= maxlinkaddr)) {
                   OSAL_printf("Failed to get Max link addr\n");
               }

     const uint8_t routablePorts[] = {
             11,7,3
     };

     errCode = EI_API_CIP_setRoutingRoutablePorts(pCipNode, routablePorts, sizeof(routablePorts)/sizeof(uint8_t));

     if (errCode != EI_API_CIP_eERR_OK) {
                  OSAL_printf("Failed to set Routable Ports\n");
              }

    /* Create instance of 0x70 CIP Routing Cylic IO messaging. */
    GENERIC_DEVICE_cipGenerateContentCipRouting(pCipNode, classId, instanceId, linkAddress);

    errCode = EI_API_CIP_createCfgAssembly(pCipNode); /* CFG ASSEMBLY */

    errCode = EI_API_CIP_createRoutingAssembly(pCipNode, 0xfe, EI_API_CIP_eAR_GET, linkAddress, EI_API_CIP_eAPP_TYPE_IO); // Input-only. (O2T)
    errCode = EI_API_CIP_createRoutingAssembly(pCipNode, 0xff, EI_API_CIP_eAR_GET, linkAddress, EI_API_CIP_eAPP_TYPE_LO); // Listen-only.

    errCode = EI_API_CIP_createRoutingAssembly(pCipNode, 3, EI_API_CIP_eAR_GET_AND_SET, linkAddress, EI_API_CIP_eAPP_TYPE_EO);//T2O
    errCode = EI_API_CIP_createRoutingAssembly(pCipNode, 1, EI_API_CIP_eAR_GET_AND_SET, linkAddress, EI_API_CIP_eAPP_TYPE_EO);//O2T

    for (uint16_t i = 0x300; i < 0x305; i++)
    {
        errCode = EI_API_CIP_addRoutingAssemblyMember(pCipNode, 3, classId, instanceId, i, linkAddress);
        if (errCode != EI_API_CIP_eERR_OK)
        {
            OSAL_printf("Failed to add Class ID %#x, Instance ID %#x, Attribute ID %#x to Assembly Instance 0x64:  Error code: 0x%08x\n", classId, instanceId, (uint16_t)i, errCode);
        }

        errCode = EI_API_CIP_addRoutingAssemblyMember(pCipNode, 1, classId, instanceId, (uint16_t)(8 + i), linkAddress);
        if (errCode != EI_API_CIP_eERR_OK) {
            OSAL_printf("Failed to add Class ID %#x, Instance ID %#x, Attribute ID %#x to Assembly Instance 0x65:  Error code: 0x%08x\n", classId, instanceId, (uint16_t)(8 + i), errCode);
        }
    }

     instanceId = 6;

     /* Creating routable instance 1 which is connected to link addr 1 in the 0x70 class. */
     errCode = EI_API_CIP_createRoutableInstance(pCipNode, classId, instanceId, linkAddress);

     if (errCode != EI_API_CIP_eERR_OK) {
         OSAL_printf("Failed to create Routable Instance\n");
     }

     /* Set class instance */
     service.code = EI_API_CIP_eSC_GETATTRSINGLE;
     errCode = EI_API_CIP_addRoutableInstanceService(pCipNode, classId, instanceId, &service, linkAddress);

     if (errCode != EI_API_CIP_eERR_OK) {
         OSAL_printf("Failed to create Routable Instance\n");
     }

     service.code = EI_API_CIP_eSC_SETATTRSINGLE;
     errCode = EI_API_CIP_addRoutableInstanceService(pCipNode, classId, instanceId, &service, linkAddress);

     if (errCode != EI_API_CIP_eERR_OK) {
         OSAL_printf("Failed to create Routable Instance\n");
     }


     attr.id = 2;
     attr.edt = EI_API_CIP_eEDT_USINT;
     attr.accessRule = EI_API_CIP_eAR_GET_AND_SET;
     attr.get_callback_routed = GENERIC_DEVICE_CLASS70_GetAttr2Inst1_Link1_cb;
     attr.set_callback_routed = GENERIC_DEVICE_CLASS70_SetAttr2Inst1_Link1_cb;
     attr.pvValue = (void *)&attr2_val;

     errCode = EI_API_CIP_addRoutableInstanceAttr(pCipNode, classId, instanceId, &attr, linkAddress);
     if (errCode != EI_API_CIP_eERR_OK) {
         OSAL_printf("Failed to add Routable Instance Attr\n");
     }
     errCode = EI_API_CIP_setRoutableInstanceAttrFunc(pCipNode, classId, instanceId, &attr, linkAddress);
     if (errCode != EI_API_CIP_eERR_OK) {
         OSAL_printf("Failed to set Routable Instance attr func\n");
     }

     /* Adding class attribute and function */
     EI_API_CIP_addClassAttr        (pCipNode, classId, &attr);
     EI_API_CIP_setClassAttrFunc    (pCipNode, classId, &attr);

    return true;
}
#endif  /* (defined BRIDGING_AND_ROUTING) && (BRIDGING_AND_ROUTING==1) */

#endif  /* (!(defined FBTLPROVIDER) || (0 == FBTLPROVIDER)) && (!(defined FBTL_REMOTE) || (0 == FBTL_REMOTE)) */

//*************************************************************************************************
