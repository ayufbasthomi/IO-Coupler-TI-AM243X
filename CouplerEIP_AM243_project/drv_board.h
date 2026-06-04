/*!
 *  \example drv_board.h
 *
 *  \brief
 *  Evaluation board info related definitions
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

#ifndef DRV_BOARD
#define DRV_BOARD

/*!
 *  \brief
 *  BOARD driver error codes.
 */
typedef enum DRV_BOARD_EError
{
    DRV_BOARD_eERR_NO_ERROR                 = 0x00000000u,  /*!< Success. */
    DRV_BOARD_eERR_GENERAL_ERROR            = 0x001f0000u,  /*!< Negative default value. */
    DRV_BOARD_eERR_MAIN_ACCESS              = 0x001f0001u,  /*!< No access to main board EEPROM. */
    DRV_BOARD_eERR_ADDON_ACCESS_1           = 0x001f0002u,  /*!< No access to Add-On board EEPROM of port 1. */
    DRV_BOARD_eERR_ADDON_ACCESS_2           = 0x001f0003u,  /*!< No access to Add-On board EEPROM of port 2. */
    DRV_BOARD_eERR_INVALID_MAC_ID           = 0x001f0004u,  /*!< Used MAC ID is higher than allowed ID (see #DRV_BOARD_EMacId_t structure). */
    DRV_BOARD_eERR_MAC_LIST_NOT_INITIALIZED = 0x001f0005u,  /*!< List of MAC addresses is not initialized (see #DRV_BOARD_initMac function). */
    DRV_BOARD_eERR_LENGTH_INVALID           = 0x001f0006u,  /*!< The buffer size is too small. At least 7 bytes are required. */
    DRV_BOARD_eERR_NO_MAC_HEADER_FOUND      = 0x001f0007u,  /*!< MAC header not found inside board info table. */
    DRV_BOARD_eERR_INSTANCE_INVALID         = 0x001f0008u,  /*!< AddOn EEPROM instance is not defined for this board. */
    DRV_BOARD_eERR_INSTANCE_NUM_INVALID     = 0x001f0009u,  /*!< Specified instance ID is higher as number of EEPROM instances configured in SysConfig. */
    DRV_BOARD_eERR_I2C_HANDLE_INVALID       = 0x001f000au,  /*!< Invalid I2c handle. */
    DRV_BOARD_eERR_EEPROM_NOT_ACCESSIBLE    = 0x001f000bu,  /*!< EEPROM is not accessible. */
    DRV_BOARD_eERR_READ_FAILED              = 0x001f000cu,  /*!< An error occurred while reading data from the EEPROM. */
}DRV_BOARD_EError_t;

#define DRV_BOAD_INFO_MAGIC_NUM            0xEE3355AA

#define DRV_BOARD_INFO_TYPE_MAIN           0x01
#define DRV_BOARD_INFO_TYPE_BOARD_ID       0x10
#define DRV_BOARD_INFO_TYPE_DDR_ID         0x12
#define DRV_BOARD_INFO_TYPE_MAC_ID         0x13

#define DRV_BOARD_MAC_SIZE                 0x06

#define DRV_BOARD_MACID_INFO_SIZE (sizeof(DRV_BOARD_INFO_SMacIdHeader_t) + (DRV_BOARD_MAC_SIZE * 3))

#define DRV_BOARD_INFO_PCB_REVISION_OFFSET 0x22
#define DRV_BOARD_INFO_PCB_REVISION_SIZE   0x02


/*!
  *  \brief Enumeration of indexes to MAC address array.
  */
typedef enum DRV_BOARD_EMacId
{
    DRV_BOARD_eMACID_MAIN_BOARD = 0,  /*!< Index of main board MAC address in the MAC address array. */
    DRV_BOARD_eMACID_PORT_1     = 1,  /*!< Index of ETHERNET port 1 MAC address in the MAC address array. */
    DRV_BOARD_eMACID_PORT_2     = 2,  /*!< Index of ETHERNET port 2 MAC address in the MAC address array. */
    DRV_BOARD_eMACID_MAX        = 3   /*!< Maximum number of MAC addresses in the MAC address array. */
} DRV_BOARD_EMacId_t;

/*!
 *  \brief
 *  Application UART driver initialization parameters.
 */
typedef struct DRV_BOARD_SInit
{
    uint32_t count;                           /* Count of boards including AddOn boards */
    uint32_t instance[DRV_BOARD_eMACID_MAX];  /* EEPROM SysConfig instance with stored board information for each board */
}DRV_BOARD_SInit_t;

/*!
  *  \brief Structure to store the board info header.
  */
typedef struct DRV_BOARD_INFO_SMainHeader
{
    uint32_t magicNum;
    uint8_t  type;
    uint16_t payloadSize;
} __attribute__((packed)) DRV_BOARD_INFO_SMainHeader_t;

/*!
 *  \brief Structure to store the block header
 */
typedef struct DRV_BOARD_INFO_SBlockHeader
{
    uint8_t  type;
    uint16_t length;
} __attribute__((packed)) DRV_BOARD_INFO_SBlockHeader_t;

/*!
 *  \brief Structure to store the block MAC ID header
 */
typedef struct DRV_BOARD_INFO_SMacIdHeader
{
    uint8_t type;
    uint16_t length;
    uint16_t control;
} __attribute__((packed)) DRV_BOARD_INFO_SMacIdHeader_t;

typedef struct DRV_BOARD_SMac
{
    uint8_t  address[DRV_BOARD_MAC_SIZE];
    bool     initialized;
}DRV_BOARD_SMac_t;

typedef struct DRV_BOARD_SEeprom
{
    uint8_t       instance;
    EEPROM_Handle handle;
}DRV_BOARD_SEeprom_t;

typedef struct DRV_BOARD_SObject
{
    uint32_t            count;
    DRV_BOARD_SEeprom_t eeprom[DRV_BOARD_eMACID_MAX];
    DRV_BOARD_SMac_t    mac[DRV_BOARD_eMACID_MAX];
}DRV_BOARD_SObject_t;


#ifdef __cplusplus
extern "C" {
#endif

uint32_t DRV_BOARD_init             (DRV_BOARD_SInit_t* pParams);
uint32_t DRV_BOARD_getMac           (uint32_t id, uint8_t** pMac);
bool     DRV_BOARD_isRequiredPcbRev (const char* pRequired);

#ifdef  __cplusplus
}
#endif

#endif // DRV_BOARD
