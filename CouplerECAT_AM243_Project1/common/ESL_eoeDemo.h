/*!
 *  \file ESL_eoeDemo.h
 *
 *  \brief
 *  EtherCAT<sup>&reg;</sup> Ethernet over EtherCAT interface.
 *
 *  \author
 *  Texas Instruments Incorporated
 *
 *  \copyright
 *  Copyright (C) 2025 Texas Instruments Incorporated
 *  SPDX-License-Identifier: LicenseRef-Texas Instruments Incorporated
 *  All rights reserved.
 */

#if !(defined __ESL_EOEDEMO_H__)
#define __ESL_EOEDEMO_H__       1

#include <stdint.h>
#include <ecSlvApi.h>
#include <defines/ecSlvApiDef_error.h>
#include <kernel/dpl/SemaphoreP.h>
#include <stdio.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#ifdef LWIP_SUPPORT
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#endif //LWIP_SUPPORT

#if (defined __cplusplus)
extern "C" {
#endif

#ifdef LWIP_SUPPORT

#define EEPROM_MAGIC_KEY_BOARD_ID \
    /* @cppcheck_justify{misra-c2012-11.6} void cast required for signature */ \
    /* cppcheck-suppress misra-c2012-11.6 */ \
    ((void*)0xEE3355AAu)

#define EOE_WEBSERVER_TASK_TICK_MS      10
#define EOE_MESSAGE_OUTPUT_DELAY_MS     1
#define EOE_MAX_BUFFER_SIZE_BYTES       1580

static void* Webserver_pTaskHandle_s;
#define WEBSERVER_TASK_SIZE_BYTE        (0x1000U)
#define WEBSERVER_TASK_SIZE             (WEBSERVER_TASK_SIZE_BYTE/sizeof(configSTACK_DEPTH_TYPE))
static StackType_t webserverTaskStack_s[WEBSERVER_TASK_SIZE] __attribute__((aligned(32), section(".threadstack"))) = {0};

#undef EOE_DEBUG_PRINTS
typedef struct eoeContext_ {
    SemaphoreP_Object txLock;
    uint8_t settingsReceived;

    int32_t socketFd;
    int32_t clientFd;
    int32_t flags;

    uint16_t frameBuffer[EOE_MAX_BUFFER_SIZE_BYTES];
} eoeContext;

struct pbuf;

err_t low_level_output(struct netif *netif, struct pbuf *p);

err_t low_level_init_dummy(struct netif *netif);

void tcp_init_done(void *arg);

err_t netif_if_init(struct netif *netif);

int EC_SLV_APP_EoE_SS_initializeWebserver(void);

void EC_SLV_APP_EoE_SS_task(void *pArg);

uint8_t Webserver_start(void);
#endif  //LWIP_SUPPORT

bool EC_SLV_APP_EoE_SS_settingIndHandler(void *pContext, uint16_t *pMac, uint16_t *pIp,
                                         uint16_t *pSubNet, uint16_t *pDefaultGateway,
                                         uint16_t* pDnsIp);

bool EC_SLV_APP_EoE_SS_receiveHandler(void *pContext, uint16_t *pData, uint16_t size);

#if (defined __cplusplus)
}
#endif

/** @} */
#endif /* __ESL_EOEDEMO_H__ */
