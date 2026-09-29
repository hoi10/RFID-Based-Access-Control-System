#include "lcd_i2c.h"

static I2C_HandleTypeDef *lcdI2C;
static uint8_t lcdAddr;

#define LCD_BACKLIGHT 0x08
#define ENABLE_BIT    0x04

static void I2C_WriteByte(uint8_t data)
{
  HAL_I2C_Master_Transmit(lcdI2C, lcdAddr, &data, 1, 100);
}

static void LCD_PulseEnable(uint8_t data)
{
  I2C_WriteByte(data | ENABLE_BIT);
  HAL_Delay(1);
  I2C_WriteByte(data & ~ENABLE_BIT);
  HAL_Delay(1);
}

static void LCD_SendNibble(uint8_t nibble, uint8_t mode)
{
  uint8_t data = (nibble & 0xF0) | mode | LCD_BACKLIGHT;
  I2C_WriteByte(data);
  LCD_PulseEnable(data);
}

static void LCD_SendByte(uint8_t byte, uint8_t mode)
{
  LCD_SendNibble(byte & 0xF0, mode);
  LCD_SendNibble((byte << 4) & 0xF0, mode);
}

static void LCD_Command(uint8_t cmd) { LCD_SendByte(cmd, 0); }
static void LCD_Data(uint8_t data)   { LCD_SendByte(data, 1); }

void LCD_Init(I2C_HandleTypeDef *hi2c, uint8_t address)
{
  lcdI2C = hi2c;
  lcdAddr = address;

  HAL_Delay(50);
  LCD_SendNibble(0x30, 0); HAL_Delay(5);
  LCD_SendNibble(0x30, 0); HAL_Delay(1);
  LCD_SendNibble(0x30, 0); HAL_Delay(1);
  LCD_SendNibble(0x20, 0); HAL_Delay(1);

  LCD_Command(0x28);
  LCD_Command(0x0C);
  LCD_Command(0x06);
  LCD_Command(0x01);
  HAL_Delay(2);
}

void LCD_Clear(void)
{
  LCD_Command(0x01);
  HAL_Delay(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
  uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
  LCD_Command(addr);
}

void LCD_SendString(char *str)
{
  while (*str) LCD_Data((uint8_t)(*str++));
}
