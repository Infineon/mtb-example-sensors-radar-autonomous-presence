/*******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the XENSIV sensor shield: Radar autonomous 
*              presence example for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2024, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "FreeRTOS.h"
#include "task.h"
#include "GUI.h"
#include "mtb_st7735s.h"
#include "Generated/Resource.h"
#include "ID_SCREEN_00.h"

/******************************************************************************
* Macros
*******************************************************************************/
#define T_DET          (CYBSP_D4)
#define P_DET          (CYBSP_D5)
#define RADAR_SEL      (CYBSP_D6)
#define SPI_SEL        (CYBSP_D9)

#define TFT_TASK_STACK_SIZE        (1024*10)
#define TFT_TASK_PRIORITY          (configMAX_PRIORITIES - 3)

#define RADAR_TASK_STACK_SIZE        (1024*2)
#define RADAR_TASK_PRIORITY          (configMAX_PRIORITIES - 5)

/* SPI baud rate in Hz */
#define SPI_FREQ_HZ                (1000000UL)
/* SPI transfer bits per frame */
#define BITS_PER_FRAME             (8)

/* Radar sensor GPIOs state */
#define INACTIVE    (0)
#define ACTIVE      (1)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* This enables RTOS aware debugging. */
volatile int uxTopUsedPriority;

/* The pins are defined by ST7735S library. */
const mtb_st7735s_pins_t tft_pins =
{
        .dc       = CYBSP_A0,
        .rst      = CYBSP_D2
};

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
void tft_task(void *arg);
void radar_task(void *arg);

/*******************************************************************************
* Function Name: main
*********************************************************************************
* Summary:
*  System entrance point. This function initializes BSP, retarget IO, sets up 
*  the TFT and radar tasks, and then starts the RTOS scheduler.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* This enables RTOS aware debugging in OpenOCD */
    uxTopUsedPriority = configMAX_PRIORITIES - 1 ;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize the retarget-io */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                  CY_RETARGET_IO_BAUDRATE);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("***********************************************************\r\n");
    printf("             Radar Autonomous Presence Example             \r\n");
    printf("***********************************************************\r\n\n");

    /* Create the TFT task */
    xTaskCreate(tft_task, "tftTask", TFT_TASK_STACK_SIZE,
                    NULL,  TFT_TASK_PRIORITY,  NULL);

    /* Create the RADAR task */
    xTaskCreate(radar_task, "radarTask", RADAR_TASK_STACK_SIZE,
                    NULL,  RADAR_TASK_PRIORITY,  NULL);

    /* Start the FreeRTOS scheduler. */
    vTaskStartScheduler();

    /* Should never get here. */
    CY_ASSERT(0);
}

/*******************************************************************************
* Function Name: void radar_task(void *arg)
*********************************************************************************
* Summary:
* This function interfaces BGT60LTR11AIP radar sensor and detects the target motion
*
* Parameters:
*  arg: task argument (unused)
*
* Return:
*  None
*
*******************************************************************************/
void radar_task(void *arg)
{
    cy_rslt_t result;

    uint32_t T_DET_Status = 0;
    uint32_t P_DET_Status = 0;
    uint16_t P_DET_prev = -1;
    uint16_t T_DET_prev = -1;

    char *status[] = {"Detected", "Not Detected", "Approaching", "Departing",
                      "           "};

    /* Initialize the P_DET, T_DET and RADAR_SEL pins */
    result = cyhal_gpio_init(P_DET, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_PULL_NONE, false);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    result = cyhal_gpio_init(T_DET, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_PULL_NONE, false);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Select the onboard radar sensor using RADAR_SEL pin */
    result = cyhal_gpio_init(RADAR_SEL, CYHAL_GPIO_DIR_OUTPUT,
                                 CYHAL_GPIO_DRIVE_STRONG, false);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    for (;;)
    {
        /* Get P_DET and T_DET status. */
        P_DET_Status = cyhal_gpio_read(P_DET);
        T_DET_Status = cyhal_gpio_read(T_DET);

        switch (T_DET_Status)
        {
            case INACTIVE:
            switch (P_DET_Status)
            {
                case INACTIVE:
                if (P_DET_prev  != P_DET_Status)
                {
                    /* Updates the status on serial terminal and TFT display*/
                    printf("Presence: Detected\r\n");
                    printf("Direction: Departing\r\n\n");
                    T_DET_prev  = T_DET_Status;
                    P_DET_prev  = P_DET_Status;
                    APPW_SetText(ID_SCREEN_00, ID_TEXT_02, status[0]);
                    APPW_SetVarData(P_DET_Stat, 1);
                    APPW_SetText(ID_SCREEN_00, ID_TEXT_03, status[3]);
                }
                break;
                case ACTIVE:
                if (P_DET_prev  != P_DET_Status)
                {
                    /* Updates the status on serial terminal and TFT display*/
                    printf("Presence: Detected\r\n");
                    printf("Direction: Approaching\r\n\n");
                    T_DET_prev  = T_DET_Status;
                    P_DET_prev  = P_DET_Status;
                    APPW_SetText(ID_SCREEN_00, ID_TEXT_02, status[0]);
                    APPW_SetVarData(P_DET_Stat, 0);
                    APPW_SetText(ID_SCREEN_00, ID_TEXT_03, status[2]);
                }
                break;
            default:
            break;
            }
            break;
            case ACTIVE:
            if (T_DET_prev  != T_DET_Status)
            {
                /* Updates the status on serial terminal and TFT display*/
                printf("Presence: Not Detected\r\n");
                printf("Direction: NA\r\n\n");
                T_DET_prev  = T_DET_Status;
                APPW_SetVarData(T_DET_Stat, 0);
                APPW_SetText(ID_SCREEN_00, ID_TEXT_02, status[1]);
                APPW_SetText(ID_SCREEN_00, ID_TEXT_03, status[4]);
            }
            break;
            default:
            break;
        }
    }
}

/*******************************************************************************
* Function Name: void tft_task(void *arg)
*********************************************************************************
* Summary: The Following functions are performed in this task
*           1. Initializes the EmWin display engine
*           2. Displays the SHT35 humidity sensor data
*
* Parameters:
*  arg: task argument (unused)
*
* Return:
*  None
*
*******************************************************************************/
void tft_task(void *arg)
{
    cy_rslt_t result;
    cyhal_spi_t mSPI;

    /* Initialize SPI_SEL pin to select the SPI CS for display */
    result = cyhal_gpio_init(SPI_SEL, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, true);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize the SPI peripheral */
    result = cyhal_spi_init(&mSPI,CYBSP_SPI_MOSI, CYBSP_SPI_MISO, CYBSP_SPI_CLK,
                                CYBSP_SPI_CS, NULL, BITS_PER_FRAME,
                                CYHAL_SPI_MODE_00_MSB, false);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Set the SPI baud rate */
    result = cyhal_spi_set_frequency(&mSPI, SPI_FREQ_HZ);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize the display controller */
    result = mtb_st7735s_init_spi(&mSPI, &tft_pins);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    MainTask();
}

/* [] END OF FILE */
