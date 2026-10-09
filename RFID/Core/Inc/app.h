#ifndef APP_H
#define APP_H

#include <stdint.h>

typedef struct
{
    uint8_t uid[4];
} AppCardEvent;

void App_Init(void);
uint8_t App_RFIDPoll(AppCardEvent *event);
void App_HandleCard(const AppCardEvent *event);

#endif
