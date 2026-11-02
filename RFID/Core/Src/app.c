#include "app.h"
#include "app_config.h"
#include "main.h"
#include "I2C_LCD.h"
#include "SERVO.h"
#include "rc522.h"
#include "uart.h"
#include "DWT_Delay.h"
#include <stdio.h>
#include <string.h>

#if APP_USE_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#endif

#define MyI2C_LCD I2C_LCD_1
#define SERVO_Motor1 0
#define MAX_AUTHORIZED_CARDS 5

#define LED_GREEN_PORT GPIOA
#define LED_GREEN_PIN GPIO_PIN_4
#define LED_RED_PORT GPIOA
#define LED_RED_PIN GPIO_PIN_5

#define BUZZER_PORT GPIOB
#define BUZZER_PIN GPIO_PIN_10

#define BARRIER_OPEN_ANGLE 90.0f
#define BARRIER_CLOSE_ANGLE 0.0f
#define BARRIER_OPEN_MS 2500
#define INVALID_CARD_ALARM_THRESHOLD 3U
#define CARD_LOG_CAPACITY 10U
#define UART_COMMAND_CAPACITY 40U
#define EMERGENCY_BUTTON_PORT GPIOB
#define EMERGENCY_BUTTON_PIN GPIO_PIN_1
#define EMERGENCY_BUTTON_DEBOUNCE_MS 30U

typedef struct
{
    uint32_t timestamp;
    uchar uid[4];
    uint8_t authorized;
} AppCardLog;

static uchar authorizedCards[MAX_AUTHORIZED_CARDS][4] = {
    {0x62, 0x99, 0x18, 0x02},
    {0x11, 0x22, 0x33, 0x44}
};
static uint8_t numAuthorizedCards = 2;
static AppCardLog cardLog[CARD_LOG_CAPACITY];
static uint8_t cardLogNext;
static uint8_t cardLogCount;
static uint8_t consecutiveInvalidCards;
static uint8_t enrollmentPending;
static uint8_t systemLocked;
static volatile uint8_t emergencyButtonPending;
static uint8_t emergencyButtonWaitingForRelease;
static uint8_t emergencyButtonReleaseTiming;
static uint32_t emergencyButtonReleaseSince;
static uint8_t emergencyHandlerActive;
static uint8_t barrierIsOpen;
static uint8_t emergencyExtensionRequested;
static char uartCommand[UART_COMMAND_CAPACITY];
static uint8_t uartCommandLength;
static uint8_t uartCommandOverflow;

#if APP_USE_FREERTOS
static SemaphoreHandle_t uartMutex;
#endif

static void HandleEmergencyOpen(void);

static void UART_Write(const char *text)
{
#if APP_USE_FREERTOS
    uint8_t mutexTaken = 0;
    if ((uartMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
    {
        if (xSemaphoreTake(uartMutex, portMAX_DELAY) == pdTRUE)
        {
            mutexTaken = 1;
        }
    }
#endif
    uart_write_str(text);
#if APP_USE_FREERTOS
    if (mutexTaken)
    {
        xSemaphoreGive(uartMutex);
    }
#endif
}

static void UART_WriteUidLine(const char *prefix, const uchar *uid, const char *suffix)
{
    char text[48];
    (void)snprintf(text, sizeof(text), "%s%02X:%02X:%02X:%02X%s",
                   prefix, uid[0], uid[1], uid[2], uid[3], suffix);
    UART_Write(text);
}

static void App_DelayMs(uint32_t ms)
{
    while (ms > 0U)
    {
        uint32_t delay = (ms > 10U) ? 10U : ms;
#if APP_USE_FREERTOS
        vTaskDelay(pdMS_TO_TICKS(delay));
#else
        HAL_Delay(delay);
#endif
        ms -= delay;

        if (!emergencyHandlerActive && App_PollEmergencyButton())
        {
            HandleEmergencyOpen();
        }
    }
}

// Phát âm thanh còi Buzzer (hỗ trợ cả còi thụ động passive và còi chủ động active)
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
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
        DWT_Delay_us(halfPeriodUs);
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
        DWT_Delay_us(halfPeriodUs);
    }
}

static void noTone(void)
{
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
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

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpioInit.Pin = BUZZER_PIN;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_PORT, &gpioInit);
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
    DWT_Delay_Init();
}

static void Barrier_OpenThenClose(void)
{
    SERVO_MoveTo(SERVO_Motor1, BARRIER_OPEN_ANGLE);
    barrierIsOpen = 1U;
    uint32_t closeAt = HAL_GetTick() + BARRIER_OPEN_MS;
    while ((int32_t)(HAL_GetTick() - closeAt) < 0)
    {
        uint32_t remaining = closeAt - HAL_GetTick();
        App_DelayMs((remaining > 10U) ? 10U : remaining);
        if (emergencyExtensionRequested)
        {
            emergencyExtensionRequested = 0U;
            closeAt = HAL_GetTick() + BARRIER_OPEN_MS;
        }
    }
    SERVO_MoveTo(SERVO_Motor1, BARRIER_CLOSE_ANGLE);
    barrierIsOpen = 0U;
}

static uint8_t IsAuthorizedCard(const uchar *scannedUID)
{
    uint8_t authorized = 0;
#if APP_USE_FREERTOS
    taskENTER_CRITICAL();
#endif
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
            authorized = 1;
            break;
        }
    }
#if APP_USE_FREERTOS
    taskEXIT_CRITICAL();
#endif
    return authorized;
}

static uint8_t UpdateInvalidCardState(uint8_t authorized)
{
    uint8_t triggerAlarm = 0;
#if APP_USE_FREERTOS
    taskENTER_CRITICAL();
#endif
    if (authorized)
    {
        consecutiveInvalidCards = 0;
    }
    else
    {
        consecutiveInvalidCards++;
        if (consecutiveInvalidCards >= INVALID_CARD_ALARM_THRESHOLD)
        {
            consecutiveInvalidCards = 0;
            triggerAlarm = 1;
        }
    }
#if APP_USE_FREERTOS
    taskEXIT_CRITICAL();
#endif
    return triggerAlarm;
}

static void RecordCardEvent(const uchar *uid, uint8_t authorized)
{
    AppCardLog entry;
    char line[48];
    entry.timestamp = HAL_GetTick();
    memcpy(entry.uid, uid, sizeof(entry.uid));
    entry.authorized = authorized;

#if APP_USE_FREERTOS
    taskENTER_CRITICAL();
#endif
    cardLog[cardLogNext] = entry;
    cardLogNext = (uint8_t)((cardLogNext + 1U) % CARD_LOG_CAPACITY);
    if (cardLogCount < CARD_LOG_CAPACITY)
    {
        cardLogCount++;
    }
#if APP_USE_FREERTOS
    taskEXIT_CRITICAL();
#endif

    (void)snprintf(line, sizeof(line), "LOG,%lu,%02X:%02X:%02X:%02X,%s\r\n",
                   (unsigned long)entry.timestamp, uid[0], uid[1], uid[2], uid[3],
                   authorized ? "ALLOW" : "DENY");
    UART_Write(line);
}

static int HexValue(char ch)
{
    if ((ch >= '0') && (ch <= '9'))
    {
        return ch - '0';
    }
    if ((ch >= 'A') && (ch <= 'F'))
    {
        return ch - 'A' + 10;
    }
    if ((ch >= 'a') && (ch <= 'f'))
    {
        return ch - 'a' + 10;
    }
    return -1;
}

static uint8_t ParseUid(const char *text, uchar *uid)
{
    for (uint8_t byte = 0; byte < 4; byte++)
    {
        int high = HexValue(text[byte * 3U]);
        int low = HexValue(text[byte * 3U + 1U]);
        if ((high < 0) || (low < 0) ||
            ((byte < 3U) && (text[byte * 3U + 2U] != ':')))
        {
            return 0;
        }
        uid[byte] = (uchar)((high << 4) | low);
    }
    return (text[11] == '\0');
}

static uint8_t UidMatches(const uchar *first, const uchar *second)
{
    return (memcmp(first, second, 4) == 0);
}

static uint8_t EnrollCard(const uchar *uid)
{
    uint8_t result = 0;
#if APP_USE_FREERTOS
    taskENTER_CRITICAL();
#endif
    if (enrollmentPending)
    {
        enrollmentPending = 0;
        result = 1;
        for (uint8_t card = 0; card < numAuthorizedCards; card++)
        {
            if (UidMatches(authorizedCards[card], uid))
            {
                result = 2;
                break;
            }
        }
        if ((result == 1U) && (numAuthorizedCards < MAX_AUTHORIZED_CARDS))
        {
            memcpy(authorizedCards[numAuthorizedCards], uid, 4);
            numAuthorizedCards++;
        }
        else if (result == 1U)
        {
            result = 3;
        }
    }
#if APP_USE_FREERTOS
    taskEXIT_CRITICAL();
#endif
    return result;
}

static void ProcessUartCommand(char *command)
{
    uchar uid[4];
    char *argument;

    for (char *ch = command; *ch != '\0'; ch++)
    {
        if ((*ch >= 'a') && (*ch <= 'z'))
        {
            *ch = (char)(*ch - 'a' + 'A');
        }
    }
    while ((*command == ' ') || (*command == '\t'))
    {
        command++;
    }
    size_t length = strlen(command);
    while ((length > 0U) &&
           ((command[length - 1U] == ' ') || (command[length - 1U] == '\t')))
    {
        command[--length] = '\0';
    }

    if (strcmp(command, "HELP") == 0)
    {
        UART_Write("Commands: HELP, LIST, ADD uid, DEL uid, ENROLL, OPEN, LOCK, UNLOCK, STATUS, LOG, LOG CLEAR\r\n");
        UART_Write("UID format: XX:XX:XX:XX\r\n");
        return;
    }
    if (strcmp(command, "ENROLL") == 0)
    {
        const char *result;
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        if (numAuthorizedCards >= MAX_AUTHORIZED_CARDS)
        {
            result = "ERR,CARD LIST FULL\r\n";
        }
        else
        {
            enrollmentPending = 1;
            result = "READY,SCAN CARD TO ENROLL\r\n";
        }
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write(result);
        return;
    }
    if (strcmp(command, "OPEN") == 0)
    {
        UART_Write("OK,MANUAL OPEN\r\n");
        Barrier_OpenThenClose();
        ShowIdleScreen();
        return;
    }
    if (strcmp(command, "LOCK") == 0)
    {
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        systemLocked = 1;
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write("OK,SYSTEM LOCKED\r\n");
        return;
    }
    if (strcmp(command, "UNLOCK") == 0)
    {
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        systemLocked = 0;
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write("OK,SYSTEM UNLOCKED\r\n");
        return;
    }
    if (strcmp(command, "STATUS") == 0)
    {
        uint8_t locked;
        uint8_t cards;
        uint8_t logs;
        uint8_t invalid;
        uint8_t enrolling;
        char status[96];
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        locked = systemLocked;
        cards = numAuthorizedCards;
        logs = cardLogCount;
        invalid = consecutiveInvalidCards;
        enrolling = enrollmentPending;
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        (void)snprintf(status, sizeof(status),
                       "STATUS,%s,CARDS=%u,LOGS=%u,INVALID=%u,ENROLL=%s\r\n",
                       locked ? "LOCKED" : "UNLOCKED",
                       (unsigned int)cards, (unsigned int)logs,
                       (unsigned int)invalid, enrolling ? "READY" : "OFF");
        UART_Write(status);
        return;
    }
    if (strcmp(command, "LIST") == 0)
    {
        uint8_t count;
        uchar cards[MAX_AUTHORIZED_CARDS][4];
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        count = numAuthorizedCards;
        memcpy(cards, authorizedCards, sizeof(cards));
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        char countText[16];
        (void)snprintf(countText, sizeof(countText), "CARDS,%u\r\n", count);
        UART_Write(countText);
        for (uint8_t card = 0; card < count; card++)
        {
            UART_WriteUidLine("CARD,", cards[card], "\r\n");
        }
        return;
    }
    if (strncmp(command, "ADD ", 4) == 0)
    {
        argument = command + 4;
        if (!ParseUid(argument, uid))
        {
            UART_Write("ERR,Use ADD XX:XX:XX:XX\r\n");
            return;
        }

        const char *result = "OK,ADDED\r\n";
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        for (uint8_t card = 0; card < numAuthorizedCards; card++)
        {
            if (UidMatches(authorizedCards[card], uid))
            {
                result = "ERR,CARD EXISTS\r\n";
                break;
            }
        }
        if ((result[0] == 'O') && (numAuthorizedCards >= MAX_AUTHORIZED_CARDS))
        {
            result = "ERR,CARD LIST FULL\r\n";
        }
        if (result[0] == 'O')
        {
            memcpy(authorizedCards[numAuthorizedCards], uid, sizeof(uid));
            numAuthorizedCards++;
        }
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write(result);
        return;
    }
    if (strncmp(command, "DEL ", 4) == 0)
    {
        argument = command + 4;
        if (!ParseUid(argument, uid))
        {
            UART_Write("ERR,Use DEL XX:XX:XX:XX\r\n");
            return;
        }

        const char *result = "ERR,CARD NOT FOUND\r\n";
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        for (uint8_t card = 0; card < numAuthorizedCards; card++)
        {
            if (UidMatches(authorizedCards[card], uid))
            {
                for (uint8_t next = card; next + 1U < numAuthorizedCards; next++)
                {
                    memcpy(authorizedCards[next], authorizedCards[next + 1U], 4);
                }
                numAuthorizedCards--;
                result = "OK,DELETED\r\n";
                break;
            }
        }
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write(result);
        return;
    }
    if (strcmp(command, "LOG") == 0)
    {
        AppCardLog entries[CARD_LOG_CAPACITY];
        uint8_t count;
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        count = cardLogCount;
        uint8_t first = (uint8_t)((cardLogNext + CARD_LOG_CAPACITY - count) %
                                  CARD_LOG_CAPACITY);
        for (uint8_t i = 0; i < count; i++)
        {
            entries[i] = cardLog[(first + i) % CARD_LOG_CAPACITY];
        }
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write(count == 0U ? "LOG,EMPTY\r\n" : "LOG,HISTORY\r\n");
        for (uint8_t i = 0; i < count; i++)
        {
            char line[48];
            (void)snprintf(line, sizeof(line),
                           "LOG,%lu,%02X:%02X:%02X:%02X,%s\r\n",
                           (unsigned long)entries[i].timestamp,
                           entries[i].uid[0], entries[i].uid[1],
                           entries[i].uid[2], entries[i].uid[3],
                           entries[i].authorized ? "ALLOW" : "DENY");
            UART_Write(line);
        }
        return;
    }
    if (strcmp(command, "LOG CLEAR") == 0)
    {
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        cardLogNext = 0;
        cardLogCount = 0;
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write("OK,LOG CLEARED\r\n");
        return;
    }
    UART_Write("ERR,Unknown command. Send HELP\r\n");
}

void App_ProcessUart(void)
{
    uint8_t ch;
    while (uart_read_char(&ch))
    {
        if ((ch == '\r') || (ch == '\n'))
        {
            if (uartCommandOverflow)
            {
                UART_Write("ERR,Command too long\r\n");
            }
            else if (uartCommandLength > 0U)
            {
                uartCommand[uartCommandLength] = '\0';
                ProcessUartCommand(uartCommand);
            }
            uartCommandLength = 0;
            uartCommandOverflow = 0;
        }
        else if (!uartCommandOverflow)
        {
            if (uartCommandLength < (UART_COMMAND_CAPACITY - 1U))
            {
                uartCommand[uartCommandLength++] = (char)ch;
            }
            else
            {
                uartCommandOverflow = 1;
            }
        }
    }
}

uint8_t App_PollEmergencyButton(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint8_t pending = emergencyButtonPending;
    emergencyButtonPending = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    if (pending)
    {
        emergencyButtonWaitingForRelease = 1U;
        emergencyButtonReleaseTiming = 0U;
        return 1U;
    }

    if (emergencyButtonWaitingForRelease)
    {
        if (HAL_GPIO_ReadPin(EMERGENCY_BUTTON_PORT, EMERGENCY_BUTTON_PIN) == GPIO_PIN_SET)
        {
            if (!emergencyButtonReleaseTiming)
            {
                emergencyButtonReleaseTiming = 1U;
                emergencyButtonReleaseSince = now;
            }
            else if ((uint32_t)(now - emergencyButtonReleaseSince) >=
                     EMERGENCY_BUTTON_DEBOUNCE_MS)
            {
                __HAL_GPIO_EXTI_CLEAR_IT(EMERGENCY_BUTTON_PIN);
                if (HAL_GPIO_ReadPin(EMERGENCY_BUTTON_PORT, EMERGENCY_BUTTON_PIN) ==
                    GPIO_PIN_SET)
                {
                    emergencyButtonWaitingForRelease = 0U;
                    emergencyButtonReleaseTiming = 0U;
                    EXTI->IMR |= EMERGENCY_BUTTON_PIN;
                }
                else
                {
                    emergencyButtonPending = 1U;
                }
            }
        }
        else
        {
            emergencyButtonReleaseTiming = 0U;
        }
    }
    return 0U;
}

void App_EmergencyButtonIRQ(void)
{
    emergencyButtonPending = 1U;
}

void App_Init(void)
{
#if APP_USE_FREERTOS
    uartMutex = xSemaphoreCreateMutex();
    if (uartMutex == NULL)
    {
        Error_Handler();
    }
#endif
    // 1. Khởi tạo Buzzer (PB10)
    Buzzer_Init();

    // 2. Tự kiểm tra phần cứng: Bật cả 2 LED xanh & đỏ trong 200ms kèm 1 tiếng bíp ngắn
    // Giúp người dùng ngay lập tức xác nhận MCU, LED và Buzzer đều hoạt động bình thường
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_SET);
    Tone(1000, 100);
    noTone();
    HAL_Delay(150);
    LED_AllOff();

    // 3. Khởi tạo Servo chắn barie
    SERVO_Init(SERVO_Motor1);
    SERVO_MoveTo(SERVO_Motor1, BARRIER_CLOSE_ANGLE);

    // 4. Khởi tạo màn hình LCD 16x2 I2C
    I2C_LCD_Init(MyI2C_LCD);
    I2C_LCD_Backlight(MyI2C_LCD);
    I2C_LCD_Display(MyI2C_LCD);
    I2C_LCD_Clear(MyI2C_LCD);
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 0);
    I2C_LCD_WriteString(MyI2C_LCD, "Dang khoi dong..");

    // 5. Khởi tạo module đọc thẻ RFID RC522 và kiểm tra phiên bản
    MFRC522_Init();
    uchar rc522_ver = MFRC522_ReadVersion();
    uint8_t rc522_ok = (rc522_ver != 0x00 && rc522_ver != 0xFF);

    char verMsg[48];
    (void)snprintf(verMsg, sizeof(verMsg), "RC522 Ver: 0x%02X%s\r\n", rc522_ver,
                   rc522_ok ? " (OK)" : " (ERR: Kiem tra day noi!)");
    UART_Write(verMsg);

    // Hiển thị trực tiếp thông tin kiểm tra RC522 lên LCD dòng 2
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
    char lcdVer[17];
    (void)snprintf(lcdVer, sizeof(lcdVer), "RC522:0x%02X %s", rc522_ver, rc522_ok ? "OK " : "ERR");
    I2C_LCD_WriteString(MyI2C_LCD, lcdVer);
    HAL_Delay(1500);

    UART_Write("RFID System Ready!\r\n");
    UART_Write("Gui HELP de xem danh sach lenh UART\r\n");

    // 6. Hiển thị màn hình chờ sẵn sàng quẹt thẻ
    ShowIdleScreen();
}

uint8_t App_RFIDPoll(AppCardEvent *event)
{
    uchar status;
    uchar tagType[4] = {0};
    uchar serialNumber[5] = {0};

    // Thử gửi lệnh REQA (0x26), nếu thẻ đang ở chế độ Halt thì thử tiếp WUPA (0x52)
    status = MFRC522_Request(PICC_REQIDL, tagType);
    if (status != MI_OK)
    {
        status = MFRC522_Request(PICC_REQALL, tagType);
    }

    if (status != MI_OK)
    {
        return 0;
    }

    // Chống va chạm và đọc UID 4-byte của thẻ Mifare
    status = MFRC522_Anticoll(serialNumber);
    if (status != MI_OK)
    {
        return 0;
    }

    for (uint8_t i = 0; i < 4; i++)
    {
        event->uid[i] = serialNumber[i];
    }
    event->type = APP_EVENT_CARD;
    MFRC522_Halt();
    return 1;
}

void App_HandleCard(const AppCardEvent *event)
{
    if (event->type == APP_EVENT_EMERGENCY_OPEN)
    {
        HandleEmergencyOpen();
        return;
    }

    UART_WriteUidLine("SCAN,", event->uid, "\r\n");

    uint8_t enrollmentResult = EnrollCard(event->uid);
    uint8_t cardIsAuthorized = IsAuthorizedCard(event->uid);
    uint8_t locked = systemLocked;
    uint8_t authorized = cardIsAuthorized && !locked;
    uint8_t triggerAlarm = UpdateInvalidCardState(cardIsAuthorized);

    if (enrollmentResult == 1U)
    {
        UART_WriteUidLine("OK,ENROLLED,", event->uid, "\r\n");
    }
    else if (enrollmentResult == 2U)
    {
        UART_WriteUidLine("INFO,CARD ALREADY EXISTS,", event->uid, "\r\n");
    }
    else if (enrollmentResult == 3U)
    {
        UART_Write("ERR,CARD LIST FULL\r\n");
    }
    RecordCardEvent(event->uid, authorized);

    if (authorized)
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
        I2C_LCD_WriteString(MyI2C_LCD, locked ? "System Locked" : "Sai The");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, locked ? "Use UART unlock" : "Tu choi");
        Tone(392, 500);
        noTone();
        if (triggerAlarm)
        {
            UART_Write("ALERT,3 consecutive invalid cards\r\n");
            for (uint8_t tone = 0; tone < 3U; tone++)
            {
                Tone(880, 180);
                App_DelayMs(100);
            }
            I2C_LCD_Clear(MyI2C_LCD);
            I2C_LCD_WriteString(MyI2C_LCD, "WARNING");
            I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
            I2C_LCD_WriteString(MyI2C_LCD, "Sai lien tiep 3");
        }
        App_DelayMs(1500);
    }

    ShowIdleScreen();
}

static void HandleEmergencyOpen(void)
{
    emergencyHandlerActive = 1U;
    UART_Write("ALERT,EMERGENCY BUTTON PRESSED\r\n");
    I2C_LCD_Clear(MyI2C_LCD);
    I2C_LCD_WriteString(MyI2C_LCD, "EMERGENCY OPEN");
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
    I2C_LCD_WriteString(MyI2C_LCD, "Barrier opening");
    Tone(1200, 150);
    noTone();

    if (barrierIsOpen)
    {
        emergencyExtensionRequested = 1U;
    }
    else
    {
        Barrier_OpenThenClose();
    }

    emergencyHandlerActive = 0U;
    ShowIdleScreen();
}
