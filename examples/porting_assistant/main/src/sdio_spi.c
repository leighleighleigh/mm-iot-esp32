/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "porting_assistant.h"
#include "sdio_spi.h"
#include "mmhal.h"

#ifndef min
#define min(a, b) ((b) < (a) ? (b) : (a))
#endif

/* Series of defines used when writing to the MM chip */
#define MORSE_REG_ADDRESS_BASE      0x10000
#define MORSE_REG_ADDRESS_WINDOW_0  MORSE_REG_ADDRESS_BASE
#define MORSE_REG_ADDRESS_WINDOW_1  (MORSE_REG_ADDRESS_BASE + 1)
#define MORSE_REG_ADDRESS_CONFIG    (MORSE_REG_ADDRESS_BASE + 2)

#define MORSE_CONFIG_ACCESS_1BYTE   0
#define MORSE_CONFIG_ACCESS_2BYTE   1
#define MORSE_CONFIG_ACCESS_4BYTE   2

/** PACK byte array to 16 bit data (little endian/LSB first) */
#define PACK_LE16(dst16_data, src8_array)                   \
    do {                                                    \
        dst16_data = *src8_array;                           \
        dst16_data |= ((uint16_t)*(src8_array+1) << 8);     \
    } while (0)

/** PACK byte array to 16 bit data (big endian/MSB first) */
#define PACK_BE16(dst16_data, src8_array)                   \
    do {                                                    \
        dst16_data = *(src8_array+1);                       \
        dst16_data |= ((uint16_t)*(src8_array) << 8);     \
    } while (0)

/** UNPACK 16 bit data to byte array (little endian/LSB first) */
#define UNPACK_LE16(dst8_array, src16_data)                 \
    do {                                                    \
        *dst8_array     = (uint8_t)(src16_data);            \
        *(dst8_array+1) = (uint8_t)(src16_data >> 8);       \
    } while (0)

/** UNPACK 16 bit data to byte array (big endian/MSB first) */
#define UNPACK_BE16(dst8_array, src16_data)                 \
    do {                                                    \
        *(dst8_array+1) = (uint8_t)(src16_data);            \
        *(dst8_array)   = (uint8_t)(src16_data >> 8);       \
    } while (0)

/** PACK byte array to 32 bit data (little endian/LSB first) */
#define PACK_LE32(dst32_data, src8_array)                   \
    do {                                                    \
        dst32_data = *src8_array;                           \
        dst32_data |= ((uint32_t)*(src8_array+1) << 8);     \
        dst32_data |= ((uint32_t)*(src8_array+2) << 16);    \
        dst32_data |= ((uint32_t)*(src8_array+3) << 24);    \
    } while (0)

/** PACK byte array to 32 bit data (big endian/MSB first) */
#define PACK_BE32(dst32_data, src8_array)                   \
    do {                                                    \
        dst32_data = *(src8_array+3);                       \
        dst32_data |= ((uint32_t)*(src8_array+2) << 8);     \
        dst32_data |= ((uint32_t)*(src8_array+1) << 16);    \
        dst32_data |= ((uint32_t)*(src8_array)   << 24);    \
    } while (0)

/** UNPACK 32 bit data to byte array (little endian/LSB first) */
#define UNPACK_LE32(dst8_array, src32_data)                 \
    do {                                                    \
        *dst8_array     = (uint8_t)(src32_data);            \
        *(dst8_array+1) = (uint8_t)(src32_data >> 8);       \
        *(dst8_array+2) = (uint8_t)(src32_data >> 16);      \
        *(dst8_array+3) = (uint8_t)(src32_data >> 24);      \
    } while (0)

/** UNPACK 32 bit data to byte array (big endian/MSB first) */
#define UNPACK_BE32(dst8_array, src32_data)                 \
    do {                                                    \
        *(dst8_array+3) = (uint8_t)(src32_data);            \
        *(dst8_array+2) = (uint8_t)(src32_data >> 8);       \
        *(dst8_array+1) = (uint8_t)(src32_data >> 16);      \
        *(dst8_array)   = (uint8_t)(src32_data >> 24);      \
    } while (0)

/** PACK byte array to 64 bit data (little endian/LSB first) */
#define PACK_LE64(dst64_data, src8_array)                   \
    do {                                                    \
        dst64_data  = ((uint64_t)*(src8_array+0) << 0);     \
        dst64_data |= ((uint64_t)*(src8_array+1) << 8);     \
        dst64_data |= ((uint64_t)*(src8_array+2) << 16);    \
        dst64_data |= ((uint64_t)*(src8_array+3) << 24);    \
        dst64_data |= ((uint64_t)*(src8_array+4) << 32);    \
        dst64_data |= ((uint64_t)*(src8_array+5) << 40);    \
        dst64_data |= ((uint64_t)*(src8_array+6) << 48);    \
        dst64_data |= ((uint64_t)*(src8_array+7) << 56);    \
    } while (0)

/** PACK byte array to 64 bit data (big endian/MSB first) */
#define PACK_BE64(dst64_data, src8_array)                   \
    do {                                                    \
        dst64_data  = ((uint64_t)*(src8_array+7) << 0);     \
        dst64_data |= ((uint64_t)*(src8_array+6) << 8);     \
        dst64_data |= ((uint64_t)*(src8_array+5) << 16);    \
        dst64_data |= ((uint64_t)*(src8_array+4) << 24);    \
        dst64_data |= ((uint64_t)*(src8_array+3) << 32);    \
        dst64_data |= ((uint64_t)*(src8_array+2) << 40);    \
        dst64_data |= ((uint64_t)*(src8_array+1) << 48);    \
        dst64_data |= ((uint64_t)*(src8_array+0) << 56);    \
    } while (0)

/** UNPACK 64 bit data to byte array (little endian/LSB first) */
#define UNPACK_LE64(dst8_array, src64_data)                 \
    do {                                                    \
        *(dst8_array+0) = (uint8_t)(src64_data >> 0);       \
        *(dst8_array+1) = (uint8_t)(src64_data >> 8);       \
        *(dst8_array+2) = (uint8_t)(src64_data >> 16);      \
        *(dst8_array+3) = (uint8_t)(src64_data >> 24);      \
        *(dst8_array+4) = (uint8_t)(src64_data >> 32);      \
        *(dst8_array+5) = (uint8_t)(src64_data >> 40);      \
        *(dst8_array+6) = (uint8_t)(src64_data >> 48);      \
        *(dst8_array+7) = (uint8_t)(src64_data >> 56);      \
    } while (0)

/** UNPACK 64 bit data to byte array (big endian/MSB first) */
#define UNPACK_BE64(dst8_array, src64_data)                 \
    do {                                                    \
        *(dst8_array+7) = (uint8_t)(src64_data >> 0);       \
        *(dst8_array+6) = (uint8_t)(src64_data >> 8);       \
        *(dst8_array+5) = (uint8_t)(src64_data >> 16);      \
        *(dst8_array+4) = (uint8_t)(src64_data >> 24);      \
        *(dst8_array+3) = (uint8_t)(src64_data >> 32);      \
        *(dst8_array+2) = (uint8_t)(src64_data >> 40);      \
        *(dst8_array+1) = (uint8_t)(src64_data >> 48);      \
        *(dst8_array+0) = (uint8_t)(src64_data >> 56);      \
    } while (0)


/* MAX blocks for single CMD53 read/write*/
#define CMD53_MAX_BLOCKS 128

enum block_size
{
    BLOCK_SIZE_FN1      = 8,
    BLOCK_SIZE_FN1_LOG2 = 3,
    BLOCK_SIZE_FN2      = 512,
    BLOCK_SIZE_FN2_LOG2 = 9,
};

enum max_block_transfer_size
{
    MAX_BLOCK_TRANSFER_SIZE_FN1 = BLOCK_SIZE_FN1 * CMD53_MAX_BLOCKS,
    MAX_BLOCK_TRANSFER_SIZE_FN2 = BLOCK_SIZE_FN2 * CMD53_MAX_BLOCKS,
};


/**
 * IO_RW_DIRECT Response (SPI mode)
 * See SDIO Specification Version 4.10, Part E1, Section 5.2.2.
 */
struct PACKED sdio_spi_r5
{
    uint8_t status;     /**< Status code. */
    uint8_t data;       /**< R/W Data. */
};


/* MORSE set chip active for CMD62 and CMD63 */
#define CHIP_ACTIVE_SEQ  (0x00000000)
#define MAX_RETRY 3

/**
 * SPI control tokens defined in the SD specification.
 *
 * See SD Physical Layer Specification Version 7.10, Section 7.3.3 Control Tokens.
 */
enum sdio_spi_control_token
{
    /** For Multiple Block Write operation */
    SDIO_SPI_TKN_MULTI_WRITE        = 0xFC,
    /** For Single Block Read, Single Block Write and Multiple Block Read (Section 7.3.3.2) */
    SDIO_SPI_TKN_READ_SINGLE_WRITE  = 0xFE,
    /** If Stop transmission is requested - Stop Tran Token */
    SDIO_SPI_TKN_STOP_TRANSACTION = 0xFD,
    /** Data accepted (Section 7.3.3.1) */
    SDIO_SPI_TKN_DATA_RSP_ACCEPTED  = 0xE1 | (0x02 << 1),
    /** Data rejected due to CRC error */
    SDIO_SPI_TKN_DATA_RSP_REJ_CRC   = 0xE1 | (0x05 << 1),
    /** Data rejected due to write error */
    SDIO_SPI_TKN_DATA_RSP_REJ_WRITE = 0xE1 | (0x06 << 1),
};


/* MAX Attempts for the bus operation */
#define MAX_BUS_ATTEMPTS (200)


/** Direction bit. */
enum sdio_direction
{
    SDIO_DIR_CARD_TO_HOST = 0,
    SDIO_DIR_HOST_TO_CARD = 1 << 6,
};

/**
 * Index to used to identify the command
 */
enum sdio_cmd_index
{
    /** GO_IDLE_STATE, used to change from SD to SPI mode. */
    SDIO_CMD0 = 0,
    /** IO_RW_DIRECT, used to read and write to a single register address. */
    SDIO_CMD52 = 52,
    /** IO_RW_EXTENDED, used to read and write to multiple register addresses with a single command. */
    SDIO_CMD53 = 53,
    /** Morse init with response, custom command to switch into SPI mode. */
    SDIO_CMD63 = 63,
};

/*
 * SDIO argument definition, per SDIO Specification Version 4.10, Part E1, Section 5.3.
 */

/** R/W flag */
enum sdio_rw
{
    SDIO_READ  = 0,
    SDIO_WRITE = 1ul << 31,
};

/** Function number */
enum sdio_function
{
    SDIO_FUNCTION_0 = 0,
    SDIO_FUNCTION_1 = 1ul << 28,
    SDIO_FUNCTION_2 = 2ul << 28,
};

/** Block mode */
enum sdio_mode
{
    SDIO_MODE_BYTE = 0,
    SDIO_MODE_BLOCK = 1ul << 27,
};

/* OP code */
enum sdio_opcode
{
    SDIO_OPCODE_FIXED_ADDR = 0,
    SDIO_OPCODE_INC_ADDR   = 1ul << 26,
};

/* Register address (17 bit) */
#define SDIO_ADDRESS_OFFSET (9)
#define SDIO_ADDRESS_MAX    ((1ul << 18) - 1)

/* CMD53 Byte/block count (9 bit) */
#define SDIO_COUNT_OFFSET   (0)
#define SDIO_COUNT_MAX      ((1ul << 10) - 1)

/* CMD52 Data (8 bit) */
#define SDIO_CMD52_DATA_OFFSET  (0)

/*
 * SDIO Card Common Control Register Flags, per SDIO Specification Version 4.10, Part E1,
 * Section 6.9.
 */

#define SDIO_CCCR_IEN_ADDR 0x04u
#define SDIO_CCCR_IEN_IENM (1u)
#define SDIO_CCCR_IEN_IEN1 (1u << 1)

#define SDIO_CCCR_BIC_ADDR 0x07u
#define SDIO_CCCR_BIC_ECSI (1u << 5)


/**
 * Static table used for the table_driven implementation.
 */
static const uint8_t crc7_lookup_table[256] =
{
    0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f,
    0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
    0x19, 0x10, 0x0b, 0x02, 0x3d, 0x34, 0x2f, 0x26,
    0x51, 0x58, 0x43, 0x4a, 0x75, 0x7c, 0x67, 0x6e,
    0x32, 0x3b, 0x20, 0x29, 0x16, 0x1f, 0x04, 0x0d,
    0x7a, 0x73, 0x68, 0x61, 0x5e, 0x57, 0x4c, 0x45,
    0x2b, 0x22, 0x39, 0x30, 0x0f, 0x06, 0x1d, 0x14,
    0x63, 0x6a, 0x71, 0x78, 0x47, 0x4e, 0x55, 0x5c,
    0x64, 0x6d, 0x76, 0x7f, 0x40, 0x49, 0x52, 0x5b,
    0x2c, 0x25, 0x3e, 0x37, 0x08, 0x01, 0x1a, 0x13,
    0x7d, 0x74, 0x6f, 0x66, 0x59, 0x50, 0x4b, 0x42,
    0x35, 0x3c, 0x27, 0x2e, 0x11, 0x18, 0x03, 0x0a,
    0x56, 0x5f, 0x44, 0x4d, 0x72, 0x7b, 0x60, 0x69,
    0x1e, 0x17, 0x0c, 0x05, 0x3a, 0x33, 0x28, 0x21,
    0x4f, 0x46, 0x5d, 0x54, 0x6b, 0x62, 0x79, 0x70,
    0x07, 0x0e, 0x15, 0x1c, 0x23, 0x2a, 0x31, 0x38,
    0x41, 0x48, 0x53, 0x5a, 0x65, 0x6c, 0x77, 0x7e,
    0x09, 0x00, 0x1b, 0x12, 0x2d, 0x24, 0x3f, 0x36,
    0x58, 0x51, 0x4a, 0x43, 0x7c, 0x75, 0x6e, 0x67,
    0x10, 0x19, 0x02, 0x0b, 0x34, 0x3d, 0x26, 0x2f,
    0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
    0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04,
    0x6a, 0x63, 0x78, 0x71, 0x4e, 0x47, 0x5c, 0x55,
    0x22, 0x2b, 0x30, 0x39, 0x06, 0x0f, 0x14, 0x1d,
    0x25, 0x2c, 0x37, 0x3e, 0x01, 0x08, 0x13, 0x1a,
    0x6d, 0x64, 0x7f, 0x76, 0x49, 0x40, 0x5b, 0x52,
    0x3c, 0x35, 0x2e, 0x27, 0x18, 0x11, 0x0a, 0x03,
    0x74, 0x7d, 0x66, 0x6f, 0x50, 0x59, 0x42, 0x4b,
    0x17, 0x1e, 0x05, 0x0c, 0x33, 0x3a, 0x21, 0x28,
    0x5f, 0x56, 0x4d, 0x44, 0x7b, 0x72, 0x69, 0x60,
    0x0e, 0x07, 0x1c, 0x15, 0x2a, 0x23, 0x38, 0x31,
    0x46, 0x4f, 0x54, 0x5d, 0x62, 0x6b, 0x70, 0x79
};

/**
 * @brief Compute the CRC-7 for SDIO commands.
 *
 * @param crc       Seed for CRC calc, zero in most cases this is zero (0).
 * @param data      Pointer to the start of the data to calculate the crc over.
 * @param data_len  length of the data array in bytes.
 *
 * @return Returns the CRC value.
 */
static uint8_t morse_crc7(uint8_t crc, const void *data, uint32_t data_len)
{
    const uint8_t *d = (uint8_t *)data;

    while (data_len--)
    {
        crc = crc7_lookup_table[(crc << 1) ^ *d++];
    }
    return crc;
}

/**
 * Static table used for the table_driven implementation.
 */
static const uint16_t crc16_lookup_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

/**
 * @brief Compute the CRC-16 for the data buffer using the XMODEM model.
 *
 * @param crc       Seed for CRC calc, zero in most cases this is zero (0).
 * @param data      Pointer to the start of the data to calculate the crc over.
 * @param data_len  Length of the data array in bytes.
 *
 * @return Returns the CRC value.
 *
 * @note This implementation(with a few modifications) and corresponding table was generated using
 *       pycrc v0.9.2 (MIT) using the XMODEM model. https://pycrc.org/. The code generated by pycrc
 *       is not considered a substantial portion of the software, therefore the license does not
 *       cover the generated code, and the author of pycrc will not claim any copyright on the
 *       generated code (https://pypi.org/project/pycrc/0.9.2/).
 */
static uint16_t morse_crc16(uint16_t crc, const void *data, size_t data_len)
{
    const uint8_t *d = (const uint8_t *)data;

    while (data_len--)
    {
        crc = (crc16_lookup_table[((crc >> 8) ^ *d++)] ^ (crc << 8));
    }
    return crc;
}

/**
 * @brief Receive a byte from transceiver via SPI.
 *
 * @return  Data byte read from chip.
 */
static uint8_t morse_receive_spi(void)
{
    return mmhal_wlan_spi_rw(0xff);
}

/**
 * @brief Check for card ready before sending data over bus.
 *
 * @return @c true if ready, @c false if timed out
 */
static bool morse_wait_ready(void)
{
    uint8_t result = 0;
    uint32_t attempt = MAX_BUS_ATTEMPTS;
    /*Loop until a valid data is read which is 0xFF */
    while (attempt--)
    {
        result = morse_receive_spi();
        if (result == 0xff)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Send a CMD53 command and validate the response.
 *
 * @param rw_flag   Read/write flag, bit shifted .
 * @param function  Function to be used for operation, bit shifted.
 * @param mode      Whether byte or block mode should be used, bit shifted.
 * @param address   Lower two(2) bytes of the destination address. @ref morse_address_base_set
 *                  is used to set the upper bytes on a per function basis.
 * @param count     Number of bytes/blocks to be read/written for this operation.
 *
 * @note For more details see SDIO Specification Part E1, Section 5.3.
 */
static int morse_cmd53_send_cmd(enum sdio_rw rw_flag, enum sdio_function function,
                                enum sdio_mode mode, uint32_t address, uint16_t count)
{
    uint32_t arg = rw_flag | function | mode | SDIO_OPCODE_INC_ADDR;

    MMOSAL_ASSERT(address <= SDIO_ADDRESS_MAX);
    MMOSAL_ASSERT(count <= SDIO_COUNT_MAX);

    arg |= address << SDIO_ADDRESS_OFFSET;
    arg |= count << SDIO_COUNT_OFFSET;

    return sdio_spi_send_cmd(53, arg, NULL);
}

/**
 * @brief Perform a CMD52 write and validate the response.
 *
 * @param address   Destination address for the data.
 * @param data      Byte of data to write.
 * @param function  SDIO function used.
 *
 * @return Result of operation
 *
 * @note For more details see SDIO Specification Part E1, Section 5.1.
 */
static int morse_cmd52_write(uint32_t address, uint8_t data, enum sdio_function function)
{
    uint32_t arg = SDIO_WRITE | function | SDIO_MODE_BYTE;

    MMOSAL_ASSERT(address <= SDIO_ADDRESS_MAX);

    arg |= address << SDIO_ADDRESS_OFFSET;
    arg |= data << SDIO_CMD52_DATA_OFFSET;

    return sdio_spi_send_cmd(SDIO_CMD52, arg, NULL);
}

/**
 * @brief SDIO CMD53 retrieve data.
 *
 * @param byte_cnt       Number of bytes to transfer. Must be <= block_size if transferring in byte
 *                       mode, or a multiple of block_size if transferring in block mode. (Note that
 *                       in block mode this is still a number of bytes.)
 * @param data           Data buffer to store the retrieved data.
 * @param block_size     Block size to of the SDIO function being used.
 *
 * @return Result of operation
 *
 * @note A corresponding @ref morse_cmd53_send_cmd call must be made first. See SDIO Specification
 *       Part E1, Section 5.3.
 */
static int morse_cmd53_get_data(uint16_t byte_cnt, uint8_t *data,
                                enum block_size block_size)
{
    int result = RC_SUCCESS;

    mmhal_wlan_spi_cs_assert();

    while (byte_cnt > 0)
    {
        uint8_t rcv_data;
        uint32_t attempt;

        /* Wait until we receive start of block or byte mode bytes */
        for (attempt = 0; attempt < MAX_BUS_ATTEMPTS; attempt++)
        {
            rcv_data = morse_receive_spi();
            if (rcv_data == SDIO_SPI_TKN_READ_SINGLE_WRITE)
            {
                break;
            }
        }

        if (rcv_data != SDIO_SPI_TKN_READ_SINGLE_WRITE)
        {
            /* Timed out waiting for CMD53 read ready */
            result = RC_RESPONSE_TIMEOUT;
            goto exit;
        }

        const uint8_t *block_start = data;

        /* Limit size based on function block size */
        uint32_t size = min(byte_cnt, block_size); // NOLINT(build/include_what_you_use)

        /* Read Data */
        mmhal_wlan_spi_read_buf(data, size);
        data += size;

        /* crc (be16) */
        uint16_t rx_crc16;
        rx_crc16 = morse_receive_spi() << 8;
        rx_crc16 |= morse_receive_spi();

        /* Verify crc of data received */
        uint16_t crc16 = morse_crc16(0, block_start, size);

        if (crc16 != rx_crc16)
        {
            result = RC_INVALID_CRC_RECIEVED;
            goto exit;
        }

        byte_cnt -= size;
    }

exit:
    mmhal_wlan_spi_cs_deassert();
    return result;
}

static int morse_test_data_rsp_token(uint8_t token)
{
    int result = RC_UNSPECIFIED_ERROR;

    switch (token)
    {
    case SDIO_SPI_TKN_DATA_RSP_ACCEPTED:
        result = RC_SUCCESS;
        goto exit;

    case SDIO_SPI_TKN_DATA_RSP_REJ_CRC:
        printf("WR: rejected due to CRC\n");
        result = RC_INVALID_RESPONSE;
        goto exit;

    case SDIO_SPI_TKN_DATA_RSP_REJ_WRITE:
        printf("WR: write error\n");
        result = RC_INVALID_RESPONSE;
        goto exit;

    default:
        printf("WR: invalid/no token received (0x%02x)\n", token);
        result = RC_INVALID_RESPONSE;
        goto exit;
    }

exit:
    return result;
}

/**
 * @brief SDIO CMD53 write implementation
 *
 *
 * @param cnt            Length of data to write (in bytes or blocks depending on @p mode )
 * @param data           Data to write.
 * @param block          Block mode (non-zero) or byte mode (zero)
 * @param block_size     Block size to of the SDIO function being used.
 *
 * @return result of operation
 *
 * @note A corresponding @ref morse_cmd53_send_cmd call must be made first. See SDIO Specification
 *       Part E1, Section 5.3.
 */
static int morse_cmd53_put_data(uint16_t cnt, const uint8_t *data, enum sdio_mode mode,
                                          enum block_size block_size)
{
    int result = RC_UNSPECIFIED_ERROR;
    bool bus_ready;

    enum sdio_spi_control_token start_tkn = SDIO_SPI_TKN_READ_SINGLE_WRITE;

    uint32_t size;
    if (mode == SDIO_MODE_BLOCK)
    {
        size = block_size;
        if (cnt > 1)
        {
            start_tkn = SDIO_SPI_TKN_MULTI_WRITE;
        }
    }
    else
    {
        size = cnt;
    }

    mmhal_wlan_spi_cs_assert();

    while (cnt > 0)
    {
        const uint8_t *block_start = data;

        /*
         * Format for sending each data block (not to scale):
         *
         *    +-------------------+-------------+--------+
         *    | Start block token |  Data block |  CRC16 |
         *    +-------------------+-------------+--------+
         *
         * See SD Physical Layer Specification Version 7.10, Sections 7.2.4 and 7.3.3.2.
         */

        if (mode == SDIO_MODE_BYTE)
        {
            MMOSAL_ASSERT(cnt <= block_size);
            cnt = 0;
        }
        else
        {
            cnt--;
        }

        /* CRC-16 for block */
        uint16_t crc16 = morse_crc16(0, block_start, size);

        bus_ready = morse_wait_ready();
        if (!bus_ready)
        {
            printf("Bus not ready\n");
            result = RC_UNSPECIFIED_ERROR;
            goto exit;
        }

        mmhal_wlan_spi_rw(start_tkn);

        /* Data transmit */
        mmhal_wlan_spi_write_buf(data, size);
        data += size;

        /* Transmit CRC16 after each block transmission */
        mmhal_wlan_spi_rw((uint8_t)(crc16 >> 8));
        mmhal_wlan_spi_rw((uint8_t)crc16);

        uint32_t attempt;
        uint8_t rcv_data;

        /* This critical section is to prevent the RTOS context switching and taking too long to get
         * the response. This would result in the next block failing when attempting to get the
         * response. */
        MMOSAL_TASK_ENTER_CRITICAL();
        /* Wait until a success/fail response is received */
        for (attempt = 0; attempt < 4; attempt++)
        {
            rcv_data = morse_receive_spi();
            if (rcv_data != 0xff)
            {
                break;
            }
        }
        MMOSAL_TASK_EXIT_CRITICAL();

        result = morse_test_data_rsp_token(rcv_data);
        if (result != RC_SUCCESS)
        {
                goto exit;
        }
    }

exit:
    /* In a Multiple Block write operation, the stop transmission will be done by sending 'Stop
     * Tran' token instead of 'Start Block' token at the beginning of the next block */
    if (start_tkn == SDIO_SPI_TKN_MULTI_WRITE)
    {
        mmhal_wlan_spi_rw(SDIO_SPI_TKN_STOP_TRANSACTION);
    }

    /* While the card is busy, resetting the CS signal will not terminate the programming process.
     * The card will release the DataOut line (tri-state) and continue with programming. If the card is
     * reselected before the programming is finished, the DataOut line will be forced back to low and
     * all commands will be rejected. SD Physical Layer Specification Version 7.10, Section 7.2.4 */
    morse_wait_ready();

    mmhal_wlan_spi_cs_deassert();
    return result;
}


/**
 * @brief Uses SDIO CMD53 to read a given amount of data.
 *
 * @param function  SDIO function to use for the read operation. This will affect the size of
 *                  the blocks used to read the data.
 * @param address   The address to read from. The first two(2 bytes) will be discarded
 *                  @ref morse_address_base_set is used to set the upper bytes.
 * @param data      Data buffer to store the read data. Should at least have a size of len.
 * @param len       Length data to read in bytes.
 *
 * @return Result of operation
 *
 * @note See SDIO Specification Part E1, Section 5.3.
 */
static int morse_cmd53_read(enum sdio_function function, uint32_t address,
                                      uint8_t *data, uint32_t len)
{
    int result = -1;

    enum block_size block_size = BLOCK_SIZE_FN2;
    enum block_size block_size_log2 = BLOCK_SIZE_FN2_LOG2;

    if (function == SDIO_FUNCTION_1)
    {
        block_size = BLOCK_SIZE_FN1;
        block_size_log2 = BLOCK_SIZE_FN1_LOG2;
    }

    /* Attempt to read as many blocks as possible */
    uint16_t num_blocks = len >> block_size_log2;
    if (num_blocks > 0)
    {
        result = morse_cmd53_send_cmd(SDIO_READ, function, SDIO_MODE_BLOCK,
                                      address & 0x0000FFFF, num_blocks);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }

        uint32_t transfer_size = num_blocks * block_size;
        result = morse_cmd53_get_data(transfer_size, data, block_size);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }

        address += transfer_size;
        data += transfer_size;
        len -= transfer_size;
    }

    /* Now we use byte mode to read anything that was left over. */
    if (len > 0)
    {
        result = morse_cmd53_send_cmd(SDIO_READ, function, SDIO_MODE_BYTE,
                                      address & 0x0000FFFF, len);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }

        result = morse_cmd53_get_data(len, data, block_size);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }
    }

exit:
    return result;
}

/**
 * @brief Uses SDIO CMD53 to write a given amount of data.
 *
 * @param function  SDIO function to use for the write operation. This will affect the size of
 *                  the blocks used to write the data.
 * @param address   The address to write to. The upper 16 bits will be discarded
 *                  @ref morse_address_base_set is used to set the upper bytes.
 * @param data      Data buffer to write data from. Should at least have a size of len.
 * @param len       Length data to write in bytes.
 *
 * @return Result of operation
 *
 * @note See SDIO Specification Part E1, Section 5.3.
 */
static int morse_cmd53_write(enum sdio_function function, uint32_t address,
                             const uint8_t *data, uint32_t len)
{
    int result = RC_UNSPECIFIED_ERROR;

    enum block_size block_size = BLOCK_SIZE_FN2;
    enum block_size block_size_log2 = BLOCK_SIZE_FN2_LOG2;

    if (function == SDIO_FUNCTION_1)
    {
        block_size = BLOCK_SIZE_FN1;
        block_size_log2 = BLOCK_SIZE_FN1_LOG2;
    }

    /* Attempt to write as many blocks as possible */
    uint16_t num_blocks = len >> block_size_log2;
    if (num_blocks > 0)
    {
        result = morse_cmd53_send_cmd(SDIO_WRITE, function, SDIO_MODE_BLOCK,
                                    address & 0x0000FFFF, num_blocks);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }

        result = morse_cmd53_put_data(num_blocks, data, SDIO_MODE_BLOCK, block_size);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }

        uint32_t transfer_size = num_blocks * block_size;
        address += transfer_size;
        data += transfer_size;
        len -= transfer_size;
    }

    /* Now we use byte mode to write anything that was left over. */
    if (len > 0)
    {
        result = morse_cmd53_send_cmd(SDIO_WRITE, function, SDIO_MODE_BYTE,
                                    address & 0x0000FFFF, len);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }

        result = morse_cmd53_put_data(len, data, SDIO_MODE_BYTE, block_size);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }
    }

exit:
    return result;
}

/**
 * @brief Writes to the keyhole registers that set upper 16 bits of addressed used by CMD52
 *        and CMD53 operations.
 *
 * @param address    The address value to set (the lower 16 bits will be ignored).
 * @param access     Access mode (one of @ref MORSE_CONFIG_ACCESS_1BYTE,
 *                   @ref MORSE_CONFIG_ACCESS_2BYTE, @ref MORSE_CONFIG_ACCESS_4BYTE).
 *
 * @return Result of operation
 */
static int morse_address_base_set(uint32_t address, uint8_t access,
                                  enum sdio_function function)
{
    int result;

    address &= 0xFFFF0000;

    MMOSAL_ASSERT(access <= MORSE_CONFIG_ACCESS_4BYTE);

    result = morse_cmd52_write(MORSE_REG_ADDRESS_WINDOW_0, (uint8_t)(address >> 16),
                               function);
    if (result != RC_SUCCESS)
    {
        goto exit;
    }

    result = morse_cmd52_write(MORSE_REG_ADDRESS_WINDOW_1, (uint8_t)(address >> 24),
                               function);
    if (result != RC_SUCCESS)
    {
        goto exit;
    }

    result = morse_cmd52_write(MORSE_REG_ADDRESS_CONFIG, access, function);
    if (result != RC_SUCCESS)
    {
        goto exit;
    }

exit:
    return result;
}

int sdio_spi_read_le32(uint32_t address, uint32_t *data)
{
    int result = -1;
    uint8_t receive_data[4];

    if (data == NULL)
    {
        return RC_INVALID_INPUT;
    }

    enum sdio_function function = SDIO_FUNCTION_1;

    result = morse_address_base_set(address, MORSE_CONFIG_ACCESS_4BYTE, function);
    if (result != RC_SUCCESS)
    {
        goto exit;
    }

    result = morse_cmd53_read(function, address, receive_data, sizeof(receive_data));
    if (result != RC_SUCCESS)
    {
        goto exit;
    }

    PACK_LE32(*data, receive_data);

exit:
    return result;
}

int sdio_spi_read_multi_byte(uint32_t address, uint8_t *data, uint32_t len)
{
    int result = -1;
    enum sdio_function function = SDIO_FUNCTION_2;
    enum max_block_transfer_size max_transfer_size = MAX_BLOCK_TRANSFER_SIZE_FN2;

    /* Length must be a non-zero multiple of 4 */
    if (len == 0 || (len & 0x03) != 0)
    {
        printf("Invalid length %lu\n", len);
        result = RC_INVALID_INPUT;
        goto exit;
    }

    /* Reads cannot cross 64K boundaries, so we may need to do several operations
     * to read the all the data. */
    while (len > 0)
    {
        result = morse_address_base_set(address, MORSE_CONFIG_ACCESS_4BYTE, function);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }

        /* We first calculate the number of bytes to transfer on this iteration of the loop. */
        uint32_t size = min(len, max_transfer_size); // NOLINT(build/include_what_you_use)

        /* Read operations cannot cross the 64K boundary. We truncate the operation if this is
         * is the case. */
        uint32_t next_boundary = (address & 0xFFFF0000) + 0x10000;
        if ((address + size) > next_boundary)
        {
            size = next_boundary - address;
        }

        result = morse_cmd53_read(function, address, data, size);
        if (result != RC_SUCCESS)
        {
           goto exit;
        }

        /* Seems like sdio/spi read can sometimes go wrong and read first 4-bytes word twice,
         * overwriting second word. It seems like reading those again will fetch the correct word.
         * Let's do that. Note: If second read is corrupted again, pass it anyway and upper layers
         * will handle it. */
        if (!memcmp(data, data+4, 4))
        {
            /* Lets try one more time before passing up */
            printf("Corrupt Payload. Re-Read first 8 bytes\n");
            result = morse_cmd53_read(function, address, data, 8);
            if (result != RC_SUCCESS)
            {
               goto exit;
            }
        }

        address += size;
        data += size;
        len -= size;
    }

exit:
    return result;
}


int sdio_spi_write_multi_byte(uint32_t address, const uint8_t *data, uint32_t len)
{
    int result = -1;
    enum sdio_function function = SDIO_FUNCTION_2;
    enum max_block_transfer_size max_transfer_size = MAX_BLOCK_TRANSFER_SIZE_FN2;

    /* Length must be a non-zero multiple of 4 */
    if (len == 0 || (len & 0x03) != 0)
    {
        printf("Invalid length %lu\n", len);
        result = RC_INVALID_INPUT;
        goto exit;
    }

    /* Writes cannot cross 64K boundaries, so we may need to do several operations
     * to write the all the given data. */
    while (len > 0)
    {
        result = morse_address_base_set(address, MORSE_CONFIG_ACCESS_4BYTE, function);
        if (result != RC_SUCCESS)
        {
            goto exit;
        }

        /* We first calculate the number of bytes to transfer on this iteration of the loop. */
        uint32_t size = min(len, max_transfer_size); // NOLINT(build/include_what_you_use)

        /* Write operations cannot cross the 64K boundary. We truncate the operation if this is
         * is the case. */
        uint32_t next_boundary = (address & 0xFFFF0000) + 0x10000;
        if ((address + size) > next_boundary)
        {
            size = next_boundary - address;
        }

        morse_cmd53_write(function, address, data, size);

        address += size;
        data += size;
        len -= size;
    }

exit:
    return result;
}

int sdio_spi_send_cmd(uint8_t cmd_idx, uint32_t arg, uint8_t *rsp)
{
    struct sdio_spi_r5 response;
    uint8_t buf[6];
    uint8_t attempt;

    int ret = -1;

    mmhal_wlan_spi_cs_assert();
    /* We do not check for card ready when sending a CMD63 as the MM-Chip will not be actively
    driving the MISO line before this has been sent. */
    if ((cmd_idx != SDIO_CMD63) && !morse_wait_ready())
    {
        ret = RC_DEVICE_NOT_READY;
        goto exit;
    }

    buf[0] = cmd_idx | SDIO_DIR_HOST_TO_CARD;

    UNPACK_BE32((buf + 1), arg);

    /* Adding crc7 to end of SDIO_CMD52 and SDIO_CMD53 */
    if (cmd_idx == SDIO_CMD52 || cmd_idx == SDIO_CMD53)
    {
        buf[5] = morse_crc7(0x00, (const uint8_t *)&buf[0], sizeof(buf)-1) << 1;
        buf[5] |= 0x1; /* End bit is always 1 */
    }
    else
    {
        /* For SDIO_CMD62 and SDIO_CMD63 */
        buf[5] = 0xFF;
    }

    /* Send command packet */
    mmhal_wlan_spi_write_buf(buf, sizeof(buf));


    response.data = morse_receive_spi();
    /* Wait for valid status byte. */
    for (attempt = 0; attempt < MAX_BUS_ATTEMPTS; attempt++)
    {
        /* Shuffle next byte in. */
        response.status = response.data;
        response.data = morse_receive_spi();

        if (rsp)
        {
            *rsp = response.status;
        }

        /* Check if we have received a valid status byte. */
        if (response.status != 0xFF)
        {
            break;
        }
    }

    if (response.status != 0x00)
    {
        ret = RC_INVALID_RESPONSE;
    }
    else
    {
        ret = RC_SUCCESS;
    }

    /* Per SDIO Specification Version 4.10, Part E1, Section 5.3.
     * For CMD53, the 8-bit data field shall be stuff bits and shall be read as 00h.*/
    if (cmd_idx == SDIO_CMD53 && response.data != 0x00)
    {
        ret = RC_INVALID_RESPONSE_DATA;
    }

exit:
    mmhal_wlan_spi_cs_deassert();
    return ret;
}
