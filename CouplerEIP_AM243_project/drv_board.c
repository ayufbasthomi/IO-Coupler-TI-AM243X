/*!
 *  \example drv_board.c
 *
 *  \brief
 *  Board driver used to obtain board information
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

#include <stdbool.h>
#include <string.h>

#include "ti_board_open_close.h"

#include "board/eeprom.h"

#include "osal.h"

#include "cfg_example.h"

#include "drivers/eeprom/drv_eeprom.h"

#include "drivers/board/drv_board.h"

#if (defined CONFIG_I2C_HLD_NUM_INSTANCES) && (CONFIG_I2C_HLD_NUM_INSTANCES > 0)
extern I2C_Handle    gI2cHandle[];
#endif

#if (defined CONFIG_EEPROM_NUM_INSTANCES) && (CONFIG_EEPROM_NUM_INSTANCES > 0)
extern EEPROM_Params gEepromParams[];
#endif

static DRV_BOARD_SObject_t DRV_BOARD_object_s = {0};
static DRV_BOARD_EError_t  DRV_BOARD_Error_s  = DRV_BOARD_eERR_GENERAL_ERROR;
static char                DRV_BOARD_pcbRev_s[DRV_BOARD_INFO_PCB_REVISION_SIZE] = {0};

static uint32_t DRV_BOARD_initMac            (uint32_t id);
static uint32_t DRV_BOARD_getInfoBlock       (EEPROM_Handle handle, uint8_t blockId, uint8_t *pData, uint32_t maxLength);
static uint32_t DRV_BOARD_isEepromAccessible (uint32_t instance);
static uint32_t DRV_BOARD_getPCBRevision     (EEPROM_Handle handle, const char* pRevision);

/**
 * \brief Initializes the board application driver object.
 *
 * \return uint32_t                             Error code
 *
 * \retval #DRV_BOARD_eERR_NO_ERROR             Success.
 * \retval #DRV_BOARD_eERR_GENERAL_ERROR        Negative default value.
 * \retval #DRV_BOARD_eERR_MAIN_ACCESS          No access to main board EEPROM.
 * \retval #DRV_BOARD_eERR_ADDON_ACCESS_1       No access to Add-On board EEPROM of port 1.
 * \retval #DRV_BOARD_eERR_ADDON_ACCESS_2       No access to Add-On board EEPROM of port 2.
 * \retval #DRV_BOARD_eERR_INVALID_MAC_ID       Used MAC ID is higher than allowed ID (see #DRV_BOARD_EMacId_t structure).
 * \retval #DRV_BOARD_eERR_LENGTH_INVALID       The buffer size is too small. At least 7 bytes are required.
 * \retval #DRV_BOARD_eERR_READ_FAILED          An error occurred while reading data from the EEPROM.
 * \retval #DRV_BOARD_eERR_NO_MAC_HEADER_FOUND  MAC header not found inside board info table.
 */
uint32_t DRV_BOARD_init (DRV_BOARD_SInit_t* pParams)
{
    DRV_BOARD_Error_s  = DRV_BOARD_eERR_GENERAL_ERROR;

    DRV_BOARD_object_s.count = pParams->count;

    for (uint32_t id = 0; id < DRV_BOARD_object_s.count; id++)
    {
        DRV_BOARD_object_s.eeprom[id].instance = pParams->instance[id];
        DRV_BOARD_object_s.eeprom[id].handle   = DRV_EEPROM_getHandle(pParams->instance[id]);

        DRV_BOARD_Error_s = DRV_BOARD_isEepromAccessible(DRV_BOARD_object_s.eeprom[id].instance);

        if (DRV_BOARD_eERR_NO_ERROR == DRV_BOARD_Error_s)
        {
            if (CFG_BOARD_EEPROM_INSTANCE == id)
            {
                DRV_BOARD_Error_s = DRV_BOARD_getPCBRevision(DRV_BOARD_object_s.eeprom[id].handle, DRV_BOARD_pcbRev_s);

                if (DRV_BOARD_eERR_NO_ERROR != DRV_BOARD_Error_s)
                {
                    continue;
                }
            }

            DRV_BOARD_Error_s = DRV_BOARD_initMac(id);

            if (DRV_BOARD_eERR_NO_ERROR != DRV_BOARD_Error_s)
            {
                continue;
            }
        }
        else
        {
            switch(id)
            {
#if defined (SOC_AM261X) && defined (CFG_BOARD_EEPROM_ADDON_INSTANCE_NUM)
                case CFG_BOARD_EEPROM_ADDON_INSTANCE_1:
                {
                    /* Can't access EEPROM of AddOn board. Is it even connected? */
                    DRV_BOARD_Error_s = DRV_BOARD_eERR_ADDON_ACCESS_1;
                    goto laError;
                }
                break;
                case CFG_BOARD_EEPROM_ADDON_INSTANCE_2:
                {
                    /* Can't access EEPROM of AddOn board. Is it even connected? */
                    DRV_BOARD_Error_s = DRV_BOARD_eERR_ADDON_ACCESS_2;
                    goto laError;
                }
                break;
#endif
                default:
                {
                    /* Can't access EEPROM of main board. */
                    DRV_BOARD_Error_s = DRV_BOARD_eERR_MAIN_ACCESS;
                    goto laError;
                }
                break;
            }
        }
    }

    DRV_BOARD_Error_s = DRV_BOARD_eERR_NO_ERROR;

laError:
    return (uint32_t) DRV_BOARD_Error_s;
}

/**
 * \brief Retrieves the MAC address from board object.
 *
 * The board object needs to be initialized before this call. See #DRV_BOARD_init.
 *
 * \param id    The ID to the array of MAC addresses stored in object. ID is based on #DRV_BOARD_EMacId_t.
 * \param pMac  Pointer to the MAC address as output value
 *
 * \return uint32_t                                  Error code
 *
 * \retval #DRV_BOARD_eERR_NO_ERROR                  Success.
 * \retval #DRV_BOARD_eERR_GENERAL_ERROR             Negative default value.
 * \retval #DRV_BOARD_eERR_INVALID_MAC_ID            Used MAC ID is higher than allowed ID (see #DRV_BOARD_EMacId_t structure).
 * \retval #DRV_BOARD_eERR_MAC_LIST_NOT_INITIALIZED  List of MAC addresses is not initialized (see #DRV_BOARD_initMac function).
 */
uint32_t DRV_BOARD_getMac (uint32_t id, uint8_t** pMac)
{
    DRV_BOARD_Error_s = DRV_BOARD_eERR_NO_ERROR;

    if (DRV_BOARD_eMACID_MAX <= id )
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_INVALID_MAC_ID;
        goto laError;
    }

    if (false == DRV_BOARD_object_s.mac[id].initialized)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_MAC_LIST_NOT_INITIALIZED;
        goto laError;
    }

    *pMac = DRV_BOARD_object_s.mac[id].address;

    DRV_BOARD_Error_s = DRV_BOARD_eERR_NO_ERROR;

laError:

    return (uint32_t) DRV_BOARD_Error_s;
}

/**
 * \brief Retrieves the MAC address from MAC ID block stored in EEPROM.
 *
 * Retrieved value is stored in board object. See #DRV_BOARD_object_s.
 *
 * \param id    The ID to the array of the MAC addresses based on #DRV_BOARD_EMacId_t.
 *
 * \return uint32_t                             Error code
 *
 * \retval #DRV_BOARD_eERR_NO_ERROR             Success.
 * \retval #DRV_BOARD_eERR_GENERAL_ERROR        Negative default value.
 * \retval #DRV_BOARD_eERR_INVALID_MAC_ID       Used MAC ID is higher than allowed ID (see #DRV_BOARD_EMacId_t structure).
 * \retval #DRV_BOARD_eERR_LENGTH_INVALID       The buffer size is too small. At least 7 bytes are required.
 * \retval #DRV_BOARD_eERR_READ_FAILED          An error occurred while reading data from the EEPROM.
 * \retval #DRV_BOARD_eERR_NO_MAC_HEADER_FOUND  MAC header not found inside board info table.
 */
static uint32_t DRV_BOARD_initMac (uint32_t id)
{
    uint8_t aBuffer[DRV_BOARD_MACID_INFO_SIZE] = {0};

    DRV_BOARD_Error_s = DRV_BOARD_eERR_GENERAL_ERROR;

    if (DRV_BOARD_eMACID_MAX <= id )
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_INVALID_MAC_ID;
        goto laError;
    }

    DRV_BOARD_Error_s = DRV_BOARD_getInfoBlock(DRV_BOARD_object_s.eeprom[id].handle, DRV_BOARD_INFO_TYPE_MAC_ID, aBuffer, DRV_BOARD_MACID_INFO_SIZE);

    if (DRV_BOARD_eERR_NO_ERROR == DRV_BOARD_Error_s)
    {
        uint8_t* offset = aBuffer + sizeof(DRV_BOARD_INFO_SMacIdHeader_t);

        if (1 == DRV_BOARD_object_s.count)
        {
            /* Just main board present, all 3 MAC's stored in same EEPROM. */
            for (uint32_t i = 0; i < DRV_BOARD_eMACID_MAX; i++)
            {
                OSAL_MEMORY_memcpy(DRV_BOARD_object_s.mac[i].address, offset, DRV_BOARD_MAC_SIZE);
                offset += DRV_BOARD_MAC_SIZE;

                DRV_BOARD_object_s.mac[i].initialized = true;
            }
        }
        else
        {
            /* Main board and 2 AddOn boards present, each has unique MAC defined in own EEPROM stored. */
            OSAL_MEMORY_memcpy(DRV_BOARD_object_s.mac[id].address, offset, DRV_BOARD_MAC_SIZE);
            DRV_BOARD_object_s.mac[id].initialized = true;
        }
    }

laError:

    return (uint32_t) DRV_BOARD_Error_s;
}

/**
 * \brief
 * Retrieves information block from the board information stored in EEPROM.
 *
 * It tries to find the block based on block header.
 *
 * \param handle The handle to the EEPROM device.
 * \param blockId The ID of the block to retrieve.
 * \param pData The buffer to store the retrieved data.
 * \param maxLength The maximum length of the data to retrieve.
 *
 * \return uint32_t                             Error code
 *
 * \retval #DRV_BOARD_eERR_NO_ERROR             Success.
 * \retval #DRV_BOARD_eERR_GENERAL_ERROR        Negative default value.
 * \retval #DRV_BOARD_eERR_LENGTH_INVALID       The buffer size is too small. At least 7 bytes are required.
 * \retval #DRV_BOARD_eERR_READ_FAILED          An error occurred while reading data from the EEPROM.
 * \retval #DRV_BOARD_eERR_NO_MAC_HEADER_FOUND  MAC header not found inside board info table.
 */
static uint32_t DRV_BOARD_getInfoBlock(EEPROM_Handle handle, uint8_t blockId, uint8_t *pData, uint32_t maxLength)
{
    bool infoFound = false;

    DRV_BOARD_Error_s = DRV_BOARD_eERR_GENERAL_ERROR;

    if (sizeof(DRV_BOARD_INFO_SMainHeader_t) > maxLength)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_LENGTH_INVALID;
        goto laError;
    }

    DRV_BOARD_Error_s = DRV_EEPROM_read(handle, 0, pData, sizeof(DRV_BOARD_INFO_SMainHeader_t));

    if (DRV_BOARD_eERR_NO_ERROR != DRV_BOARD_Error_s)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_READ_FAILED;
        goto laError;
    }
    else
    {
        DRV_BOARD_INFO_SMainHeader_t *pMainHeader = (DRV_BOARD_INFO_SMainHeader_t *)pData;
        uint16_t payloadSize = pMainHeader->payloadSize;

        if ((DRV_BOAD_INFO_MAGIC_NUM == pMainHeader->magicNum) &&
            (DRV_BOARD_INFO_TYPE_MAIN == pMainHeader->type))
        {
            for (uint16_t offset = sizeof(DRV_BOARD_INFO_SMainHeader_t); offset < payloadSize; )
            {
                DRV_BOARD_Error_s = DRV_EEPROM_read(handle,
                                                    offset,
                                                    pData,
                                                    sizeof(DRV_BOARD_INFO_SBlockHeader_t));

                if (DRV_BOARD_eERR_NO_ERROR != DRV_BOARD_Error_s)
                {
                    DRV_BOARD_Error_s = DRV_BOARD_eERR_READ_FAILED;
                    goto laError;
                }

                DRV_BOARD_INFO_SBlockHeader_t *pBlockHeader = (DRV_BOARD_INFO_SBlockHeader_t *)pData;

                if (blockId == pBlockHeader->type)
                {
                    uint16_t readSize = maxLength;

                    if (maxLength > pBlockHeader->length)
                    {
                        readSize = pBlockHeader->length;
                    }

                    DRV_BOARD_Error_s = DRV_EEPROM_read(handle,
                                                        offset + sizeof(DRV_BOARD_INFO_SBlockHeader_t),
                                                        pData + sizeof(DRV_BOARD_INFO_SBlockHeader_t),
                                                        readSize - sizeof(DRV_BOARD_INFO_SBlockHeader_t));

                    if (DRV_BOARD_eERR_NO_ERROR != DRV_BOARD_Error_s)
                    {
                        DRV_BOARD_Error_s = DRV_BOARD_eERR_READ_FAILED;
                        goto laError;
                    }

                    DRV_BOARD_Error_s = DRV_BOARD_eERR_NO_ERROR;

                    infoFound = true;
                    break;
                }
                else
                {
                    offset += sizeof(DRV_BOARD_INFO_SBlockHeader_t) + pBlockHeader->length;
                }
            }
        }
    }

    if (true != infoFound)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_NO_MAC_HEADER_FOUND;
    }

laError:
    if (DRV_BOARD_eERR_NO_ERROR != DRV_BOARD_Error_s)
    {
        OSAL_MEMORY_memset(pData, 0, maxLength);
    }

    return (uint32_t) DRV_BOARD_Error_s;
}

/**
 * \brief
 * Checks whether specified EEPROM instance of AddOn or Main board is accessible.
 *
 * It tries to find the block based on block header.
 *
 * \param instance EEPROM instance of AddOn or Main board configured in SysConfig.
 *
 * \return uint32_t                               Error code
 *
 * \retval #DRV_BOARD_eERR_NO_ERROR               Success.
 * \retval #DRV_BOARD_eERR_GENERAL_ERROR          Negative default value.
 * \retval #DRV_BOARD_eERR_INSTANCE_INVALID       AddOn EEPROM instance is not defined for this board.
 * \retval #DRV_BOARD_eERR_INSTANCE_NUM_INVALID   Specified instance ID is higher as number of EEPROM instances configured in SysConfig.
 * \retval #DRV_BOARD_eERR_I2C_HANDLE_INVALID     Invalid I2c handle.
 * \retval #DRV_BOARD_eERR_EEPROM_NOT_ACCESSIBLE  EEPROM is not accessible.
  */
static uint32_t DRV_BOARD_isEepromAccessible (uint32_t instance)
{
    uint8_t    address  = 0;
    int32_t    status   = SystemP_FAILURE;
    I2C_Handle handle   = NULL;

    DRV_BOARD_Error_s = DRV_BOARD_eERR_GENERAL_ERROR;

    if (CFG_BOARD_INSTANCE_INVALID == instance)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_INSTANCE_INVALID;
        goto laError;
    }

    if (CONFIG_EEPROM_NUM_INSTANCES <=  instance)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_INSTANCE_NUM_INVALID;
        goto laError;
    }

    EEPROM_Params* pEeParams = &gEepromParams[instance];

    handle  = gI2cHandle[pEeParams->driverInstance];
    address = pEeParams->i2cAddress;

    if (NULL == handle)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_I2C_HANDLE_INVALID;
        goto laError;
    }

    status = I2C_probe(handle, address);

    if(SystemP_FAILURE == status)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_EEPROM_NOT_ACCESSIBLE;
        goto laError;
    }

    DRV_BOARD_Error_s = DRV_BOARD_eERR_NO_ERROR;

laError:

    return (uint32_t) DRV_BOARD_Error_s;
}

/*
 * \brief
 * Reads PCB revision from board info table.
 *
 * \param handle The handle to the EEPROM device.
 * \param pRevision Place holder where result needs to be stored.
 *
 * \return uint32_t                               Error code
 *
 * \retval #DRV_BOARD_eERR_NO_ERROR               Success.
 * \retval #DRV_BOARD_eERR_GENERAL_ERROR          Negative default value.
 * \retval #DRV_BOARD_eERR_INSTANCE_INVALID       AddOn EEPROM instance is not defined for this board.
 * \retval #DRV_BOARD_eERR_INSTANCE_NUM_INVALID   Specified instance ID is higher as number of EEPROM instances configured in SysConfig.
 * \retval #DRV_BOARD_eERR_I2C_HANDLE_INVALID     Invalid I2c handle.
 * \retval #DRV_BOARD_eERR_EEPROM_NOT_ACCESSIBLE  EEPROM is not accessible.
 */
static uint32_t DRV_BOARD_getPCBRevision (EEPROM_Handle handle, const char* pRevision)
{
    DRV_BOARD_Error_s = DRV_BOARD_eERR_GENERAL_ERROR;

    DRV_BOARD_Error_s = DRV_EEPROM_read(handle,
                                        DRV_BOARD_INFO_PCB_REVISION_OFFSET,
                                        (const uint8_t*) pRevision,
                                        DRV_BOARD_INFO_PCB_REVISION_SIZE);

    if (DRV_BOARD_eERR_NO_ERROR != DRV_BOARD_Error_s)
    {
        DRV_BOARD_Error_s = DRV_BOARD_eERR_READ_FAILED;
        goto laError;
    }

    DRV_BOARD_Error_s = DRV_BOARD_eERR_NO_ERROR;

laError:

    return (uint32_t) DRV_BOARD_Error_s;
}

/*
 * \brief
 * Compares the required revision string with revision string stored in EEPROM.
 *
 * \param pRequired Pointer to required revision string to compare.
 *
 * \return uint32_t                               Error code
 *
 * \retval #DRV_BOARD_eERR_NO_ERROR               Success.
 * \retval #DRV_BOARD_eERR_GENERAL_ERROR          Negative default value.
 * \retval #DRV_BOARD_eERR_INSTANCE_INVALID       AddOn EEPROM instance is not defined for this board.
 * \retval #DRV_BOARD_eERR_INSTANCE_NUM_INVALID   Specified instance ID is higher as number of EEPROM instances configured in SysConfig.
 * \retval #DRV_BOARD_eERR_I2C_HANDLE_INVALID     Invalid I2c handle.
 * \retval #DRV_BOARD_eERR_EEPROM_NOT_ACCESSIBLE  EEPROM is not accessible.
 */
bool DRV_BOARD_isRequiredPcbRev(const char* pRequired)
{
    bool result = false;

    if (0 == strncmp(pRequired, DRV_BOARD_pcbRev_s, DRV_BOARD_INFO_PCB_REVISION_SIZE))
    {
        result = true;
    }

    return result;
}
