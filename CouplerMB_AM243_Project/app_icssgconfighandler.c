/*!
 *  \file app_icssgconfighandler.c
 *
 *  \brief
 *  ICSS-G configuration for Modbus server.
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

/* ========================================================================== */
/*                             Include Files                                  */
/* ========================================================================== */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
/* lwIP core includes */
#include "lwip/opt.h"
/* SDK includes */
#include <enet_apputils.h>
#include <enet_board.h>
#include <kernel/dpl/TaskP.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/QueueP.h>
#include <include/per/icssg.h>
#include "ti_board_config.h"
#include "ti_board_open_close.h"
#include "ti_drivers_open_close.h"
#include "ti_enet_config.h"
#include "ti_enet_open_close.h"
#include "ti_drivers_config.h"

/* ========================================================================== */
/*                           Macros & Typedefs                                */
/* ========================================================================== */

/* ========================================================================== */
/*                         Structure Declarations                             */
/* ========================================================================== */

/* ========================================================================== */
/*                          Function Declarations                             */
/* ========================================================================== */


#if (ENET_SYSCFG_ENABLE_EXTPHY == 0U)
static void EnetApp_mdioLinkStatusChange(Icssg_MdioLinkStateChangeInfo *info,
                                         void *appArg);
#endif

static void EnetApp_portLinkStatusChangeCb(Enet_MacPort macPort,
                                           bool isLinkUp,
                                           void *appArg);

/* ========================================================================== */
/*                            Global Variables                                */
/* ========================================================================== */

#define MII_LINK0_EVENT      41
#define MII_LINK1_EVENT      53

/* ========================================================================== */
/*                          Function Definitions                              */
/* ========================================================================== */

static void EnetApp_updateMdioLinkIntCfg(Enet_Type enetType, uint32_t instId, Icssg_mdioLinkIntCfg *mdioLinkIntCfg)
{
    /*! INTC Module mapping data passed by application for configuring PRU to R5F interrupts */
#if (ENET_SYSCFG_ICSSG0_ENABLED == 1)
    mdioLinkIntCfg->prussIntcInitData =  &icss0_intc_initdata;
#endif
#if (ENET_SYSCFG_ICSSG1_ENABLED == 1)
    mdioLinkIntCfg->prussIntcInitData =  &icss1_intc_initdata;
#endif
    mdioLinkIntCfg->coreIntrNum = 254;
    mdioLinkIntCfg->pruEvtNum[0] = MII_LINK0_EVENT;
    mdioLinkIntCfg->pruEvtNum[1] = MII_LINK1_EVENT;
    mdioLinkIntCfg->isPulseIntr = 0;
    mdioLinkIntCfg->intrPrio = 15;
}

void EnetApp_updateIcssgInitCfg(Enet_Type enetType, uint32_t instId, Icssg_Cfg *icssgCfg)
{
#if (ENET_SYSCFG_ENABLE_MDIO_MANUALMODE == 1U)
    icssgCfg->mdioLinkIntCfg.mdioLinkStateChangeCb = NULL;
    icssgCfg->mdioLinkIntCfg.mdioLinkStateChangeCbArg  = NULL;
#else
    #if (ENET_SYSCFG_ENABLE_EXTPHY == 1U)
        EnetApp_initMdioLinkIntCfg(enetType, instId, icssgCfg);
    #else
        icssgCfg->mdioLinkIntCfg.mdioLinkStateChangeCb = &EnetApp_mdioLinkStatusChange;
        icssgCfg->mdioLinkIntCfg.mdioLinkStateChangeCbArg  = NULL;
    #endif
    EnetApp_updateMdioLinkIntCfg(enetType, instId, &icssgCfg->mdioLinkIntCfg);
#endif
}

#if (ENET_SYSCFG_ENABLE_EXTPHY == 0U)
static void EnetApp_mdioLinkStatusChange(Icssg_MdioLinkStateChangeInfo *info,
                                         void *appArg)
{
    EnetAppUtils_print("Link Status Changed. PHY: 0x%x, state: %s\r\n",
            info->phyAddr,
            info->isLinked? "up" : "down");
}
#endif

static void EnetApp_portLinkStatusChangeCb(Enet_MacPort macPort,
                                           bool isLinkUp,
                                           void *appArg)
{
    EnetAppUtils_print("MAC Port %u: link %s\r\n",
                       ENET_MACPORT_ID(macPort), isLinkUp ? "up" : "down");
}
