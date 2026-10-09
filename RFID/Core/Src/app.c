#include "app.h"
#include "app_config.h"
#include "main.h"
#include "I2C_LCD.h"
#include "SERVO.h"
#include "rc522.h"
#include "uart.h"
#include "DWT_Delay.h"

#if APP_USE_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#endif

#define MyI2C_LCD I2C_LCD_1
#define SERVO_Motor1 0
#define MAX_AUTHORIZED_CARDS 5

#define LED_GREEN_PORT GPIOA
#define LED_GREEN_PIN GPIO_PIN_4
#define LED_RED_PORT GPIOA
#define LED_RED_PIN GPIO_PIN_5

#define BARRIER_OPEN_ANGLE 90.0f
#define BARRIER_CLOSE_ANGLE 0.0f
#define BARRIER_OPEN_MS 2500

static uchar authorizedCards[MAX_AUTHORIZED_CARDS][4] = {
    {0x62, 0x99, 0x18, 0x02},
    {0x11, 0x22, 0x33, 0x44}
};
static uint8_t numAuthorizedCards = 2;

static void App_DelayMs(uint32_t ms)
{
#if APP_USE_FREERTOS
    vTaskDelay(pdMS_TO_TICKS(ms));
#else
    HAL_Delay(ms);
#endif
}

static void Tone(uint32_t frequency, uint32_t duration)
{
    if ((frequency == 0) || (duration == 0))
    {
        return;
    }

    uint32_t halfPeriodUs = 500000UL / frequency;
    uint32_t cycles = (frequency * duration + 500UL) / 1000UL;

    for (uint32_t i = 0; i < cycles; i++)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
        DWT_Delay_us(halfPeriodUs);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
        DWT_Delay_us(halfPeriodUs);
    }
}

static void noTone(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
}

static void LED_AllOff(void)
{
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_RESET);
}

static void LED_GreenOn(void)
{
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_SET);
}

static void LED_RedOn(void)
{
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_SET);
}

static void ShowIdleScreen(void)
{
    LED_AllOff();
    I2C_LCD_Clear(MyI2C_LCD);
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 0);
    I2C_LCD_WriteString(MyI2C_LCD, "Quet the RFID");
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
    I2C_LCD_WriteString(MyI2C_LCD, "Moi quet the...");
}

static void Buzzer_Init(void)
{
    GPIO_InitTypeDef gpioInit = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpioInit.Pin = GPIO_PIN_2;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpioInit);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
    DWT_Delay_Init();
}

static void Barrier_OpenThenClose(void)
{
    SERVO_MoveTo(SERVO_Motor1, BARRIER_OPEN_ANGLE);
    App_DelayMs(BARRIER_OPEN_MS);
    SERVO_MoveTo(SERVO_Motor1, BARRIER_CLOSE_ANGLE);
}

static uint8_t IsAuthorizedCard(const uchar *scannedUID)
{
    for (uint8_t card = 0; card < numAuthorizedCards; card++)
    {
        uint8_t matches = 1;
        for (uint8_t byte = 0; byte < 4; byte++)
        {
            if (scannedUID[byte] != authorizedCards[card][byte])
            {
                matches = 0;
                break;
            }
        }
        if (matches)
        {
            return 1;
        }
    }
    return 0;
}

void App_Init(void)
{
    Buzzer_Init();
    Tone(1000, 150);
    noTone();
    SERVO_Init(SERVO_Motor1);
    SERVO_MoveTo(SERVO_Motor1, BARRIER_CLOSE_ANGLE);
    I2C_LCD_Init(MyI2C_LCD);
    I2C_LCD_Backlight(MyI2C_LCD);
    I2C_LCD_Display(MyI2C_LCD);
    LED_AllOff();
    MFRC522_Init();
    uart_write_str("RFID ready\r\n");
    ShowIdleScreen();
}

uint8_t App_RFIDPoll(AppCardEvent *event)
{
    uchar status;
    uchar tagType[2];
    uchar serialNumber[5] = {0};

    status = MFRC522_Request(PICC_REQIDL, tagType);
    if (status != MI_OK)
    {
        return 0;
    }

    status = MFRC522_Anticoll(serialNumber);
    if (status != MI_OK)
    {
        return 0;
    }

    for (uint8_t i = 0; i < 4; i++)
    {
        event->uid[i] = serialNumber[i];
    }
    MFRC522_Halt();
    return 1;
}

void App_HandleCard(const AppCardEvent *event)
{
    if (IsAuthorizedCard(event->uid))
    {
        LED_GreenOn();
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "Welcome");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Mo rao chan");
        Tone(659, 200);
        Tone(784, 200);
        noTone();
        Barrier_OpenThenClose();
        App_DelayMs(300);
    }
    else
    {
        LED_RedOn();
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "Sai The");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Tu choi");
        Tone(392, 500);
        noTone();
        App_DelayMs(1500);
    }

    ShowIdleScreen();
}
