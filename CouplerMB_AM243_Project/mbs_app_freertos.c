/*!
 *  \file mbs_app_freertos.c
 *
 *  \brief
 *  Modbus server application implementation for FreeRTOS environments.
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

#include <stdint.h>
#include <mbs_tcpclient.h>
#include <mbs_tcpserver.h>
#include <mbs_devicechannel.h>
#include "mbs_app.h"
#include "lwip/err.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
/* ========================================================================== */
/*                            Global Variables                                */
/* ========================================================================== */
#define MODBUSSLAVE_THREAD_STACKSIZE 1024
#define MODBUSSLAVE_THREAD_PRIO 31

static TaskHandle_t ModbusServerTask;

MbsDeviceChannelConfig mbs_app_deviceChannelConfig;
static MbsTcpServer tcpServer;
static nmbs_callbacks callbacks;
SemaphoreHandle_t deviceChannelMutex;
static MbsDeviceChannel deviceChannel;
static volatile bool g_requestClose = false;
/* ========================================================================== */
/*                          Function Declarations                             */
/* ========================================================================== */
/* ========================================================================== */
/*                          Function Definitions                              */
/* ========================================================================== */
// static void MbsApp_ModbusSlaveTask(void *arg)
// {
//     int32_t result = 0;
//     MbsDeviceChannel deviceChannel = (MbsDeviceChannel)arg;
//     while(1)
//     {
//         result = MbsDeviceChannel_process(deviceChannel);
//         if(NMBS_ERROR_TRANSPORT == result)
//         {
//             MbsDeviceChannel_destroy(deviceChannel);
//             break;
//         }
//     }
//     vTaskDelete(NULL);
// }

static void MbsApp_ModbusSlaveTask(void *arg)
{
    (void)arg;

    int32_t result = 0;
    uint32_t lastActivity = xTaskGetTickCount();
    const uint32_t NO_ACTIVITY_TIMEOUT_MS = 2000;

    while (!g_requestClose)
    {
        int32_t result = MbsDeviceChannel_process(deviceChannel);

        if (result == NMBS_ERROR_TRANSPORT)
        {
            printf("MODBUS client disconnect\r\n");
            break;
        }
        else if (result < 0 && result != NMBS_ERROR_TIMEOUT)
        {
            printf("MODBUS error %d\r\n", result);
            break;
        }

        // no delay → keep responsive
    }

    printf("Terminated MbsApp_ModbusSlaveTask...\r\n");

    MbsDeviceChannel_destroy(deviceChannel);
    deviceChannel = NULL;
    g_requestClose = false;
    
    vTaskDelay(100);
    vTaskDelete(NULL);
}

static int32_t MbsApp_newClientConnected(MbsTcpServer tcpServer, MbsTcpClient client, void* context)
{
    err_t err = ERR_OK;
    if (deviceChannel != NULL)
    {
        printf("Closing previous client...\r\n");

        g_requestClose = true;
        uint32_t count_wait = 0;
        // wait until task cleans up
        while (deviceChannel != NULL)
        {
            printf("Waiting task terminated!...\r\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            count_wait++;
            if(count_wait > 5){
                MbsDeviceChannel_destroy(deviceChannel);
                vTaskDelete(ModbusServerTask);
            }
        }
    }
    
    if (deviceChannel == NULL)
    {
        deviceChannel = MbsDeviceChannel_create(&mbs_app_deviceChannelConfig, client);

        if (deviceChannel == NULL)
        {
            return ERR_MEM;
        }

        printf("New MODBUS client connected %p\r\n", client);

        if (pdPASS != xTaskCreate(
                MbsApp_ModbusSlaveTask,
                "AppTcp_ModbusSlaveTask",
                MODBUSSLAVE_THREAD_STACKSIZE,
                NULL,
                MODBUSSLAVE_THREAD_PRIO,
                &ModbusServerTask))
        {
            printf("Cannot create task for %p\r\n", client);

            MbsDeviceChannel_destroy(deviceChannel);
            deviceChannel = NULL;
            err = ERR_MEM;
        }
    }

    return err;
}

void MbsApp_startTcpServer()
{
    deviceChannelMutex = xSemaphoreCreateMutex();
    MBS_DEVICE_CHANNEL_CONFIG_INIT(&mbs_app_deviceChannelConfig);
    mbs_app_deviceChannelConfig.ModbusSlaveCallbacks = NULL;
    mbs_app_deviceChannelConfig.ReadTimeout = 1;
    mbs_app_deviceChannelConfig.ByteTimeout = 1;
    nmbs_callbacks_create(&callbacks);
    callbacks.read_coils = MbsApp_read_coils;
    callbacks.write_multiple_coils = MbsApp_write_multiple_coils;
    callbacks.read_holding_registers = MbsApp_read_holding_registers;
    callbacks.write_multiple_registers = MbsApp_write_multiple_registers;
    callbacks.read_discrete_inputs = MbsApp_read_discrete_input;
    callbacks.write_single_coil = MbsApp_write_single_coils;
    callbacks.read_input_registers = MbsApp_read_input_registers;
    callbacks.write_single_register = MbsApp_write_single_registers;
    mbs_app_deviceChannelConfig.ModbusSlaveCallbacks = &callbacks;
    tcpServer = MbsTcpServer_create();
    if(tcpServer != NULL)
    {
        err_t err = MbsTcpServer_start(tcpServer, MBS_APP_SERVER_PORT, &MbsApp_newClientConnected, NULL);
        if(err == ERR_OK)
        {
            printf("TCP server started successfully @ %d\r\n", MBS_APP_SERVER_PORT);
        }
    }
}
