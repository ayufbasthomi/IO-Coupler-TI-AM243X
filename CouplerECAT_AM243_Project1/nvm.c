/*!
 *  \file nvm.c
 *
 *  \brief
 *  Persistent data storage API.
 *
 *  \author
 *  Texas Instruments Incorporated
 *
 *  \copyright
 *  Copyright (C) 2024 Texas Instruments Incorporated
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

#include "nvm.h"
#include "FreeRTOS.h"
#include "nvm_drv_eeprom.h"
#include "nvm_drv_flash.h"
#include "kernel/dpl/SemaphoreP.h"

#define WRITE_STACK_SIZE_BYTE           1024
#define WRITE_FLASH_STACK_SIZE          (WRITE_STACK_SIZE_BYTE/sizeof(configSTACK_DEPTH_TYPE))
#define MAX_WRITE_REQUEST_QUEUE_SIZE    10

typedef struct NVM_APP_writeParam
{
    NVM_type_t                  type;
    uint32_t                    offset;
    uint32_t                    id;
    uint32_t                    length;
    void*                       pData;
    uint32_t                    forceErase;
    uint8_t                     isStaticData;
    struct NVM_APP_writeParam   *next;
} NVM_APP_writeParam_t;

typedef struct NVM_APP_writeQueue
{
    NVM_APP_writeParam_t        *head;
    NVM_APP_writeParam_t        *tail;
    uint32_t                    queueLength;
    OSAL_SCHED_MutexHandle_t    *pQueueMtx;
} NVM_APP_writeQueue_t;

typedef struct NVM_APP_handle
{
    NVM_APP_writeCallback_t callback;
    void* task;
    void* semaphore;
    NVM_APP_writeParam_t data;
    NVM_err_t status;
}NVM_APP_handle_t;

static NVM_APP_handle_t     NVM_handle  = {0};
static NVM_APP_writeQueue_t NVM_queue   = {NULL, NULL, 0, NULL};
SemaphoreP_Object           *pNvmLock   = NULL;

OSAL_FUNC_NORETURN static void NVM_APP_writeTask(void *pArg);

static StackType_t writeTaskStack[WRITE_FLASH_STACK_SIZE] __attribute__((aligned(32), section(".threadstack"))) = {0};
static uint32_t NVM_APP_pushWriteReqToQueue     (
                                                const NVM_type_t type,
                                                const uint32_t id,
                                                const uint32_t offset,
                                                const uint32_t length,
                                                const void * const pData,
                                                const uint32_t forceErase,
                                                const uint8_t isStaticData);
 static uint32_t NVM_APP_pullWriteReqFromQueue  (NVM_APP_writeParam_t* pWriteReq);
 static uint32_t NVM_APP_removeWriteReqFromQueue(void);
 static uint32_t NVM_APP_getPendingWriteReqCount(void);

/*!
 * \brief
 * Init NVM library.
 *
 * \details
 * It initializes the asynchronous write thread and the OS semaphore.
 *
 * \param[in]     priority                  Write task priority.
 *
 * \return        NVM_err_t as uint32_t.
 * \retval        NVM_SUCCESS               Success.
 * \retval        NVM_ERR_FAIL              Failed to create OSAL signal or write task.
 *
 * \see NVM_APP_writeTask
 *
 * \ingroup NVM_APP
 *
 */
uint32_t NVM_APP_init(const OSAL_TASK_Priority_t priority)
{
    uint32_t error = NVM_ERR_FAIL;

    // initialize NVM queue
    NVM_queue.head = NULL;
    NVM_queue.tail = NULL;
    NVM_queue.queueLength = 0;

    //Mutex for NVM Queue
    if(NVM_queue.pQueueMtx == NULL)
    {
        NVM_queue.pQueueMtx = OSAL_MTXCTRLBLK_alloc();
        if(NVM_queue.pQueueMtx == NULL)
        {
            error = NVM_ERR_MALLOC;
        }
    }
        
    if(NVM_handle.semaphore == NULL && NVM_queue.pQueueMtx != NULL)
    {
        NVM_handle.semaphore = OSAL_createSignal("NVM_APP_WriteAsyncSignal");
        if(NVM_handle.semaphore == NULL)
        {
            error = NVM_ERR_SEM_CREATE;
        }
    }

    if(NVM_queue.pQueueMtx != NULL && NVM_handle.semaphore != NULL && NVM_handle.task == NULL)
    {
        OSAL_MTX_init(NVM_queue.pQueueMtx);
        NVM_handle.task =  OSAL_SCHED_startTask(NVM_APP_writeTask,
                                                NULL,
                                                priority,
                                                (uint8_t*) writeTaskStack,
                                                WRITE_STACK_SIZE_BYTE,
                                                OSAL_OS_START_TASK_FLG_NONE,
                                                "NVM_APP_WriteAsyncTask");
        error = NVM_SUCCESS;
    }
    return error;
}

/*!
 * \brief
 * Close NVM library.
 *
 * \return        NVM_err_t as uint32_t.
 * \retval        NVM_SUCCESS       Success.
 *
 * \see NVM_APP_init
 *
 * \ingroup NVM_APP
 *
 */
uint32_t NVM_APP_close(void)
{
    uint32_t error = NVM_SUCCESS;

    if(NVM_queue.pQueueMtx != NULL)
    {
        OSAL_MTX_deinit(NVM_queue.pQueueMtx);
        NVM_queue.pQueueMtx = NULL;
    }
    
    if(NVM_handle.semaphore != NULL)
    {
        OSAL_deleteSignal(NVM_handle.semaphore);
        NVM_handle.semaphore = NULL;
    }

    if(NVM_handle.task != NULL)
    {
        OSAL_SCHED_exitTask(NVM_handle.task);
        NVM_handle.task = NULL;
    }
    return error;
}


/*!
 * \brief
 * Set handle for lock object
 *
 * \details
 * This object is used to lock periphery during the nvm access
 *
 * \param[in]     handle                    native lock handle
 *
 * \return        NVM_err_t as uint32_t.
 * \retval        NVM_SUCCESS               Success.
 *
 * \ingroup NVM_APP
 *
 */
uint32_t NVM_APP_setLockHandle(void *handle)
{
    pNvmLock = (SemaphoreP_Object *)handle;
    return NVM_SUCCESS;
}

/*!
 * \brief
 * Register write callback.
 *
 * \details
 * The registered function is called when the asynchronous write function finishes to write data
 * or when something went wrong.
 *
 * \param[in]     cb                    Write callback function.
 *
 * \return        NVM_err_t as uint32_t.
 * \retval        NVM_SUCCESS           Success.
 * \retval        NVM_ERR_INVALID       Something went wrong.
 *
 * \see NVM_APP_write
 *
 * \ingroup NVM_APP
 *
 */
uint32_t NVM_APP_registerCallback(NVM_APP_writeCallback_t cb)
{
    uint32_t error = NVM_ERR_INVALID;
    if(cb != NULL)
    {
        NVM_handle.callback = cb;
        error = NVM_SUCCESS;
    }
    return error;
}

/*!
 * \brief
 * Read data from persistent data storage.
 *
 * \details
 * Read some data from persistent storage. The function is a blocking function.
 *
 * \param[in]     type                      Type of persistent storage.
 * \param[in]     id                        Device ID from sysconfig.
 * \param[in]     offset                    Offset on storage device.
 * \param[in]     length                    Data length in bytes.
 * \param[in]     pData                     Data buffer.
 *
 * \return        NVM_err_t as uint32_t.
 * \retval        NVM_SUCCESS               Success.
 * \retval        NVM_ERR_INVALID           Something went wrong.
 *
 * \see NVM_APP_write NVM_APP_writeAsync
 *
 * \ingroup NVM_APP
 *
 */
uint32_t NVM_APP_read(
    const NVM_type_t type,
    const uint32_t id,
    const uint32_t offset,
    const uint32_t length,
    void * const pData)
{
    uint32_t error = NVM_SUCCESS;
    if (pNvmLock)
    {
        SemaphoreP_pend(pNvmLock, SystemP_WAIT_FOREVER);
    }
    switch (type)
    {
        case NVM_TYPE_EEPROM:
        {
            error = NVM_DRV_EEPROM_read(id, offset, length, pData);
            break;
        }
        case NVM_TYPE_FLASH:
        {
            error = NVM_DRV_FLASH_read(id, offset, length, pData);
            break;
        }
        default:
            error = NVM_ERR_INVALID;
    }
    if (pNvmLock)
    {
        SemaphoreP_post(pNvmLock);
    }
    return error;
}

/*!
 * \brief
 * Write data to persistent data storage.
 *
 * \details
 * Write some data to persistent storage. The function is a blocking function.
 *
 * \param[in]     type                      Type of persistent storage.
 * \param[in]     id                        Device ID from sysconfig.
 * \param[in]     offset                    Offset on storage device.
 * \param[in]     length                    Data length in bytes.
 * \param[in]     pData                     Data buffer.
 * \param[in]     forceErase                Block force erase flag.
 *
 * \return        NVM_err_t as uint32_t.
 * \retval        NVM_SUCCESS               Success.
 * \retval        NVM_ERR_INVALID           Something went wrong.
 *
 * \see NVM_APP_read NVM_APP_writeAsync
 *
 * \ingroup NVM_APP
 *
 */
uint32_t NVM_APP_write(
    const NVM_type_t type,
    const uint32_t id,
    const uint32_t offset,
    const uint32_t length,
    const void * const pData,
    const uint32_t forceErase)
{
    uint32_t error = NVM_SUCCESS;

    if (pNvmLock)
    {
        SemaphoreP_pend(pNvmLock, SystemP_WAIT_FOREVER);
    }

    switch (type)
    {
        case NVM_TYPE_EEPROM:
        {
            error = NVM_DRV_EEPROM_write(id, offset, length, pData);
            break;
        }
        case NVM_TYPE_FLASH:
        {
            error = NVM_DRV_FLASH_write(id, offset, length, pData, forceErase);
            break;
        }
        default:
        {
            error = NVM_ERR_INVALID;
        }
    }
    if (pNvmLock)
    {
        SemaphoreP_post(pNvmLock);
    }
    return error;
}

/*!
 * \brief
 * Write data to persistent data storage.
 *
 * \details
 * This tasks writes data to persistent storage.
 *
 * \param[in]   pArg    Thread arguments.
 *
 * \see NVM_APP_read NVM_APP_write NVM_APP_registerCallback
 *
 * \ingroup NVM_APP
 *
 */
OSAL_FUNC_NORETURN static void NVM_APP_writeTask(void *pArg)
{
    uint32_t result = NVM_SUCCESS;
    int32_t error = OSAL_ERR_InvalidParm;

    OSALUNREF_PARM(pArg);

    while (true)
    {
        //wait for signal
        error = OSAL_waitSignal(NVM_handle.semaphore, OSAL_WAIT_INFINITE);
        //Reject more NVM_APP_writeAsync calls

        if (error == OSAL_ERR_NoError)
        {
            result = NVM_APP_pullWriteReqFromQueue(&NVM_handle.data);
            if(NVM_SUCCESS == result)
            {
                result = NVM_APP_write( NVM_handle.data.type,
                                        NVM_handle.data.id,
                                        NVM_handle.data.offset,
                                        NVM_handle.data.length,
                                        NVM_handle.data.pData,
                                        NVM_handle.data.forceErase);
            }
            if(NVM_SUCCESS == result)
            {
                result = NVM_APP_removeWriteReqFromQueue();
            }
            NVM_handle.status = result;

            //call callback with write result
            if(NVM_handle.callback != NULL)
            {
                NVM_handle.callback(result);
            }
        }
        else
        {
            OSAL_SCHED_yield();
        }
    }
}

/*!
 * \brief
 * Write data to persistent data storage.
 *
 * \details
 * Write some data to persistent storage. The function is a non blocking function.
 * This function does NOT copy the data to another buffer, therefore please ensure
 * that the buffer is not destroyed after calling this function.
 *
 * Hint: Use the registered callback to free the data buffer.
 *
 * \param[in]     type                      Type of persistent storage.
 * \param[in]     id                        Device ID from sysconfig.
 * \param[in]     offset                    Offset on storage device.
 * \param[in]     length                    Data length in bytes.
 * \param[in]     pData                     Data buffer.
 * \param[in]     forceErase                Block force erase flag.
 * \param[in]     isStaticData              True, if pData is pointing to static data otherwise false.  
 *
 * \return        NVM_err_t as uint32_t.
 * \retval        NVM_SUCCESS               Success.
 * \retval        NVM_ERR_BUSY              Write task is busy.
 *
 * \see NVM_APP_read NVM_APP_write NVM_APP_registerCallback
 *
 * \ingroup NVM_APP
 *
 */
uint32_t NVM_APP_writeAsync(
    const NVM_type_t type,
    const uint32_t id,
    const uint32_t offset,
    const uint32_t length,
    const void * const pData,
    const uint32_t forceErase,
    const uint8_t isStaticData)
{
    uint32_t error = NVM_ERR_QUEUE_FULL;

    if(NVM_handle.status != NVM_ERR_QUEUE_FULL)
    {
        error = NVM_APP_pushWriteReqToQueue(type, id, offset, length, pData, forceErase, isStaticData);

        if(MAX_WRITE_REQUEST_QUEUE_SIZE <= NVM_APP_getPendingWriteReqCount())
        {
            NVM_handle.status = NVM_ERR_QUEUE_FULL;
        }

        if(error == NVM_SUCCESS)
        {
            //push to queue and send signal to write thread
            OSAL_postSignal(NVM_handle.semaphore);
        }
    }
    return error;
}

/**
 * \brief 
 * Function that adds a new write request to the NVM FIFO queue.
 *
 * \details
 * This function creates a new write request and adds it to the end of the queue.
 *
 * \param[in]   type          Type of persistent storage.
 * \param[in]   id            Device ID from sysconfig.
 * \param[in]   offset        Offset on storage device.
 * \param[in]   length        Data length in bytes.
 * \param[in]   pData         Data buffer.
 * \param[in]   forceErase    Block force erase flag.
 * \param[in]   isStaticData  True, if pData is pointing to static data otherwise false.
 * \return      uint32_t      NVM_SUCCESS if the request is successfully pushed to the queue, NVM_ERR_MALLOC if memory allocation fails.
 *
 * \ingroup NVM_APP
 */
static uint32_t NVM_APP_pushWriteReqToQueue(
    const NVM_type_t type,
    const uint32_t id,
    const uint32_t offset,
    const uint32_t length,
    const void * const pData,
    const uint32_t forceErase,
    const uint8_t isStaticData)
{
    uint32_t error = NVM_SUCCESS;
    error = OSAL_MTX_get(NVM_queue.pQueueMtx, OSAL_WAIT_INFINITE, NULL);
    if(OSAL_eERR_NOERROR == error)
    {
        NVM_APP_writeParam_t *pWriteReq = (NVM_APP_writeParam_t *)OSAL_MEMORY_calloc(1,sizeof(NVM_APP_writeParam_t));
        if (pWriteReq != NULL)
        {
            if(!isStaticData)
            {
                uint8_t* pDataBuffer = (uint8_t*)OSAL_MEMORY_calloc(1,length);
                OSAL_MEMORY_memcpy(pDataBuffer,pData,length);
                pWriteReq->pData = pDataBuffer;
                pWriteReq->isStaticData = false;
            }
            else
            {
                pWriteReq->pData = (uint8_t*)pData;
                pWriteReq->isStaticData = true;
            }
            pWriteReq->type = type;
            pWriteReq->id = id;
            pWriteReq->offset = offset;
            pWriteReq->length = length;
            pWriteReq->forceErase = forceErase;
            pWriteReq->next = NULL;

            if(NVM_queue.head == NULL)
            {
                NVM_queue.head = pWriteReq;
                NVM_queue.tail = pWriteReq;
                NVM_queue.queueLength = 1;
            }
            else
            {
                NVM_queue.tail->next = pWriteReq;
                NVM_queue.tail = pWriteReq;
                NVM_queue.queueLength += 1;
            }
            error = NVM_SUCCESS;
        }
        else
        {
            error = NVM_ERR_MALLOC;
        }
        OSAL_MTX_release(NVM_queue.pQueueMtx);
    }
    else
    {
        error = NVM_ERR_QUEUE_MTX;
    }
    return error;
}

/*!
 * \brief 
 * Function that returns the pending NVM write request from the NVM queue.
 * 
 * \details
 * This function retrives the pending NVM write request from the NVM FIFO queue and returns it to the caller.
 *
 * \param[in]   pWriteReq   Pointer to NVM_APP_writeParam_t to return the removed request.
 * \return      uint32_t    NVM_err_t. NVM_SUCCESS when write request is retrieved or NVM_ERR_QUEUE_EMPTY when queue is empty.
 *
 * \see NVM_APP_removeWriteReqFromQueue NVM_APP_pushWriteReqToQueue
 *
 * \ingroup NVM_APP
 */
static uint32_t NVM_APP_pullWriteReqFromQueue(NVM_APP_writeParam_t* pWriteReq)
{
    uint32_t error = NVM_SUCCESS;
    error = OSAL_MTX_get(NVM_queue.pQueueMtx, OSAL_WAIT_INFINITE, NULL);
    if(OSAL_eERR_NOERROR == error)
    {
        if(NVM_queue.queueLength == 0)
        {
            error = NVM_ERR_QUEUE_EMPTY;
        }
        else
        {
            OSAL_MEMORY_memcpy(pWriteReq, NVM_queue.head, sizeof(NVM_APP_writeParam_t));
            error = NVM_SUCCESS;
        }
        OSAL_MTX_release(NVM_queue.pQueueMtx);
    }
    else
    {
        error = NVM_ERR_QUEUE_MTX;
    }
    return error;
}

/*!
 * \brief
 * Function that removes a NVM write request from a queue.
 *
 * \details
 * This function removes a NVM write request from a queue and it is intended to be called after 
 * the write request has been completed and the data has been written to persistent storage successfully.
 * This function releases the memory allocated for the data buffer and the write request.
 * 
 * \return  uint32_t      NVM_err_t. NVM_SUCCESS when write request is removed or NVM_ERR_QUEUE_EMPTY when queue is empty.
 *
 * \see NVM_APP_pullWriteReqFromQueue NVM_APP_pushWriteReqToQueue
 *
 * \ingroup NVM_APP
 *
 */
static uint32_t NVM_APP_removeWriteReqFromQueue(void)
{
    uint32_t error = NVM_SUCCESS;
    error = OSAL_MTX_get(NVM_queue.pQueueMtx, OSAL_WAIT_INFINITE, NULL);
    if(OSAL_eERR_NOERROR == error)
    {
        if(NVM_queue.queueLength == 0 || NVM_queue.head == NULL)
        {
            error = NVM_ERR_QUEUE_EMPTY;
        }
        else
        {
            NVM_APP_writeParam_t *temp = NVM_queue.head;
            NVM_queue.head = NVM_queue.head->next;
            if(NVM_queue.head == NULL)
            {
                NVM_queue.tail = NULL;
            }
            NVM_queue.queueLength -= 1;
            if(!temp->isStaticData)
            {
                OSAL_MEMORY_free(temp->pData);
            }
            OSAL_MEMORY_free(temp);
            error = NVM_SUCCESS;
        }
        OSAL_MTX_release(NVM_queue.pQueueMtx);
    }
    else
    {
        error = NVM_ERR_QUEUE_MTX;
    }
    return error;
}

/*!
 * \brief
 * Function that returns the number of pending NVM write requests in the queue.
 *
 * \details
 * Returns the number of unprocessed NVM write requests in the queue.
 *
 * \return  uint32_t    Number of pending write requests in the queue. 
 *
 * \see NVM_APP_pullWriteReqFromQueue NVM_APP_pushWriteReqToQueue NVM_APP_removeWriteReqFromQueue
 *
 * \ingroup NVM_APP
 *
 */
static uint32_t NVM_APP_getPendingWriteReqCount(void)
{
    return NVM_queue.queueLength;
}