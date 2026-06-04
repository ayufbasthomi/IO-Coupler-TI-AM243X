/*!
 *  \file ecSubDeviceWebserver.c
 *
 *  \brief
 *  EtherCAT<sup>&reg;</sup> SubDevice EoE Webserver Application
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

#define SHOW_LOOPCOUNT 0
#define SHOW_ESCSTATUS 0

#define ENABLE_I2CLEDS 1

#define BIT2BYTE(x) (((x) + 7) >> 3)

/*-----------------------------------------------------------------------------------------
------
------     Includes
------
-----------------------------------------------------------------------------------------*/

#include "ecSubDeviceWebserver.h"
#include <defines/ecSlvApiDef.h>
#include "project.h"
#include <stdio.h>
#include <string.h>
#include <kernel/dpl/DebugP.h>
#include <ESL_os.h>
#include <ESL_BOARD_config.h>
#include <ESL_vendor.h>

#include <ESL_gpioHelper.h>
#include <ESL_foeDemo.h>
#include <ESL_eoeDemo.h>
#include <ESL_soeDemo.h>
#include <ESL_eeprom.h>
#include <ESL_version.h>
#include "mcan_loopback_interrupt.h"

#if !(defined FBTL_REMOTE) && !(defined DPRAM_REMOTE)
#include <CUST_PHY_base.h>
#endif

#include <ecSlvApi.h>

#if !(defined MBXMEM)
#define MBXMEM
#endif

/*-----------------------------------------------------------------------------------------
------
------     local variables and constants
------
-----------------------------------------------------------------------------------------*/
#define I2C_IOEXP_ADDR                0x60 // The I2C address for GPIO expander

// object indices
#define SLS_OBJIDX_DIAGNOSIS          0x2018
#define SLS_OBJIDX_FSOE_DIG_IO_MODULE 0xA000
#define SLS_OBJIDX_FSOE_CONN_STATE    0x0800

#define FSOE_CONN_ST_RESET            100
#define FSOE_CONN_ST_SESSION          101
#define FSOE_CONN_ST_CONNECTION       102
#define FSOE_CONN_ST_PARAMETER        103
#define FSOE_CONN_ST_DATA             104
#define FSOE_CONN_ST_FAILSAFE         105

#define NUM_SUB_INDEX_DATA  8U

extern uint8_t DI_NODE_COUNT;
extern uint8_t DO_NODE_COUNT;
extern uint8_t AO_NODE_COUNT;
extern uint8_t AI_NODE_COUNT;

extern CANopenModule gModules[MAX_NODES];
extern uint8_t gModuleCount;

static void EC_SLV_APP_SS_applicationRun(void *pAppCtxt_p);
static EC_SLV_APP_SS_Application_t *g_pApp = NULL;

/******************************************************************************
 * Sync EtherCAT + CANopen Input Process Image
 *
 * Copy data:
 *
 * CANopen Slave Object Dictionary
 *                ->
 * EtherCAT Input Process Image Buffer
 *
 ******************************************************************************/
void GS_APP_SyncInputProcessImage(void)
{
    uint16_t *pWord = (uint16_t *)pdBuffer_input;

    memset(pdBuffer_input, 0, sizeof(pdBuffer_input));

    for(uint16_t slot=0; slot<gTxCount; slot++)
    {
        CANopenModule *m = &gTxModules[slot];

        uint16_t offset = slot * 8;

        switch(m->ioType)
        {
            case DI:
            {
                pWord[offset] = gIOData.di[m->diIndex];

                // DebugP_log(
                //     "[ECAT TX][DI] slot=%u node=%u value=0x%04X\r\n",
                //     slot,
                //     m->nodeId,
                //     pWord[offset]);

                // for(uint16_t i = 0; i < MAX_DI; i++)
                // {
                //     DebugP_log("DI[%u] = %d\r\n", i, gIOData.di[i]);
                //     DebugP_log("diIndex = %d | offset = %d\r\n", m->diIndex, offset);
                // }

                DebugP_log(
                    "WRITE ECAT: DI[0]=%u -> WORD[%u]=%u\r\n",
                    gIOData.di[m->diIndex],
                    offset,
                    ((uint16_t *)pdBuffer_input)[offset]);

                break;
            }

            case AI_C:
            case AI_V:
            case RTDY:
            case RTDB:
            {
                memcpy(
                    &pWord[offset],
                    &gIOData.ai[m->aiIndex * 8],
                    8*sizeof(int16_t));

                break;
            }

            default:
                break;
        }
    }
}

/******************************************************************************
 * Sync EtherCAT Output Process Image
 *
 * Copy data:
 *
 * EtherCAT Output Process Image Buffer
 *                ->
 * CANopen Slave Object Dictionary
 *
 ******************************************************************************/
void GS_APP_SyncOutputProcessImage(void)
{
    uint16_t *pWord = (uint16_t *)pdBuffer_output;

    for(uint16_t slot=0; slot<gRxCount; slot++)
    {
        CANopenModule *m =
            &gRxModules[slot];

        uint16_t offset =
            slot * 8;

        switch(m->ioType)
        {
            case DO:
            {
                gIOData.do_[m->doIndex] = pWord[offset];

                // DebugP_log(
                //     "[ECAT RX][DO] slot=%u node=%u value=0x%04X\r\n",
                //     slot,
                //     m->nodeId,
                //     pWord[offset]);

                CANopen_writeRPDO(m->nodeId, pWord[offset]);

                break;
            }

            case AO_C:
            case AO_V:
            {
                memcpy(
                    &gIOData.ao[m->aoIndex * 8],
                    &pWord[offset],
                    8*sizeof(int16_t));

                break;
            }

            default:
                break;
        }
    }
}

/******************************************************************************
 * Helper : Write U16 Object Entry
 ******************************************************************************/
static EC_API_EError_t SetObjEntryU16(
    EC_API_SLV_SHandle_t *ptSlave,
    uint16_t index,
    uint8_t subIndex,
    uint16_t value)
{
    EC_API_SLV_SCoE_ObjEntry_t *ptEntry = NULL;
    EC_API_EError_t error;

    error = (EC_API_EError_t)
        EC_API_SLV_CoE_getObjectEntry(
            ptSlave,
            index,
            subIndex,
            &ptEntry);

    if(error != EC_API_eERR_NONE)
    {
        OSAL_printf(
            "SetObjEntryU16 Get Entry Failed idx=0x%04X sub=0x%02X err=0x%08X\r\n",
            index,
            subIndex,
            error);

        return error;
    }

    error = (EC_API_EError_t)
        EC_API_SLV_CoE_setObjectEntryData(
            ptSlave,
            ptEntry,
            sizeof(value),
            &value);

    if(error != EC_API_eERR_NONE)
    {
        OSAL_printf(
            "SetObjEntryU16 Set Failed idx=0x%04X sub=0x%02X err=0x%08X\r\n",
            index,
            subIndex,
            error);
    }

    return error;
}

// /******************************************************************************
//  * Helper : Get PDO Offset
//  ******************************************************************************/
// static EC_API_EError_t GetPdoOffset(
//     EC_API_SLV_SHandle_t *ptSlave,
//     EC_API_SLV_Pdo_t *ptPdo,
//     uint16_t *pOffset)
// {
//     if((ptSlave == NULL) || (ptPdo == NULL) || (pOffset == NULL))
//     {
//         return EC_API_eERR_INVALID;
//     }

//     EC_API_SLV_PDO_getOffset(
//         ptSlave,
//         ptPdo,
//         pOffset);

//     return EC_API_eERR_NONE;
// }

/******************************************************************************
 * Helper : Get PDO Length
 ******************************************************************************/
// static EC_API_EError_t GetPdoLength(
//     EC_API_SLV_SHandle_t *ptSlave,
//     EC_API_SLV_Pdo_t *ptPdo,
//     uint16_t *pLength)
// {
//     if((ptSlave == NULL) || (ptPdo == NULL) || (pLength == NULL))
//     {
//         return EC_API_eERR_INVALID;
//     }

//     EC_API_SLV_PDO_getLength(
//         ptSlave,
//         ptPdo,
//         pLength);

//     return EC_API_eERR_NONE;
// }

/******************************************************************************
 * Initialize Module Database
 ******************************************************************************/
static void GS_APP_InitModuleTable(
    EC_SLV_APP_SS_Application_t *pApp)
{
    uint16_t i;

    if(pApp == NULL)
    {
        return;
    }

    pApp->moduleCount = 0U;

    for(i = 0U; i < MAX_NODES; i++)
    {
        pApp->moduleList[i].slot = i;

        pApp->moduleList[i].productCode = 0U;

        pApp->moduleList[i].state =
            GS_MODULE_STATE_OFFLINE;

        pApp->moduleList[i].errorCode = 0U;

        pApp->moduleList[i].modulePresent = false;

        pApp->moduleList[i].inputSize = 0U;
        pApp->moduleList[i].outputSize = 0U;

        pApp->moduleList[i].txPdoOffset = 0U;
        pApp->moduleList[i].txPdoLength = 0U;

        pApp->moduleList[i].rxPdoOffset = 0U;
        pApp->moduleList[i].rxPdoLength = 0U;

        memset(
            pApp->moduleList[i].moduleName,
            0,
            sizeof(pApp->moduleList[i].moduleName));

        memset(
            pApp->moduleList[i].diagnosticText,
            0,
            sizeof(pApp->moduleList[i].diagnosticText));
    }
}

/******************************************************************************
 * Update Module Database
 ******************************************************************************/
static EC_API_EError_t GS_APP_UpdateModuleDatabase(
    EC_SLV_APP_SS_Application_t *pApp)
{
    uint16_t idx = 0;
    uint16_t i;

    if (pApp == NULL)
    {
        return EC_API_eERR_INVALID;
    }

    /*************************************************************
     * Build module list from your 4 groups
     *************************************************************/

    /* ================= DI ================= */
    for (i = 0; i < DI_NODE_COUNT; i++)
    {
        pApp->moduleList[idx].slot = idx;
        pApp->moduleList[idx].productCode = IO_DEVICE_TYPE_DI16;
        pApp->moduleList[idx].state = GS_MODULE_STATE_OK;
        pApp->moduleList[idx].modulePresent = true;

        strncpy(pApp->moduleList[idx].moduleName, "DI16", GS_MODULE_NAME_LENGTH - 1);

        pApp->moduleList[idx].inputSize  = 2;
        pApp->moduleList[idx].outputSize = 0;

        strncpy(pApp->moduleList[idx].diagnosticText, "DI OK", GS_DIAG_TEXT_LENGTH - 1);

        idx++;
    }

    /* ================= DO ================= */
    for (i = 0; i < DO_NODE_COUNT; i++)
    {
        pApp->moduleList[idx].slot = idx;
        pApp->moduleList[idx].productCode = IO_DEVICE_TYPE_DO16;
        pApp->moduleList[idx].state = GS_MODULE_STATE_OK;
        pApp->moduleList[idx].modulePresent = true;

        strncpy(pApp->moduleList[idx].moduleName, "DO16", GS_MODULE_NAME_LENGTH - 1);

        pApp->moduleList[idx].inputSize  = 0;
        pApp->moduleList[idx].outputSize = 2;

        strncpy(pApp->moduleList[idx].diagnosticText, "DO OK", GS_DIAG_TEXT_LENGTH - 1);

        idx++;
    }

    /* ================= AI ================= */
    for (i = 0; i < AI_NODE_COUNT; i++)
    {
        pApp->moduleList[idx].slot = idx;
        pApp->moduleList[idx].productCode = IO_DEVICE_TYPE_AIV8;
        pApp->moduleList[idx].state = GS_MODULE_STATE_OK;
        pApp->moduleList[idx].modulePresent = true;

        strncpy(pApp->moduleList[idx].moduleName, "AI8", GS_MODULE_NAME_LENGTH - 1);

        pApp->moduleList[idx].inputSize  = 16; // 8ch * 2 byte
        pApp->moduleList[idx].outputSize = 0;

        strncpy(pApp->moduleList[idx].diagnosticText, "AI OK", GS_DIAG_TEXT_LENGTH - 1);

        idx++;
    }

    /* ================= AO ================= */
    for (i = 0; i < AO_NODE_COUNT; i++)
    {
        pApp->moduleList[idx].slot = idx;
        pApp->moduleList[idx].productCode = IO_DEVICE_TYPE_AOV8;
        pApp->moduleList[idx].state = GS_MODULE_STATE_OK;
        pApp->moduleList[idx].modulePresent = true;

        strncpy(pApp->moduleList[idx].moduleName, "AO8", GS_MODULE_NAME_LENGTH - 1);

        pApp->moduleList[idx].inputSize  = 0;
        pApp->moduleList[idx].outputSize = 16;

        strncpy(pApp->moduleList[idx].diagnosticText, "AO OK", GS_DIAG_TEXT_LENGTH - 1);

        idx++;
    }

    /*************************************************************
     * Total module count
     *************************************************************/
    pApp->moduleCount = idx;

    return EC_API_eERR_NONE;
}

// static EC_API_EError_t GS_APP_CreateModuleTableObjects(
//     EC_SLV_APP_SS_Application_t *pApp)
// {
//     EC_API_SLV_SHandle_t       *ptSubDevice;
//     EC_API_EError_t error;
//     uint16_t        i;
//     uint16_t        objIndex;

//     ptSubDevice = pApp->ptEcSlvApi;

//     for(i = 0U; i < MAX_IO_DEVICES; i++)
//     {
//         objIndex = (uint16_t)(0x3000U + i);

//         error = EC_API_SLV_CoE_odAddRecord(
//             ptSubDevice,
//             objIndex,
//             "ModuleInfo",
//             NULL,
//             NULL,
//             NULL,
//             NULL,
//             &pApp->ptModuleInfo3000[i]);

//         if(error != EC_API_eERR_NONE)
//         {
//             return error;
//         }

//         /*
//          * Add SubIndexes
//          */

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                  pApp->ptModuleInfo3000[i],
//                 1U,
//                 "productCode",
//                 DEFTYPE_UNSIGNED16,
//                 16U,
//                 ACCESS_READ);

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                  pApp->ptModuleInfo3000[i],
//                 2U,
//                 "State",
//                 DEFTYPE_UNSIGNED16,
//                 16U,
//                 ACCESS_READ);

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                  pApp->ptModuleInfo3000[i],
//                 3U,
//                 "ErrorCode",
//                 DEFTYPE_UNSIGNED16,
//                 16U,
//                 ACCESS_READ);

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                  pApp->ptModuleInfo3000[i],
//                 4U,
//                 "TxPdoOffset",
//                 DEFTYPE_UNSIGNED16,
//                 16U,
//                 ACCESS_READ);

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                 pApp->ptModuleInfo3000[i],
//                 5U,
//                 "TxPdoLength",
//                 DEFTYPE_UNSIGNED16,
//                 16U,
//                 ACCESS_READ);

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                  pApp->ptModuleInfo3000[i],
//                 6U,
//                 "RxPdoOffset",
//                 DEFTYPE_UNSIGNED16,
//                 16U,
//                 ACCESS_READ);

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                  pApp->ptModuleInfo3000[i],
//                 7U,
//                 "RxPdoLength",
//                 DEFTYPE_UNSIGNED16,
//                 16U,
//                 ACCESS_READ);
//     }

//     return EC_API_eERR_NONE;
// }
/******************************************************************************
 * Export Module Table To EtherCAT Objects
 *
 * Object Layout:
 *
 * 0x3000 + slot
 *
 * SubIndex:
 * 1 -> Module ID
 * 2 -> State
 * 3 -> Error Code
 * 4 -> TX PDO Offset
 * 5 -> TX PDO Length
 * 6 -> RX PDO Offset
 * 7 -> RX PDO Length
 *
 ******************************************************************************/
static EC_API_EError_t GS_APP_UpdateModuleObjects(
    EC_SLV_APP_SS_Application_t *pApp)
{
    EC_API_EError_t      error;
    EC_API_SLV_SHandle_t *ptSlave;
    uint16_t             objIndex;
    uint16_t             i;

    if(pApp == NULL)
    {
        return EC_API_eERR_INVALID;
    }

    ptSlave = pApp->ptEcSlvApi;

    for(i = 0U; i < pApp->moduleCount; i++)
    {
        objIndex = (uint16_t)(0x3000U + i);

        error = SetObjEntryU16(
            ptSlave,
            objIndex,
            1U,
            pApp->moduleList[i].productCode);

        if(error != EC_API_eERR_NONE)
        {
            return error;
        }

        error = SetObjEntryU16(
            ptSlave,
            objIndex,
            2U,
            (uint16_t)pApp->moduleList[i].state);

        if(error != EC_API_eERR_NONE)
        {
            return error;
        }

        error = SetObjEntryU16(
            ptSlave,
            objIndex,
            3U,
            (uint16_t)pApp->moduleList[i].errorCode);

        if(error != EC_API_eERR_NONE)
        {
            return error;
        }

        error = SetObjEntryU16(
            ptSlave,
            objIndex,
            4U,
            pApp->moduleList[i].txPdoOffset);

        if(error != EC_API_eERR_NONE)
        {
            return error;
        }

        error = SetObjEntryU16(
            ptSlave,
            objIndex,
            5U,
            pApp->moduleList[i].txPdoLength);

        if(error != EC_API_eERR_NONE)
        {
            return error;
        }

        error = SetObjEntryU16(
            ptSlave,
            objIndex,
            6U,
            pApp->moduleList[i].rxPdoOffset);

        if(error != EC_API_eERR_NONE)
        {
            return error;
        }

        error = SetObjEntryU16(
            ptSlave,
            objIndex,
            7U,
            pApp->moduleList[i].rxPdoLength);

        if(error != EC_API_eERR_NONE)
        {
            return error;
        }
    }

    return EC_API_eERR_NONE;
}
/*-----------------------------------------------------------------------------------------
------
------     application specific functions
------
-----------------------------------------------------------------------------------------*/

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Callback for diagnosis example. It Sends a new Diagnosis message with the received data.
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pApplicationCtxt_p  application instance
 *  \param[in]  index_p                 object index
 *  \param[in]  subindex_p             object subIndex
 *  \param[in]  size_p                  size of data buffer
 *  \param[in]  pData_p                 buffer to be read from
 *  \param[in]  completeAccess_p     using complete access
 *  \return      CoE ErrorCode
 *
 *  \remarks
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP
 *
 * */
static uint8_t EC_SLV_APP_writeDiagnosis(
    void            *pApplicationCtxt_p,
    uint16_t         index_p,
    uint8_t          subindex_p,
    uint32_t         size_p,
    uint16_t MBXMEM *pData_p,
    uint8_t          completeAccess_p)
{
    EC_API_EError_t              error        = (EC_API_EError_t)ABORT_NOERROR;
    EC_SLV_APP_SS_Application_t *pAppInstance = NULL;
    EC_API_SLV_SDIAG_parameter_t param
        = { DEFTYPE_UNSIGNED8, EC_API_SLV_DIAG_MSG_PARAM_TYPE_DATA, sizeof(uint8_t), (uint8_t *)pData_p };

    OSALUNREF_PARM(completeAccess_p);

    if ((pApplicationCtxt_p != NULL) && (pData_p != NULL))
    {
        OSAL_printf(
            "%s ==> Idx: 0x%04x:%d | Size: %d | Value: %d\r\n",
            __func__,
            index_p,
            subindex_p,
            size_p,
            pData_p[0]);

        /* @cppcheck_justify{misra-c2012-11.5} generic API requires cast */
        /* cppcheck-suppress misra-c2012-11.5 */
        pAppInstance = (EC_SLV_APP_SS_Application_t *)pApplicationCtxt_p;
        EC_API_SLV_DIAG_newMessage(
            pAppInstance->ptEcSlvApi,
            EC_API_SLV_DIAG_CODE_EMCY(pData_p[0]),
            EC_API_SLV_DIAG_MSG_TYPE_ERROR,
            pData_p[0],
            1,
            &param);
    }
    return (uint8_t)error;
}

static EC_API_EError_t EC_SLV_APP_SS_populateSlaveInfo(
    EC_SLV_APP_SS_Application_t *pApplicationInstance_p)
{
    EC_API_EError_t       error = EC_API_eERR_INVALID;
    EC_API_SLV_SHandle_t *ptSubDevice;

    if (!pApplicationInstance_p)
    {
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }
    ptSubDevice = pApplicationInstance_p->ptEcSlvApi;

    error = (EC_API_EError_t)EC_API_SLV_setVendorId(ptSubDevice, ECAT_VENDORID);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setRevisionNumber(ptSubDevice, EC_REVISION);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setSerialNumber(ptSubDevice, 0x00000000);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setProductCode(ptSubDevice, ECAT_PRODUCTCODE_WEBSERVER);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setProductName(ptSubDevice, ECAT_PRODUCTNAME_WEBSERVER);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setGroupType(ptSubDevice, "EtherCAT Toolkit");
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setHwVersion(ptSubDevice, "R01");
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = ESL_setSWVersion(ptSubDevice);
    if (error != EC_API_eERR_NONE)
    {
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    /**********************************************************************
     * Initialize Module Database
     **********************************************************************/

    GS_APP_InitModuleTable(pApplicationInstance_p);

    error = GS_APP_UpdateModuleDatabase(
        pApplicationInstance_p);

    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf(
            "GS_APP_UpdateModuleDatabase Failed 0x%08X\r\n",
            error);

        goto Exit;
    }

    /* Former Project.h */
    
    error = (EC_API_EError_t)EC_API_SLV_setPDOSize(ptSubDevice, EC_MAX_PD_LEN, EC_MAX_PD_LEN);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setPDICfg(ptSubDevice, ESC_EE_PDI_CONTROL, ESC_EE_PDI_CONFIG);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setBootStrapMailbox(
        ptSubDevice,
        EC_BOOTSTRAP_MBXOUT_START,
        EC_BOOTSTRAP_MBXOUT_DEF_LENGTH,
        EC_BOOTSTRAP_MBXIN_START,
        EC_BOOTSTRAP_MBXIN_DEF_LENGTH);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setStandardMailbox(
        ptSubDevice,
        EC_MBXOUT_START,
        EC_MBXOUT_DEF_LENGTH,
        EC_MBXIN_START,
        EC_MBXIN_DEF_LENGTH);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setSyncManConfig(
        ptSubDevice,
        0,
        EC_MBXOUT_START,
        EC_MBXOUT_DEF_LENGTH,
        EC_MBXOUT_CONTROLREG,
        EC_MBXOUT_ENABLE);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setSyncManConfig(
        ptSubDevice,
        1,
        EC_MBXIN_START,
        EC_MBXIN_DEF_LENGTH,
        EC_MBXIN_CONTROLREG,
        EC_MBXIN_ENABLE);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setSyncManConfig(
        ptSubDevice,
        2,
        EC_OUTPUT_START,
        EC_OUTPUT_DEF_LENGTH,
        EC_OUTPUT_CONTROLREG,
        EC_OUTPUT_ENABLE);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_setSyncManConfig(
        ptSubDevice,
        3,
        EC_INPUT_START,
        EC_INPUT_DEF_LENGTH,
        EC_INPUT_CONTROLREG,
        EC_INPUT_ENABLE);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("%s:%d Error code: 0x%08x\r\n", __func__, __LINE__, error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    /* /Former Project.h */

    /**********************************************************************
     * Export Module Information Objects
     **********************************************************************/

    // GS_APP_CreateModuleTableObjects(pApplicationInstance_p);

    // error = GS_APP_UpdateModuleObjects(
    //     pApplicationInstance_p);

    // if (error != EC_API_eERR_NONE)
    // {
    //     OSAL_printf(
    //         "GS_APP_UpdateModuleObjects Failed 0x%08X\r\n",
    //         error);

    //     goto Exit;
    // }

    error = EC_API_eERR_NONE;
Exit:
    return error;
}

static EC_API_EError_t GS_EC_SLV_APP_SS_populateIOObjects(
    EC_SLV_APP_SS_Application_t *pApplicationInstance)
{
    EC_API_EError_t       error = EC_API_eERR_INVALID;
    EC_API_SLV_SHandle_t *ptSubDevice;

    const uint32_t objIndexInput  = 0x2200U;
    const uint32_t objIndexOutput = 0x2300U;

    uint16_t i;
    uint16_t ch;

    if(pApplicationInstance == NULL)
    {
        goto Exit;
    }

    ptSubDevice = pApplicationInstance->ptEcSlvApi;

    /* ========================================================= */
    /* Create IO Objects                                         */
    /* ========================================================= */

    for(i = 0U; i < MAX_NODES; i++)
    {
        char outputName[32];
        char inputName[32];

        snprintf(
            outputName,
            sizeof(outputName),
            "Output Slot %u",
            i + 1U);

        snprintf(
            inputName,
            sizeof(inputName),
            "Input Slot %u",
            i + 1U);

        /* ===================================================== */
        /* Create OUTPUT Record                                  */
        /* ===================================================== */

        error = (EC_API_EError_t)
            EC_API_SLV_CoE_odAddRecord(
                ptSubDevice,
                (uint16_t)(objIndexOutput + i),
                outputName,
                NULL,
                NULL,
                NULL,
                NULL,
                &pApplicationInstance->ptOutput[i]);

        if(error != EC_API_eERR_NONE)
        {
            OSAL_printf(
                "Create Output Object 0x%04X Error: 0x%08X\r\n",
                (uint16_t)(objIndexOutput + i),
                error);

            goto Exit;
        }

        /*
         * Configure Output Channels
         */
        for(ch = 0U; ch < NUM_SUB_INDEX_DATA; ch++)
        {
            char channelName[32];

            snprintf(
                channelName,
                sizeof(channelName),
                "Channel %u",
                ch + 1U);

            error = (EC_API_EError_t)
                EC_API_SLV_CoE_configRecordSubIndex(
                    ptSubDevice,
                    pApplicationInstance->ptOutput[i],
                    (uint8_t)(ch + 1U),
                    channelName,
                    DEFTYPE_UNSIGNED16,
                    16U,
                    ACCESS_READWRITE |
                    OBJACCESS_RXPDOMAPPING);

            if(error != EC_API_eERR_NONE)
            {
                OSAL_printf(
                    "Config Output 0x%04X:%u Error: 0x%08X\r\n",
                    (uint16_t)(objIndexOutput + i),
                    (uint8_t)(ch + 1U),
                    error);

                goto Exit;
            }
        }

        /* ===================================================== */
        /* Create INPUT Record                                   */
        /* ===================================================== */

        error = (EC_API_EError_t)
            EC_API_SLV_CoE_odAddRecord(
                ptSubDevice,
                (uint16_t)(objIndexInput + i),
                inputName,
                NULL,
                NULL,
                NULL,
                NULL,
                &pApplicationInstance->ptInput[i]);

        if(error != EC_API_eERR_NONE)
        {
            OSAL_printf(
                "Create Input Object 0x%04X Error: 0x%08X\r\n",
                (uint16_t)(objIndexInput + i),
                error);

            goto Exit;
        }

        /*
         * Configure Input Channels
         */
        for(ch = 0U; ch < NUM_SUB_INDEX_DATA; ch++)
        {
            char channelName[32];

            snprintf(
                channelName,
                sizeof(channelName),
                "Channel %u",
                ch + 1U);

            error = (EC_API_EError_t)
                EC_API_SLV_CoE_configRecordSubIndex(
                    ptSubDevice,
                    pApplicationInstance->ptInput[i],
                    (uint8_t)(ch + 1U),
                    channelName,
                    DEFTYPE_UNSIGNED16,
                    16U,
                    ACCESS_READ |
                    OBJACCESS_TXPDOMAPPING);

            if(error != EC_API_eERR_NONE)
            {
                OSAL_printf(
                    "Config Input 0x%04X:%u Error: 0x%08X\r\n",
                    (uint16_t)(objIndexInput + i),
                    (uint8_t)(ch + 1U),
                    error);

                goto Exit;
            }
        }
    }

    error = EC_API_eERR_NONE;

Exit:
    return error;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Create FSoE connection diagnosis object.
 *
 *  \details
 *  This object shows the usage of ENUM objects.
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pApplicationInstance_p  Application instance
 *  \return      ErrorCode
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  // required variables
 *  EC_API_EError_t  retVal = 0;
 *  EC_SLV_APP_Sapplication_t* pApplicationInstance_p;
 *
 *  // the Call
 *  retVal = EC_SLV_APP_populateFSoEObject(pApplicationInstance_p);
 *  \endcode
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP
 *
 * */
static EC_API_EError_t EC_SLV_APP_populateFSoEObject(EC_SLV_APP_SS_Application_t *pApplicationInstance)
{
    EC_API_EError_t       error   = EC_API_eERR_INVALID;
    EC_API_SLV_SHandle_t *ptSubDevice = NULL;

    if (!pApplicationInstance)
    {
        goto Exit;
    }

    ptSubDevice = pApplicationInstance->ptEcSlvApi;

    // Create ENUM object
    error = (EC_API_EError_t)EC_API_SLV_CoE_odAddEnum(
        ptSubDevice,
        SLS_OBJIDX_FSOE_CONN_STATE,
        &pApplicationInstance->pt0800EnumObj);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object SLS_OBJIDX_FSOE_CONN_STATE Error code: 0x%08x\r\n", error);
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_CoE_configEnum(
        ptSubDevice,
        pApplicationInstance->pt0800EnumObj,
        FSOE_CONN_ST_RESET,
        "Reset");
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0x0800 SubIndex 1 Error code: 0x%08x\r\n", error);
        goto Exit;
    }
    error = (EC_API_EError_t)EC_API_SLV_CoE_configEnum(
        ptSubDevice,
        pApplicationInstance->pt0800EnumObj,
        FSOE_CONN_ST_SESSION,
        "Session");
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0x0800 SubIndex 2 Error code: 0x%08x\r\n", error);
        goto Exit;
    }
    error = (EC_API_EError_t)EC_API_SLV_CoE_configEnum(
        ptSubDevice,
        pApplicationInstance->pt0800EnumObj,
        FSOE_CONN_ST_CONNECTION,
        "Connection");
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0x0800 SubIndex 3 Error code: 0x%08x\r\n", error);
        goto Exit;
    }
    error = (EC_API_EError_t)EC_API_SLV_CoE_configEnum(
        ptSubDevice,
        pApplicationInstance->pt0800EnumObj,
        FSOE_CONN_ST_PARAMETER,
        "Parameter");
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0x0800 SubIndex 4 Error code: 0x%08x\r\n", error);
        goto Exit;
    }
    error = (EC_API_EError_t)EC_API_SLV_CoE_configEnum(
        ptSubDevice,
        pApplicationInstance->pt0800EnumObj,
        FSOE_CONN_ST_DATA,
        "Data");
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0x0800 SubIndex 5 Error code: 0x%08x\r\n", error);
        goto Exit;
    }
    error = (EC_API_EError_t)EC_API_SLV_CoE_configEnum(
        ptSubDevice,
        pApplicationInstance->pt0800EnumObj,
        FSOE_CONN_ST_FAILSAFE,
        "FailSafe");
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0x0800 SubIndex 6 Error code: 0x%08x\r\n", error);
        goto Exit;
    }

    // Create FSoE Diagnosis object
    error = (EC_API_EError_t)EC_API_SLV_CoE_odAddRecord(
        ptSubDevice,
        SLS_OBJIDX_FSOE_DIG_IO_MODULE,
        "FSoE Digital IO Module",
        NULL,
        NULL,
        NULL,
        NULL,
        &pApplicationInstance->ptA000RecObj);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0xA000 Error code: 0x%08x\r\n", error);
        goto Exit;
    }

    error = (EC_API_EError_t)EC_API_SLV_CoE_configRecordSubIndex(
        ptSubDevice,
        pApplicationInstance->ptA000RecObj,
        1,
        "Connection State",
        SLS_OBJIDX_FSOE_CONN_STATE,
        16,
        ACCESS_READ);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0xA000 SubIndex 1 Error code: 0x%08x\r\n", error);
        goto Exit;
    }
    error = (EC_API_EError_t)EC_API_SLV_CoE_configRecordSubIndex(
        ptSubDevice,
        pApplicationInstance->ptA000RecObj,
        2,
        "Connection Diagnosis",
        DEFTYPE_UNSIGNED16,
        16,
        ACCESS_READ);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Object 0xA000 SubIndex 1 Error code: 0x%08x\r\n", error);
        goto Exit;
    }

    error = EC_API_eERR_NONE;
Exit:
    return error;
}

/*
 * ================================================================
 *  EtherCAT Modular IO Coupler Diagnostic Objects
 *
 *  Object Layout
 *
 *  0x2100 : Coupler Information
 *  0x2200 : Input Process Data
 *  0x2300 : Output Process Data
 *  0x2400 : Module Type Table
 *  0x2500 : Module Status Table
 *  0x2600 : Diagnostic Information
 *
 * ================================================================
 */
// static EC_API_EError_t GS_EC_SLV_APP_SS_populateDiagnosticObjects(
//     EC_SLV_APP_SS_Application_t *pApplicationInstance_p)
// {
//     EC_API_EError_t       error = EC_API_eERR_INVALID;
//     EC_API_SLV_SHandle_t *ptSubDevice;

//     uint16_t slot;

//     if (!pApplicationInstance_p)
//     {
//         goto Exit;
//     }

//     ptSubDevice = pApplicationInstance_p->ptEcSlvApi;

//     /* ========================================================= */
//     /* 0x2100 - Coupler Information                              */
//     /* ========================================================= */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_odAddRecord(
//             ptSubDevice,
//             0x2100,
//             "Coupler Information",
//             NULL,
//             NULL,
//             NULL,
//             NULL,
//             &pApplicationInstance_p->ptCoupler2100);

//     if (error != EC_API_eERR_NONE)
//     {
//         OSAL_printf(
//             "Create 0x2100 Error: 0x%08x\r\n",
//             error);

//         goto Exit;
//     }

//     /*
//      * SubIndex 1
//      * Installed Module Count
//      */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_configRecordSubIndex(
//             ptSubDevice,
//             pApplicationInstance_p->ptCoupler2100,
//             1,
//             "Installed Module Count",
//             DEFTYPE_UNSIGNED8,
//             8,
//             ACCESS_READ);

//     if (error != EC_API_eERR_NONE)
//     {
//         goto Exit;
//     }

//     /*
//      * SubIndex 2
//      * Input Process Data Size
//      */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_configRecordSubIndex(
//             ptSubDevice,
//             pApplicationInstance_p->ptCoupler2100,
//             2,
//             "Input Process Data Size (Bytes)",
//             DEFTYPE_UNSIGNED16,
//             16,
//             ACCESS_READ);

//     if (error != EC_API_eERR_NONE)
//     {
//         goto Exit;
//     }

//     /*
//      * SubIndex 3
//      * Output Process Data Size
//      */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_configRecordSubIndex(
//             ptSubDevice,
//             pApplicationInstance_p->ptCoupler2100,
//             3,
//             "Output Process Data Size (Bytes)",
//             DEFTYPE_UNSIGNED16,
//             16,
//             ACCESS_READ);

//     if (error != EC_API_eERR_NONE)
//     {
//         goto Exit;
//     }

//     /*
//      * SubIndex 4
//      * Coupler Status
//      */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_configRecordSubIndex(
//             ptSubDevice,
//             pApplicationInstance_p->ptCoupler2100,
//             4,
//             "Coupler Status",
//             DEFTYPE_UNSIGNED16,
//             16,
//             ACCESS_READ);

//     if (error != EC_API_eERR_NONE)
//     {
//         goto Exit;
//     }

//     /* ========================================================= */
//     /* 0x2400 - Module Type Table                                */
//     /* ========================================================= */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_odAddRecord(
//             ptSubDevice,
//             0x2400,
//             "Module Type Table",
//             NULL,
//             NULL,
//             NULL,
//             NULL,
//             &pApplicationInstance_p->ptModuleType2400);

//     if (error != EC_API_eERR_NONE)
//     {
//         OSAL_printf(
//             "Create 0x2400 Error: 0x%08x\r\n",
//             error);

//         goto Exit;
//     }

//     /*
//      * Create 20 module entries
//      */

//     for (slot = 0; slot < pApplicationInstance_p->moduleCount; slot++)
//     {
//         char name[64];

//         snprintf(name,
//                  sizeof(name),
//                  "%u",
//                  pApplicationInstance_p->moduleList[slot].productCode);

//         /*
//          * Product Code stored here
//          */

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                 pApplicationInstance_p->ptModuleType2400,
//                 slot + 1,
//                 name,
//                 DEFTYPE_UNSIGNED32,
//                 32,
//                 ACCESS_READ);

//         if (error != EC_API_eERR_NONE)
//         {
//             goto Exit;
//         }
//     }

//     /* ========================================================= */
//     /* 0x2500 - Module Status Table                              */
//     /* ========================================================= */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_odAddRecord(
//             ptSubDevice,
//             0x2500,
//             "Module Status Table",
//             NULL,
//             NULL,
//             NULL,
//             NULL,
//             &pApplicationInstance_p->ptModuleStatus2500);

//     if (error != EC_API_eERR_NONE)
//     {
//         OSAL_printf(
//             "Create 0x2500 Error: 0x%08x\r\n",
//             error);

//         goto Exit;
//     }

//     for (slot = 0; slot < pApplicationInstance_p->moduleCount; slot++)
//     {
//         char name[64];

//         snprintf(name,
//                  sizeof(name),
//                  "Slot %u Status",
//                  slot + 1);

//         /*
//          * Example Status:
//          *
//          * 0x0000 = OK
//          * 0x0001 = Module Missing
//          * 0x0002 = Configuration Error
//          * 0x0003 = Communication Error
//          */

//         error = (EC_API_EError_t)
//             EC_API_SLV_CoE_configRecordSubIndex(
//                 ptSubDevice,
//                 pApplicationInstance_p->ptModuleStatus2500,
//                 slot + 1,
//                 name,
//                 DEFTYPE_UNSIGNED16,
//                 16,
//                 ACCESS_READ);

//         if (error != EC_API_eERR_NONE)
//         {
//             goto Exit;
//         }
//     }

//     /* ========================================================= */
//     /* 0x2600 - Diagnostic Information                           */
//     /* ========================================================= */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_odAddRecord(
//             ptSubDevice,
//             0x2600,
//             "Diagnostic Information",
//             NULL,
//             NULL,
//             NULL,
//             NULL,
//             &pApplicationInstance_p->ptDiag2600);

//     if (error != EC_API_eERR_NONE)
//     {
//         OSAL_printf(
//             "Create 0x2600 Error: 0x%08x\r\n",
//             error);

//         goto Exit;
//     }

//     /*
//      * Diagnostic Counter
//      */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_configRecordSubIndex(
//             ptSubDevice,
//             pApplicationInstance_p->ptDiag2600,
//             1,
//             "Diagnostic Counter",
//             DEFTYPE_UNSIGNED32,
//             32,
//             ACCESS_READ);

//     if (error != EC_API_eERR_NONE)
//     {
//         goto Exit;
//     }

//     /*
//      * Last Error Code
//      */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_configRecordSubIndex(
//             ptSubDevice,
//             pApplicationInstance_p->ptDiag2600,
//             2,
//             "Last Error Code",
//             DEFTYPE_UNSIGNED32,
//             32,
//             ACCESS_READ);

//     if (error != EC_API_eERR_NONE)
//     {
//         goto Exit;
//     }

//     /*
//      * System Warning Flags
//      */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_configRecordSubIndex(
//             ptSubDevice,
//             pApplicationInstance_p->ptDiag2600,
//             3,
//             "Warning Flags",
//             DEFTYPE_UNSIGNED32,
//             32,
//             ACCESS_READ);

//     if (error != EC_API_eERR_NONE)
//     {
//         goto Exit;
//     }

//     /*
//      * System Error Flags
//      */

//     error = (EC_API_EError_t)
//         EC_API_SLV_CoE_configRecordSubIndex(
//             ptSubDevice,
//             pApplicationInstance_p->ptDiag2600,
//             4,
//             "Error Flags",
//             DEFTYPE_UNSIGNED32,
//             32,
//             ACCESS_READ);

//     if (error != EC_API_eERR_NONE)
//     {
//         goto Exit;
//     }

//     error = EC_API_eERR_NONE;

// Exit:
//     return error;
// }

static EC_API_EError_t GS_EC_SLV_APP_SS_populateRxPDO(
    EC_SLV_APP_SS_Application_t *pApplicationInstance_p)
{
    EC_API_SLV_SHandle_t       *ptSubDevice;
    EC_API_EError_t             error = EC_API_eERR_INVALID;
    EC_API_SLV_SCoE_ObjEntry_t *ptObjEntry;

    const uint16_t pdoIndex = 0x1600U;

    char entryName[32];

    if(pApplicationInstance_p == NULL)
    {
        goto Exit;
    }

    ptSubDevice = pApplicationInstance_p->ptEcSlvApi;

    /* ===================================================== */
    /* Create RxPDOs                                         */
    /* ===================================================== */

    for(uint8_t i = 0U; i < MAX_NODES; i++)
    {
        uint16_t objIndex;
        uint8_t  subIndex;

        /*
         * Create PDO
         */
        error = (EC_API_EError_t)
            EC_API_SLV_PDO_create(
                ptSubDevice,
                "RxPDO",
                (uint16_t)(pdoIndex + i),
                &pApplicationInstance_p->ptRxPdo[i]);

        if(error != EC_API_eERR_NONE)
        {
            OSAL_printf(
                "Create RxPDO 0x%04X Error: 0x%08X\r\n",
                (pdoIndex + i),
                error);

            goto Exit;
        }

        /*
         * Application Object Index
         */
        objIndex = (uint16_t)(0x2300U + i);

        /*
         * Create PDO Entries
         */
        for(subIndex = 1U;
            subIndex <= NUM_SUB_INDEX_DATA;
            subIndex++)
        {
            error = (EC_API_EError_t)
                EC_API_SLV_CoE_getObjectEntry(
                    ptSubDevice,
                    objIndex,
                    subIndex,
                    &ptObjEntry);

            if(error != EC_API_eERR_NONE)
            {
                OSAL_printf(
                    "Get Object 0x%04X:%u Error: 0x%08X\r\n",
                    objIndex,
                    subIndex,
                    error);

                goto Exit;
            }

            snprintf(
                entryName,
                sizeof(entryName),
                "Slot %u Ch %u",
                i + 1U,
                subIndex);

            error = (EC_API_EError_t)
                EC_API_SLV_PDO_createEntry(
                    ptSubDevice,
                    pApplicationInstance_p->ptRxPdo[i],
                    entryName,
                    ptObjEntry);

            if(error != EC_API_eERR_NONE)
            {
                OSAL_printf(
                    "PDO Entry Slot %u Ch %u Error: 0x%08X\r\n",
                    i + 1U,
                    subIndex,
                    error);

                goto Exit;
            }
        }
    }

    error = EC_API_eERR_NONE;

Exit:
    return error;
}

static EC_API_EError_t GS_EC_SLV_APP_SS_populateTxPDO(
    EC_SLV_APP_SS_Application_t *pApplicationInstance_p)
{
    EC_API_SLV_SHandle_t       *ptSubDevice;
    EC_API_EError_t             error = EC_API_eERR_INVALID;
    EC_API_SLV_SCoE_ObjEntry_t *ptObjEntry;

    const uint16_t pdoIndex = 0x1A00U;

    char entryName[32];

    if(pApplicationInstance_p == NULL)
    {
        goto Exit;
    }

    ptSubDevice = pApplicationInstance_p->ptEcSlvApi;

    /* ===================================================== */
    /* Create TxPDOs                                         */
    /* ===================================================== */

    for(uint8_t i = 0U; i < MAX_NODES; i++)
    {
        uint16_t objIndex;
        uint8_t  subIndex;

        /*
         * Create PDO
         */
        error = (EC_API_EError_t)
            EC_API_SLV_PDO_create(
                ptSubDevice,
                "TxPDO",
                (uint16_t)(pdoIndex + i),
                &pApplicationInstance_p->ptTxPdo[i]);

        if(error != EC_API_eERR_NONE)
        {
            OSAL_printf(
                "Create TxPDO 0x%04X Error: 0x%08X\r\n",
                (pdoIndex + i),
                error);

            goto Exit;
        }

        /*
         * Application Object Index
         */
        objIndex = (uint16_t)(0x2200U + i);

        /*
         * Create PDO Entries
         */
        for(subIndex = 1U;
            subIndex <= NUM_SUB_INDEX_DATA;
            subIndex++)
        {
            error = (EC_API_EError_t)
                EC_API_SLV_CoE_getObjectEntry(
                    ptSubDevice,
                    objIndex,
                    subIndex,
                    &ptObjEntry);

            if(error != EC_API_eERR_NONE)
            {
                OSAL_printf(
                    "Get Object 0x%04X:%u Error: 0x%08X\r\n",
                    objIndex,
                    subIndex,
                    error);

                goto Exit;
            }

            snprintf(
                entryName,
                sizeof(entryName),
                "Slot %u Ch %u",
                i + 1U,
                subIndex);

            error = (EC_API_EError_t)
                EC_API_SLV_PDO_createEntry(
                    ptSubDevice,
                    pApplicationInstance_p->ptTxPdo[i],
                    entryName,
                    ptObjEntry);

            if(error != EC_API_eERR_NONE)
            {
                OSAL_printf(
                    "PDO Entry Slot %u Ch %u Error: 0x%08X\r\n",
                    i + 1U,
                    subIndex,
                    error);

                goto Exit;
            }
        }
    }

    error = EC_API_eERR_NONE;

Exit:
    return error;
}

#if !(defined DPRAM_REMOTE) && !(defined FBTL_REMOTE)
/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Reset / UnReset Ethernet PHY
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pCtxt_p      application instance
 *  \param[in]  phyIdx_p     PHY index (0/1)
 *  \param[in]  reset_p      true: reset, false: run
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  // required variables
 *  void* pApp;
 *
 *  // the Call
 *  \endcode
 *  void EC_SLV_APP_SS_boardPhyReset(pApp, 0, true);
 *
 *  <!-- References: -->
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP
 *
 * */
static OSAL_FUNC_UNUSED void EC_SLV_APP_SS_boardPhyReset(void *pCtxt_p, uint8_t phyIdx_p, bool reset_p)
{
    /* @cppcheck_justify{misra-c2012-11.5} generic API requires cast */
    /* cppcheck-suppress misra-c2012-11.5 */
    EC_SLV_APP_SS_Application_t *pApplicationInstance = (EC_SLV_APP_SS_Application_t *)pCtxt_p;

    if (NULL == pApplicationInstance)
    {
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    ESL_BOARD_OS_phyReset(
        pApplicationInstance->gpioHandle,
        pApplicationInstance->selectedPruInstance,
        phyIdx_p,
        reset_p);

Exit:
    return;
}
#endif

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Stimulate board status LED callback
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pCallContext_p        application instance handle.
 *  \param[in]  runLed_p                true: run LED on, false: off
 *  \param[in]  errLed_p                true: error LED on, false: off
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  // required variables
 *  void* pAppL;
 *
 *  // the Call
 *  EC_SLV_APP_SS_appBoardStatusLed(pAppL, NULL, true, false);
 *  \endcode
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP
 *
 * */
static void EC_SLV_APP_SS_appBoardStatusLed(void *pCallContext_p, bool runLed_p, bool errLed_p)
{
    /* @cppcheck_justify{misra-c2012-11.5} generic API requires cast */
    /* cppcheck-suppress misra-c2012-11.5 */
    EC_SLV_APP_SS_Application_t *pApplicationInstance = (EC_SLV_APP_SS_Application_t *)pCallContext_p;

    if (NULL == pApplicationInstance)
    {
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    ESL_BOARD_OS_statusLED(
        pApplicationInstance->gpioHandle,
        pApplicationInstance->selectedPruInstance,
        runLed_p,
        errLed_p);

Exit:
    return;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Register board related functions
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pAppInstance_p        Application instance handle
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  // required variables
 *  EC_API_EError_t retVal;
 *
 *  retVal = EC_SLV_APP_SS_populateBoardFunctions *pAppInstance;
 *
 *  // the Call
 *  EC_SLV_APP_SS_initBoardFunctions(pAppInstance);
 *  \endcode
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP_WEBSERVER
 *
 * */
static EC_API_EError_t EC_SLV_APP_SS_populateBoardFunctions(
    EC_SLV_APP_SS_Application_t *pApplicationInstance)
{
    EC_API_EError_t error = EC_API_eERR_INVALID;

    if (!pApplicationInstance)
    {
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    EC_API_SLV_cbRegisterBoardStatusLed(
        pApplicationInstance->ptEcSlvApi,
        EC_SLV_APP_SS_appBoardStatusLed,
        pApplicationInstance);

    error = EC_API_eERR_NONE;
Exit:
    return error;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Initialize board related functions
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pAppInstance_p        Application instance handle
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  // required variables
 *  EC_SLV_APP_Application_t *pAppInstance;
 *
 *  // the Call
 *  EC_SLV_APP_SS_initBoardFunctions(pAppInstance);
 *  \endcode
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP_WEBSERVER
 *
 * */
void EC_SLV_APP_SS_initBoardFunctions(EC_SLV_APP_SS_Application_t *pAppInstance_p)
{
    if (!pAppInstance_p)
    {
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    /* open gpio instance */
    pAppInstance_p->gpioHandle = ESL_GPIO_init();

#if !(defined DPRAM_REMOTE) && !(defined FBTL_REMOTE)
    /* Configure manual MDIO mode workaround if configured in SysConfig or similar
     * This is to enable the TI workaround for errata i2329. The activation is
     * detected and provisioned in this call.
     */
    ESL_OS_manualMdioConfig(pAppInstance_p->ptEcSlvApi);

#if (!(defined SOC_AM263PX) && !(defined SOC_AM261X))
    /* configure Phy Reset Pin */
    ESL_BOARD_OS_configureResets(pAppInstance_p->gpioHandle, pAppInstance_p->selectedPruInstance);
#endif // !SOC_AM263PX && !SOC_AM261X
#else
    OSALUNREF_PARM(pAppInstance_p);
#endif
#if !(defined SOC_AM263PX)
    /* configure LED Pin */
    ESL_BOARD_OS_initStatusLED(pAppInstance_p->gpioHandle, pAppInstance_p->selectedPruInstance);
#endif //SOC_AM263PX

    ESL_GPIO_apply(pAppInstance_p->gpioHandle);

Exit:
    return;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Register board related functions, which do not use stack handle
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pAppInstance_p        Application instance handle
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  // required variables
 *  EC_SLV_APP_Application_t *pAppInstance;
 *
 *  // the Call
 *  EC_SLV_APP_registerStacklessBoardFunctions(pAppInstance);
 *  \endcode
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP_WEBSERVER
 *
 * */
void EC_SLV_APP_SS_registerStacklessBoardFunctions(EC_SLV_APP_SS_Application_t *pAppInstance_p)
{
    if (!pAppInstance_p)
    {
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

#if !(defined DPRAM_REMOTE) && !(defined FBTL_REMOTE)
    ESL_BOARD_OS_registerPhys(pAppInstance_p->ptEcSlvApi, pAppInstance_p->selectedPruInstance);

    CUST_PHY_CBregisterLibDetect(CUST_PHY_detect, pAppInstance_p);
#if (!(defined SOC_AM263PX) && !(defined SOC_AM261X))
    CUST_PHY_CBregisterReset(EC_SLV_APP_SS_boardPhyReset, pAppInstance_p);
#endif // !SOC_AM263PX && !SOC_AM261X
#endif
Exit:
    return;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Change on PDO assignment configuration
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pContext_p          The pointer to the EtherCAT API instance.
 *  \param[in]  pRxPdoAssignMap_p   pointer to SM2 PDO reconfigure assignments.
 *  \param[in]  pTxPdoAssignMap_p   pointer to SM3 PDO reconfigure assignments.
 *  \return     DTK error code
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP
 *
 * */
static uint32_t EC_SLAVE_APP_assignmentChangedHandler(
    void                                *pContext_p,
    EC_API_SLV_PDO_SReconfigAssignMap_t *pRxPdoAssignMap_p,
    EC_API_SLV_PDO_SReconfigAssignMap_t *pTxPdoAssignMap_p)
{
    uint32_t error = EC_API_eERR_NONE;
    OSALUNREF_PARM(pContext_p);

    OSAL_printf("**************************************\r\n");
    if (pRxPdoAssignMap_p->pdoAssignmentChanged)
    {
        if (pRxPdoAssignMap_p->pPdoIndexArray != NULL)
        {
            uint8_t idx;
            OSAL_printf("New assignments for SyncManager 2:\r\n");
            for (idx = 0; idx <pRxPdoAssignMap_p->pdoCount; idx++)
            {
                OSAL_printf("PDO: 0x%04x\r\n", pRxPdoAssignMap_p->pPdoIndexArray[idx]);
            }
        }
    }
    if (pTxPdoAssignMap_p->pdoAssignmentChanged)
    {
        if (pTxPdoAssignMap_p->pPdoIndexArray != NULL)
        {
            uint8_t idx;
            OSAL_printf("New assignments for SyncManager 3:\r\n");
            for (idx = 0; idx <pTxPdoAssignMap_p->pdoCount; idx++)
            {
                OSAL_printf("PDO: 0x%04x\r\n", pTxPdoAssignMap_p->pPdoIndexArray[idx]);
            }
        }
    }
    OSAL_printf("**************************************\r\n");

    if (pRxPdoAssignMap_p->pdoCount == 0u && pTxPdoAssignMap_p->pdoCount == 0u)
    {
        error = EC_API_eERR_ABORT;
    }
    return error;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Change on PDO mapping configuration
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]        pContext_p             The pointer to the EtherCAT API instance.
 *  \param[in]        pdoIndex_p             PDO index number.
 *  \param[in]        count_p                 Number of objects mapped as PDO.
 *  \param[in]        pPdoMap_p              PDO mapping entries.
 *  \return      DTK error code
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP
 *
 * */
static uint32_t EC_SLAVE_APP_mappingChangedHandler(
    void                       *pContext_p,
    uint16_t                    pdoIndex_p,
    uint8_t                     count_p,
    EC_API_SLV_PDO_SEntryMap_t *pPdoMap_p)
{
    uint32_t error = EC_API_eERR_NONE;
    OSALUNREF_PARM(pContext_p);
    if (pPdoMap_p != NULL)
    {
        uint8_t idx;
        OSAL_printf("**************************************\r\n");
        OSAL_printf("New mapping for PDO 0x%04x: \r\n", pdoIndex_p);
        for (idx = 0; idx < count_p; idx++)
        {
            OSAL_printf("0x%04x:%d:0x%02x\r\n", pPdoMap_p[idx].index, pPdoMap_p[idx].subIndex,pPdoMap_p[idx].size );
        }
        OSAL_printf("**************************************\r\n");
    }
    if (count_p == 0u)
    {
        error = EC_API_eERR_ABORT;
    }
    return error;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  AoE read request handler
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]        pContext_p              The pointer to the EtherCAT API instance.
 *  \param[in]        port_p                  AMS port.
 *  \param[in]        index_p                 16 bit index value from IndexOffset.
 *  \param[in]        subIndex_p              8 bit subIndex value from IndexOffset.
 *  \param[in]        completeAccess_p        CoE Complete Access flag.
 *  \param[in,out]    pLength_p               Request data length.
 *  \param[in]        pData_p                 Pointer to data.
 *  \return           ADS error code          32-bit error code
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP
 *
 * */
static uint32_t EC_SLV_APP_AoE_readRequest(
    void     *pContext_p,
    uint16_t  port_p,
    uint16_t  index_p,
    uint8_t   subIndex_p,
    bool      completeAccess_p,
    uint32_t *pLength_p,
    uint16_t *pData_p)
{
    uint32_t adsError = ERR_NOERROR;

    EC_API_SLV_SHandle_t       *pEcSlvApi    = (EC_API_SLV_SHandle_t *)pContext_p;
    EC_API_SLV_SCoE_ObjEntry_t *pObjectEntry = NULL;
    EC_API_SLV_SCoE_Object_t   *pObject      = NULL;
    uint32_t                    length       = 0;

    OSALUNREF_PARM(port_p);

    if ((NULL != pLength_p) && (NULL != pData_p))
    {
        uint32_t dtkError;
        if (completeAccess_p == false)
        {
            dtkError = EC_API_SLV_CoE_getObjectEntry(pEcSlvApi, index_p, subIndex_p, &pObjectEntry);
            if (dtkError == EC_API_eERR_NONE)
            {
                EC_API_SLV_CoE_getObjectEntryLength(pEcSlvApi, pObjectEntry, &length);
                if (length <= *pLength_p)
                {
                    *pLength_p = length;
                    dtkError
                        = EC_API_SLV_CoE_getObjectEntryData(pEcSlvApi, pObjectEntry, length, pData_p);
                    if (dtkError != EC_API_eERR_NONE)
                    {
                        adsError = ADSERR_DEVICE_ERROR;
                    }
                }
                else
                {
                    adsError = ADSERR_DEVICE_INVALIDSIZE;
                }
            }
            else
            {
                adsError = ADSERR_DEVICE_NOTFOUND;
            }
        }
        else
        {
            dtkError = EC_API_SLV_CoE_getObject(pEcSlvApi, index_p, &pObject);
            if (dtkError == EC_API_eERR_NONE)
            {
                if (subIndex_p == 0u)
                {
                    EC_API_SLV_CoE_getObjectLength(pEcSlvApi, pObject, &length);
                    if (length <= *pLength_p)
                    {
                        *pLength_p = length;
                        EC_API_SLV_CoE_getObjectEntryCount(pEcSlvApi, pObject, (uint8_t *)&pData_p[0]);
                        dtkError
                            = EC_API_SLV_CoE_getObjectData(pEcSlvApi, pObject, length, &pData_p[1]);
                        if (dtkError != EC_API_eERR_NONE)
                        {
                            adsError = ADSERR_DEVICE_ERROR;
                        }
                    }
                    else
                    {
                        adsError = ADSERR_DEVICE_INVALIDSIZE;
                    }
                }
                else if (subIndex_p == 1u)
                {
                    EC_API_SLV_CoE_getObjectLength(pEcSlvApi, pObject, &length);
                    if (length <= *pLength_p)
                    {
                        *pLength_p = length;
                        dtkError = EC_API_SLV_CoE_getObjectData(pEcSlvApi, pObject, length, pData_p);
                        if (dtkError != EC_API_eERR_NONE)
                        {
                            adsError = ADSERR_DEVICE_ERROR;
                        }
                    }
                    else
                    {
                        adsError = ADSERR_DEVICE_INVALIDSIZE;
                    }
                }
                else
                {
                    adsError = ADSERR_DEVICE_INVALIDPARM;
                }
            }
            else
            {
                adsError = ADSERR_DEVICE_NOTFOUND;
            }
        }
    }
    else
    {
        adsError = ADSERR_DEVICE_INVALIDPARM;
    }
    return adsError;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  AoE write request handler
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]        pContext_p             The pointer to the EtherCAT API instance.
 *  \param[in]        port_p                 AMS port.
 *  \param[in]        index_p                16 bit index value from IndexOffset.
 *  \param[in]        subIndex_p             8 bit subIndex value from IndexOffset.
 *  \param[in]        completeAccess_p       CoE Complete Access flag.
 *  \param[in,out]    pLength_p              Request data length.
 *  \param[in]        pData_p                Pointer to data.
 *  \return           ADS error code         32-bit error code
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP
 *
 * */
static uint32_t EC_SLV_APP_AoE_writeRequest(
    void     *pContext_p,
    uint16_t  port_p,
    uint16_t  index_p,
    uint8_t   subIndex_p,
    bool      completeAccess_p,
    uint32_t *pLength_p,
    uint16_t *pData_p)
{
    uint32_t adsError = ERR_NOERROR;

    EC_API_SLV_SHandle_t       *pEcSlvApi    = (EC_API_SLV_SHandle_t *)pContext_p;
    EC_API_SLV_SCoE_ObjEntry_t *pObjectEntry = NULL;
    EC_API_SLV_SCoE_Object_t   *pObject      = NULL;

    OSALUNREF_PARM(port_p);

    if (pData_p != NULL)
    {
        uint32_t dtkError;
        if (completeAccess_p == false)
        {
            dtkError = EC_API_SLV_CoE_getObjectEntry(pEcSlvApi, index_p, subIndex_p, &pObjectEntry);
            if (dtkError == EC_API_eERR_NONE)
            {
                dtkError
                    = EC_API_SLV_CoE_setObjectEntryData(pEcSlvApi, pObjectEntry, *pLength_p, pData_p);
                if (dtkError != EC_API_eERR_NONE)
                {
                    adsError = ADSERR_DEVICE_ERROR;
                }
            }
            else
            {
                adsError = ADSERR_DEVICE_NOTFOUND;
            }
        }
        else
        {
            dtkError = EC_API_SLV_CoE_getObject(pEcSlvApi, index_p, &pObject);
            if (dtkError == EC_API_eERR_NONE)
            {
                if (subIndex_p == 0u)
                {
                    dtkError = EC_API_SLV_CoE_setObjectData(
                        pEcSlvApi,
                        pObject,
                        subIndex_p,
                        *pLength_p,
                        &pData_p[1]);
                    if (dtkError != EC_API_eERR_NONE)
                    {
                        adsError = ADSERR_DEVICE_ERROR;
                    }
                }
                else if (subIndex_p == 1u)
                {
                    dtkError
                        = EC_API_SLV_CoE_setObjectData(pEcSlvApi, pObject, subIndex_p, *pLength_p, pData_p);
                    if (dtkError != EC_API_eERR_NONE)
                    {
                        adsError = ADSERR_DEVICE_ERROR;
                    }
                }
                else
                {
                    adsError = ADSERR_DEVICE_INVALIDPARM;
                }
            }
            else
            {
                adsError = ADSERR_DEVICE_NOTFOUND;
            }
        }
    }
    else
    {
        adsError = ADSERR_DEVICE_NOMEMORY;
    }
    return adsError;
}

void EC_SLV_APP_SS_applicationInit(EC_SLV_APP_SS_Application_t *pAppInstance_p)
{
    EC_API_EError_t error;

    if (!pAppInstance_p)
    {
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    // Initialize SDK
    pAppInstance_p->ptEcSlvApi = EC_API_SLV_new();
    if (!pAppInstance_p->ptEcSlvApi)
    {
        OSAL_error(__func__, __LINE__, OSAL_CONTAINER_NOMEMORY, true, 0);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }
    g_pApp = pAppInstance_p;

    error = EC_SLV_APP_SS_populateBoardFunctions(pAppInstance_p);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Populate board functions Error code: 0x%08x\r\n", error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    error = EC_SLV_APP_SS_populateSlaveInfo(pAppInstance_p);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Create SubDevice Info Error code: 0x%08x\r\n", error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    /////////////////////////////////////////////////////////
    //////////  Generate application OBD                /////////
    /////////////////////////////////////////////////////////

    /* Creation of Object Data */
    error = GS_EC_SLV_APP_SS_populateIOObjects(pAppInstance_p);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Create IO Object Record Error code: 0x%08x\r\n", error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    // error = GS_EC_SLV_APP_SS_populateDiagnosticObjects(pAppInstance_p);
    // if (error != EC_API_eERR_NONE)
    // {
    //     OSAL_printf("Create Diagnostic Object Error code: 0x%08x\r\n", error);
    //     /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
    //     /* cppcheck-suppress misra-c2012-15.1 */
    //     goto Exit;
    // }

    error = EC_SLV_APP_populateFSoEObject(pAppInstance_p);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Create FSoE connection state object error code: 0x%08x\r\n", error);
        goto Exit;
    }

    /////////////////////////////////////////////////////////
    //////////          Define Application PDOs         /////////
    /////////////////////////////////////////////////////////

    /////////////////////////////////////
    // Output PDO (master to SubDevice comm)
    /////////////////////////////////////

    error = GS_EC_SLV_APP_SS_populateRxPDO(pAppInstance_p);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Create RX PDO Error code: 0x%08x\r\n", error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    /////////////////////////////////////
    // Input PDO (SubDevice to master comm)
    /////////////////////////////////////
    error = GS_EC_SLV_APP_SS_populateTxPDO(pAppInstance_p);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("Create TX PDO Error code: 0x%08x\r\n", error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    /*PDO Mapping Changes*/
    EC_API_SLV_PDO_registerAssignmentChanges(
        pAppInstance_p->ptEcSlvApi,
        EC_SLAVE_APP_assignmentChangedHandler,
        pAppInstance_p);
    EC_API_SLV_PDO_registerMappingChanges(
        pAppInstance_p->ptEcSlvApi,
        EC_SLAVE_APP_mappingChangedHandler,
        pAppInstance_p);

    /*AoE*/
    EC_API_SLV_AoE_cbRegisterReadRequestHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_AoE_readRequest,
        pAppInstance_p->ptEcSlvApi);
    EC_API_SLV_AoE_cbRegisterWriteRequestHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_AoE_writeRequest,
        pAppInstance_p->ptEcSlvApi);
    /*EoE*/

    EC_API_SLV_EoE_cbRegisterReceiveHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_EoE_SS_receiveHandler,
        pAppInstance_p);
    EC_API_SLV_EoE_cbRegisterSettingIndHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_EoE_SS_settingIndHandler,
        pAppInstance_p);

    /*FoE*/
    EC_API_SLV_FoE_cbRegisterOpenFileHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_FoE_fileOpen,
        pAppInstance_p->ptEcSlvApi);
    EC_API_SLV_FoE_cbRegisterReadFileHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_FoE_fileRead,
        pAppInstance_p->ptEcSlvApi);
    EC_API_SLV_FoE_cbRegisterWriteFileHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_FoE_fileWrite,
        pAppInstance_p->ptEcSlvApi);
    EC_API_SLV_FoE_cbRegisterCloseFileHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_FoE_fileClose,
        pAppInstance_p->ptEcSlvApi);

    EC_API_SLV_FoE_cbRegisterStartBLHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_FoE_startBL,
        pAppInstance_p->ptEcSlvApi);
    EC_API_SLV_FoE_cbRegisterStopBLHandler(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_FoE_stopBL,
        pAppInstance_p->ptEcSlvApi);

    /*Diagnosis support */
    EC_API_SLV_DIAG_enable(pAppInstance_p->ptEcSlvApi);

#if !(defined DPRAM_REMOTE) && !(defined FBTL_REMOTE)                                              \
    && !(defined OSAL_FREERTOS_JACINTO) /* first omit flash */
    EC_API_SLV_EEPROM_cbRegisterInit(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_EEP_init,
        pAppInstance_p->ptEcSlvApi);
    /* @cppcheck_justify{misra-c2012-11.6} void cast required for signature */
    /* cppcheck-suppress misra-c2012-11.6 */
    EC_API_SLV_EEPROM_cbRegisterWrite(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_EEP_write, EEPROM_MAGIC_KEY);
    /* @cppcheck_justify{misra-c2012-11.6} void cast required for signature */
    /* cppcheck-suppress misra-c2012-11.6 */
    EC_API_SLV_EEPROM_cbRegisterRead(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_EEP_read, EEPROM_MAGIC_KEY);
#endif
    error = EC_API_SLV_cbRegisterUserApplicationRun(
        pAppInstance_p->ptEcSlvApi,
        EC_SLV_APP_SS_applicationRun,
        pAppInstance_p);

    error = (EC_API_EError_t)EC_API_SLV_init(pAppInstance_p->ptEcSlvApi);
    if (error != EC_API_eERR_NONE)
    {
        OSAL_printf("SubDevice Init Error Code: 0x%08x\r\n", error);
        /* @cppcheck_justify{misra-c2012-15.1} goto is used to assure single point of exit */
        /* cppcheck-suppress misra-c2012-15.1 */
        goto Exit;
    }

    pAppInstance_p->state   = EC_API_SLV_eESM_init;
    pAppInstance_p->msec    = 0;
    pAppInstance_p->trigger = 1000; /* 1000 ms */

    pAppInstance_p->prev = ESL_OS_clockGet();

    error = EC_API_SLV_run(pAppInstance_p->ptEcSlvApi);
    if (EC_API_eERR_NONE != error)
    {
      OSAL_printf("%s:%d:0x%x\r\n", __func__, __LINE__, error);
    }

Exit:
    return;
}

/// \cond DO_NOT_DOCUMENT
#if (defined SHOW_ESCSTATUS) && (SHOW_ESCSTATUS == 1)
static void EC_SLV_APP_escStatusAnalysis(EC_SLV_APP_SS_Application_t *pAppInstance_p)
{
    static uint16_t lastPortState = 0;
    uint16_t        portState;

    static uint16_t lastCounters[8] = { 0 };
    uint16_t        counters[8]     = { 0 };
    uint8_t         cntIdx          = 0;
    uint32_t        error;

    error = EC_API_SLV_readWordEscRegister(pAppInstance_p->ptEcSlvApi, 0x110, &portState);
    if (EC_API_eERR_NONE != error)
    {
        OSAL_printf("%s:%d:E=0x%x\r\n", __func__, __LINE__, error);
    }
    if (lastPortState != portState)
    {
        OSAL_printf("PortState 0x%04x->0x%04x\r\n", lastPortState, portState);
    }
    lastPortState = portState;

    for (cntIdx = 0; cntIdx < 0x0e; cntIdx += 2)
    {
        error = EC_API_SLV_readWordEscRegister(
            pAppInstance_p->ptEcSlvApi,
            (0x300 | cntIdx),
            &counters[cntIdx >> 1]);

        if (EC_API_eERR_NONE != error)
        {
            OSAL_printf("%s:%d:E=0x%x\r\n", __func__, __LINE__, error);
        }
    }

    if (0 != OSAL_MEMORY_memcmp(counters, lastCounters, sizeof(counters)))
    {
        for (cntIdx = 0; cntIdx < 8; ++cntIdx)
        {
            OSAL_printf("Counter 0x%04x: %04x\r\n", 0x300 | (cntIdx * 2), counters[cntIdx]);
        }
    }
    OSAL_MEMORY_memcpy(lastCounters, counters, sizeof(counters));
}
#endif
/// \endcond
static void EC_SLV_APP_SS_applicationRun(void *pAppCtxt_p)
{
    /* @cppcheck_justify{misra-c2012-11.5} generic API requires cast */
    /* cppcheck-suppress misra-c2012-11.5 */
    EC_SLV_APP_SS_Application_t *pApplicationInstance = (EC_SLV_APP_SS_Application_t *)pAppCtxt_p;
    /* @cppcheck_justify{threadsafety-threadsafety} thread is not reentrant */
    /* cppcheck-suppress threadsafety-threadsafety */
    static uint8_t lastLed = 0;
    /* @cppcheck_justify{threadsafety-threadsafety} thread is not reentrant */
    /* cppcheck-suppress threadsafety-threadsafety */
    static EC_API_SLV_EEsmState_t lastState = EC_API_SLV_eESM_uninit; /* last known stack state to
                                                                         notify changes */
    EC_API_SLV_EEsmState_t curState = EC_API_SLV_eESM_uninit;         /* current stack state */

    /* @cppcheck_justify{threadsafety-threadsafety} thread is not reentrant */
    /* cppcheck-suppress threadsafety-threadsafety */
    uint16_t        alErrorCode = 0;
    uint32_t        error;

#if (defined SHOW_LOOPCOUNT) && (SHOW_LOOPCOUNT == 1)
    static uint32_t loops = 0;
#endif

    if (NULL == pApplicationInstance)
    {
        OSAL_error(__func__, __LINE__, OSAL_ERR_InvalidState, true, 1, "App Instance = NULL");
    }
    else
    {
#if (defined ENABLE_I2CLEDS) && (ENABLE_I2CLEDS == 1)
        /* be sure we own I2C in this thread */
        if (NULL == pApplicationInstance->ioexpLedHandle)
        {
            pApplicationInstance->ioexpLedHandle = ESL_OS_ioexp_leds_init();
        }
#endif

        error = EC_API_SLV_getState(pApplicationInstance->ptEcSlvApi, &curState, &alErrorCode);
        if (EC_API_eERR_NONE != error)
        {
            OSAL_printf("%s:%d:E=0x%x\r\n", __func__, __LINE__, error);
        }

        if (curState != lastState)
        {
            DebugP_log("Current State = %d\r\n", curState);
            OSAL_printf("State change: 0x%x -> 0x%x\r\n", lastState, curState);
            pApplicationInstance->prev = ESL_OS_clockGet();
        }
        
        lastState = curState;

        if (EC_API_SLV_eESM_op == curState)
        {
            uint32_t pdOutLen;
            uint32_t pdInLen;

            EC_API_SLV_getOutputProcDataLength(
                pApplicationInstance->ptEcSlvApi, &pdOutLen);

            EC_API_SLV_getInputProcDataLength(
                pApplicationInstance->ptEcSlvApi, &pdInLen);

            pdOutLen = BIT2BYTE(pdOutLen);
            pdInLen = BIT2BYTE(pdInLen);
            
            error = EC_API_SLV_getOutputData(
                pApplicationInstance->ptEcSlvApi,
                pdOutLen,
                pdBuffer_output);

            if (EC_API_eERR_NONE != error)
            {
                OSAL_printf(
                    "%s:%d:E=0x%x\r\n",
                    __func__,
                    __LINE__,
                    error);
            }

            GS_APP_SyncOutputProcessImage();

            GS_APP_SyncInputProcessImage();

            error = EC_API_SLV_setInputData(
                pApplicationInstance->ptEcSlvApi,
                pdInLen,
                pdBuffer_input);

            if (EC_API_eERR_NONE != error)
            {
                OSAL_printf(
                    "%s:%d:E=0x%x\r\n",
                    __func__,
                    __LINE__,
                    error);
            }

#if 0
            uint16_t Offset = 0;
            error = EC_API_SLV_PDO_getOffset(
                pApplicationInstance->ptEcSlvApi,
                pApplicationInstance->ptRxPdo[0],
                &Offset);
            if (EC_API_eERR_NONE != error)
            {
                OSAL_printf("%s:%d:E=0x%x\r\n", __func__, __LINE__, error);
            }
            OSAL_printf("Offset = %d\r\n", Offset);
            uint16_t PData = pdBuffer_output[Offset] | (pdBuffer_output[Offset+1] << 8);
            OSAL_printf("PData = %d\r\n", PData);
#endif
        }
        else
        {
            clock_t now = ESL_OS_clockGet();

            if (pApplicationInstance->prev > now)
            {
                pApplicationInstance->prev = now;
            }
            pApplicationInstance->diff = ESL_OS_clockDiff(pApplicationInstance->prev, &now);

            if (pApplicationInstance->ioexpLedHandle && pApplicationInstance->diff)
            {
#if (defined SHOW_LOOPCOUNT) && (SHOW_LOOPCOUNT == 1)
                OSAL_printf("LoopCount 0x%08x\r\n", loops);
#endif

#if (defined ENABLE_I2CLEDS) && (ENABLE_I2CLEDS == 1)
                ESL_OS_ioexp_leds_write(pApplicationInstance->ioexpLedHandle, lastLed);
#else
                OSAL_printf("Update I2C to 0x%x\r\n", lastLed);
#endif

                pApplicationInstance->prev = now;

                if (0u == lastLed)
                {
                    lastLed = 1u;
                }
                else
                {
                    lastLed = (lastLed << 1u);
                }
            }
        }

#if (defined SHOW_ESCSTATUS) && (SHOW_ESCSTATUS == 1)
        EC_SLV_APP_escStatusAnalysis(pApplicationInstance);
#endif
#if (defined SHOW_LOOPCOUNT) && (SHOW_LOOPCOUNT == 1)
        loops++;
#endif

    }
}
