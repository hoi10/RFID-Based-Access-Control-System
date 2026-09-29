#ifndef MFRC522_H
#define MFRC522_H

#include "main.h"

void MFRC522_Init(SPI_HandleTypeDef *hspi);
uint8_t MFRC522_Check(uint8_t *id);
uint8_t MFRC522_Compare(uint8_t *cardID, uint8_t *compareID);

#endif
