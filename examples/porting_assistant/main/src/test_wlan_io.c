/*
 * Copyright 2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "porting_assistant.h"
#include "sdio_spi.h"
#include "mmhal.h"
#include "mmutils.h"

/** Address where the chip ID is stored on the MM6108 */
#define MM6108_REG_CHIP_ID      0x10054d20

/** Address used when measuring raw throughput */
#define MM6108_BENCHMARK_ADDR_START      0x80100000

/** Packet length used for bulk read/write operations. Chosen because it is the approximate size of
 * a max data frame. */
#define BULK_RW_PACKET_LEN_BYTES (1496)

/** Duration to perform the benchmark over. This was arbitrarily chosen. */
#define BENCHMARK_WAIT_MS (2500)

/** Array of valid chip ids for the MM6108 */
const uint32_t valid_chip_ids[] = {
    0x206,
    0x306,
    0x406,
};

/**
 * Function to validate if a given chip id is valid.
 *
 * @param chip_id Chip ID to validate
 *
 * @return @c true if the given id is in the list of valid ids, else @c false
 */
static bool valid_chip_id(uint32_t chip_id)
{
    int ii;
    int num_valid_chip_ids = sizeof(valid_chip_ids)/sizeof(valid_chip_ids[0]);

    for (ii = 0; ii < num_valid_chip_ids; ii++)
    {
        if (chip_id == valid_chip_ids[ii])
        {
            return true;
        }
    }

    return false;
}

TEST_STEP(test_step_mmhal_wlan_init, "WLAN HAL initialisation")
{
    MM_UNUSED(log_buf);
    MM_UNUSED(log_buf_len);

    mmhal_init();
    mmhal_wlan_init();

    /* We don't get indication of success or failure from mmhal_wlan_init() so we return
     * "no result". */
    return TEST_NO_RESULT;
}

TEST_STEP(test_step_mmhal_wlan_hard_reset, "Hard reset device")
{
    MM_UNUSED(log_buf);
    MM_UNUSED(log_buf_len);

    mmhal_wlan_hard_reset();

    /*
     * Since mmhal_wlan_hard_reset() does not return a status code we cannot verify here whether
     * the MM chip was reset successfully at this stage, so we return "no result". To validate
     * reset behavior an external logic analyzer can be used to probe the reset line.
     */
    return TEST_NO_RESULT;
}

TEST_STEP(test_step_mmhal_wlan_send_training_seq, "Send training sequence")
{
    MM_UNUSED(log_buf);
    MM_UNUSED(log_buf_len);

    mmhal_wlan_send_training_seq();

    /*
     * Since mmhal_wlan_send_training_seq() does not return a status code we cannot verify here
     * whether the training sequence was sent successfully at this stage, so we return "no result".
     * To validate this behavior an external logic analyzer can be used to probe the SPI lines.
     */
    return TEST_NO_RESULT;
}

TEST_STEP(test_step_device_ready, "Check for MM chip ready for SDIO/SPI commands")
{
    uint8_t result = 0;
    uint32_t attempt = 255;

    mmhal_wlan_spi_cs_assert();

    /*Loop until a valid data is read which is 0xFF */
    while (attempt--)
    {
        result = mmhal_wlan_spi_rw(0xff);
        if (result == 0xff)
        {
            mmhal_wlan_spi_cs_deassert();
            return TEST_PASSED;
        }
    }

    mmhal_wlan_spi_cs_deassert();

    if (result == 0)
    {
        TEST_LOG_APPEND(
                 "Read 0x00 from SPI port, but expecting 0xff. Possible causes:\n"
                 " - SPI peripheral not configured correctly\n"
                 " - SPI pins not set to correct function (e.g., output instead of alternative)\n"
                 " - SPI chip select not being asserted (should be low during the transfer)\n"
                 " - MM chip not powered on\n\n");
    }
    else
    {
        TEST_LOG_APPEND(
                 "Read 0x%02x from SPI port, but expecting 0xff. Possible causes:\n"
                 " - SPI peripheral not configured correctly\n"
                 " - SPI pins not set to correct function (e.g., output instead of alternative)\n"
                 " - Wrong SPI device selected\n\n", result);
    }

    return TEST_FAILED;
}

TEST_STEP(test_step_set_spi_mode, "Put MM chip into SPI mode")
{
    uint8_t rsp = 0xff;
    unsigned ii;
    int ret = RC_UNSPECIFIED_ERROR;

    /* Issue command to put the device into SPI mode. Reset and retry until successful or retry
     * limit reached. */
    for (ii = 0; ii < 3; ii++)
    {
        ret = sdio_spi_send_cmd(63, 0, &rsp);
        if (ret == RC_SUCCESS)
        {
            return TEST_PASSED;
        }
        (void)sdio_spi_send_cmd(0, 0, NULL);
    }

    switch (ret)
    {
    case RC_SUCCESS:
        /* Shouldn't get here. */
        return TEST_PASSED;

    case RC_UNSPECIFIED_ERROR:
        /* Technically shouldn't be possible, but handle this in case it slips through. */
        TEST_LOG_APPEND(
                 "Failed to issue CMD63 due to an unknown error\n\n");
        break;

    case RC_DEVICE_NOT_READY:
        /* We shouldn't get this error code in this test, since it should have caused the
         * previous test to fail. */
        TEST_LOG_APPEND(
                 "Failed to issue CMD63 due to the device not being ready. Possible causes:\n"
                 " - SPI peripheral not configured correctly\n"
                 " - SPI pins not set to correct function (e.g., output instead of alternative)\n"
                 " - SPI chip select not being asserted (should be low during the transfer)\n"
                 " - MM chip not powered on\n\n");
        break;

    case RC_INVALID_RESPONSE:
        TEST_LOG_APPEND(
                 "Invalid response received in response to CMD63. Expected 0x00, got 0x%02x.\n"
                 "See SDIO Specification 4.10, Part E1, Section 5.2.2 for meaning of bits.\n"
                 "A possible cause may be incorrect SPI clock/data alignment\n\n", rsp);
        break;

    case RC_INVALID_RESPONSE_DATA:
        /* Should not be possible for a CMD63. */
        TEST_LOG_APPEND("CMD63 failed due to invalid response data\n\n");
        break;
    }

    return TEST_FAILED;
}

TEST_STEP(test_step_read_chip_id, "Read chip id from the MM chip")
{
    /*
     * This step is deceptively complicated. Reading the chip id using the SDIO over SPI protocol
     * requires a decent amount of setup. At a high-level the following gets executed:
     *
     * 1. The upper 16bits of the address is set into keyhole registers (relevant for CMD52/53)
     *    - This requires three (3) CMD52 writes to be achieved
     * 2. We execute a CMD53 read
     *    - We first write a CMD53 to the chip indicating that we want to read data and how much.
     *    - We then read out the amount of data requested plus a CRC which is used to validate
     *      the data integrity.
     *
     * This is glossing over the details but what we want to convey here is that sdio_spi_read_le32
     * is not just a read but a series of reads and writes.
     */

    int ret = RC_UNSPECIFIED_ERROR;
    uint32_t data;
    int ii;

    /* MM chip requires few bytes to be written after CMD63 to get it to active state. We just
     * attempt to read the chip id a few times. */
    for (ii = 0; ii < 3; ii++)
    {
        ret = sdio_spi_read_le32(MM6108_REG_CHIP_ID, &data);
        if (ret == RC_SUCCESS)
        {
            break;
        }
    }

    switch (ret)
    {
    case RC_SUCCESS:
        if (valid_chip_id(data))
        {
            return TEST_PASSED;
        }
        TEST_LOG_APPEND("Failed to read valid chip id, recieved 0x%04lx\n\n", data);
        break;

    case RC_UNSPECIFIED_ERROR:
        /* Technically shouldn't be possible, but handle this in case it slips through. */
        TEST_LOG_APPEND("Failed to read chip id due to an unknown error\n\n");
        break;

    case RC_DEVICE_NOT_READY:
        /* We shouldn't get this error code in this test, since it should have caused the
         * previous test to fail. */
        TEST_LOG_APPEND(
            "Failed to read chip id due to the device not being ready. Possible causes:\n"
            " - SPI peripheral not configured correctly\n"
            " - SPI pins not set to correct function (e.g., output instead of alternative)\n"
            " - SPI chip select not being asserted (should be low during the transfer)\n"
            " - MM chip not powered on\n\n");
        break;

    case RC_INVALID_RESPONSE:
        TEST_LOG_APPEND("Invalid response received during sdio_spi_read_le32.\n\n");
        break;

    case RC_INVALID_RESPONSE_DATA:
        TEST_LOG_APPEND(
            "CMD53 failed due to invalid response data. Recieved a non-zero num\n"
            "Per SDIO Specification Version 4.10, Part E1, Section 5.3.\n"
            "For CMD53, the 8-bit data field shall be stuff bits and shall be read as 00h.\n\n");
        break;

    case RC_INVALID_CRC_RECIEVED:
        TEST_LOG_APPEND("Failed to validate CRC for recieved data. Possible causes:\n"
                        " - Error in reading data from SPI peripheral\n"
                        " - Possible noise on the SPI lines causing corruption\n\n");
        break;

    case RC_RESPONSE_TIMEOUT:
        TEST_LOG_APPEND("Failed to get a response from the MM Chip after sending and SDIO CMD.");
        break;

    case RC_INVALID_INPUT:
        /* We should not reach this. */
        TEST_LOG_APPEND("Invalid input was given to sdio_spi_read_le32.\n"
                        "Likely a NULL pointer for the data variable\n\n");
        break;
    }

    return TEST_FAILED;
}

/**
 * Function to process return codes from @ref sdio_spi_write_multi_byte() and @ref
 * sdio_spi_read_multi_byte(). This attempts to give some hints as to what might be the cause of the
 * error.
 *
 * @param ret           Return code to process.
 * @param log_buf       Reference to the log buffer to append any messages.
 * @param log_buf_len   Length of the log buffer.
 *
 * @return @c true if it was a successful return code, else @c false for all other codes.
 */
bool process_sdio_spi_multi_byte_return(int ret, char *log_buf, size_t log_buf_len)
{
    switch (ret)
    {
    case RC_SUCCESS:
        return true;
        break;

    case RC_UNSPECIFIED_ERROR:
        TEST_LOG_APPEND("Failed multi byte operation due to an unknown error\n\n");
        break;

    case RC_DEVICE_NOT_READY:
        /* We shouldn't get this error code in this test, since it should have caused the
         * previous test to fail. */
        TEST_LOG_APPEND(
            "Failed to read chip id due to the device not being ready. Possible causes:\n"
            " - SPI peripheral not configured correctly\n"
            " - SPI pins not set to correct function (e.g., output instead of alternative)\n"
            " - SPI chip select not being asserted (should be low during the transfer)\n"
            " - MM chip not powered on\n\n");
        break;

    case RC_INVALID_RESPONSE:
        TEST_LOG_APPEND("Invalid response received during sdio_spi_read_le32.\n\n");
        break;

    case RC_INVALID_RESPONSE_DATA:
        TEST_LOG_APPEND(
            "CMD53 failed due to invalid response data. Recieved a non-zero num\n"
            "Per SDIO Specification Version 4.10, Part E1, Section 5.3.\n"
            "For CMD53, the 8-bit data field shall be stuff bits and shall be read as 00h.\n\n");
        break;

    case RC_INVALID_CRC_RECIEVED:
        TEST_LOG_APPEND("Failed to validate CRC for recieved data. Possible causes:\n"
                        " - Error in reading data from SPI peripheral\n"
                        " - Possible noise on the SPI lines causing corruption\n\n");
        break;

    case RC_RESPONSE_TIMEOUT:
        TEST_LOG_APPEND("Failed to get a response from the MM Chip after sending and SDIO CMD.");
        break;

    case RC_INVALID_INPUT:
        /* We should not reach this. */
        TEST_LOG_APPEND("Invalid input was given to sdio_spi_read_le32.\n"
                        "Likely a NULL pointer for the data variable\n\n");
        break;
    }

    return TEST_FAILED;
}

/**
 * Function to populate a buffer with a specific pattern.
 *
 * @param data      Reference to the buffer.
 * @param length    Length of the given buffer.
 */
void populate_buffer(uint8_t *data, uint32_t length)
{
    uint8_t value = 0;
    while (length != 0)
    {
        length--;
        data[length] = value++;
    }
}

/**
 * Function to validate the contents of a buffer matches what would be expected if @ref
 * populate_buffer() was called on it.
 *
 * @param data      Reference to the buffer.
 * @param length    Length of the given buffer.
 *
 * @return @c true if the contents matches the expected patter, else @c false
 */
bool valid_buffer(uint8_t *data, uint32_t length)
{
    uint8_t value = 0;
    while (length != 0)
    {
        length--;
        if (data[length] != value++)
        {
            return false;
        }
    }
    return true;
}

TEST_STEP(test_step_bulk_write_read, "Bulk write/read into the MM chip")
{
    int ret = RC_UNSPECIFIED_ERROR;
    bool ok;
    enum test_result result = TEST_PASSED;

    uint8_t *tx_data = (uint8_t *)mmosal_malloc(BULK_RW_PACKET_LEN_BYTES);
    uint8_t *rx_data = (uint8_t *)mmosal_malloc(BULK_RW_PACKET_LEN_BYTES);
    if ((tx_data == NULL) || (rx_data == NULL))
    {
        TEST_LOG_APPEND("Failed to allocate write/read buffers. Is there enough heap allocated?");
        result = TEST_FAILED;
        goto exit;
    }

    populate_buffer(tx_data, BULK_RW_PACKET_LEN_BYTES);

    ret = sdio_spi_write_multi_byte(MM6108_BENCHMARK_ADDR_START, tx_data, BULK_RW_PACKET_LEN_BYTES);
    ok = process_sdio_spi_multi_byte_return(ret, log_buf, log_buf_len);
    if (!ok)
    {
        TEST_LOG_APPEND("Failure during sdio_spi_write_multi_byte\n");
        result = TEST_FAILED;
        goto exit;
    }

    ret = sdio_spi_read_multi_byte(MM6108_BENCHMARK_ADDR_START, rx_data, BULK_RW_PACKET_LEN_BYTES);
    ok = process_sdio_spi_multi_byte_return(ret, log_buf, log_buf_len);
    if (!ok)
    {
        TEST_LOG_APPEND("Failure during sdio_spi_read_multi_byte\n");
        result = TEST_FAILED;
        goto exit;
    }

    if (!valid_buffer(rx_data, BULK_RW_PACKET_LEN_BYTES))
    {
        TEST_LOG_APPEND("Data read from the MM chip does not match the data written.\n");
        result = TEST_FAILED;
        goto exit;
    }

exit:
    if (tx_data != NULL)
    {
        mmosal_free(tx_data);
    }

    if (rx_data != NULL)
    {
        mmosal_free(rx_data);
    }

    return result;
}


TEST_STEP(test_step_raw_tput, "Raw throughput test")
{
    /* Please note that this test is intended to give some indication of the raw throughput that can
     * be achieved when transferring data across the bus to/from the MM chip. It serves more as an
     * upper limit for the WLAN throughput that can be achieved. The actual throughput that can be
     * achieved when transmitting will be lower than this. This is because there are additional
     * overheads that are not captured as part of this test. */
    int ret = RC_UNSPECIFIED_ERROR;
    bool ok;
    enum test_result result = TEST_PASSED;

    uint32_t start_time;
    uint32_t end_time;
    uint32_t time_taken_ms;
    uint32_t benchmark_end_time;
    uint32_t transaction_count = 0;

    uint8_t *tx_data = (uint8_t *)mmosal_malloc(BULK_RW_PACKET_LEN_BYTES);
    uint8_t *rx_data = (uint8_t *)mmosal_malloc(BULK_RW_PACKET_LEN_BYTES);
    if ((tx_data == NULL) || (rx_data == NULL))
    {
        TEST_LOG_APPEND("Failed to allocate write/read buffers. Is there enough heap allocated?");
        result = TEST_FAILED;
        goto exit;
    }

    populate_buffer(tx_data, BULK_RW_PACKET_LEN_BYTES);

    start_time = mmosal_get_time_ms();
    benchmark_end_time = start_time + BENCHMARK_WAIT_MS;
    while (mmosal_time_le(mmosal_get_time_ms(), benchmark_end_time))
    {
        ret = sdio_spi_write_multi_byte(MM6108_BENCHMARK_ADDR_START, tx_data,
                                        BULK_RW_PACKET_LEN_BYTES);
        ok = process_sdio_spi_multi_byte_return(ret, log_buf, log_buf_len);
        if (!ok)
        {
            TEST_LOG_APPEND("Failure during sdio_spi_write_multi_byte\n");
            result = TEST_FAILED;
            goto exit;
        }

        ret = sdio_spi_read_multi_byte(MM6108_BENCHMARK_ADDR_START, rx_data,
                                       BULK_RW_PACKET_LEN_BYTES);
        ok = process_sdio_spi_multi_byte_return(ret, log_buf, log_buf_len);
        if (!ok)
        {
            TEST_LOG_APPEND("Failure during sdio_spi_read_multi_byte\n");
            result = TEST_FAILED;
            goto exit;
        }

        transaction_count++;
    }
    end_time = mmosal_get_time_ms();

    /* We are only validating the contents of the buffer once because there are already checks in
     * place at the transport layer to validate the contents. This is in the form of CRCs. We just
     * perform this check for sanity's sake. */
    if (!valid_buffer(rx_data, BULK_RW_PACKET_LEN_BYTES))
    {
        TEST_LOG_APPEND("Data read from the MM chip does not match the data written.\n");
        result = TEST_FAILED;
        goto exit;
    }

    time_taken_ms = end_time - start_time;
    TEST_LOG_APPEND("Note: This will not be the final WLAN TPUT. See test step implementation"
                    "in test_wlan_io.c for more information.\n");
    TEST_LOG_APPEND("\tTime spent (ms): %lu\n", time_taken_ms);
    TEST_LOG_APPEND("\tRaw TPUT (kbit/s): %lu\n\n",
                    (transaction_count * 2 * BULK_RW_PACKET_LEN_BYTES * 8) / time_taken_ms);

exit:
    if (tx_data != NULL)
    {
        mmosal_free(tx_data);
    }

    if (rx_data != NULL)
    {
        mmosal_free(rx_data);
    }

    return result;
}
