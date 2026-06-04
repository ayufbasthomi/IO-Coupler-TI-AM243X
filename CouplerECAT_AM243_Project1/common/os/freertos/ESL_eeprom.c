/*!
 *  \file ESL_eeprom.c
 *
 *  \brief
 *  EEPROM load and store function.
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

#include <defines/ecSlvApiDef.h>
#include <ecSlvApi.h>
#include <ESL_eeprom.h>
#include <osal.h>
#include "ti_board_config.h"
#include <string.h>
#if !(defined FBTLPROVIDER) || (FBTLPROVIDER==0)
#include "nvm.h"
#endif


#define EEPROM_EMPTY_DATA   (0xFFFFFFFFU)
#if defined(ENABLE_TIDEP01032_REF_DESIGN_CONFIG)
#define APP_OSPI_FLASH_OFFSET_BASE  (0x200000U)
static uint32_t flashForceErase = 0;
#else
#define EEPROM_DATA_OFFSET  (0x200U)
#endif // ENABLE_TIDEP01032_REF_DESIGN_CONFIG

#define DRV_BOARD_INFO_TYPE_MAIN           0x01
#define DRV_BOARD_INFO_TYPE_BOARD_ID       0x10
#define DRV_BOARD_INFO_TYPE_MAC_ID         0x13
#define DRV_BOARD_MAC_SIZE                 0x06

/// EEPROM header containing magic key and data length parameters
typedef struct ESL_EEP_header
{
    uint32_t    magicKey;
    uint32_t    dataSize;
} ESL_EEP_header_t;

typedef struct ESL_EEP_BOARD_INFO_SMainHeader
{
    uint32_t magicNum;
    uint8_t  type;
    uint16_t payloadSize;
} __attribute__((packed)) ESL_EEP_BOARD_INFO_SMainHeader_t;

typedef struct ESL_EEP_BOARD_INFO_SBlockHeader
{
    uint8_t type;
    uint16_t length;
} __attribute__((packed)) ESL_EEP_BOARD_INFO_SBlockHeader_t;

typedef struct ESL_EEP_BOARD_INFO_SBoardInfoHeader_t
{
    uint8_t type;
    uint16_t length;
    uint8_t name[16];
    uint8_t designRev[2];
} __attribute__((packed)) ESL_EEP_BOARD_INFO_SBoardInfoHeader_t;

typedef struct ESL_EEP_BOARD_INFO_SMacAddrHeader
{
    uint8_t type;
    uint16_t length;
    uint16_t control;
    uint8_t address[DRV_BOARD_MAC_SIZE * 3];
} __attribute__((packed)) ESL_EEP_BOARD_INFO_SMacAddrHeader_t;

/// EtherCAT EEPROM header containing identity data. Refer to ETG2010 Table 2
typedef struct ESL_EEP_EC_info
{
    uint16_t pdiControl;
    uint16_t pdiConfiguration;

    uint16_t syncImpulseLen;
    uint16_t pdiConfiguration2;

    uint16_t stationAlias;
    uint16_t reserved[2];
    uint16_t checksum;

    uint32_t vendorID;
    uint32_t productCode;
    uint32_t revisionNo;
    uint32_t serialNo;
}ESL_EEP_EC_info_t;

static ESL_EEP_EC_info_t ESL_EEP_identity = {0};

static bool EC_SLV_APP_EEP_verifyIntegrity(ESL_EEP_EC_info_t *pAppInfo);

/*! <!-- Description: -->
 *
 *  \brief
 *  Initialize EEPROM instance
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pContext  application context
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  #include <ESL_eeprom.h>
 *
 *  // required variables
 *  void* pHandle;
 *
 *  // the Call
 *  EC_SLV_APP_EEP_init(pHandle);
 *  \endcode
 *
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP_EEPROM
 *
 * */
void EC_SLV_APP_EEP_init(void* pContext)
{
    OSALUNREF_PARM(pContext);

#if !(defined FBTLPROVIDER) || (FBTLPROVIDER==0)
    NVM_APP_init(OSAL_TASK_Prio_Normal);
#endif
}

/*! <!-- Description: -->
 *
 *  \brief
 *  Write EEPROM to memory.
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pContext      Application context
 *  \param[in]  pEeprom     Pointer to eeprom memory address.
 *  \param[in]  length    Eeprom length.
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  #include <ecSlvApi.h>
 *
 *  // required variables
 *  void*       handle;
 *  uint16_t    pEeprom[0x400];
 *  uint32_t    length = 0x400;
 *
 *  // the Call
 *  EC_SLV_APP_EEP_write(handle, pEeprom, length);
 *  \endcode
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP_EEPROM
 *
 * */
void EC_SLV_APP_EEP_write(void *pContext, void* pEeprom, uint32_t length)
{
    uint32_t magicKey  = (uint32_t)pContext;
    ESL_EEP_header_t* pageHead = NULL;
    bool validIdentity = false;

#if !(defined FBTLPROVIDER) || (FBTLPROVIDER==0)

    if(pEeprom != NULL && length > sizeof(ESL_EEP_EC_info_t))
    {
        //Read current EEPROM content and compare with application data
        validIdentity = EC_SLV_APP_EEP_verifyIntegrity((ESL_EEP_EC_info_t *)pEeprom);
    }
    if(!validIdentity)
    {
        //Allocate memory for header and eeprom data
        pageHead = (ESL_EEP_header_t*)OSAL_MEMORY_calloc(sizeof(ESL_EEP_header_t)+ length,
                                                          sizeof(uint8_t));
        if(pageHead != NULL)
        {
            pageHead[0].magicKey = magicKey;
            pageHead[0].dataSize = length;
            OSAL_MEMORY_memcpy((void*)&pageHead[1], pEeprom, length);

            //If application data is different to EEPROM content, overwrite EEPROM content
#if defined(ENABLE_TIDEP01032_REF_DESIGN_CONFIG)
            NVM_APP_write(NVM_TYPE_FLASH,
                          CONFIG_FLASH0,
                          APP_OSPI_FLASH_OFFSET_BASE,
                          sizeof(ESL_EEP_header_t) + length,
                          pageHead,
                          flashForceErase);
#else
            NVM_APP_write(NVM_TYPE_EEPROM,
                          CONFIG_EEPROM0,
                          EEPROM_DATA_OFFSET,
                          sizeof(ESL_EEP_header_t) + length,
                          pageHead,
                          false);
#endif // ENABLE_TIDEP01032_REF_DESIGN_CONFIG
        }
        OSAL_MEMORY_free(pageHead);
    }
#endif
}

/*! <!-- Description: -->
 *
 *  \brief
 *  Load EEPROM from memory.
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pContext      application context
 *  \param[out] pEeprom       Pointer to eeprom memory address.
 *  \param[out] pLength       Eeprom length.
 *
 *  \return     bool            eeprom loaded correctly or not.
 *
 *
 *  <!-- Example: -->
 *
 *  \par Example
 *  \code{.c}
 *  #include <ecSlvApi.h>
 *
 *  // required variables
 *  uint8_t* pEeprom = NULL;;
 *  uint32_t length;
 *
 *  // the Call
 *  pEeprom = (uint8_t*)OSAL_MEMORY_calloc(length, sizeof(uint8_t));
 *  EC_SLV_APP_EEP_read(pEeprom, &length);
 *  \endcode
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP_EEPROM
 *
 * */
bool EC_SLV_APP_EEP_read(void*pContext, void*pEeprom, uint32_t *pLength)
{
    bool ret = false;
    ESL_EEP_header_t pageProto = {0};
    uint32_t magicKey = (uint32_t)pContext;

#if !(defined FBTLPROVIDER) || (FBTLPROVIDER==0)
    uint32_t error = NVM_SUCCESS;

    OSAL_MEMORY_memset(&ESL_EEP_identity, 0, sizeof(ESL_EEP_EC_info_t));

    //Read EEPROM header (magic key and eeprom length)
#if defined(ENABLE_TIDEP01032_REF_DESIGN_CONFIG)
    error = NVM_APP_read(NVM_TYPE_FLASH,
                 CONFIG_FLASH0,
                 APP_OSPI_FLASH_OFFSET_BASE,
                 sizeof(ESL_EEP_header_t),
                 &pageProto);
#else
    error = NVM_APP_read(NVM_TYPE_EEPROM,
                 CONFIG_EEPROM0,
                 EEPROM_DATA_OFFSET,
                 sizeof(ESL_EEP_header_t),
                 &pageProto);
#endif // ENABLE_TIDEP01032_REF_DESIGN_CONFIG

    if(error == NVM_SUCCESS &&
        pageProto.magicKey == magicKey &&
        pageProto.dataSize != EEPROM_EMPTY_DATA)
    {
        //Read EEPROM data (EtherCAT EEPROM content)
#if defined(ENABLE_TIDEP01032_REF_DESIGN_CONFIG)
        error = NVM_APP_read(NVM_TYPE_FLASH,
                             CONFIG_FLASH0,
                             APP_OSPI_FLASH_OFFSET_BASE + sizeof(ESL_EEP_header_t),
                             pageProto.dataSize,
                             pEeprom);
#else
        error = NVM_APP_read(NVM_TYPE_EEPROM,
                             CONFIG_EEPROM0,
                             EEPROM_DATA_OFFSET + sizeof(ESL_EEP_header_t),
                             pageProto.dataSize,
                             pEeprom);
#endif // ENABLE_TIDEP01032_REF_DESIGN_CONFIG

        if (error == NVM_SUCCESS && pLength != NULL)
        {
            *pLength = pageProto.dataSize;

            //Copy EtherCAT info for verification
            if(pageProto.dataSize > sizeof(ESL_EEP_EC_info_t))
            {
                OSAL_MEMORY_memcpy(&ESL_EEP_identity, pEeprom, sizeof(ESL_EEP_EC_info_t));
            }
            ret = true;
        }
    }
#endif
    return ret;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Verify EEPROM Cache VendorID, Product Code, Revision Number and CRC.
 *
 *  \details
 *  This function compares the EtherCAT identity data and the CRC between the EEPROM data and
 *  the application data. If this data is the same, then the EEPROM write function does not
 *  write into the EEPROM to prevent physical wearing.

 *  \remarks
 *  The EtherCAT stack calls EEPROM read and therefore the identity data and CRC are known.
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pAppInfo    EtherCAT EEPROM info from application.
 *  \return     bool        EEPROM Identity correct or not.
 *
 *  <!-- Group: -->
 *
 *  \ingroup SLVAPI
 *
 * */
static bool EC_SLV_APP_EEP_verifyIntegrity(ESL_EEP_EC_info_t *pAppInfo)
{
    bool ret = false;
    if(pAppInfo != NULL)
    {
        if(pAppInfo->vendorID == ESL_EEP_identity.vendorID &&
           pAppInfo->productCode == ESL_EEP_identity.productCode &&
           pAppInfo->revisionNo == ESL_EEP_identity.revisionNo &&
           pAppInfo->checksum == ESL_EEP_identity.checksum)
        {
            ret = true;
        }
    }
    return ret;
}

/*!
 *  <!-- Description: -->
 *
 *  \\brief
 *  Retrieves information block from the board information stored in EEPROM.
 *
 *  \\details
 *  This function tries to find the block based on block type. It first reads the
 *  main header to verify the magic number and type, then iterates through blocks
 *  until it finds the requested block type.
 *
 *  <!-- Parameters and return values: -->
 *
 *  \\param[in]  pMagicKey     Magic key for EEPROM validation.
 *  \\param[in]  blockId       The ID of the block to retrieve.
 *  \\param[out] pBuffer       Pointer to buffer where the data will be stored.
 *  \\param[in,out] pLength    Size of the buffer on input, actual read size on output.
 *  \\return     bool          True if data was read successfully, false otherwise.
 *
 *  <!-- Group: -->
 *
 *  \\ingroup EC_SLV_APP
 *
 * */
static bool EC_SLV_APP_EEP_getInfoBlock(void *pMagicKey, uint8_t blockId, void *pBuffer, uint32_t *pLength)
{

#if defined(ENABLE_TIDEP01032_REF_DESIGN_CONFIG)
    return false;
#else

    bool infoFound = false;

    if (pMagicKey == NULL || pBuffer == NULL || pLength == NULL || *pLength == 0)
    {
        return false;
    }

#if !(defined FBTLPROVIDER) || (FBTLPROVIDER==0)
    ESL_EEP_BOARD_INFO_SMainHeader_t mainHeader = {0};
    uint32_t magicKey = (uint32_t)pMagicKey;

    // Read the main header first
    uint32_t error = NVM_APP_read(NVM_TYPE_EEPROM,
                          CONFIG_EEPROM0,
                          0,
                          sizeof(ESL_EEP_BOARD_INFO_SMainHeader_t),
                          (uint8_t*)&mainHeader);

    if (error != NVM_SUCCESS)
    {
        return false;
    }

    // Check magic key and type
    if ((magicKey == mainHeader.magicNum) &&
        (DRV_BOARD_INFO_TYPE_MAIN == mainHeader.type))
    {
        // Temporary buffer for block headers
        ESL_EEP_BOARD_INFO_SBlockHeader_t blockHeader = {0};

        // Iterate through blocks starting after the main header
        for (uint16_t offset = sizeof(ESL_EEP_BOARD_INFO_SMainHeader_t);
             offset < mainHeader.payloadSize; )
        {
            // Read the block header
            error = NVM_APP_read(NVM_TYPE_EEPROM,
                          CONFIG_EEPROM0,
                          offset,
                          sizeof(ESL_EEP_BOARD_INFO_SBlockHeader_t),
                          (uint8_t*)&blockHeader);

            if (error != NVM_SUCCESS)
            {
                break;
            }

            // Check if this is the block we're looking for
            if (blockId == blockHeader.type)
            {
                uint32_t readSize = *pLength;

                // Adjust read size if buffer is smaller than block length
                if (readSize > blockHeader.length)
                {
                    readSize = blockHeader.length;
                }

                // Read the block data
                error = NVM_APP_read(NVM_TYPE_EEPROM,
                              CONFIG_EEPROM0,
                              offset,
                              readSize,
                              (uint8_t*)pBuffer);

                if (error == NVM_SUCCESS)
                {
                    *pLength = readSize;
                    infoFound = true;
                }

                break;
            }
            else
            {
                // Move to the next block
                offset += sizeof(ESL_EEP_BOARD_INFO_SBlockHeader_t) + blockHeader.length;
            }
        }
    }
#endif

    return infoFound;

#endif // ENABLE_TIDEP01032_REF_DESIGN_CONFIG
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Retrieves the MAC address from the EEPROM.
 *
 *  \details
 *  This function reads the MAC address block from EEPROM using block type identification
 *  and extracts the 6-byte MAC address from the header structure. The MAC address is
 *  copied to the provided buffer if the read operation is successful.
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pMagicKey     Magic key for EEPROM validation.
 *  \param[out] pMacAddress   Pointer to store the 6-byte MAC address.
 *  \return     bool          True if MAC address was read successfully, false otherwise.
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP_EEPROM
 *
 * */
bool EC_SLV_APP_EEP_getMacAddress(void *pMagicKey, uint8_t *pMacAddress)
{
    bool ret = false;

    if (pMagicKey != NULL && pMacAddress != NULL)
    {
        uint8_t buffer[sizeof(ESL_EEP_BOARD_INFO_SMacAddrHeader_t)] = {0};
        uint32_t bufferSize = sizeof(ESL_EEP_BOARD_INFO_SMacAddrHeader_t);

        // Read the MAC address block from EEPROM using block type
        ret = EC_SLV_APP_EEP_getInfoBlock(pMagicKey, DRV_BOARD_INFO_TYPE_MAC_ID, buffer, &bufferSize);

        if (ret)
        {
            // Extract the MAC address from the buffer
            ESL_EEP_BOARD_INFO_SMacAddrHeader_t *pMacHeader =
                (ESL_EEP_BOARD_INFO_SMacAddrHeader_t *)buffer;

            // Copy just the first MAC address (6 bytes) to the output parameter
            memcpy(pMacAddress, pMacHeader->address, 6);
        }
    }

    return ret;
}

/*!
 *  <!-- Description: -->
 *
 *  \brief
 *  Retrieves the board revision information from the EEPROM.
 *
 *  \details
 *  This function reads the board information block from EEPROM using block type identification
 *  and extracts the 2-byte design revision. The revision data is copied to the provided buffer
 *  if the read operation is successful.
 *
 *  <!-- Parameters and return values: -->
 *
 *  \param[in]  pMagicKey       Magic key for EEPROM validation.
 *  \param[out] pBoardRevision  Pointer to store the 2-byte board revision.
 *  \return     bool            True if board revision was read successfully, false otherwise.
 *
 *  <!-- Group: -->
 *
 *  \ingroup EC_SLV_APP_EEPROM
 *
 * */
bool EC_SLV_APP_EEP_getBoardRevision(void *pMagicKey, uint8_t *pBoardRevision)
{
    bool ret = false;

    if (pMagicKey != NULL && pBoardRevision != NULL)
    {
        uint8_t buffer[sizeof(ESL_EEP_BOARD_INFO_SBoardInfoHeader_t)] = {0};
        uint32_t bufferSize = sizeof(ESL_EEP_BOARD_INFO_SBoardInfoHeader_t);

        // Read the board info block from EEPROM using block type
        ret = EC_SLV_APP_EEP_getInfoBlock(pMagicKey, DRV_BOARD_INFO_TYPE_BOARD_ID, buffer, &bufferSize);

        if (ret)
        {
            // Extract the board revision from the buffer
            ESL_EEP_BOARD_INFO_SBoardInfoHeader_t *pInfoHeader =
                (ESL_EEP_BOARD_INFO_SBoardInfoHeader_t *)buffer;

            // Copy just the Board revision (2 bytes) to the output parameter
            memcpy(pBoardRevision, pInfoHeader->designRev, 2);
        }
    }

    return ret;
}

//*************************************************************************************************
