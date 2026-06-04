/*!
 *  \file cust_drivers.h
 *
 *  \brief
 *  Custom drivers support.
 *
 *  \author
 *  Texas Instruments Incorporated
 *
 *  \copyright
 *  Copyright (C) 2022 Texas Instruments Incorporated
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

#if !(defined PROTECT_CUST_DRIVERS_H)
#define PROTECT_CUST_DRIVERS_H      1

#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "kernel/dpl/TaskP.h"

#include "board/eeprom.h"
#include "board/ethphy.h"
#include "board/flash.h"
#include "board/led.h"

#include "eeprom/cust_eeprom.h"
#include "ethphy/cust_ethphy.h"
#include "flash/cust_flash.h"
#include "ioexp/cust_ioexp.h"
#include "led/cust_led.h"

/*!
 *  \brief
 *  Custom driver error codes.
 */
typedef enum CUST_DRIVERS_EError
{
    CUST_DRIVERS_eERR_NOERROR               =  0,   /*!< No error, everything is fine. */
    CUST_DRIVERS_eERR_NO_NVM_STORAGE        = -21,  /*!< No storage defined for non-volatile configuration data */
    CUST_DRIVERS_eERR_EEPROM_HANDLE_INVALID = -20,  /*!< EEPROM handle is invalid */
    CUST_DRIVERS_eERR_EEPROM_DATA_INVALID   = -19,  /*!< Pointer to data for eeprom write is invalid. */
    CUST_DRIVERS_eERR_EEPROM_LENGTH_INVALID = -18,  /*!< Length of data for eeprom write is invalid. */
    CUST_DRIVERS_eERR_EEPROM_READ           = -17,  /*!< EEPROM read error. */
    CUST_DRIVERS_eERR_EEPROM_WRITE          = -16,  /*!< EEPROM write error. */
    CUST_DRIVERS_eERR_EEPROM                = -15,  /*!< EEPROM driver error. */
    CUST_DRIVERS_eERR_FLASH_HANDLE_INVALID  = -14,  /*!< FLASH handle is invalid. */
    CUST_DRIVERS_eERR_FLASH_DATA_INVALID    = -13,  /*!< Pointer to data to be flashed is invalid. */
    CUST_DRIVERS_eERR_FLASH_LENGTH_INVALID  = -12,  /*!< Length of data to be flashed is invalid. */
    CUST_DRIVERS_eERR_FLASH_READ            = -11,  /*!< FLASH read error. */
    CUST_DRIVERS_eERR_FLASH_WRITE           = -10,  /*!< FLASH write error. */
    CUST_DRIVERS_eERR_FLASH_ERASE           = -9,   /*!< FLASH erase error. */
    CUST_DRIVERS_eERR_FLASH_OFFSET          = -8,   /*!< FLASH offset error. */
    CUST_DRIVERS_eERR_FLASH                 = -7,   /*!< FLASH driver error. */
    CUST_DRIVERS_eERR_ETHPHY                = -6,   /*!< PHY driver error. */
    CUST_DRIVERS_eERR_UART                  = -5,   /*!< UART driver error. */
    CUST_DRIVERS_eERR_LED                   = -4,   /*!< LED driver error. */
    CUST_DRIVERS_eERR_IOEXP                 = -3,   /*!< IOEXP driver error. */
    CUST_DRIVERS_eERR_PRUICSS               = -2,   /*!< PRU-ICSS driver error. */
    CUST_DRIVERS_eERR_GENERALERROR          = -1    /*!< General error */
} CUST_DRIVERS_EError_t;

/*!
 *  \brief
 *  Custom Device Types definition.
 */
typedef enum CUST_DEVICE_Type
{
    CUST_DEVICE_TypeEthphy,   /*!< ETHPHY device supported by SysConfig */
    CUST_DEVICE_TypeEeprom,   /*!< EEPROM device supported by SysConfig */
    CUST_DEVICE_TypeFlash,    /*!< FLASH device supported by SysConfig */
    CUST_DEVICE_TypeLed       /*!< LED device supported by SysConfig */
} CUST_DEVICE_Type_t;

/*!
 *  \brief
 *  Drivers initialization parameters.
 */
typedef struct CUST_DRIVERS_SInit
{
    uint32_t                        pruIcssId;      /* Instance of PRU-ICSS block used by stack (as defined by SysConfig) */
    CUST_ETHPHY_SInit_t             ethPhy;         /* ETH PHY parameters */
    CUST_EEPROM_SInit_t             eeprom;         /* EEPROM parameters */
    CUST_FLASH_SInit_t              flash;          /* FLASH parameters */
    CUST_LED_SInit_t                led;            /* LED parameters */
}CUST_DRIVERS_SInit_t;

#if (defined __cplusplus)
extern "C" {
#endif

extern uint32_t  CUST_DRIVERS_init       (CUST_DRIVERS_SInit_t* pParams_p);
extern uint32_t  CUST_DRIVERS_deinit     (void);

#if (defined __cplusplus)
}
#endif

#endif /* PROTECT_CUST_DRIVERS_H */
