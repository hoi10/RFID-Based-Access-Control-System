// He thong kiem soat ra vao bang the RFID (STM32F103 + FreeRTOS)
// MFRC522 (Software SPI), I2C LCD 1602, Servo 360, Active Buzzer, LED, UART

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

// Dinh nghia LCD va Servo
#define MyI2C_LCD                    I2C_LCD_1
#define SERVO_Motor1                 0

// Chan GPIO cho LED
#define LED_GREEN_PORT               GPIOA
#define LED_GREEN_PIN                GPIO_PIN_4
#define LED_RED_PORT                 GPIOA
#define LED_RED_PIN                  GPIO_PIN_5

// Chan GPIO cho coi bao (Active Buzzer - PB10)
#define BUZZER_PORT                  GPIOB
#define BUZZER_PIN                   GPIO_PIN_10

// Thoi gian coi bao (ms)
#define BUZZER_BEEP_BOOT_MS          70U
#define BUZZER_BEEP_ALLOW_MS         90U
#define BUZZER_BEEP_DENY_ON_MS       70U
#define BUZZER_BEEP_DENY_OFF_MS      50U
#define BUZZER_BEEP_ALARM_ON_MS      80U
#define BUZZER_BEEP_ALARM_OFF_MS     50U

// Thong so dieu khien servo barrier (goc quay thuc te 90 do)
#define BARRIER_CLOSED_ANGLE         0.0f
#define BARRIER_OPEN_ANGLE           90.0f
#define BARRIER_OPEN_MS              2500U
#define INVALID_CARD_ALARM_THRESHOLD 3U
#define CARD_LOG_CAPACITY            10U
#define UART_COMMAND_CAPACITY        40U

// Nguong chong doc lap the
#define CARD_ABSENT_CONFIRM_CYCLES   8U
#define CARD_ABSENT_MIN_MS           400U
#define CARD_MAX_ERR_STREAK          5U
#define RC522_HEALTH_CHECK_CYCLES    40U

// Trang thai hien dien cua the
typedef enum
{
    CARD_STATE_WAIT_NEW     = 0,
    CARD_STATE_HOLD_PRESENT = 1
} CardPresenceState;

// Nhat ky quet the
typedef struct
{
    uint32_t timestamp;
    uchar    uid[4];
    uint8_t  authorized;
} AppCardLog;

// Danh sach UID hop le trong RAM (toi da 3 the)
static uchar authorizedCards[MAX_AUTHORIZED_CARDS][4] = {
    {0x62, 0x99, 0x18, 0x02},
    {0x11, 0x22, 0x33, 0x44}
};
static uint8_t numAuthorizedCards = 2U;

// UID mac dinh khi khoi tao Flash
static const uchar s_defaultCards[2][4] = {
    {0x62, 0x99, 0x18, 0x02},
    {0x11, 0x22, 0x33, 0x44}
};

// Bien Flash Two-Page A/B
static uint32_t s_activeFlashPage  = APP_FLASH_PAGE_62_ADDR;
static uint32_t s_activeGeneration = 0U;
static volatile uint8_t s_flashBusy = 0U;

// Bien trang thai he thong
static AppCardLog cardLog[CARD_LOG_CAPACITY];
static uint8_t cardLogNext;
static uint8_t cardLogCount;
static uint8_t consecutiveInvalidCards;
static uint8_t enrollmentPending;
static uint8_t systemLocked;
static uint8_t manualLedGreen;
static uint8_t manualLedRed;
static uint8_t manualBuzzer;
static uint8_t barrierIsOpen;

// Buffer nhan lenh UART
static char uartCommand[UART_COMMAND_CAPACITY];
static uint8_t uartCommandLength;
static uint8_t uartCommandOverflow;

// Bien chong doc lap the
static CardPresenceState cardPresenceState = CARD_STATE_WAIT_NEW;
static uint8_t  cardAbsentCount = 0U;
static uint8_t  cardErrStreak = 0U;
static uint32_t cardLastSeenTick = 0U;
static uint8_t  heldUidLen = 0U;
static uchar    heldUid[10] = {0};
static uint8_t  cardCollisionUnknownUid = 0U;
static uint8_t  rc522HealthCheckCounter = 0U;

// Bien chan doan FreeRTOS
static uint32_t queueDropCount = 0U;
static volatile uint8_t queuedCardEvents = 0U;

#if APP_USE_FREERTOS
static SemaphoreHandle_t uartMutex;
static SemaphoreHandle_t rc522Mutex;
static TaskHandle_t s_rfidTaskHandle;
static TaskHandle_t s_appTaskHandle;
#endif

// Gui chuoi qua UART (co Mutex bao ve khi chay FreeRTOS)
static void UART_Write(const char *text)
{
#if APP_USE_FREERTOS
    uint8_t mutexTaken = 0U;
    if ((uartMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
    {
        if (xSemaphoreTake(uartMutex, portMAX_DELAY) == pdTRUE)
        {
            mutexTaken = 1U;
        }
    }
#endif
    uart_write_str(text);
#if APP_USE_FREERTOS
    if (mutexTaken != 0U)
    {
        (void)xSemaphoreGive(uartMutex);
    }
#endif
}

// Dinh dang UID thanh chuoi hex XX:XX:XX:XX
static void FormatUidBytes(const uchar *uid, uint8_t uidLen, char *outBuf, size_t outSize)
{
    size_t pos = 0U;
    if ((outBuf == NULL) || (outSize == 0U))
    {
        return;
    }
    outBuf[0] = '\0';
    for (uint8_t i = 0U; i < uidLen; i++)
    {
        int written = snprintf(outBuf + pos, outSize - pos,
                               (i + 1U < uidLen) ? "%02X:" : "%02X", uid[i]);
        if ((written <= 0) || ((size_t)written >= (outSize - pos)))
        {
            break;
        }
        pos += (size_t)written;
    }
}

// Gui mot dong log UID qua UART
static void UART_WriteUidLine(const char *prefix, const uchar *uid, uint8_t uidLen, const char *suffix)
{
    char uidStr[36];
    char line[80];
    FormatUidBytes(uid, uidLen, uidStr, sizeof(uidStr));
    (void)snprintf(line, sizeof(line), "%s%s%s", prefix, uidStr, suffix);
    UART_Write(line);
}

// Tinh CRC-32 theo chuan IEEE 802.3
static uint32_t App_Flash_CalculateCrc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < length; i++)
    {
        crc ^= (uint32_t)data[i];
        for (uint8_t b = 0U; b < 8U; b++)
        {
            if (crc & 1U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return ~crc;
}

// Kiem tra tinh hop le cua ban ghi Flash
static uint8_t App_Flash_VerifyRecord(const AppFlashRecord *rec)
{
    if (rec == NULL)
    {
        return 0U;
    }
    if ((rec->magic != APP_FLASH_MAGIC) || (rec->version != APP_FLASH_VERSION))
    {
        return 0U;
    }
    if (rec->count > MAX_AUTHORIZED_CARDS)
    {
        return 0U;
    }
    uint32_t expectedCrc = App_Flash_CalculateCrc32((const uint8_t *)rec,
                                                   (uint32_t)(offsetof(AppFlashRecord, crc32)));
    if (rec->crc32 != expectedCrc)
    {
        return 0U;
    }
    return 1U;
}

// Ghi ban ghi AppFlashRecord xuong 1 trang Flash (Page 62 hoac 63)
static uint8_t App_Flash_WritePage(uint32_t pageAddress, const AppFlashRecord *record)
{
    // Kiem tra dia chi va dung luong Flash
    if ((pageAddress != APP_FLASH_PAGE_62_ADDR) && (pageAddress != APP_FLASH_PAGE_63_ADDR))
    {
        return 0U;
    }
    if ((*(const uint16_t *)FLASHSIZE_BASE) < 64U)
    {
        return 0U;
    }

    uint8_t writeSuccess = 0U;

    // Tam tat ngat UART va suspend scheduler FreeRTOS
    NVIC_DisableIRQ(USART1_IRQn);
#if APP_USE_FREERTOS
    vTaskSuspendAll();
#endif

    // Thao tac xoa va ghi Flash
    if (HAL_FLASH_Unlock() == HAL_OK)
    {
        FLASH_EraseInitTypeDef eraseInit;
        eraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
        eraseInit.PageAddress = pageAddress;
        eraseInit.NbPages     = 1U;
        uint32_t pageError    = 0U;

        if (HAL_FLASHEx_Erase(&eraseInit, &pageError) == HAL_OK)
        {
            const uint16_t *data16 = (const uint16_t *)record;
            uint32_t halfWords = sizeof(AppFlashRecord) / sizeof(uint16_t);
            uint8_t progOk = 1U;

            for (uint32_t i = 0U; i < halfWords; i++)
            {
                if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                      pageAddress + (i * 2U),
                                      (uint64_t)data16[i]) != HAL_OK)
                {
                    progOk = 0U;
                    break;
                }
            }
            if (progOk != 0U)
            {
                writeSuccess = 1U;
            }
        }

        // Luon khoa lai Flash
        (void)HAL_FLASH_Lock();
    }

    // Don dep trang thai UART sau khi Flash hoan tat
    __IO uint32_t dummy = USART1->SR;
    dummy = USART1->DR;
    (void)dummy;

    NVIC_ClearPendingIRQ(USART1_IRQn);
    NVIC_EnableIRQ(USART1_IRQn);

#if APP_USE_FREERTOS
    (void)xTaskResumeAll();
#endif

    return writeSuccess;
}

// Ghi du lieu xuong Flash theo co che Two-Page A/B
static uint8_t App_Flash_Commit(const uchar cards[][4], uint8_t count)
{
    if ((count > MAX_AUTHORIZED_CARDS) || (s_flashBusy != 0U))
    {
        return 0U;
    }

    s_flashBusy = 1U;

    uint32_t targetPage = (s_activeFlashPage == APP_FLASH_PAGE_62_ADDR) ?
                          APP_FLASH_PAGE_63_ADDR : APP_FLASH_PAGE_62_ADDR;

    AppFlashRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = APP_FLASH_MAGIC;
    rec.version = APP_FLASH_VERSION;
    rec.count = count;
    rec.generation = s_activeGeneration + 1U;
    if (count > 0U)
    {
        memcpy(rec.cards, cards, (size_t)count * 4U);
    }
    rec.crc32 = App_Flash_CalculateCrc32((const uint8_t *)&rec,
                                        (uint32_t)(offsetof(AppFlashRecord, crc32)));

    uint8_t written = App_Flash_WritePage(targetPage, &rec);
    if (written == 0U)
    {
        s_flashBusy = 0U;
        return 0U;
    }

    // Doc lai va xac minh CRC
    const AppFlashRecord *verifyRec = (const AppFlashRecord *)targetPage;
    if (App_Flash_VerifyRecord(verifyRec) == 0U)
    {
        s_flashBusy = 0U;
        return 0U;
    }

    // Chuyen active page sang trang moi
    s_activeFlashPage = targetPage;
    s_activeGeneration = rec.generation;
    s_flashBusy = 0U;
    return 1U;
}

// Khoi tao Flash luc boot, chon ban ghi hop le co Generation moi nhat
void App_Flash_Init(void)
{
    uint16_t reportedFlashKb = *(const uint16_t *)FLASHSIZE_BASE;
    if (reportedFlashKb < 64U)
    {
        UART_Write("ERR,FLASH_TOO_SMALL\r\n");
        memcpy(authorizedCards, s_defaultCards, sizeof(s_defaultCards));
        numAuthorizedCards = 2U;
        return;
    }

    const AppFlashRecord *recA = (const AppFlashRecord *)APP_FLASH_PAGE_62_ADDR;
    const AppFlashRecord *recB = (const AppFlashRecord *)APP_FLASH_PAGE_63_ADDR;

    uint8_t validA = App_Flash_VerifyRecord(recA);
    uint8_t validB = App_Flash_VerifyRecord(recB);

    const AppFlashRecord *selectedRec = NULL;
    uint32_t selectedPage = 0U;

    if (validA && validB)
    {
        if (recA->generation >= recB->generation)
        {
            selectedRec = recA;
            selectedPage = APP_FLASH_PAGE_62_ADDR;
        }
        else
        {
            selectedRec = recB;
            selectedPage = APP_FLASH_PAGE_63_ADDR;
        }
    }
    else if (validA)
    {
        selectedRec = recA;
        selectedPage = APP_FLASH_PAGE_62_ADDR;
    }
    else if (validB)
    {
        selectedRec = recB;
        selectedPage = APP_FLASH_PAGE_63_ADDR;
    }

    if (selectedRec != NULL)
    {
        s_activeFlashPage = selectedPage;
        s_activeGeneration = selectedRec->generation;
        numAuthorizedCards = (uint8_t)selectedRec->count;
        if (numAuthorizedCards > 0U)
        {
            memcpy(authorizedCards, selectedRec->cards, (size_t)numAuthorizedCards * 4U);
        }
    }
    else
    {
        // Khong co trang hop le: Nap mac dinh va ghi Page 62
        memcpy(authorizedCards, s_defaultCards, sizeof(s_defaultCards));
        numAuthorizedCards = 2U;
        s_activeFlashPage = APP_FLASH_PAGE_63_ADDR;
        s_activeGeneration = 0U;

        (void)App_Flash_Commit(authorizedCards, numAuthorizedCards);
    }
}

// Ham delay ho tro ca FreeRTOS va HAL
static void App_DelayMs(uint32_t ms)
{
#if APP_USE_FREERTOS
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }
#endif
    HAL_Delay(ms);
}

// Khoi tao coi bao PB10
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

static void Buzzer_Off(void)
{
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, manualBuzzer ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void Buzzer_Beep(uint32_t durationMs)
{
    if (durationMs == 0U)
    {
        return;
    }
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
    App_DelayMs(durationMs);
    Buzzer_Off();
}

static void Buzzer_BeepPattern(uint8_t count, uint32_t onMs, uint32_t offMs)
{
    for (uint8_t i = 0U; i < count; i++)
    {
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
        App_DelayMs(onMs);
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
        if ((i + 1U < count) && (offMs > 0U))
        {
            App_DelayMs(offMs);
        }
    }
    Buzzer_Off();
}

static void LED_AllOff(void)
{
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_RESET);
}

static void LED_RestoreManual(void)
{
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, manualLedGreen ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, manualLedRed ? GPIO_PIN_SET : GPIO_PIN_RESET);
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

// Hien thi man hinh cho
static void ShowIdleScreen(void)
{
    LED_RestoreManual();
    I2C_LCD_Clear(MyI2C_LCD);
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 0);
    I2C_LCD_WriteString(MyI2C_LCD, "Quet the RFID");
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
    I2C_LCD_WriteString(MyI2C_LCD, "Moi quet the...");
}

// Giu hien thi LCD nhung van phuc vu UART
static void App_HoldDisplayMs(uint32_t totalMs)
{
    uint32_t startTick = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - startTick) < totalMs)
    {
        App_ProcessUart();
        if (queuedCardEvents > 0U)
        {
            break;
        }
        uint32_t elapsed = (uint32_t)(HAL_GetTick() - startTick);
        uint32_t remain = (totalMs > elapsed) ? (totalMs - elapsed) : 0U;
        if (remain == 0U)
        {
            break;
        }
        App_DelayMs((remain > 20U) ? 20U : remain);
    }
}

static void Barrier_Open(void)
{
    if (!barrierIsOpen)
    {
        SERVO_MoveTo(SERVO_Motor1, BARRIER_OPEN_ANGLE);
        barrierIsOpen = 1U;
    }
}

static void Barrier_Close(void)
{
    if (barrierIsOpen)
    {
        SERVO_MoveTo(SERVO_Motor1, BARRIER_CLOSED_ANGLE);
        barrierIsOpen = 0U;
    }
}

static void Barrier_OpenThenClose(void)
{
    Barrier_Open();
    App_DelayMs(BARRIER_OPEN_MS);
    Barrier_Close();
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

// Chuyen chuoi "XX:XX:XX:XX" sang UID 4 byte
static uint8_t ParseUid(const char *text, uchar *uid)
{
    for (uint8_t byte = 0U; byte < 4U; byte++)
    {
        int high = HexValue(text[byte * 3U]);
        int low = HexValue(text[byte * 3U + 1U]);
        if ((high < 0) || (low < 0) ||
            ((byte < 3U) && (text[byte * 3U + 2U] != ':') && (text[byte * 3U + 2U] != ' ')))
        {
            return 0U;
        }
        uid[byte] = (uchar)((high << 4) | low);
    }
    return (text[11] == '\0') ? 1U : 0U;
}

// Kiem tra UID co trong danh sach hop le khong
static uint8_t IsAuthorizedCard(const uchar *scannedUID)
{
    uint8_t authorized = 0U;
#if APP_USE_FREERTOS
    taskENTER_CRITICAL();
#endif
    for (uint8_t card = 0U; card < numAuthorizedCards; card++)
    {
        if (memcmp(scannedUID, authorizedCards[card], 4U) == 0)
        {
            authorized = 1U;
            break;
        }
    }
#if APP_USE_FREERTOS
    taskEXIT_CRITICAL();
#endif
    return authorized;
}

// Cap nhat dem the sai lien tiep (bao dong khi sai 3 lan)
static uint8_t UpdateInvalidCardState(uint8_t authorized)
{
    uint8_t triggerAlarm = 0U;
#if APP_USE_FREERTOS
    taskENTER_CRITICAL();
#endif
    if (authorized)
    {
        consecutiveInvalidCards = 0U;
    }
    else
    {
        consecutiveInvalidCards++;
        if (consecutiveInvalidCards >= INVALID_CARD_ALARM_THRESHOLD)
        {
            triggerAlarm = 1U;
            consecutiveInvalidCards = 0U;
        }
    }
#if APP_USE_FREERTOS
    taskEXIT_CRITICAL();
#endif
    return triggerAlarm;
}

// Ghi su kien vao nhat ky
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

// Dang ky the 4-byte (RAM Staging & Rollback)
static uint8_t EnrollCard4Byte(const uchar *uid)
{
    uchar stagedCards[MAX_AUTHORIZED_CARDS][4];
    uint8_t stagedCount = 0U;
    uint8_t wasPending = 0U;

#if APP_USE_FREERTOS
    taskENTER_CRITICAL();
#endif
    if (enrollmentPending)
    {
        enrollmentPending = 0U;
        wasPending = 1U;
        stagedCount = numAuthorizedCards;
        memcpy(stagedCards, authorizedCards, sizeof(stagedCards));
    }
#if APP_USE_FREERTOS
    taskEXIT_CRITICAL();
#endif

    if (!wasPending)
    {
        return 0U;
    }

    // Kiem tra the da ton tai chua
    for (uint8_t card = 0U; card < stagedCount; card++)
    {
        if (memcmp(stagedCards[card], uid, 4U) == 0)
        {
            return 2U;
        }
    }

    // Kiem tra danh sach day
    if (stagedCount >= MAX_AUTHORIZED_CARDS)
    {
        return 3U;
    }

    stagedCards[stagedCount][0] = uid[0];
    stagedCards[stagedCount][1] = uid[1];
    stagedCards[stagedCount][2] = uid[2];
    stagedCards[stagedCount][3] = uid[3];
    stagedCount++;

    // Ghi xuong Flash, chi cap nhat RAM khi thanh cong
    if (App_Flash_Commit(stagedCards, stagedCount))
    {
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        memcpy(authorizedCards, stagedCards, sizeof(authorizedCards));
        numAuthorizedCards = stagedCount;
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        return 1U;
    }
    else
    {
        return 4U;
    }
}

void App_SetTaskHandles(void *rfidTaskHandle, void *appTaskHandle)
{
#if APP_USE_FREERTOS
    s_rfidTaskHandle = (TaskHandle_t)rfidTaskHandle;
    s_appTaskHandle = (TaskHandle_t)appTaskHandle;
#else
    (void)rfidTaskHandle;
    (void)appTaskHandle;
#endif
}

// In thong so FreeRTOS
void App_PrintRtosStats(void)
{
#if APP_USE_FREERTOS
    char line[128];
    size_t freeHeap = xPortGetFreeHeapSize();
    size_t minEverHeap = xPortGetMinimumEverFreeHeapSize();
    UBaseType_t rfidWords = (s_rfidTaskHandle != NULL) ? uxTaskGetStackHighWaterMark(s_rfidTaskHandle) : 0U;
    UBaseType_t appWords = (s_appTaskHandle != NULL) ? uxTaskGetStackHighWaterMark(s_appTaskHandle) : 0U;
    uint32_t rfidBytes = (uint32_t)rfidWords * (uint32_t)sizeof(StackType_t);
    uint32_t appBytes = (uint32_t)appWords * (uint32_t)sizeof(StackType_t);

    (void)snprintf(line, sizeof(line),
                   "RTOS,HEAP_FREE=%uB,HEAP_MIN_EVER=%uB,STACK_HW_RFID=%luw(%luB),STACK_HW_APP=%luw(%luB),Q_DROP=%lu\r\n",
                   (unsigned int)freeHeap,
                   (unsigned int)minEverHeap,
                   (unsigned long)rfidWords, (unsigned long)rfidBytes,
                   (unsigned long)appWords, (unsigned long)appBytes,
                   (unsigned long)queueDropCount);
    UART_Write(line);
#endif
}

void App_EmergencyButtonIRQ(void)
{
    // Du phong: PB1 khong gan nut nhan
}

void App_NotifyCardQueueResult(uint8_t sentOk)
{
    if (sentOk == 0U)
    {
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        queueDropCount++;
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write("ERR,CARD_QUEUE_FULL,EVENT_DROPPED\r\n");
    }
    else
    {
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        if (queuedCardEvents < 255U)
        {
            queuedCardEvents++;
        }
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
    }
}

// Xu ly lenh tu terminal UART
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
        UART_Write("Commands: HELP, STATUS, LIST, ADD uid, DEL uid, ENROLL, OPEN, CLOSE, LOCK, UNLOCK, LOG, LOG CLEAR, BEEP\r\n");
        UART_Write("UID format (Auth): XX:XX:XX:XX (4-byte UID)\r\n");
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
            enrollmentPending = 1U;
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
        LED_GreenOn();
        Buzzer_Beep(BUZZER_BEEP_ALLOW_MS);
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "Welcome");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Mo rao chan");
        Barrier_OpenThenClose();
        LED_AllOff();
        ShowIdleScreen();
        return;
    }
    if (strcmp(command, "BARRIER OPEN") == 0)
    {
        LED_GreenOn();
        Buzzer_Beep(BUZZER_BEEP_ALLOW_MS);
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "Welcome");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Mo rao chan");
        Barrier_Open();
        UART_Write("OK,BARRIER OPEN\r\n");
        return;
    }
    if ((strcmp(command, "CLOSE") == 0) || (strcmp(command, "BARRIER CLOSE") == 0))
    {
        Barrier_Close();
        UART_Write("OK,BARRIER CLOSED\r\n");
        LED_AllOff();
        ShowIdleScreen();
        return;
    }
    if (strcmp(command, "LOCK") == 0)
    {
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        systemLocked = 1U;
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
        systemLocked = 0U;
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
        CardPresenceState cState;
        uint8_t absCnt;
        uint8_t collUnk;
        char status[128];
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        locked = systemLocked;
        cards = numAuthorizedCards;
        logs = cardLogCount;
        invalid = consecutiveInvalidCards;
        enrolling = enrollmentPending;
        cState = cardPresenceState;
        absCnt = cardAbsentCount;
        collUnk = cardCollisionUnknownUid;
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        (void)snprintf(status, sizeof(status),
                       "STATUS,%s,CARDS=%u,LOGS=%u,INVALID=%u,ENROLL=%s,CARD_STATE=%s,ABSENT=%u,COLL_UNK=%u\r\n",
                       locked ? "LOCKED" : "UNLOCKED",
                       (unsigned int)cards, (unsigned int)logs,
                       (unsigned int)invalid, enrolling ? "READY" : "OFF",
                       (cState == CARD_STATE_HOLD_PRESENT) ? "HOLD_PRESENT" : "WAIT_NEW",
                       (unsigned int)absCnt, (unsigned int)collUnk);
        UART_Write(status);
        App_PrintRtosStats();
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
        for (uint8_t card = 0U; card < count; card++)
        {
            UART_WriteUidLine("CARD,", cards[card], 4U, "\r\n");
        }
        return;
    }
    if (strncmp(command, "ADD ", 4) == 0)
    {
        argument = command + 4;
        if (!ParseUid(argument, uid))
        {
            UART_Write("ERR,Use ADD XX:XX:XX:XX (Only 4-byte UID supported)\r\n");
            return;
        }

        uchar stagedCards[MAX_AUTHORIZED_CARDS][4];
        uint8_t stagedCount;
        uint8_t exists = 0U;

#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        stagedCount = numAuthorizedCards;
        memcpy(stagedCards, authorizedCards, sizeof(stagedCards));
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif

        for (uint8_t card = 0U; card < stagedCount; card++)
        {
            if (memcmp(stagedCards[card], uid, 4U) == 0)
            {
                exists = 1U;
                break;
            }
        }
        if (exists != 0U)
        {
            UART_Write("ERR,CARD EXISTS\r\n");
            return;
        }
        if (stagedCount >= MAX_AUTHORIZED_CARDS)
        {
            UART_Write("ERR,CARD LIST FULL\r\n");
            return;
        }

        memcpy(stagedCards[stagedCount], uid, 4U);
        stagedCount++;

        if (App_Flash_Commit(stagedCards, stagedCount))
        {
#if APP_USE_FREERTOS
            taskENTER_CRITICAL();
#endif
            memcpy(authorizedCards, stagedCards, sizeof(authorizedCards));
            numAuthorizedCards = stagedCount;
#if APP_USE_FREERTOS
            taskEXIT_CRITICAL();
#endif
            UART_Write("OK,ADDED\r\n");
        }
        else
        {
            UART_Write("ERR,FLASH_WRITE_FAILED\r\n");
        }
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

        uchar stagedCards[MAX_AUTHORIZED_CARDS][4];
        uint8_t stagedCount;
        int foundIndex = -1;

#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        stagedCount = numAuthorizedCards;
        memcpy(stagedCards, authorizedCards, sizeof(stagedCards));
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif

        for (uint8_t card = 0U; card < stagedCount; card++)
        {
            if (memcmp(stagedCards[card], uid, 4U) == 0)
            {
                foundIndex = (int)card;
                break;
            }
        }
        if (foundIndex < 0)
        {
            UART_Write("ERR,CARD NOT FOUND\r\n");
            return;
        }

        for (uint8_t next = (uint8_t)foundIndex; next + 1U < stagedCount; next++)
        {
            memcpy(stagedCards[next], stagedCards[next + 1U], 4U);
        }
        stagedCount--;

        if (App_Flash_Commit(stagedCards, stagedCount))
        {
#if APP_USE_FREERTOS
            taskENTER_CRITICAL();
#endif
            memcpy(authorizedCards, stagedCards, sizeof(authorizedCards));
            numAuthorizedCards = stagedCount;
#if APP_USE_FREERTOS
            taskEXIT_CRITICAL();
#endif
            UART_Write("OK,DELETED\r\n");
        }
        else
        {
            UART_Write("ERR,FLASH_WRITE_FAILED\r\n");
        }
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
        for (uint8_t i = 0U; i < count; i++)
        {
            entries[i] = cardLog[(first + i) % CARD_LOG_CAPACITY];
        }
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write(count == 0U ? "LOG,EMPTY\r\n" : "LOG,HISTORY\r\n");
        for (uint8_t i = 0U; i < count; i++)
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
        cardLogNext = 0U;
        cardLogCount = 0U;
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        UART_Write("OK,LOG CLEARED\r\n");
        return;
    }
    if (strcmp(command, "LED GREEN ON") == 0)
    {
        manualLedGreen = 1U;
        HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_SET);
        UART_Write("OK,LED GREEN ON\r\n");
        return;
    }
    if (strcmp(command, "LED GREEN OFF") == 0)
    {
        manualLedGreen = 0U;
        HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
        UART_Write("OK,LED GREEN OFF\r\n");
        return;
    }
    if (strcmp(command, "LED RED ON") == 0)
    {
        manualLedRed = 1U;
        HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_SET);
        UART_Write("OK,LED RED ON\r\n");
        return;
    }
    if (strcmp(command, "LED RED OFF") == 0)
    {
        manualLedRed = 0U;
        HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_RESET);
        UART_Write("OK,LED RED OFF\r\n");
        return;
    }
    if (strcmp(command, "BUZZER ON") == 0)
    {
        manualBuzzer = 1U;
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
        UART_Write("OK,BUZZER ON\r\n");
        return;
    }
    if (strcmp(command, "BUZZER OFF") == 0)
    {
        manualBuzzer = 0U;
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
        UART_Write("OK,BUZZER OFF\r\n");
        return;
    }
    if ((strcmp(command, "BEEP") == 0) || (strcmp(command, "BUZZER BEEP") == 0))
    {
        Buzzer_Beep(BUZZER_BEEP_ALLOW_MS);
        UART_Write("OK,BUZZER BEEP\r\n");
        return;
    }
    UART_Write("ERR,Unknown command. Send HELP\r\n");
}

// Doc buffer UART
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
            uartCommandLength = 0U;
            uartCommandOverflow = 0U;
        }
        else if (ch >= 32U && ch <= 126U && !uartCommandOverflow)
        {
            if (uartCommandLength < (UART_COMMAND_CAPACITY - 1U))
            {
                uartCommand[uartCommandLength++] = (char)ch;
            }
            else
            {
                uartCommandOverflow = 1U;
            }
        }
    }

    if (manualBuzzer)
    {
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
    }
}

// Khoi tao toan bo ung dung
void App_Init(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
    App_Flash_Init();

    Buzzer_Init();

    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_SET);
    Buzzer_Beep(BUZZER_BEEP_BOOT_MS);
    HAL_Delay(150);
    LED_AllOff();

    SERVO_Init(SERVO_Motor1);
    SERVO_MoveTo(SERVO_Motor1, BARRIER_CLOSED_ANGLE);

    I2C_LCD_Init(MyI2C_LCD);
    I2C_LCD_Backlight(MyI2C_LCD);
    I2C_LCD_Display(MyI2C_LCD);
    I2C_LCD_Clear(MyI2C_LCD);

    MFRC522_Init();

    uchar rc522_ver = MFRC522_ReadVersion();
    uint8_t rc522_ok = (rc522_ver != 0x00U && rc522_ver != 0xFFU);

    ShowIdleScreen();

#if APP_USE_FREERTOS
    uartMutex = xSemaphoreCreateMutex();
    rc522Mutex = xSemaphoreCreateMutex();
    if ((uartMutex == NULL) || (rc522Mutex == NULL))
    {
        Error_Handler();
    }
#endif

    if (rc522_ok)
    {
        UART_Write("SYSTEM STARTED SUCCESSFULLY\r\n");
    }
    else
    {
        UART_Write("ERR,RC522_INIT_FAILED\r\n");
    }
}

// Quet the RFID bang RC522 (chay trong RFIDTask)
uint8_t App_RFIDPoll(AppCardEvent *event)
{
    uint8_t newCardEvent = 0U;
    MFRC522_CardInfo cardInfo;
    uchar status;

    if (event == NULL)
    {
        return 0U;
    }

    if (barrierIsOpen != 0U)
    {
        return 0U;
    }

#if APP_USE_FREERTOS
    if ((rc522Mutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
    {
        if (xSemaphoreTake(rc522Mutex, portMAX_DELAY) != pdTRUE)
        {
            return 0U;
        }
    }
#endif

    // Kiem tra phuc hoi anten dinh ky
    rc522HealthCheckCounter++;
    if (rc522HealthCheckCounter >= RC522_HEALTH_CHECK_CYCLES)
    {
        rc522HealthCheckCounter = 0U;
        MFRC522_CheckAndRecover();
    }

    if (cardPresenceState == CARD_STATE_WAIT_NEW)
    {
        // Cho the moi (REQA)
        status = MFRC522_ReadCardCascade(PICC_REQIDL, &cardInfo);
        if (status == MI_OK)
        {
            (void)MFRC522_Halt();

            event->type = APP_EVENT_CARD;
            event->uidLen = cardInfo.uidLen;
            memcpy(event->uid, cardInfo.uid, sizeof(event->uid));
            event->sak = cardInfo.sak;

            heldUidLen = cardInfo.uidLen;
            memcpy(heldUid, cardInfo.uid, sizeof(heldUid));
            cardPresenceState = CARD_STATE_HOLD_PRESENT;
            cardAbsentCount = 0U;
            cardErrStreak = 0U;
            cardCollisionUnknownUid = 0U;
            cardLastSeenTick = HAL_GetTick();
            newCardEvent = 1U;
        }
        else if ((status == MI_COLLERR) || (status == MI_ERR))
        {
            Write_MFRC522(CommandReg, PCD_IDLE);
            Write_MFRC522(FIFOLevelReg, 0x80U);
        }
    }
    else
    {
        // The dang giu truoc dau doc (WUPA)
        status = MFRC522_ReadCardCascade(PICC_REQALL, &cardInfo);
        if (status == MI_OK)
        {
            (void)MFRC522_Halt();
            cardAbsentCount = 0U;
            cardErrStreak = 0U;
            cardCollisionUnknownUid = 0U;
            cardLastSeenTick = HAL_GetTick();
        }
        else if (status == MI_COLLERR)
        {
            Write_MFRC522(CommandReg, PCD_IDLE);
            Write_MFRC522(FIFOLevelReg, 0x80U);
            cardCollisionUnknownUid = 1U;
            cardAbsentCount = 0U;
            cardErrStreak = 0U;
            cardLastSeenTick = HAL_GetTick();
        }
        else if (status == MI_ERR)
        {
            Write_MFRC522(CommandReg, PCD_IDLE);
            Write_MFRC522(FIFOLevelReg, 0x80U);
            if (cardErrStreak < CARD_MAX_ERR_STREAK)
            {
                cardErrStreak++;
                cardAbsentCount = 0U;
                cardLastSeenTick = HAL_GetTick();
            }
            else
            {
                if (cardAbsentCount < 255U)
                {
                    cardAbsentCount++;
                }
                uint32_t silentMs = (uint32_t)(HAL_GetTick() - cardLastSeenTick);
                if ((cardAbsentCount >= CARD_ABSENT_CONFIRM_CYCLES) &&
                    (silentMs >= CARD_ABSENT_MIN_MS))
                {
                    cardPresenceState = CARD_STATE_WAIT_NEW;
                    cardAbsentCount = 0U;
                    cardErrStreak = 0U;
                    cardCollisionUnknownUid = 0U;
                    heldUidLen = 0U;
                    memset(heldUid, 0, sizeof(heldUid));
                }
            }
        }
        else
        {
            cardErrStreak = 0U;
            if (cardAbsentCount < 255U)
            {
                cardAbsentCount++;
            }
            uint32_t silentMs = (uint32_t)(HAL_GetTick() - cardLastSeenTick);
            if ((cardAbsentCount >= CARD_ABSENT_CONFIRM_CYCLES) &&
                (silentMs >= CARD_ABSENT_MIN_MS))
            {
                cardPresenceState = CARD_STATE_WAIT_NEW;
                cardAbsentCount = 0U;
                cardErrStreak = 0U;
                cardCollisionUnknownUid = 0U;
                heldUidLen = 0U;
                memset(heldUid, 0, sizeof(heldUid));
            }
        }
    }

#if APP_USE_FREERTOS
    if ((rc522Mutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
    {
        (void)xSemaphoreGive(rc522Mutex);
    }
#endif

    return newCardEvent;
}

// Xu ly su kien the tu Queue (chay trong CardDisplayTask)
void App_HandleCard(const AppCardEvent *event)
{
    if (event == NULL)
    {
        return;
    }

#if APP_USE_FREERTOS
    taskENTER_CRITICAL();
#endif
    if (queuedCardEvents > 0U)
    {
        queuedCardEvents--;
    }
#if APP_USE_FREERTOS
    taskEXIT_CRITICAL();
#endif

    UART_WriteUidLine("SCAN,", event->uid, event->uidLen, "\r\n");

    // Chi cap quyen cho the 4-byte UID
    if (event->uidLen != 4U)
    {
        uint8_t wasEnrolling = 0U;
#if APP_USE_FREERTOS
        taskENTER_CRITICAL();
#endif
        if (enrollmentPending)
        {
            enrollmentPending = 0U;
            wasEnrolling = 1U;
        }
#if APP_USE_FREERTOS
        taskEXIT_CRITICAL();
#endif
        if (wasEnrolling)
        {
            UART_Write("ERR,ONLY 4-BYTE UID SUPPORTED FOR ENROLL\r\n");
        }
        else
        {
            UART_Write("WARN,UNSUPPORTED UID LENGTH FOR 4-BYTE AUTH\r\n");
        }

        LED_RedOn();
        Buzzer_BeepPattern(2U, BUZZER_BEEP_DENY_ON_MS, BUZZER_BEEP_DENY_OFF_MS);
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "UID > 4 Bytes");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Khong cap quyen");
        App_HoldDisplayMs(600U);
        ShowIdleScreen();
        return;
    }

    uint8_t enrollmentResult = EnrollCard4Byte(event->uid);
    uint8_t cardIsAuthorized = IsAuthorizedCard(event->uid);
    uint8_t locked = systemLocked;
    uint8_t authorized = (cardIsAuthorized != 0U) && (locked == 0U);
    uint8_t triggerAlarm = UpdateInvalidCardState(cardIsAuthorized);

    if (enrollmentResult == 1U)
    {
        UART_WriteUidLine("OK,ENROLLED,", event->uid, 4U, "\r\n");
    }
    else if (enrollmentResult == 2U)
    {
        UART_WriteUidLine("INFO,CARD ALREADY EXISTS,", event->uid, 4U, "\r\n");
    }
    else if (enrollmentResult == 3U)
    {
        UART_Write("ERR,CARD LIST FULL\r\n");
    }
    else if (enrollmentResult == 4U)
    {
        UART_Write("ERR,FLASH_WRITE_FAILED\r\n");
    }
    RecordCardEvent(event->uid, authorized);

    if (authorized)
    {
        LED_GreenOn();
        Buzzer_Beep(BUZZER_BEEP_ALLOW_MS);
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "Welcome");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Mo rao chan");
        Barrier_OpenThenClose();
        App_HoldDisplayMs(200U);
    }
    else if (triggerAlarm)
    {
        LED_RedOn();
        UART_Write("ALERT,3 consecutive invalid cards\r\n");
        Buzzer_BeepPattern(3U, BUZZER_BEEP_ALARM_ON_MS, BUZZER_BEEP_ALARM_OFF_MS);
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "WARNING");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Sai lien tiep 3");
        App_HoldDisplayMs(750U);
    }
    else
    {
        LED_RedOn();
        Buzzer_BeepPattern(2U, BUZZER_BEEP_DENY_ON_MS, BUZZER_BEEP_DENY_OFF_MS);
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, locked ? "System Locked" : "Sai The");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, locked ? "Use UART unlock" : "Tu choi");
        App_HoldDisplayMs(600U);
    }

    ShowIdleScreen();
}
