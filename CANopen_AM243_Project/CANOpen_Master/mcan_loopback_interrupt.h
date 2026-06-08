#ifndef MCAN_LOOPBACK_INTERRUPT_H
#define MCAN_LOOPBACK_INTERRUPT_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

/* =========================================================
 * Debug Configuration
 * ========================================================= */
#define ENABLE_DEBUG_LOG  0

#if ENABLE_DEBUG_LOG
    #define DEBUG_LOG(...) DebugP_log(__VA_ARGS__)
#else
    #define DEBUG_LOG(...)
#endif

#define CAN_RX_MODE_POLLING     0
#define CAN_RX_MODE_INTERRUPT   1
#define CAN_RX_MODE CAN_RX_MODE_INTERRUPT
#define APP_MCAN_INTR_NUM       (CONFIG_MCAN0_INTR)

/* =========================================================
 * System Configuration
 * ========================================================= */
#define CANOPEN_OK 0
#define APP_MCAN_BASE_ADDR       (CONFIG_MCAN0_BASE_ADDR)
#define MAX_NODES 20
#define MAX_ANALOG_CH 8

#define MAX_DI   MAX_NODES
#define MAX_DO   MAX_NODES
#define MAX_AI   (MAX_NODES * MAX_ANALOG_CH)
#define MAX_AO   (MAX_NODES * MAX_ANALOG_CH)

#define CAN_TX_QUEUE_SIZE 32

#define UNKNOWN 0
#define DO      1
#define DI      2
#define AI_C    3
#define AI_V    4
#define AO_C    5
#define AO_V    6
#define RTDY    7
#define RTDB    8

enum io_device_type {
    IO_DEVICE_TYPE_DO16 = 0x01,
    IO_DEVICE_TYPE_DI16 = 0x02,
    IO_DEVICE_TYPE_AIC8 = 0x03,
    IO_DEVICE_TYPE_AIV8 = 0x04,
    IO_DEVICE_TYPE_AOC8 = 0x05,
    IO_DEVICE_TYPE_AOV8 = 0x06,
    IO_DEVICE_TYPE_RTDY = 0x07,
    IO_DEVICE_TYPE_RTDB = 0x08,
};

typedef struct {
    uint8_t nodeId;
    uint8_t type;
    uint16_t value;
    int16_t analog[8];
} CAN_TxMsg;

typedef struct
{
    /* Runtime value */
    uint16_t digital;
    int16_t analog[8];

    /* Metadata */
    uint8_t ioType;

    char hwVer[16];
    char fwVer[16];
    char SN[32];

    /* State */
    char nodeState[20];

    /* Error handling */
    uint8_t  lastErrorType;
    uint32_t lastErrorCode;

    /* Internal flag */
    uint8_t initialized;

} CANopenNodeData;

typedef struct {
    uint16_t di[MAX_DI];
    uint16_t do_[MAX_DO];
    int16_t  ai[MAX_AI];
    int16_t  ao[MAX_AO];
} IO_DataModel;

typedef struct
{
    uint8_t nodeId;
    uint8_t ioType;

    uint16_t diIndex;
    uint16_t doIndex;
    uint16_t aiIndex;
    uint16_t aoIndex;

} CANopenModule;

/* TxPDO mapping */
extern CANopenModule gTxModules[MAX_NODES];
extern uint16_t gTxCount;

/* RxPDO mapping */
extern CANopenModule gRxModules[MAX_NODES];
extern uint16_t gRxCount;

extern SemaphoreHandle_t gIODataMutex;
extern QueueHandle_t gCanTxQueue;
extern IO_DataModel gIOData;

int32_t CANopen_writeRPDO(uint8_t nodeId, uint16_t value);
int32_t CANopen_writeRPDO_Analog(uint8_t nodeId, int16_t values[8]);
void ECAT_BuildModuleMapping(void);

#endif