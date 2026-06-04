/*!
 *  \file drv_uart.c
 *
 *  \brief
 *  UART application driver.
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

#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "ti_drivers_open_close.h"

#include "osal.h"

#include "drivers/common/drv_common.h"
#include "drivers/uart/drv_uart.h"

#define DRV_UART_TASK_STACK_SIZE             1024
#define DRV_UART_BUFFER_SIZE                 0x300

typedef struct DRV_Uart
{
    UART_Handle                        handle;
    uint32_t                           instance;
    void*                              uartTaskHandle;
    char                               aOutStream[DRV_UART_BUFFER_SIZE];
    UART_Transaction                   transaction;
    void*                              uartSignal;
    uint32_t                           uartWritePos;
    uint32_t                           uartReadPos;
    bool                               isInitialized;
}DRV_Uart_t;

static DRV_Uart_t DRV_uart_s = {0};


static void DRV_UART_task(void *pvTaskArg);

/*!
*
*  \brief
*  Provides handle to UART driver.
*
*  \return     UART_Handle  Handle to UART driver.
*
*  \retval     NULL           Failed.
*  \retval     Other          Success.
*
*/
UART_Handle DRV_UART_getHandle(uint32_t instanceId)
{
    UART_Handle handle = NULL;

#if (defined CONFIG_UART_NUM_INSTANCES) && (CONFIG_UART_NUM_INSTANCES > 0)
    if (CONFIG_UART_NUM_INSTANCES > instanceId)
    {
        handle = gUartHandle[instanceId];
    }
#else
    OSALUNREF_PARM(instanceId);
#endif

    return handle;
}

/*!
*
*  \brief
*  Initialize UART application part.
*
*  \param[in]  UART initialization parameters.
*
*  \return     error code as uint32_t
*
*  \retval     #OSAL_NO_ERROR                Success.
*  \retval     #OSAL_GENERAL_ERROR           Negative default value.
*  \retval     #OSAL_UART_DRV_HANDLE_INVALID UART handle is NULL.
*
*/
uint32_t DRV_UART_init(const DRV_UART_SInit_t* pParams)
{
    uint32_t result = OSAL_GENERAL_ERROR;

    if(FALSE == DRV_uart_s.isInitialized)
    {
        DRV_uart_s.instance = pParams->instance;
        DRV_uart_s.handle   = DRV_UART_getHandle(DRV_uart_s.instance);
        DRV_uart_s.uartSignal = OSAL_createSignal("uartSignal");
        DRV_uart_s.uartWritePos = 0;
        DRV_uart_s.uartReadPos = 0;
        if((NULL == DRV_uart_s.handle) || (NULL == DRV_uart_s.uartSignal))
        {
            result = OSAL_UART_DRV_HANDLE_INVALID;
            goto laError;
        }
    
        DRV_uart_s.uartTaskHandle = OSAL_SCHED_startTask(
            DRV_UART_task,
            (void *)&DRV_uart_s,
            pParams->taskPrio,
            NULL,
            DRV_UART_TASK_STACK_SIZE,
            OSAL_OS_START_TASK_FLG_NONE,
            "uart_task");
    
        if(NULL == DRV_uart_s.uartTaskHandle)
        {
            result = OSAL_GENERAL_ERROR;
            goto laError;
        }
        
        DRV_uart_s.isInitialized = TRUE;
        result = OSAL_NO_ERROR;
    }
laError:
  return result;
}

/*!
*
*  \brief
*  De-initialize UART application part.
*
*  \return     error code as uint32_t
*
*  \retval     #OSAL_NO_ERROR                Success.
*  \retval     #OSAL_GENERAL_ERROR           Negative default value.
*  \retval     #OSAL_UART_DRV_HANDLE_INVALID UART handle is NULL.
*
*/
uint32_t DRV_UART_deInit(void)
{
    uint32_t result = OSAL_GENERAL_ERROR;

    if (NULL != DRV_uart_s.handle)
    {
        UART_flushTxFifo(DRV_uart_s.handle);
    }

    result = OSAL_NO_ERROR;

    return result;
}

/*!
 * \brief
 *  uart print task
 *
 * \details
 * This function, is used to do printf on a low prio thread
 *
 */
static void DRV_UART_task(void *pvTaskArg)
{
    DRV_Uart_t *pDrvUart = (DRV_Uart_t *)pvTaskArg;
    DRV_COMMON_Mutex_EError_t mutexRetVal;

    while(1)
    {
        uint32_t bytesToWrite;
        OSAL_waitSignal(pDrvUart->uartSignal, OSAL_WAIT_INFINITE);

        mutexRetVal = DRV_COMMON_Mutex_Lock(DRV_COMMON_MUTEX_UART, OSAL_WAIT_INFINITE);

        if(DRV_COMMON_MUTEX_eERR_NOERROR == mutexRetVal)
        {
            if(pDrvUart->uartReadPos == pDrvUart->uartWritePos)
            {
                DRV_COMMON_Mutex_Unlock(DRV_COMMON_MUTEX_UART);
                continue;
            }

            UART_flushTxFifo(pDrvUart->handle);
            UART_Transaction_init(&pDrvUart->transaction);

            // single write operation
            if(pDrvUart->uartWritePos > pDrvUart->uartReadPos)
            {
                bytesToWrite = pDrvUart->uartWritePos - pDrvUart->uartReadPos;
                if (bytesToWrite > 0) {
                    pDrvUart->transaction.count = bytesToWrite;
                    pDrvUart->transaction.buf = (void *)&pDrvUart->aOutStream[pDrvUart->uartReadPos];
                    pDrvUart->transaction.args = NULL;

                    (void)UART_write(pDrvUart->handle, &pDrvUart->transaction);

                    pDrvUart->uartReadPos = (pDrvUart->uartReadPos + bytesToWrite) % DRV_UART_BUFFER_SIZE;
                }
            }
            // two write operations
            else
            {
                //first part
                bytesToWrite =  DRV_UART_BUFFER_SIZE -  pDrvUart->uartReadPos ;
                if (bytesToWrite > 0) {
                    pDrvUart->transaction.count = bytesToWrite;
                    pDrvUart->transaction.buf = (void *)&pDrvUart->aOutStream[pDrvUart->uartReadPos];
                    pDrvUart->transaction.args = NULL;

                    (void)UART_write(pDrvUart->handle, &pDrvUart->transaction);

                    pDrvUart->uartReadPos = (pDrvUart->uartReadPos + bytesToWrite) % DRV_UART_BUFFER_SIZE;
                }
                //second part
                bytesToWrite = pDrvUart->uartWritePos;
                if (bytesToWrite > 0) {
                    pDrvUart->transaction.count = bytesToWrite;
                    pDrvUart->transaction.buf = (void *)&pDrvUart->aOutStream[0];
                    pDrvUart->transaction.args = NULL;

                    (void)UART_write(pDrvUart->handle, &pDrvUart->transaction);
                    pDrvUart->uartReadPos = (pDrvUart->uartReadPos + bytesToWrite) % DRV_UART_BUFFER_SIZE;
                }
            }

            DRV_COMMON_Mutex_Unlock(DRV_COMMON_MUTEX_UART);
        }
    }
    OSAL_SCHED_exitTask(NULL);
}
/*!
*
*  \brief
*  UART printf output function.
*
*  \details
*  Printing of specific string to UART output.
*
*  \param[in]  pContext      Call context
*  \param[in]  pFormat       Format string.
*  \param[in]  argptr        Parameter list.
*
*
*/
void DRV_UART_printf(void* pContext, const char* pFormat, va_list arg)
{
        /* @cppcheck_justify{unusedVariable} false-positive: variable is used */
    //cppcheck-suppress unusedVariable
    DRV_COMMON_Mutex_EError_t mutexRetVal;

    OSALUNREF_PARM(pContext);

    int lengthWritten;
    char localString[256] = {0};

    OSALUNREF_PARM(pContext);

    lengthWritten = vsnprintf(localString, sizeof(localString), pFormat, arg);

    if (lengthWritten > 0)
    {
        uint32_t bytesToWrite = (uint32_t)lengthWritten;

        mutexRetVal = DRV_COMMON_Mutex_Lock(DRV_COMMON_MUTEX_UART, OSAL_WAIT_INFINITE);
        if(DRV_COMMON_MUTEX_eERR_NOERROR == mutexRetVal)
        {
            uint32_t availableSpace;
            if (DRV_uart_s.uartReadPos > DRV_uart_s.uartWritePos)
            {
                availableSpace = DRV_uart_s.uartReadPos - DRV_uart_s.uartWritePos - 1;
            }
            else
            {
                availableSpace = DRV_UART_BUFFER_SIZE - DRV_uart_s.uartWritePos;
                if (DRV_uart_s.uartReadPos == 0)
                  availableSpace -= 1;
            }

            // Single write operation
            if (availableSpace > bytesToWrite)
            {
                memcpy(&DRV_uart_s.aOutStream[DRV_uart_s.uartWritePos], localString, bytesToWrite);
                DRV_uart_s.uartWritePos = (DRV_uart_s.uartWritePos + bytesToWrite) % DRV_UART_BUFFER_SIZE;
            }
            // Divide the write operation into two parts
            else if((DRV_uart_s.uartWritePos == DRV_uart_s.uartReadPos) && (0 != DRV_uart_s.uartWritePos))
            {
                uint32_t firstPart = availableSpace;
                uint32_t secondPart = bytesToWrite - availableSpace;

                memcpy(&DRV_uart_s.aOutStream[DRV_uart_s.uartWritePos], localString, firstPart);
                memcpy(DRV_uart_s.aOutStream, &localString[firstPart], secondPart);

                DRV_uart_s.uartWritePos = secondPart;
            }
            else
            {
                // Buffer is full
            }

            DRV_COMMON_Mutex_Unlock(DRV_COMMON_MUTEX_UART);
            // Notify the UART task to write the data
            OSAL_postSignal(DRV_uart_s.uartSignal);
        }
    }
}

/*!
*
*  \brief
*  DebugLog printf output function.
*
*  \details
*  Printing of specific string to CCS console output.
*
*  \param[in]  pContext      Call context
*  \param[in]  pFormat       Format string.
*  \param[in]  argptr        Parameter list.
*
*
*/
void DRV_UART_LOG_printf(void* pContext, const char* pFormat, va_list argptr)
{
    OSALUNREF_PARM(pContext);

    OSAL_MEMORY_memset(DRV_uart_s.aOutStream, 0, sizeof(DRV_uart_s.aOutStream));
    (void)vsnprintf(DRV_uart_s.aOutStream, sizeof(DRV_uart_s.aOutStream), pFormat, argptr);

    DebugP_log(DRV_uart_s.aOutStream);
}
