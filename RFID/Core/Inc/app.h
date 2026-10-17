#ifndef APP_H
#define APP_H

#include <stdint.h>

typedef struct
{
    uint8_t type;
    uint8_t uid[4];
} AppCardEvent;

#define APP_EVENT_CARD 0U
#define APP_EVENT_EMERGENCY_OPEN 1U

void App_Init(void);
uint8_t App_RFIDPoll(AppCardEvent *event);
void App_HandleCard(const AppCardEvent *event);
void App_ProcessUart(void);
uint8_t App_PollEmergencyButton(void);
void App_EmergencyButtonIRQ(void);

#endif
