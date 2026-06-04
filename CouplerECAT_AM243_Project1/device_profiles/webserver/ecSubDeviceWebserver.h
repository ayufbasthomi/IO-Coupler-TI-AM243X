/*!
 *  \example ecSubDeviceWebserver.h
 *
 *  \brief
 *  EtherCAT<sup>&reg;</sup> SubDevice Example interface.
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

#if !(defined PROTECT_ECSSUBDEVICEWEBSERVER_H)
#define PROTECT_ECSUBDEVICEWEBSERVER_H 1

#include <osal.h>
#include "ecSlvApi.h"

#include <ESL_os.h>
#include "mcan_loopback_interrupt.h"
/*-----------------------------------------------------------------------------------------
------
------    Includes
------
-----------------------------------------------------------------------------------------*/

// typedef struct EC_SLV_APP_SS_application
// {
//     uint32_t                        selectedPruInstance;

//     /* Threads */
//     TaskP_Object                    mainThreadHandle;
//     TaskP_Params                    mainThreadParam;
//     void*                           loopThreadHandle;
//     void*                           webServerThreadHandle;

//     /* Resources */
//     void*                           gpioHandle;
//     void*                           remoteHandle;
//     void*                           ioexpLedHandle;

//     int32_t                         msec;
//     int32_t                         trigger;

//     uint8_t                         state;
//     uint8_t                         rsvd[3]; /* better be uint32_t aligned */
//     clock_t                         prev;
//     clock_t                         diff;

//     EC_API_SLV_SCoE_Object_t*       ptRecObjOut;
//     EC_API_SLV_SCoE_Object_t*       pt2002RecObj;
//     EC_API_SLV_SCoE_Object_t*       pt2007RecObj;
//     EC_API_SLV_SCoE_Object_t*       pt200FRecObj;
//     EC_API_SLV_SCoE_Object_t*       ptA000RecObj;
//     EC_API_SLV_SCoE_Object_t*       pt0800EnumObj;

//     EC_API_SLV_Pdo_t*               ptRxPdo1600;
//     EC_API_SLV_Pdo_t*               ptRxPdo1601;
//     EC_API_SLV_Pdo_t*               ptTxPdo1A00;
//     EC_API_SLV_Pdo_t*               ptTxPdo1A01;

//     EC_API_SLV_SHandle_t*           ptEcSlvApi;

//     uint8_t                         pdBuffer[128];
// } EC_SLV_APP_SS_Application_t;

#define GS_MODULE_NAME_LENGTH      (32U)
#define GS_DIAG_TEXT_LENGTH        (96U)

#define IO_ANALOG_MODULE_BITS   (128U)
#define IO_DIGITAL_MODULE_BITS  (16U)

#define UNKNOWN 0
#define DO      1
#define DI      2
#define AI_C    3
#define AI_V    4
#define AO_C    5
#define AO_V    6
#define RTDY    7
#define RTDB    8
/* ============================= */
/* EtherCAT Process Data Buffers */
/* ============================= */
#define ECAT_PD_SIZE   (MAX_NODES * MAX_ANALOG_CH * 2) 

uint8_t pdBuffer_input[ECAT_PD_SIZE];
uint8_t pdBuffer_output[ECAT_PD_SIZE];

/* Module diagnostic state */
typedef enum
{
    GS_MODULE_STATE_OK = 0,
    GS_MODULE_STATE_WARNING,
    GS_MODULE_STATE_ERROR,
    GS_MODULE_STATE_OFFLINE
} GS_ModuleState_e;

/* Module information for web UI / diagnostics / object dictionary */
typedef struct GS_ModuleInfo
{
    uint16_t            productCode;
    uint16_t            slot;

    char                moduleName[GS_MODULE_NAME_LENGTH];

    uint8_t             inputSize;
    uint8_t             outputSize;

    uint16_t            txPdoOffset;
    uint16_t            txPdoLength;

    uint16_t            rxPdoOffset;
    uint16_t            rxPdoLength;

    GS_ModuleState_e    state;

    uint16_t            errorCode;

    char                diagnosticText[GS_DIAG_TEXT_LENGTH];

    bool                modulePresent;
} GS_ModuleInfo_t;

/* System diagnostic summary */
typedef struct GS_SystemDiagnostic
{
    uint16_t totalModules;
    uint16_t onlineModules;
    uint16_t errorModules;
    uint16_t warningModules;

    bool     systemError;
} GS_SystemDiagnostic_t;

typedef struct EC_SLV_APP_SS_application
{
    uint32_t                        selectedPruInstance;

    /************************************************************/
    /* Threads                                                  */
    /************************************************************/
    TaskP_Object                    mainThreadHandle;
    TaskP_Params                    mainThreadParam;

    void*                           loopThreadHandle;
    void*                           webServerThreadHandle;

    /************************************************************/
    /* Resources                                                */
    /************************************************************/
    void*                           gpioHandle;
    void*                           remoteHandle;
    void*                           ioexpLedHandle;

    /************************************************************/
    /* Runtime                                                  */
    /************************************************************/
    int32_t                         msec;
    int32_t                         trigger;

    uint8_t                         state;
    uint8_t                         rsvd[3];

    clock_t                         prev;
    clock_t                         diff;
    
    /************************************************************/
    /* EtherCAT Object Dictionary                               */
    /************************************************************/

    EC_API_SLV_SCoE_Object_t*       ptA000RecObj;
    EC_API_SLV_SCoE_Object_t*       pt0800EnumObj;

    // /*
    //  * 0x2100 : Coupler Information
    //  */
    // EC_API_SLV_SCoE_Object_t*       ptCoupler2100;

    /*
     * 0x2200 : Input Process Data
     */
    EC_API_SLV_SCoE_Object_t*       ptInput[MAX_NODES];

    /*
     * 0x2300 : Output Process Data
     */
    EC_API_SLV_SCoE_Object_t*       ptOutput[MAX_NODES];

    // /*
    //  * 0x2400 : Module Type Table
    //  */
    // EC_API_SLV_SCoE_Object_t*       ptModuleType2400;

    // /*
    //  * 0x2500 : Module Status Table
    //  */
    // EC_API_SLV_SCoE_Object_t*       ptModuleStatus2500;

    // /*
    //  * 0x2600 : Diagnostic Information
    //  */
    // EC_API_SLV_SCoE_Object_t*       ptDiag2600;

    // /*
    // * 0x3000 + slot : Module Runtime Information
    // *
    // * SubIndex:
    // * 1 -> Module ID
    // * 2 -> State
    // * 3 -> Error Code
    // * 4 -> TX PDO Offset
    // * 5 -> TX PDO Length
    // * 6 -> RX PDO Offset
    // * 7 -> RX PDO Length
    // */
    // EC_API_SLV_SCoE_Object_t*       ptModuleInfo3000[MAX_NODES];

    /************************************************************/
    /* PDO Handles                                              */
    /************************************************************/
    EC_API_SLV_Pdo_t*               ptRxPdo[MAX_NODES];

    EC_API_SLV_Pdo_t*               ptTxPdo[MAX_NODES];
    /************************************************************/
    /* EtherCAT Slave API                                       */
    /************************************************************/
    EC_API_SLV_SHandle_t*           ptEcSlvApi;

    /************************************************************/
    /* Modular IO Information                                   */
    /************************************************************/
    uint16_t                        moduleCount;

    GS_ModuleInfo_t                 moduleList[MAX_NODES];

    GS_SystemDiagnostic_t           systemDiag;

    /************************************************************/
    /* Webserver / UI State                                     */
    /************************************************************/
    // bool                            webDataUpdated;
    // bool                            diagnosticChanged;

    // uint32_t                        lastDiagnosticUpdateMs;
} EC_SLV_APP_SS_Application_t;

#if (defined __cplusplus)
extern "C" {
#endif

extern void EC_SLV_APP_SS_initBoardFunctions             (EC_SLV_APP_SS_Application_t*     pAppInstance_p);
extern void EC_SLV_APP_SS_registerStacklessBoardFunctions(EC_SLV_APP_SS_Application_t*     pAppInstance_p);
extern void EC_SLV_APP_SS_applicationInit                (EC_SLV_APP_SS_Application_t*     pAppInstance_p);

#if (defined __cplusplus)
}
#endif

#endif /* PROTECT_ECSUBDEVICEIMPLE_H */
