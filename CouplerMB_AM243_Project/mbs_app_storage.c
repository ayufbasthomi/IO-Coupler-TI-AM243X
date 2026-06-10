/*!
 *  \file mbs_app_storage.c
 *
 *  \brief
 *  Storage implementation for Modbus server register and coil data.
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
#include "mcan_loopback_interrupt.h"

// extern void sync_mb_di(void);
// extern void sync_mb_coil(void);
/* ========================================================================== */
/*                            Global Variables                                */
/* ========================================================================== */
extern IO_DataModel gIOData;
MbsDeviceChannelConfig mbs_app_deviceChannelConfig;

extern uint8_t DI_NODE_COUNT;
extern uint8_t DO_NODE_COUNT;
extern uint8_t AO_NODE_COUNT;
extern uint8_t AI_NODE_COUNT;
/* ========================================================================== */
/*                          Function Declarations                             */
/* ========================================================================== */

/* ========================================================================== */
/*                          Function Definitions                              */
/* ========================================================================== */
static inline int nmbs_bitfield_get(const nmbs_bitfield bf, uint16_t index)
{
    return (bf[index / 8] >> (index % 8)) & 1;
}

nmbs_error MbsApp_read_coils(uint16_t address, uint16_t quantity, nmbs_bitfield coils_out, uint8_t unit_id, void* arg)
{
    for(uint16_t i = 0; i < quantity; i++)
    {
        uint16_t idx = (address + i) / 16;
        uint8_t bit  = (address + i) % 16;

        if(idx >= DO_NODE_COUNT)
        {
            nmbs_bitfield_write(coils_out, i, 0);
            continue;
        }

        uint16_t val = gIOData.do_[idx];
        uint8_t state = (val >> bit) & 0x01;

        nmbs_bitfield_write(coils_out, i, state);
    }

    return NMBS_ERROR_NONE;
}

nmbs_error MbsApp_write_single_coils(uint16_t address, bool value, uint8_t unit_id, void* arg)
{
    uint16_t idx = address / 16;
    uint8_t bit  = address % 16;

    uint16_t current = gIOData.do_[idx];

    if (value)
        current |= (1 << bit);
    else
        current &= ~(1 << bit);

    gIOData.do_[idx] = current;

    if(idx >= DO_NODE_COUNT)
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    CANopenModule *m = CANopen_findDO(idx);

    if(m == NULL)
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    CANopen_writeRPDO(
        m->nodeId,
        current);

    return NMBS_ERROR_NONE;
}

nmbs_error MbsApp_write_multiple_coils(
    uint16_t address,
    uint16_t quantity,
    const nmbs_bitfield coils,
    uint8_t unit_id,
    void *arg)
{
    uint16_t startIdx = address / 16;
    uint16_t endIdx   = (address + quantity - 1) / 16;

    if(startIdx != endIdx)
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    uint16_t value = gIOData.do_[startIdx];

    for(uint16_t i=0; i<quantity; i++)
    {
        uint8_t bit = (address + i) % 16;

        if(nmbs_bitfield_get(coils, i))
            value |=  (1U << bit);
        else
            value &= ~(1U << bit);
    }

    gIOData.do_[startIdx] = value;

    CANopenModule *m =
        CANopen_findDO(startIdx);

    if(m == NULL)
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    CANopen_writeRPDO(
        m->nodeId,
        value);

    return NMBS_ERROR_NONE;
}

nmbs_error MbsApp_read_discrete_input(uint16_t address, uint16_t quantity, nmbs_bitfield inputs_out, uint8_t unit_id, void* arg)
{
    for (uint16_t i = 0; i < quantity; i++)
    {
        uint16_t idx = (address + i) / 16;
        uint8_t bit  = (address + i) % 16;

        if(idx >= DI_NODE_COUNT)
        {
            nmbs_bitfield_write(inputs_out, i, 0);
            continue;
        }
        
        uint16_t val = gIOData.di[idx];
        uint8_t state = (val >> bit) & 0x01;

        nmbs_bitfield_write(inputs_out, i, state);
    }

    return NMBS_ERROR_NONE;
}

nmbs_error MbsApp_read_input_registers(
    uint16_t address,
    uint16_t quantity,
    uint16_t *registers_out,
    uint8_t unit_id,
    void *arg)
{
    for(uint16_t i = 0; i < quantity; i++)
    {
        uint16_t reg = address + i;

        uint16_t module  = reg / 8;
        uint16_t channel = reg % 8;

        if(module >= AI_NODE_COUNT)
        {
            registers_out[i] = 0;
            continue;
        }

        registers_out[i] =
            (uint16_t)gIOData.ai[module * 8 + channel];
    }

    return NMBS_ERROR_NONE;
}

nmbs_error MbsApp_read_holding_registers(uint16_t address,
                                         uint16_t quantity,
                                         uint16_t* registers_out,
                                         uint8_t unit_id,
                                         void* arg)
{
    for (uint16_t i = 0; i < quantity; i++)
    {
        uint16_t idx = address + i;

        if(idx >= (AO_NODE_COUNT * 8))
        {
            registers_out[i] = 0;
            continue;
        }

        registers_out[i] = (uint16_t)gIOData.ao[idx];
    }

    return NMBS_ERROR_NONE;
}

nmbs_error MbsApp_write_single_registers(uint16_t address, uint16_t value, uint8_t unit_id, void* arg)
{
    uint16_t module = address / 8;
    uint16_t ch     = address % 8;

    if(module >= AO_NODE_COUNT)
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    int16_t buffer[8];

    memcpy(buffer, &gIOData.ao[module * 8], sizeof(buffer));

    buffer[ch] = (int16_t)value;

    CANopenModule *m =
        CANopen_findAO(module);

    if(m == NULL)
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    gIOData.ao[module * 8 + ch] = (int16_t)value;

    CANopen_writeRPDO_Analog(m->nodeId, buffer);

    return NMBS_ERROR_NONE;
}

nmbs_error MbsApp_write_multiple_registers(
    uint16_t address,
    uint16_t quantity,
    const uint16_t *registers,
    uint8_t unit_id,
    void *arg)
{
    uint16_t module = address / 8;

    if(module >= AO_NODE_COUNT)
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    if((address / 8) !=
       ((address + quantity - 1) / 8))
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    int16_t values[8];

    memcpy(
        values,
        &gIOData.ao[module * 8],
        sizeof(values));

    for(uint16_t i=0; i<quantity; i++)
    {
        uint16_t ch =
            (address + i) % 8;

        values[ch] =
            (int16_t)registers[i];
    }

    memcpy(
        &gIOData.ao[module * 8],
        values,
        sizeof(values));

    CANopenModule *m =
        CANopen_findAO(module);

    if(m == NULL)
    {
        return NMBS_ERROR_INVALID_ARGUMENT;
    }

    CANopen_writeRPDO_Analog(
        m->nodeId,
        values);

    return NMBS_ERROR_NONE;
}