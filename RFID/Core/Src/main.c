#include "main.h"

#include "app.h"
#include "app_config.h"
#include "uart.h"

#if APP_USE_FREERTOS
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#endif

I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim1;

#if APP_USE_FREERTOS
static QueueHandle_t cardQueue;
static TaskHandle_t rfidTaskHandle;
static TaskHandle_t appTaskHandle;
#endif

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);

#if APP_USE_FREERTOS
static void RFIDTask(void *argument)
{
    AppCardEvent event;
    (void)argument;

    for (;;)
    {
        if (App_RFIDPoll(&event))
        {
            BaseType_t sent = xQueueSend(cardQueue, &event, 0U);
            App_NotifyCardQueueResult((sent == pdPASS) ? 1U : 0U);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void CardDisplayTask(void *argument)
{
    AppCardEvent event;
    (void)argument;

    for (;;)
    {
        App_ProcessUart();
        if (xQueueReceive(cardQueue, &event, pdMS_TO_TICKS(20)) == pdPASS)
        {
            App_HandleCard(&event);
        }
    }
}

void vApplicationMallocFailedHook(void)
{
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *taskName)
{
    (void)task;
    (void)taskName;
    Error_Handler();
}
#endif

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_I2C1_Init();
    USART1_Init();
    App_Init();

#if APP_USE_FREERTOS
    cardQueue = xQueueCreate(4U, sizeof(AppCardEvent));
    if (cardQueue == NULL)
    {
        Error_Handler();
    }
    if ((xTaskCreate(RFIDTask, "RFID", 256U, NULL, tskIDLE_PRIORITY + 2U, &rfidTaskHandle) != pdPASS) ||
        (xTaskCreate(CardDisplayTask, "CardUI", 512U, NULL, tskIDLE_PRIORITY + 1U, &appTaskHandle) != pdPASS))
    {
        Error_Handler();
    }
    App_SetTaskHandles(rfidTaskHandle, appTaskHandle);
    vTaskStartScheduler();
    Error_Handler();
#endif

    while (1)
    {
#if !APP_USE_FREERTOS
        AppCardEvent event;
        App_ProcessUart();
        if (App_RFIDPoll(&event))
        {
            App_HandleCard(&event);
        }
        App_ProcessUart();
        HAL_Delay(50);
#else
        __WFI();
#endif
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);

    /* Cấu hình PA4 (LED Xanh) và PA5 (LED Đỏ) */
    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Cấu hình PB0 (RC522 RST) và PB10 (Buzzer) - Không sử dụng PB1 */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Cấu hình PB12 (RC522 CS) */
    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
