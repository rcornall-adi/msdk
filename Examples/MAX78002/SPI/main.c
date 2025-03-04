/******************************************************************************
 *
 * Copyright (C) 2022-2023 Maxim Integrated Products, Inc. (now owned by 
 * Analog Devices, Inc.),
 * Copyright (C) 2023-2024 Analog Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ******************************************************************************/

/**
 * @file    main.c
 * @brief   SPI Controller Demo
 * @details This example demonstrates the SPI controller loopback transactions.
 */

/***** Includes *****/
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "mxc_device.h"
#include "mxc_delay.h"
#include "mxc_pins.h"
#include "nvic_table.h"
#include "uart.h"
#include "spi.h"
#include "dma.h"
#include "led.h"

/***** Preprocessors *****/
#define CONTROLLER_SYNC 0
#define CONTROLLER_ASYNC 0
#define CONTROLLER_DMA 1

// Preprocessor Error Checking
#if (!(CONTROLLER_SYNC || CONTROLLER_ASYNC || CONTROLLER_DMA))
#error "You must set either CONTROLLER_SYNC or CONTROLLER_ASYNC or CONTROLLER_DMA to 1."
#endif
#if ((CONTROLLER_SYNC && CONTROLLER_ASYNC) || (CONTROLLER_ASYNC && CONTROLLER_DMA) || \
     (CONTROLLER_DMA && CONTROLLER_SYNC))
#error "You must select either CONTROLLER_SYNC or CONTROLLER_ASYNC or CONTROLLER_DMA, not all 3."
#endif

/***** Definitions *****/
#define DATA_LEN 100 // Words
#define DATA_VALUE 0xA5B7 // This is for Controller mode only...
#define VALUE 0xFFFF
#define SPI_SPEED 100000 // Bit Rate

#define SPI_INSTANCE_NUM 1

/***** Globals *****/
uint16_t rx_data[DATA_LEN];
uint16_t tx_data[DATA_LEN];
volatile int SPI_FLAG;
int TX_DMA_CH, RX_DMA_CH;

/***** Functions *****/
#if (SPI_INSTANCE_NUM == 0)
#define SPI MXC_SPI0
#define SPI_IRQ SPI0_IRQn
void SPI0_IRQHandler(void)
{
    MXC_SPI_AsyncHandler(SPI);
}
#elif (SPI_INSTANCE_NUM == 1)
#define SPI MXC_SPI1
#define SPI_IRQ SPI1_IRQn
void SPI1_IRQHandler(void)
{
    MXC_SPI_AsyncHandler(SPI);
}
#endif

void DMA_TX_IRQHandler(void)
{
    MXC_SPI_DMA_TX_Handler(SPI);
}

void DMA_RX_IRQHandler(void)
{
    MXC_SPI_DMA_RX_Handler(SPI);
}

void SPI_Callback(void *data, int error)
{
    SPI_FLAG = error;
}

int main(void)
{
    int i, j, retVal;
    uint16_t temp;
    mxc_spi_cfg_t cfg;

    printf("\n**************************** SPI CONTROLLER TEST *************************\n");
    printf("This example configures the SPI to send data between the MISO (P0.22) and\n");
    printf("MOSI (P0.21) pins.  Connect these two pins together.\n\n");
    printf("Multiple word sizes (2 through 16 bits) are demonstrated.\n\n");

#if CONTROLLER_SYNC
    printf("Performing blocking (synchronous) transactions...\n");
#endif
#if CONTROLLER_ASYNC
    printf("Performing non-blocking (asynchronous) transactions...\n");
#endif
#if CONTROLLER_DMA
    printf("Performing transactions with DMA...\n");
#endif

    for (i = 2; i < 17; i++) {
        // Sending out 2 to 16 bits
        for (j = 0; j < DATA_LEN; j++) {
            tx_data[j] = DATA_VALUE;
        }

        mxc_spi_pins_t spi_pins;
        // This example enables the TS0 HW pin.
        spi_pins.ss0 = true; // TS0
        spi_pins.ss1 = false; // TS1
        spi_pins.ss2 = false; // TS2
        spi_pins.vddioh = true;
        spi_pins.drvstr = MXC_GPIO_DRVSTR_0;

        retVal = MXC_SPI_Init(SPI, MXC_SPI_TYPE_CONTROLLER, MXC_SPI_INTERFACE_STANDARD, 0, 0b000,
                              SPI_SPEED, spi_pins);
        if (retVal != E_NO_ERROR) {
            printf("\nSPI INITIALIZATION ERROR\n");
            return retVal;
        }

        // SPI Settings.
        cfg.spi = SPI;
        cfg.clk_mode = MXC_SPI_CLKMODE_0; // CPOL: 0, CPHA: 0
        cfg.frame_size = i;

        // DMA Settings.
#if CONTROLLER_DMA
        cfg.use_dma_tx = true;
        cfg.use_dma_rx = true;
        cfg.dma = MXC_DMA;
#else
        cfg.use_dma_rx = false;
        cfg.use_dma_tx = false;
        cfg.dma = MXC_DMA;
#endif

        retVal = MXC_SPI_Config(&cfg);
        if (retVal != E_NO_ERROR) {
            printf("\nSPI CONFIGURATION ERROR\n");
            return retVal;
        }

        memset(rx_data, 0x0, DATA_LEN * sizeof(uint16_t));

        // SPI Request (Callback)
        mxc_spi_req_t req;
        req.spi = SPI;
        req.txData = (uint8_t *)tx_data;
        req.txLen = DATA_LEN;
        req.rxData = (uint8_t *)rx_data;
        req.rxLen = DATA_LEN;
        req.ssDeassert = 1;
        req.completeCB = SPI_Callback;
        SPI_FLAG = 1;

#if CONTROLLER_SYNC
        MXC_SPI_ControllerTransaction(&req);
#endif

#if CONTROLLER_ASYNC
        NVIC_EnableIRQ(SPI_IRQ);
        MXC_SPI_ControllerTransactionAsync(&req);

        while (SPI_FLAG == 1) {}
#endif

#if CONTROLLER_DMA
        TX_DMA_CH = MXC_SPI_DMA_GetTXChannel(SPI);
        RX_DMA_CH = MXC_SPI_DMA_GetRXChannel(SPI);

        NVIC_EnableIRQ(MXC_DMA_CH_GET_IRQ(TX_DMA_CH));
        NVIC_EnableIRQ(MXC_DMA_CH_GET_IRQ(RX_DMA_CH));

        MXC_NVIC_SetVector(MXC_DMA_CH_GET_IRQ(TX_DMA_CH), DMA_TX_IRQHandler);
        MXC_NVIC_SetVector(MXC_DMA_CH_GET_IRQ(RX_DMA_CH), DMA_RX_IRQHandler);

        MXC_SPI_ControllerTransactionDMA(&req);

        while (SPI_FLAG == 1) {}
#endif

        uint8_t bits = MXC_SPI_GetFrameSize(SPI);

        // Reformat tx_data to match rx_data frame size.
        for (j = 0; j < DATA_LEN; j++) {
            if (bits <= 8) {
                if (j < (DATA_LEN / 2)) {
                    temp = VALUE >> (16 - bits);
                    temp = (temp << 8) | temp;
                    temp &= DATA_VALUE;
                    tx_data[j] = temp;
                } else if (j == (DATA_LEN / 2) && DATA_LEN % 2 == 1) {
                    temp = VALUE >> (16 - bits);
                    temp &= DATA_VALUE;
                    tx_data[j] = temp;
                } else {
                    tx_data[j] = 0x0000;
                }
            } else {
                temp = VALUE >> (16 - bits);
                temp &= DATA_VALUE;
                tx_data[j] = temp;
            }
        }

        // Compare Sent data vs Received data
        // Printf needs the Uart turned on since they share the same pins
        if (memcmp(rx_data, tx_data, sizeof(tx_data)) != 0) {
            printf("\n-->%2d Bits Transaction Failed\n", i);
            LED_On(1);
            return E_COMM_ERR;
        } else {
            printf("-->%2d Bits Transaction Successful\n", i);
        }

        retVal = MXC_SPI_Shutdown(SPI);

        if (retVal != E_NO_ERROR) {
            printf("\n-->SPI SHUTDOWN ERROR: %d\n", retVal);
            return retVal;
        }
    }

    printf("\nExample Complete.\n");
    LED_On(0);
    return E_NO_ERROR;
}
