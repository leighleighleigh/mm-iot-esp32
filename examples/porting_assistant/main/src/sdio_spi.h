/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

enum sdio_spi_rc
{
    RC_SUCCESS,
    RC_UNSPECIFIED_ERROR,
    RC_DEVICE_NOT_READY,
    RC_INVALID_RESPONSE,
    RC_INVALID_RESPONSE_DATA,
    RC_INVALID_CRC_RECIEVED,
    RC_RESPONSE_TIMEOUT,
    RC_INVALID_INPUT,
};

/**
 * Send an SDIO command to the transceiver and validate the response.
 *
 * @param cmd_idx   Command Index (e.g., 52 for CMD52).
 * @param arg       Argument for the command (i.e., the 32 bits following the command index).
 * @param rsp       If not NULL, then will be set to the first octet of the received response,
 *                  if applicable.
 *
 * @return Result of operation
 */
int sdio_spi_send_cmd(uint8_t cmd_idx, uint32_t arg, uint8_t *rsp);

/**
 * Carry out the steps to read a le32 value from the transceiver at the specified address.
 *
 * @param address   Address to read from.
 * @param data      Reference to the location to store the data. This must not be NULL.
 *
 * @return Result of operation
 */
int sdio_spi_read_le32(uint32_t address, uint32_t *data);

/**
 * Carry out the steps to write a large buffer to a specified address in the transceiver.
 *
 * @param address   Address to write to.
 * @param data      Data to write to the specified address.
 * @param len       Length of the data to write.
 *
 * @return Result of operation
 */
int sdio_spi_write_multi_byte(uint32_t address, const uint8_t *data, uint32_t len);

/**
 * Carry out the steps to read a large buffer from a specified address in the transceiver.
 *
 * @param address   Address to read from.
 * @param data      Buffer to write the data into.
 * @param len       Length of the data to read.
 *
 * @return Result of operation
 */
int sdio_spi_read_multi_byte(uint32_t address, uint8_t *data, uint32_t len);
