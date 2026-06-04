/*
 *  Copyright (C) 2021 Texas Instruments Incorporated
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

/* This example demonstrates the CAN message transmission and reception in
 * digital loop back mode with the following configuration.
 *
 * CAN FD Message Format.
 * Message ID Type is Standard, Msg Id 0xC0.
 * MCAN is configured in Interrupt Mode.
 * MCAN Interrupt Line Number 0.
 * Arbitration Bit Rate 1Mbps.
 * Data Bit Rate 5Mbps.
 * Buffer mode is used for Tx and RX to store message in message RAM.
 *
 * Message is transmitted and received back internally using internal loopback
 * mode. When the received message id and the data matches with the transmitted
 * one, then the example is completed.
 *
 */

#include <stdio.h>
#include <string.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/AddrTranslateP.h>
#include <kernel/dpl/SemaphoreP.h>
#include <drivers/mcan.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"

#define APP_MCAN_BASE_ADDR                       (CONFIG_MCAN0_BASE_ADDR)

/* Allocate Message RAM memory section to filter elements, buffers, FIFO */
/* Maximum STD Filter Element can be configured is 128 */
#define APP_MCAN_STD_ID_FILTER_CNT               (1U)
/* Maximum EXT Filter Element can be configured is 64 */
#define APP_MCAN_EXT_ID_FILTER_CNT               (0U)
/* Maximum TX Buffer + TX FIFO, combined can be configured is 32 */
#define APP_MCAN_TX_BUFF_CNT                     (32U)
#define APP_MCAN_TX_FIFO_CNT                     (0U)
/* Maximum TX Event FIFO can be configured is 32 */
#define APP_MCAN_TX_EVENT_FIFO_CNT               (0U)
/* Maximum RX FIFO 0 can be configured is 64 */
#define APP_MCAN_FIFO_0_CNT                      (64U)
/* Maximum RX FIFO 1 can be configured is 64 and
 * rest of the memory is allocated to RX buffer which is again of max size 64 */
#define APP_MCAN_FIFO_1_CNT                      (0U)

/* Standard Id configured in this app */
#define APP_MCAN_STD_ID                          (0x01U)
#define APP_MCAN_STD_ID_MASK                     (0x7FFU)
#define APP_MCAN_STD_ID_SHIFT                    (18U)

#define APP_MCAN_EXT_ID_MASK                     (0x1FFFFFFFU)

/* Semaphore to indicate transfer completion */
static SemaphoreP_Object gMcanTxDoneSem, gMcanRxDoneSem;
static uint32_t          gMcanBaseAddr;

/* Static Function Declarations */
static void    App_mcanConfig();
static void    App_mcanInitMsgRamConfigParams(
               MCAN_MsgRAMConfigParams *msgRAMConfigParams);
static void    App_mcanCompareMsg(MCAN_TxBufElement *txMsg,
                                  MCAN_RxBufElement *rxMsg);

void mcan_recv_task(void *args)
{
    int32_t                 status = SystemP_SUCCESS;
    MCAN_TxBufElement       txMsg;
    MCAN_RxBufElement       rxMsg;
    uint32_t                i = 0U;

    /* Open drivers to open the UART driver for console */
    Drivers_open();
    Board_driversOpen();

    DebugP_log("[MCAN_RECVTASK] MCAN Recv Task started 1...\r\n");

    /* Construct Tx/Rx Semaphore objects */
    status = SemaphoreP_constructBinary(&gMcanTxDoneSem, 0);
    DebugP_assert(SystemP_SUCCESS == status);
    status = SemaphoreP_constructBinary(&gMcanRxDoneSem, 0);
    DebugP_assert(SystemP_SUCCESS == status);

    /* Assign MCAN instance address */
    gMcanBaseAddr = (uint32_t) AddrTranslateP_getLocalAddr(APP_MCAN_BASE_ADDR);

    /* Configure MCAN module */
    App_mcanConfig();

    int count_read = 1;
    char data[2*8 + 1];
    MCAN_RxFIFOStatus fifoStatus;
    
    while(1){
        fifoStatus.num = MCAN_RX_FIFO_NUM_0;
        MCAN_getRxFIFOStatus(gMcanBaseAddr, &fifoStatus);
        
        if (fifoStatus.fillLvl > 0)
        {
            MCAN_readMsgRam(
                gMcanBaseAddr,
                MCAN_MEM_TYPE_FIFO,
                fifoStatus.getIdx,
                fifoStatus.num,
                &rxMsg
            );

            MCAN_writeRxFIFOAck(
                gMcanBaseAddr,
                fifoStatus.num,
                fifoStatus.getIdx
            );
            
            uint8_t dlc = rxMsg.dlc;
            uint16_t id = (rxMsg.id >> APP_MCAN_STD_ID_SHIFT) & 0x7FF;

            if (dlc > 8 || dlc == 0){
                continue;
            }

            for (int i = 0; i < dlc; i++)
            {
                sprintf(&data[i*2], "%02x", rxMsg.data[i]);
            }

            data[dlc*2] = '\0';

            DebugP_log("[MCAN] Recv no:%d id: %d dlc %d data: 0x%s\r\n",
                    count_read,
                    id,
                    dlc,
                    data);

            count_read++;
        }
    }

    SemaphoreP_destruct(&gMcanTxDoneSem);
    SemaphoreP_destruct(&gMcanRxDoneSem);

    DebugP_log("APP Stopped!\r\n");

    Board_driversClose();
    Drivers_close();

    return;
}

static void App_mcanConfig()
{
    MCAN_StdMsgIDFilterElement stdFiltElem[APP_MCAN_STD_ID_FILTER_CNT] = {0U};
    MCAN_InitParams            initParams = {0U};
    MCAN_ConfigParams          configParams = {0U};
    MCAN_MsgRAMConfigParams    msgRAMConfigParams = {0U};
    MCAN_BitTimingParams       bitTimes = {0U};
    uint32_t                   i;

    /* Initialize MCAN module initParams */
    MCAN_initOperModeParams(&initParams);
    /* CAN FD Mode and Bit Rate Switch Enabled */
    initParams.fdMode          = FALSE;
    initParams.brsEnable       = FALSE;

    /* Initialize MCAN module Global Filter Params */
    MCAN_initGlobalFilterConfigParams(&configParams);
    configParams.filterConfig.anfe = 0;
    configParams.filterConfig.anfs = 0;
    configParams.filterConfig.rrfe = 1;
    configParams.filterConfig.rrfs = 1;
    
    /* Initialize MCAN module Bit Time Params */
    /* Configuring default 1Mbps and 5Mbps as nominal and data bit-rate resp */
    MCAN_initSetBitTimeParams(&bitTimes);

    /* Initialize MCAN module Message Ram Params */
    App_mcanInitMsgRamConfigParams(&msgRAMConfigParams);

    /* Initialize Filter element to receive msg, should be same as tx msg id */
    stdFiltElem[0].sfid1 = APP_MCAN_STD_ID;
    stdFiltElem[0].sfid2 = APP_MCAN_STD_ID_MASK;
    stdFiltElem[0].sfec = MCAN_STD_FILT_ELEM_DISABLE;
    stdFiltElem[0].sft = MCAN_STD_FILT_TYPE_RANGE;
    
    /* wait for memory initialization to happen */
    while (FALSE == MCAN_isMemInitDone(gMcanBaseAddr))
    {}

    /* Put MCAN in SW initialization mode */
    MCAN_setOpMode(gMcanBaseAddr, MCAN_OPERATION_MODE_SW_INIT);
    while (MCAN_OPERATION_MODE_SW_INIT != MCAN_getOpMode(gMcanBaseAddr))
    {}

    /* Initialize MCAN module */
    MCAN_init(gMcanBaseAddr, &initParams);
    /* Configure MCAN module Gloabal Filter */
    MCAN_config(gMcanBaseAddr, &configParams);
    /* Configure Bit timings */
    MCAN_setBitTime(gMcanBaseAddr, &bitTimes);
    /* Configure Message RAM Sections */
    MCAN_msgRAMConfig(gMcanBaseAddr, &msgRAMConfigParams);
    /* Set Extended ID Mask */
    MCAN_setExtIDAndMask(gMcanBaseAddr, APP_MCAN_EXT_ID_MASK);

    /* Configure Standard ID filter element */
    MCAN_addStdMsgIDFilter(gMcanBaseAddr, 0, &stdFiltElem[0]);

    /* Take MCAN out of the SW initialization mode */
    MCAN_setOpMode(gMcanBaseAddr, MCAN_OPERATION_MODE_NORMAL);
    while (MCAN_OPERATION_MODE_NORMAL != MCAN_getOpMode(gMcanBaseAddr))
    {}

    return;
}

static void App_mcanInitMsgRamConfigParams(MCAN_MsgRAMConfigParams
                                           *msgRAMConfigParams)
{
    int32_t status;

    MCAN_initMsgRamConfigParams(msgRAMConfigParams);

    /* Configure the user required msg ram params */
    msgRAMConfigParams->lss = APP_MCAN_STD_ID_FILTER_CNT;
    msgRAMConfigParams->lse = APP_MCAN_EXT_ID_FILTER_CNT;
    msgRAMConfigParams->txBufCnt = APP_MCAN_TX_BUFF_CNT;
    msgRAMConfigParams->txFIFOCnt = APP_MCAN_TX_FIFO_CNT;
    /* Buffer/FIFO mode is selected */
    msgRAMConfigParams->txBufMode = MCAN_TX_MEM_TYPE_QUEUE;
    msgRAMConfigParams->txEventFIFOCnt = APP_MCAN_TX_EVENT_FIFO_CNT;
    msgRAMConfigParams->rxFIFO0Cnt = APP_MCAN_FIFO_0_CNT;
    msgRAMConfigParams->rxFIFO1Cnt = APP_MCAN_FIFO_1_CNT;
    /* FIFO blocking mode is selected */
    msgRAMConfigParams->rxFIFO0OpMode = MCAN_RX_FIFO_OPERATION_MODE_BLOCKING;
    msgRAMConfigParams->rxFIFO1OpMode = MCAN_RX_FIFO_OPERATION_MODE_BLOCKING;

    status = MCAN_calcMsgRamParamsStartAddr(msgRAMConfigParams);
    DebugP_assert(status == CSL_PASS);

    return;
}
