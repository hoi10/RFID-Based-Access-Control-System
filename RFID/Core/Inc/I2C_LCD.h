#ifndef I2C_LCD_H_
#define I2C_LCD_H_

#include <stdint.h>

#define I2C_LCD_MAX    1
#define I2C_LCD_1      0

void I2C_LCD_Init(uint8_t I2C_LCD_InstanceIndex);
void I2C_LCD_Clear(uint8_t I2C_LCD_InstanceIndex);
void I2C_LCD_SetCursor(uint8_t I2C_LCD_InstanceIndex, uint8_t Col, uint8_t Row);
void I2C_LCD_WriteString(uint8_t I2C_LCD_InstanceIndex, char* Str);
void I2C_LCD_Backlight(uint8_t I2C_LCD_InstanceIndex);
void I2C_LCD_Display(uint8_t I2C_LCD_InstanceIndex);

#endif /* I2C_LCD_H_ */
