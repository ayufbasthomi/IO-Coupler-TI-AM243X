#include <stdio.h>
#include <string.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/TaskP.h>
#include <kernel/dpl/AddrTranslateP.h>
#include <kernel/dpl/ClockP.h>
#include <drivers/mcan.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "mcan_loopback_interrupt.h"
#include "queue.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* ================= CONFIG ================= */
/* CANopen heartbeat: Node 1–16 */
#define CANOPEN_ID_START         (0x701U)
#define CANOPEN_ID_END           (0x77FU)

/* Message RAM */
#define APP_MCAN_STD_ID_FILTER_CNT   (3U)
#define APP_MCAN_RX_FIFO0_CNT        (64U)

#define MCAN_TASK_STACK (4096 / sizeof(StackType_t))
StackType_t gMcanTaskStack[MCAN_TASK_STACK] __attribute__((aligned(32)));
TaskP_Object gMcanTask;

SemaphoreHandle_t gIODataMutex = NULL;
QueueHandle_t gCanTxQueue;

static SemaphoreHandle_t gTxMutex;

#define TX_BUF_COUNT 15U     /* buffer 0..14 */
#define SDO_TX_BUF   15U     /* dedicated SDO */

static uint8_t gNextTxBuf = 0;

IO_DataModel gIOData;
CANopenModule gModules[MAX_NODES];
CANopenModule gTxModules[MAX_NODES];
CANopenModule gRxModules[MAX_NODES];
CANopenNodeData latestCanopenData[MAX_NODES + 1];

static uint32_t last_heartbeat_time[MAX_NODES + 1];
static uint32_t gMcanBaseAddr;

static uint8_t discovered_nodes[MAX_NODES + 1];  // index = nodeId
static uint8_t node_list[MAX_NODES];
static uint8_t node_started[MAX_NODES + 1] = {0};
static uint8_t tpdo_received[MAX_NODES + 1][2];

static volatile uint8_t sdo_in_progress = 0;

uint32_t txStatus;

uint16_t gTxCount = 0;
uint16_t gRxCount = 0;

uint8_t node_count = 0;
uint8_t DI_NODE_COUNT = 0;
uint8_t DO_NODE_COUNT = 0;
uint8_t AO_NODE_COUNT = 0;
uint8_t AI_NODE_COUNT = 0;
uint8_t gModuleCount = 0;

#if CAN_RX_MODE == CAN_RX_MODE_INTERRUPT
volatile uint32_t gCanRxPending = 0;
static HwiP_Object gMcanHwiObject;
#endif

// uint8_t mb_do_nodes[MAX_NODES];
// uint8_t mb_di_nodes[MAX_NODES];
// uint8_t mb_ai_nodes[MAX_NODES];
// uint8_t mb_ao_nodes[MAX_NODES];

// uint16_t mb_do_count = 0;
// uint16_t mb_di_count = 0;
// uint16_t mb_ai_count = 0;
// uint16_t mb_ao_count = 0;

static CANopenModule* findModule(uint8_t nid);
static void CANopen_onTPDO(MCAN_RxBufElement *rxMsg);

/* ================= IO TYPE ================= */
static uint8_t capability[MAX_NODES + 1];

/* ================= TIMER ================= */
static uint32_t get_time_ms()
{
    return ClockP_getTimeUsec() / 1000;
}

static uint16_t le16(const uint8_t *d)
{
    return (uint16_t)(d[0] | (d[1] << 8));
}

/* ================= MCAN CONFIG ================= */
static void App_mcanConfig(void)
{
    MCAN_InitParams initParams = {0};
    MCAN_ConfigParams configParams = {0};
    MCAN_MsgRAMConfigParams msgRAMConfigParams = {0};
    MCAN_BitTimingParams bitTimes = {0};
    MCAN_StdMsgIDFilterElement stdFilt = {0};

    /* Init default */
    MCAN_initOperModeParams(&initParams);

    /* CAN CLASSIC ONLY */
    initParams.fdMode    = FALSE;
    initParams.brsEnable = FALSE;

    MCAN_initGlobalFilterConfigParams(&configParams);

    /* Accept all frames */
    configParams.filterConfig.rrfe = 0;
    configParams.filterConfig.rrfs = 0;
    configParams.filterConfig.anfe = 0x2;
    configParams.filterConfig.anfs = 0x2;

    /* 1 Mbps */
    MCAN_initSetBitTimeParams(&bitTimes);

    /* Message RAM */
    MCAN_initMsgRamConfigParams(&msgRAMConfigParams);
    msgRAMConfigParams.lss = APP_MCAN_STD_ID_FILTER_CNT;
    msgRAMConfigParams.lse = 0;

    msgRAMConfigParams.txBufCnt   = 16;
    msgRAMConfigParams.txFIFOCnt  = 0;

    msgRAMConfigParams.rxFIFO0Cnt = APP_MCAN_RX_FIFO0_CNT;
    msgRAMConfigParams.rxFIFO0OpMode = MCAN_RX_FIFO_OPERATION_MODE_BLOCKING;

    MCAN_calcMsgRamParamsStartAddr(&msgRAMConfigParams);

    /* Wait memory init */
    while (!MCAN_isMemInitDone(gMcanBaseAddr));

    /* Enter init mode */
    MCAN_setOpMode(gMcanBaseAddr, MCAN_OPERATION_MODE_SW_INIT);
    while (MCAN_getOpMode(gMcanBaseAddr) != MCAN_OPERATION_MODE_SW_INIT);

    /* Apply config */
    MCAN_init(gMcanBaseAddr, &initParams);
    MCAN_config(gMcanBaseAddr, &configParams);
    MCAN_setBitTime(gMcanBaseAddr, &bitTimes);
    MCAN_msgRAMConfig(gMcanBaseAddr, &msgRAMConfigParams);
    
#if CAN_RX_MODE == CAN_RX_MODE_INTERRUPT
    MCAN_enableIntr(gMcanBaseAddr, MCAN_INTR_MASK_ALL, (uint32_t)TRUE);
    MCAN_enableIntr(gMcanBaseAddr, MCAN_INTR_SRC_RES_ADDR_ACCESS, (uint32_t)FALSE);
    /* Select Interrupt Line 0 */
    MCAN_selectIntrLine(gMcanBaseAddr, MCAN_INTR_MASK_ALL, MCAN_INTR_LINE_NUM_0);
    /* Enable Interrupt Line */
    MCAN_enableIntrLine(gMcanBaseAddr, MCAN_INTR_LINE_NUM_0, (uint32_t)TRUE);
#endif

    /* ================= FILTER CONFIG ================= */

    /* --- Filter 0: Heartbeat (0x701 – 0x77F) --- */
    stdFilt.sfid1 = CANOPEN_ID_START;
    stdFilt.sfid2 = CANOPEN_ID_END;
    stdFilt.sfec  = MCAN_STD_FILT_ELEM_FIFO0;
    stdFilt.sft   = MCAN_STD_FILT_TYPE_RANGE;

    MCAN_addStdMsgIDFilter(gMcanBaseAddr, 0, &stdFilt);

    /* --- Filter 1: SDO Response (0x580 – 0x5FF) --- */
    stdFilt.sfid1 = 0x580;
    stdFilt.sfid2 = 0x5FF;
    stdFilt.sfec  = MCAN_STD_FILT_ELEM_FIFO0;
    stdFilt.sft   = MCAN_STD_FILT_TYPE_RANGE;

    MCAN_addStdMsgIDFilter(gMcanBaseAddr, 1, &stdFilt);

    /* --- Filter 2: (Optional) PDO Range (0x180 – 0x4FF) --- */
    stdFilt.sfid1 = 0x180;
    stdFilt.sfid2 = 0x4FF;
    stdFilt.sfec  = MCAN_STD_FILT_ELEM_FIFO0;
    stdFilt.sft   = MCAN_STD_FILT_TYPE_RANGE;

    MCAN_addStdMsgIDFilter(gMcanBaseAddr, 2, &stdFilt);

    /* Normal mode */
    MCAN_setOpMode(gMcanBaseAddr, MCAN_OPERATION_MODE_NORMAL);
    while (MCAN_getOpMode(gMcanBaseAddr) != MCAN_OPERATION_MODE_NORMAL);
}

#if CAN_RX_MODE == CAN_RX_MODE_INTERRUPT
static void App_mcanIntrISR(void *args)
{
    uint32_t intrStatus;

    intrStatus =
        MCAN_getIntrStatus(gMcanBaseAddr);

    MCAN_clearIntrStatus(
        gMcanBaseAddr,
        intrStatus);

    if(intrStatus & MCAN_INTR_SRC_RX_FIFO0_NEW_MSG)
    {
        gCanRxPending++;
    }
}

static void canIntcfg(void)
{
    int32_t status;
    HwiP_Params hwiPrms;

    HwiP_Params_init(&hwiPrms);

    hwiPrms.intNum   = APP_MCAN_INTR_NUM;
    hwiPrms.callback = App_mcanIntrISR;

    status = HwiP_construct(
        &gMcanHwiObject,
        &hwiPrms);

    DebugP_assert(status == SystemP_SUCCESS);
}
#endif

#if COMM_TYPE == COMM_TYPE_ETHERCAT 
void ECAT_BuildModuleMapping(void)
{
    uint16_t tx = 0;
    uint16_t rx = 0;

    memset(gTxModules, 0, sizeof(gTxModules));
    memset(gRxModules, 0, sizeof(gRxModules));

    for(uint8_t i=0; i<gModuleCount; i++)
    {
        CANopenModule *src = &gModules[i];

        switch(src->ioType)
        {
            /* ============================= */
            /* TxPDO                         */
            /* ============================= */

            case AI_C:
            case AI_V:
            case RTDY:
            case RTDB:

                gTxModules[tx++] = *src;
                break;

            case DI:

                gTxModules[tx++] = *src;
                break;

            /* ============================= */
            /* RxPDO                         */
            /* ============================= */

            case AO_C:
            case AO_V:

                gRxModules[rx++] = *src;
                break;

            case DO:

                gRxModules[rx++] = *src;
                break;

            default:
                break;
        }
    }

    gTxCount = tx;
    gRxCount = rx;

    DEBUG_LOG("\r\n===== ECAT TX PDO =====\r\n");

    for(uint16_t i=0;i<gTxCount;i++)
    {
        DEBUG_LOG(
            "TX Slot=%u Node=%u Type=%u\r\n",
            i,
            gTxModules[i].nodeId,
            gTxModules[i].ioType);
    }

    DEBUG_LOG("\r\n===== ECAT RX PDO =====\r\n");

    for(uint16_t i=0;i<gRxCount;i++)
    {
        DEBUG_LOG(
            "RX Slot=%u Node=%u Type=%u\r\n",
            i,
            gRxModules[i].nodeId,
            gRxModules[i].ioType);
    }
}
#elif COMM_TYPE == COMM_TYPE_MODBUSTCP
// void MB_BuildMapping(void)
// {
//     mb_do_count = 0;
//     mb_di_count = 0;
//     mb_ai_count = 0;
//     mb_ao_count = 0;

//     for(uint16_t i=0; i<gModuleCount; i++)
//     {
//         switch(gModules[i].ioType)
//         {
//             case DO:
//                 mb_do_nodes[mb_do_count++] =
//                     gModules[i].nodeId;
//                 break;

//             case DI:
//                 mb_di_nodes[mb_di_count++] =
//                     gModules[i].nodeId;
//                 break;

//             case AI_C:
//             case AI_V:
//             case RTDY:
//             case RTDB:
//                 mb_ai_nodes[mb_ai_count++] =
//                     gModules[i].nodeId;
//                 break;

//             case AO_C:
//             case AO_V:
//                 mb_ao_nodes[mb_ao_count++] =
//                     gModules[i].nodeId;
//                 break;

//             default:
//                 break;
//         }
//     }

//     DebugP_log("\r\n=== MODBUS MAP ===\r\n");

//     for(uint16_t i=0;i<mb_do_count;i++)
//         DebugP_log("DO[%u] = Node %u\r\n", i, mb_do_nodes[i]);

//     for(uint16_t i=0;i<mb_di_count;i++)
//         DebugP_log("DI[%u] = Node %u\r\n", i, mb_di_nodes[i]);

//     for(uint16_t i=0;i<mb_ai_count;i++)
//         DebugP_log("AI[%u] = Node %u\r\n", i, mb_ai_nodes[i]);

//     for(uint16_t i=0;i<mb_ao_count;i++)
//         DebugP_log("AO[%u] = Node %u\r\n", i, mb_ao_nodes[i]);
// }
#endif

CANopenModule* CANopen_findDO(uint16_t doIndex)
{
    for(uint16_t i = 0; i < gModuleCount; i++)
    {
        if(gModules[i].ioType == DO &&
           gModules[i].doIndex == doIndex)
        {
            return &gModules[i];
        }
    }

    return NULL;
}

CANopenModule* CANopen_findDI(uint16_t diIndex)
{
    for(uint16_t i = 0; i < gModuleCount; i++)
    {
        if(gModules[i].ioType == DI &&
           gModules[i].diIndex == diIndex)
        {
            return &gModules[i];
        }
    }

    return NULL;
}

CANopenModule* CANopen_findAI(uint16_t aiIndex)
{
    for(uint16_t i = 0; i < gModuleCount; i++)
    {
        if((gModules[i].ioType == AI_C ||
            gModules[i].ioType == AI_V ||
            gModules[i].ioType == RTDY ||
            gModules[i].ioType == RTDB) &&
            gModules[i].aiIndex == aiIndex)
        {
            return &gModules[i];
        }
    }

    return NULL;
}

CANopenModule* CANopen_findAO(uint16_t aoIndex)
{
    for(uint16_t i = 0; i < gModuleCount; i++)
    {
        if((gModules[i].ioType == AO_C ||
            gModules[i].ioType == AO_V) &&
            gModules[i].aoIndex == aoIndex)
        {
            return &gModules[i];
        }
    }

    return NULL;
}

static void CANopen_processRxFIFO(void)
{
    if(sdo_in_progress)
    {
        return;
    }
    
    MCAN_RxBufElement rxMsg;
    MCAN_RxFIFOStatus fifoStatus;

    fifoStatus.num = MCAN_RX_FIFO_NUM_0;

    MCAN_getRxFIFOStatus(
        gMcanBaseAddr,
        &fifoStatus);

    while(fifoStatus.fillLvl > 0)
    {
        MCAN_readMsgRam(
            gMcanBaseAddr,
            MCAN_MEM_TYPE_FIFO,
            fifoStatus.getIdx,
            fifoStatus.num,
            &rxMsg);

        MCAN_writeRxFIFOAck(
            gMcanBaseAddr,
            fifoStatus.num,
            fifoStatus.getIdx);

        CANopen_onTPDO(&rxMsg);

        MCAN_getRxFIFOStatus(
            gMcanBaseAddr,
            &fifoStatus);
    }
}

static int CANopen_sendFrame(uint32_t cobId, uint8_t dlc, const uint8_t *data)
{
    MCAN_TxBufElement txMsg;

    if(xSemaphoreTake(gTxMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        DEBUG_LOG("[CAN TX] mutex timeout\r\n");
        return -1;
    }

    uint32_t pending =
        MCAN_getTxBufReqPend(gMcanBaseAddr);

    uint8_t buf = 0xFF;

    for(uint8_t i=0; i<TX_BUF_COUNT; i++)
    {
        uint8_t idx =
            (gNextTxBuf + i) % TX_BUF_COUNT;

        if((pending & (1U << idx)) == 0)
        {
            buf = idx;
            gNextTxBuf = (idx + 1) % TX_BUF_COUNT;
            break;
        }
    }

    if(buf == 0xFF)
    {
        xSemaphoreGive(gTxMutex);

        DEBUG_LOG(
            "[CAN TX] no free tx buffer\r\n");

        return -2;
    }

    memset(&txMsg, 0, sizeof(txMsg));

    txMsg.id  = (cobId << 18);
    txMsg.dlc = dlc;

    memcpy(txMsg.data, data, dlc);

    MCAN_writeMsgRam(
        gMcanBaseAddr,
        MCAN_MEM_TYPE_BUF,
        buf,
        &txMsg);

    if(MCAN_txBufAddReq(
            gMcanBaseAddr,
            buf) != CSL_PASS)
    {
        xSemaphoreGive(gTxMutex);
        return -3;
    }

    // DEBUG_LOG(
    //     "[CAN TX] buf=%u cob=0x%03X\r\n",
    //     buf,
    //     cobId);

    xSemaphoreGive(gTxMutex);

    return 0;
}

static void CANopen_processCommandQueue(void)
{
    CAN_TxMsg msg;

    while (xQueueReceive(gCanTxQueue, &msg, 0) == pdTRUE)
    {
        if (msg.type == 0)
        {
            CANopen_writeRPDO(msg.nodeId, msg.value);
        }
        else
        {
            CANopen_writeRPDO_Analog(msg.nodeId, msg.analog);
        }
    }
}

static void CANopen_updateDI(uint8_t nid, uint16_t value)
{
    CANopenModule *m = findModule(nid);

    if(m == NULL)
    {
        DEBUG_LOG(
            "[DI UPDATE] Node=%u NOT FOUND\r\n",
            nid);
        return;
    }

    DEBUG_LOG(
        "[DI UPDATE] Node=%u "
        "Type=%u "
        "DI=%u "
        "DO=%u "
        "AI=%u "
        "AO=%u "
        "Value=0x%04X\r\n",
        m->nodeId,
        m->ioType,
        m->diIndex,
        m->doIndex,
        m->aiIndex,
        m->aoIndex,
        value);

    gIOData.di[m->diIndex] = value;

    DEBUG_LOG(
        "[DI STORE] gIOData.di[%u] = 0x%04X\r\n",
        m->diIndex,
        gIOData.di[m->diIndex]);
}

static void CANopen_updateDO(uint8_t nid, uint16_t value)
{
    CANopenModule *m = findModule(nid);

    if(m == NULL)
        return;

    gIOData.do_[m->doIndex] = value;
}

static void CANopen_updateAI(uint8_t nid, int16_t *values)
{
    CANopenModule *m = findModule(nid);

    if(m == NULL)
        return;

    int offset = m->aiIndex * MAX_ANALOG_CH;

    if(offset + 7 >= MAX_AI)
        return;

    if(xSemaphoreTake(gIODataMutex, portMAX_DELAY))
    {
        memcpy(&gIOData.ai[offset],
               values,
               8*sizeof(int16_t));

        xSemaphoreGive(gIODataMutex);
    }
}

static void CANopen_updateAO(uint8_t nid, int16_t *values)
{
    CANopenModule *m = findModule(nid);

    if(m == NULL)
        return;

    int offset = m->aoIndex * MAX_ANALOG_CH;

    if(offset + 7 >= MAX_AO)
        return;

    if(xSemaphoreTake(gIODataMutex, portMAX_DELAY))
    {
        memcpy(&gIOData.ao[offset],
               values,
               8*sizeof(int16_t));

        xSemaphoreGive(gIODataMutex);
    }
}

/* ================= Flush Rx FIFO ================= */
static void CANopen_flushRxFIFO(void)
{
    MCAN_RxFIFOStatus fifoStatus;
    MCAN_RxBufElement rxMsg;

    fifoStatus.num = MCAN_RX_FIFO_NUM_0;

    MCAN_getRxFIFOStatus(gMcanBaseAddr, &fifoStatus);

    while (fifoStatus.fillLvl > 0)
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

        MCAN_getRxFIFOStatus(gMcanBaseAddr, &fifoStatus);
    }
}

/* ================= SDO UPLOAD ================= */
static int32_t CANopen_SDO_upload(uint8_t nodeId, uint16_t index, uint8_t subIndex, uint32_t *value)
{
    MCAN_TxBufElement txMsg;
    MCAN_RxBufElement rxMsg;
    MCAN_RxFIFOStatus fifoStatus;

    uint32_t txId = 0x600 + nodeId;
    uint32_t rxId = 0x580 + nodeId;

    sdo_in_progress = 1;

    /* STEP 1: Flush FIFO */
    CANopen_flushRxFIFO();

    /* STEP 2: Send SDO request */
    memset(&txMsg, 0, sizeof(txMsg));

    txMsg.id  = (txId << 18);
    txMsg.dlc = 8;

    txMsg.data[0] = 0x40;
    txMsg.data[1] = index & 0xFF;
    txMsg.data[2] = (index >> 8) & 0xFF;
    txMsg.data[3] = subIndex;

    MCAN_writeMsgRam(gMcanBaseAddr, MCAN_MEM_TYPE_BUF, SDO_TX_BUF, &txMsg);
    MCAN_txBufAddReq(gMcanBaseAddr, SDO_TX_BUF);

    /* Wait TX done */
    do {
        txStatus = MCAN_getTxBufTransmissionStatus(gMcanBaseAddr);
    } while ((txStatus & (1U << SDO_TX_BUF)) == 0);

    /* STEP 3: Wait response */
    uint32_t start = get_time_ms();

    while ((get_time_ms() - start) < 500)
    {
        fifoStatus.num = MCAN_RX_FIFO_NUM_0;
        MCAN_getRxFIFOStatus(gMcanBaseAddr, &fifoStatus);

        if (fifoStatus.fillLvl == 0)
            {
                ClockP_usleep(1000);
                continue;
            }

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

        uint32_t canId = (rxMsg.id >> 18) & 0x7FF;

        /* Only accept correct SDO response */
        if (canId != rxId)
            continue;

        /* Validate index/subindex */
        if (rxMsg.data[1] != (index & 0xFF) ||
            rxMsg.data[2] != ((index >> 8) & 0xFF) ||
            rxMsg.data[3] != subIndex)
        {
            continue;
        }

        /* Abort */
        if (rxMsg.data[0] == 0x80)
        {
            uint32_t abort_code =
                rxMsg.data[4] |
                (rxMsg.data[5] << 8) |
                (rxMsg.data[6] << 16) |
                (rxMsg.data[7] << 24);

            DEBUG_LOG("[SDO] Abort: 0x%08X\r\n", abort_code);

            sdo_in_progress = 0;
            return -2;
        }

        /* Expedited response */
        if ((rxMsg.data[0] & 0xE0) == 0x40)
        {
            *value =
                (rxMsg.data[4]) |
                (rxMsg.data[5] << 8) |
                (rxMsg.data[6] << 16) |
                (rxMsg.data[7] << 24);

            DEBUG_LOG("[SDO] OK value=0x%08X\r\n", *value);

            sdo_in_progress = 0;
            return 0;
        }
    }

    sdo_in_progress = 0;
    return -1; // timeout
}

/* ================= SEND NMT ================= */
static void CANopen_sendNMT(uint8_t command, uint8_t nodeId)
{
    MCAN_TxBufElement txMsg;

    memset(&txMsg, 0, sizeof(txMsg));

    txMsg.id  = (0x000 << 18);
    txMsg.rtr = 0;
    txMsg.xtd = 0;
    txMsg.esi = 0;

    txMsg.dlc = 2;

    txMsg.data[0] = command;
    txMsg.data[1] = nodeId;

    uint8_t data[2];

    data[0] = command;
    data[1] = nodeId;

    CANopen_sendFrame(
        0x000,
        2,
        data);

    DEBUG_LOG("[NMT] Sent Cmd:0x%02X Node:%d\r\n", command, nodeId);
}

/* ================= AUTODISCOVER ================= */
static void CANopen_autodiscover(uint32_t timeout_per_node_ms)
{
    MCAN_RxBufElement rxMsg;
    MCAN_RxFIFOStatus fifoStatus;

    gModuleCount = 0;
    memset(gModules, 0, sizeof(gModules));
    memset(discovered_nodes, 0, sizeof(discovered_nodes));
    node_count = 0;

    DEBUG_LOG("🔍 Active scanning nodes 1..32\r\n");

    for (uint8_t nid = 1; nid <= MAX_NODES; nid++)
    {
        uint32_t start = get_time_ms();
        uint8_t found = 0;

        /* 🔥 Trigger node: send NMT (start remote node) */
        CANopen_sendNMT(0x01, nid);

        while ((get_time_ms() - start) < timeout_per_node_ms)
        {
            fifoStatus.num = MCAN_RX_FIFO_NUM_0;
            MCAN_getRxFIFOStatus(gMcanBaseAddr, &fifoStatus);

            if (fifoStatus.fillLvl == 0)
            {
                ClockP_usleep(1000);
                continue;
            }

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

            uint32_t canId = (rxMsg.id >> 18) & 0x7FF;

            /* Check heartbeat response */
            if (canId == (0x700 + nid))
            {
                found = 1;
                last_heartbeat_time[nid] = get_time_ms();
                break;
            }
        }

        if (found)
        {
            discovered_nodes[nid] = 1;
            node_list[node_count++] = nid;

            DEBUG_LOG("✔ Node %d detected\r\n", nid);
        }
        else
        {
            DEBUG_LOG("✖ Node %d not found\r\n", nid);
        }
    }

    DEBUG_LOG("✔ Scan Done. Total nodes: %d\r\n", node_count);
}

/* ================= DETECT IO TYPE ================= */
static uint8_t CANopen_detectCapability(uint8_t nodeId)
{
    uint32_t product_code = 0;

    int32_t ret = CANopen_SDO_upload(nodeId, 0x1018, 2, &product_code);

    if (ret != 0)
    {
        DEBUG_LOG("[CAP] Node %d → SDO FAIL\r\n", nodeId);
        return UNKNOWN;
    }

    DEBUG_LOG("[CAP] Node %d → ProductCode: 0x%08X\r\n",
               nodeId, product_code);

    switch (product_code)
    {
        case 0x01: return DO;
        case 0x02: return DI;
        case 0x03: return AI_C;
        case 0x04: return AI_V;
        case 0x05: return AO_C;
        case 0x06: return AO_V;
        case 0x07: return RTDY;
        case 0x08: return RTDB;
        default:
            DEBUG_LOG("[CAP] UNKNOWN: 0x%08X\r\n", product_code);
            return UNKNOWN;
    }
}

/* ================= INIT NODE STRUCTURE ================= */
static void CANopen_initNodeIfNeeded(uint8_t nid, uint8_t ioType)
{
    if (nid == 0 || nid > MAX_NODES)
        return;

    if (latestCanopenData[nid].initialized)
        return;

    memset(&latestCanopenData[nid], 0, sizeof(CANopenNodeData));

    latestCanopenData[nid].ioType = ioType;

    strcpy(latestCanopenData[nid].nodeState, "Unknown_state");
    latestCanopenData[nid].lastErrorType = CANOPEN_OK;
    latestCanopenData[nid].lastErrorCode = 0;

    latestCanopenData[nid].initialized = 1;

    DEBUG_LOG("[INIT NODE] Node %d initialized\r\n", nid);
}

/* ================= ON TPDO ================= */
static void CANopen_onTPDO(MCAN_RxBufElement *rxMsg)
{
    uint32_t cob = (rxMsg->id >> 18) & 0x7FF;

    uint8_t *raw = rxMsg->data;

    uint8_t nid;
    uint8_t pdo;

    if(cob >= 0x180 && cob <= 0x1FF)
    {
        nid = cob - 0x180;
        pdo = 1;
    }
    else if(cob >= 0x280 && cob <= 0x2FF)
    {
        nid = cob - 0x280;
        pdo = 2;
    }
    else
    {
        return;
    }

    uint8_t type =
        capability[nid];

    switch(type)
    {
        case DI:
        {
            uint16_t val = raw[0] | (raw[1] << 8);
            DEBUG_LOG("[DI] ID %d, Value %d\r\n", nid, val);
            CANopen_updateDI(nid, val);

            break;
        }

        case DO:
        {
            uint16_t val = raw[0] | (raw[1] << 8);

            latestCanopenData[nid].digital = val;

            break;
        }

        case AI_C:
        case AI_V:
        case AO_C:
        case AO_V:
        case RTDY:
        case RTDB:
        {
            if(tpdo_received[nid][0] &&
            tpdo_received[nid][1])
            {
                if(type == AI_C ||
                type == AI_V ||
                type == RTDY ||
                type == RTDB)
                {
                    CANopen_updateAI(
                        nid,
                        latestCanopenData[nid].analog);
                }
                else if(type == AO_C ||
                        type == AO_V)
                {
                    CANopen_updateAO(
                        nid,
                        latestCanopenData[nid].analog);
                }

                tpdo_received[nid][0] = 0;
                tpdo_received[nid][1] = 0;
            }

            break;
        }

        default:
            break;
    }
}

/* ================= WRITE RPDO ================= */
int32_t CANopen_writeRPDO(uint8_t nodeId, uint16_t value)
{
    // DEBUG_LOG(
    // "[RPDO SEND] node=%u value=%04X tick=%u\r\n",
    // nodeId,
    // value,
    // ClockP_getTicks());
    
    MCAN_TxBufElement txMsg;
    uint32_t cob = 0x200 + nodeId;

    if (nodeId == 0 || nodeId > MAX_NODES)
        return -1;

    uint8_t ioType = capability[nodeId];
    if (ioType != DO)
        return -2;

    CANopen_initNodeIfNeeded(nodeId, ioType);

    memset(&txMsg, 0, sizeof(txMsg));

    txMsg.id  = (cob << 18);
    txMsg.dlc = 2;

    txMsg.data[0] = value & 0xFF;
    txMsg.data[1] = (value >> 8) & 0xFF;

    // DEBUG_LOG("[TX RPDO] Node=%d COB=0x%03X Value=0x%04X\r\n", nodeId, cob, value);

    uint8_t data[2];

    data[0] = value & 0xFF;
    data[1] = value >> 8;

    CANopen_sendFrame(
        0x200 + nodeId,
        2,
        data);

    /* 🔥 UPDATE BOTH */
    latestCanopenData[nodeId].digital = value;
    CANopen_updateDO(nodeId, value);

    return 0;
}

static void CANopen_sendInitialRPDOZero(uint8_t nodeId)
{
    uint8_t ioType = capability[nodeId];

    /* Only reset OUTPUT devices */
    if (ioType == DO)
    {
        DEBUG_LOG("[RPDO INIT] DO Node %d → 0x0000\r\n", nodeId);
        CANopen_writeRPDO(nodeId, 0x0000);
    }
    else if (ioType == AO_C || ioType == AO_V)
    {
        int16_t zero[8] = {0};

        DEBUG_LOG("[RPDO INIT] AO Node %d → all 0\r\n", nodeId);
        CANopen_writeRPDO_Analog(nodeId, zero);
    }
}

/* ================= WRITE RPDO ANALOG ================= */
int32_t CANopen_writeRPDO_Analog(uint8_t nodeId, int16_t values[8])
{
    uint8_t frame1[8];
    uint8_t frame2[8];

    for(int i=0;i<4;i++)
    {
        frame1[i*2]     = values[i] & 0xFF;
        frame1[i*2 + 1] = values[i] >> 8;
    }

    for(int i=0;i<4;i++)
    {
        frame2[i*2]     = values[i+4] & 0xFF;
        frame2[i*2 + 1] = values[i+4] >> 8;
    }

    CANopen_sendFrame(
        0x200 + nodeId,
        8,
        frame1);

    CANopen_sendFrame(
        0x300 + nodeId,
        8,
        frame2);

    memcpy(latestCanopenData[nodeId].analog, values, sizeof(int16_t)*8);

    CANopen_updateAO(nodeId, values);

    return 0;
}

static CANopenModule* findModule(uint8_t nid)
{
    for(int i=0;i<gModuleCount;i++)
    {
        if(gModules[i].nodeId == nid)
            return &gModules[i];
    }

    return NULL;
}

/* ================= INIT NETWORK ================= */
static void CANopen_initNetwork(void)
{
    DEBUG_LOG("=== CANopen INIT START ===\r\n");
    gIODataMutex = xSemaphoreCreateMutex();
    if(gIODataMutex == NULL)
    {
        DEBUG_LOG("Mutex create failed\r\n");
        return;
    }

    gTxMutex = xSemaphoreCreateMutex();
    if(gTxMutex == NULL)
    {
        DEBUG_LOG("TX Mutex create failed\r\n");
        return;
    }

    gCanTxQueue = xQueueCreate(CAN_TX_QUEUE_SIZE, sizeof(CAN_TxMsg));

    /* ================= HARD RESET (IMPORTANT) ================= */
    memset(capability, 0, sizeof(capability));
    memset(node_started, 0, sizeof(node_started));
    memset(latestCanopenData, 0, sizeof(latestCanopenData));

    DI_NODE_COUNT = 0;
    DO_NODE_COUNT = 0;
    AO_NODE_COUNT = 0;
    AI_NODE_COUNT = 0;
    
    /* Step 1: Discover nodes (5 sec) */
    CANopen_autodiscover(1000);

    /* Step 2: Initialize each node */
    for (int i = 0; i < node_count; i++)
    {
        uint8_t nid = node_list[i];

        DEBUG_LOG("\r\n[INIT] Node %d\r\n", nid);

        /* Step 2.1: Reset Communication */
        CANopen_sendNMT(0x82, nid);
        ClockP_usleep(100 * 1000);

        /* Detect IO Type */
        uint8_t cap = CANopen_detectCapability(nid);
        capability[nid] = cap;

        DEBUG_LOG("[INIT] Node %d Capability = %d\r\n", nid, cap);
        
        /* Step 2.2: Set Operational */
        CANopen_sendNMT(0x01, nid);

        ClockP_usleep(100 * 1000);  // small delay (100ms recommended)
        CANopen_sendInitialRPDOZero(nid);

        CANopenModule *m = &gModules[gModuleCount++];

        m->nodeId = nid;
        m->ioType = cap;

        switch(cap) {
            case DI:

                m->diIndex = DI_NODE_COUNT++;
                break;

            case DO:

                m->doIndex = DO_NODE_COUNT++;
                break;

            case AI_C:
            case AI_V:
            case RTDY:
            case RTDB:

                m->aiIndex = AI_NODE_COUNT++;
                break;

            case AO_C:
            case AO_V:

                m->aoIndex = AO_NODE_COUNT++;
                break;
        }

        DEBUG_LOG("[NMT] Node %d → Operational\r\n", nid);
    }

    DEBUG_LOG("=== CANopen INIT DONE ===\r\n");
}

/* ================= MAIN LOOP ================= */
void mcan_main(void *args)
{
    gMcanBaseAddr = (uint32_t) AddrTranslateP_getLocalAddr(APP_MCAN_BASE_ADDR);

    App_mcanConfig();
#if CAN_RX_MODE == CAN_RX_MODE_INTERRUPT
    canIntcfg();
#endif
    CANopen_initNetwork();

#if COMM_TYPE == COMM_TYPE_ETHERCAT
    ECAT_BuildModuleMapping();
#elif COMM_TYPE == COMM_TYPE_MODBUSTCP
    // MB_BuildMapping();
#endif

    while (1)
    {
#if CAN_RX_MODE == CAN_RX_MODE_POLLING
        CANopen_processCommandQueue();

        CANopen_processRxFIFO();
        
        vTaskDelay(1);
#endif
#if CAN_RX_MODE == CAN_RX_MODE_INTERRUPT
        CANopen_processCommandQueue();

        while(gCanRxPending)
        {
            gCanRxPending--;

            CANopen_processRxFIFO();
        }

        vTaskDelay(1);
#endif
    }
}

void mcan_task(void *args)
{
    mcan_main(args);
}

void create_mcan_task(void)
{
    TaskP_Params params;

    TaskP_Params_init(&params);

    params.name = "MCAN";
    params.stack = (uint8_t *)gMcanTaskStack;
    params.stackSize = sizeof(gMcanTaskStack);

    params.priority = configMAX_PRIORITIES-1;
    params.taskMain = mcan_task;
    params.args = NULL;

    TaskP_construct(&gMcanTask, &params);
}