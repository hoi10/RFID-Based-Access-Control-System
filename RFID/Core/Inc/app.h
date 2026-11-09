#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stddef.h>

#define MAX_AUTHORIZED_CARDS         3U
#define APP_FLASH_MAGIC              0x52464944U /* "RFID" */
#define APP_FLASH_VERSION            1U

#define APP_FLASH_PAGE_62_ADDR       0x0800F800U
#define APP_FLASH_PAGE_63_ADDR       0x0800FC00U

/* Bản ghi lưu trữ trên Flash: 4 + 2 + 2 + 4 + 12 + 4 = 28 bytes */
typedef struct __attribute__((packed, aligned(4)))
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t generation;
    uint8_t  cards[MAX_AUTHORIZED_CARDS][4];
    uint32_t crc32;
} AppFlashRecord;

typedef struct
{
    uint8_t type;
    uint8_t uidLen;      /* 4, 7, hoặc 10 bytes */
    uint8_t uid[10];     /* Toàn bộ các byte UID thực tế */
    uint8_t sak;         /* Byte SAK cuối cùng */
} AppCardEvent;

#define APP_EVENT_CARD 0U

void    App_Init(void);
void    App_Flash_Init(void);
uint8_t App_RFIDPoll(AppCardEvent *event);
void    App_NotifyCardQueueResult(uint8_t sentOk);
void    App_HandleCard(const AppCardEvent *event);
void    App_ProcessUart(void);
void    App_SetTaskHandles(void *rfidTaskHandle, void *appTaskHandle);
void    App_PrintRtosStats(void);
void    App_EmergencyButtonIRQ(void);

#endif
