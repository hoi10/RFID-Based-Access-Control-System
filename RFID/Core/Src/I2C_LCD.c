/*
 * File: I2C_LCD.c
 * Driver Name: [[ I2C_LCD Display ]]
 * SW Layer:   ECUAL
 * Created on: Jan 28, 2024
 * Author:     Khaled Magdy
 * -------------------------------------------
 * For More Information, Tutorials, etc.
 * Visit Website: www.DeepBlueMbedded.com
 *
 */

#include "I2C_LCD.h"
#include "I2C_LCD_cfg.h"
#include "DWT_Delay.h"

/*-----------------------[INTERNAL DEFINITIONS]-----------------------*/
// CMD
#define LCD_CLEARDISPLAY        0x01
#define LCD_RETURNHOME          0x02
#define LCD_ENTRYMODESET        0x04
#define LCD_DISPLAYCONTROL      0x08
#define LCD_CURSORSHIFT         0x10
#define LCD_FUNCTIONSET         0x20
#define LCD_SETCGRAMADDR        0x40
#define LCD_SETDDRAMADDR        0x80
// DISPLAY ENTRY
#define LCD_ENTRYRIGHT          0x00
#define LCD_ENTRYLEFT           0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00
// DISPLAY CONTROL
#define LCD_DISPLAYON           0x04
#define LCD_DISPLAYOFF          0x00
#define LCD_CURSORON            0x02
#define LCD_CURSOROFF           0x00
#define LCD_BLINKON             0x01
#define LCD_BLINKOFF            0x00
// CURSOR MOTION
#define LCD_DISPLAYMOVE         0x08
#define LCD_CURSORMOVE          0x00
#define LCD_MOVERIGHT           0x04
#define LCD_MOVELEFT            0x00
// FUNCTION SET
#define LCD_8BITMODE            0x10
#define LCD_4BITMODE            0x00
#define LCD_2LINE               0x08
#define LCD_1LINE               0x00
#define LCD_5x10DOTS            0x04
#define LCD_5x8DOTS             0x00
// BACKLIGHT CONTROL
#define LCD_BACKLIGHT           0x08
#define LCD_NOBACKLIGHT         0x00
#define EN                      0b00000100  // Enable bit
#define RW                      0b00000010  // Read/Write bit
#define RS                      0b00000001  // Register select bit

/*-----------------------[INTERNAL VARIABLES]-----------------------*/

typedef struct I2C_LCD_InfoParam_s
{
    uint8_t DisplayCtrl;
    uint8_t BacklightVal;
    uint8_t DeviceAddress;
} I2C_LCD_InfoParam_t;

static I2C_LCD_InfoParam_t I2C_LCD_InfoParam_g[I2C_LCD_MAX];

/*---------------------[STATIC INTERNAL FUNCTIONS]-----------------------*/

static void I2C_LCD_ExpanderWrite(uint8_t I2C_LCD_InstanceIndex, uint8_t DATA)
{
    uint8_t TxData = (DATA) | I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].BacklightVal;
    uint8_t addr = I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DeviceAddress;
    HAL_I2C_Master_Transmit(I2C_LCD_CfgParam[I2C_LCD_InstanceIndex].I2C_Handle, addr, &TxData, 1, 20);
}

static void I2C_LCD_EnPulse(uint8_t I2C_LCD_InstanceIndex, uint8_t DATA)
{
    I2C_LCD_ExpanderWrite(I2C_LCD_InstanceIndex, (DATA | EN)); // En high
    DWT_Delay_us(2); // Enable pulse width > 450ns

    I2C_LCD_ExpanderWrite(I2C_LCD_InstanceIndex, (DATA & ~EN)); // En low
    DWT_Delay_us(40); // Command execution time > 37us
}

// Gửi chính xác 1 nibble (4-bit) duy nhất - Dùng riêng cho chuỗi khởi tạo chuyển sang 4-bit mode
static void I2C_LCD_WriteNibble(uint8_t I2C_LCD_InstanceIndex, uint8_t Val, uint8_t Mode)
{
    uint8_t HighNib = (Val & 0xF0) | Mode;
    I2C_LCD_ExpanderWrite(I2C_LCD_InstanceIndex, HighNib);
    I2C_LCD_EnPulse(I2C_LCD_InstanceIndex, HighNib);
}

// Gửi 1 byte (2 nibbles: High trước, Low sau)
static void I2C_LCD_Send(uint8_t I2C_LCD_InstanceIndex, uint8_t Val, uint8_t Mode)
{
    uint8_t HighNib = (Val & 0xF0) | Mode;
    uint8_t LowNib = ((Val << 4) & 0xF0) | Mode;
    I2C_LCD_WriteNibble(I2C_LCD_InstanceIndex, HighNib, Mode);
    I2C_LCD_WriteNibble(I2C_LCD_InstanceIndex, LowNib, Mode);
}

static void I2C_LCD_Cmd(uint8_t I2C_LCD_InstanceIndex, uint8_t CMD)
{
    I2C_LCD_Send(I2C_LCD_InstanceIndex, CMD, 0);
}

static void I2C_LCD_Data(uint8_t I2C_LCD_InstanceIndex, uint8_t DATA)
{
    I2C_LCD_Send(I2C_LCD_InstanceIndex, DATA, RS);
}

/*-----------------------[USER EXTERNAL FUNCTIONS]-----------------------*/

void I2C_LCD_Init(uint8_t I2C_LCD_InstanceIndex)
{
    // Đợi điện áp LCD ổn định (>40ms theo datasheet HD44780)
    HAL_Delay(50);

    // Tự động quét địa chỉ 0x27 hoặc 0x3F để phù hợp cả chip PCF8574T và PCF8574AT
    I2C_HandleTypeDef *hi2c = I2C_LCD_CfgParam[I2C_LCD_InstanceIndex].I2C_Handle;
    uint8_t detectedAddr = (I2C_LCD_CfgParam[I2C_LCD_InstanceIndex].I2C_LCD_Address << 1);

    if (HAL_I2C_IsDeviceReady(hi2c, (0x27 << 1), 2, 5) == HAL_OK)
    {
        detectedAddr = (0x27 << 1);
    }
    else if (HAL_I2C_IsDeviceReady(hi2c, (0x3F << 1), 2, 5) == HAL_OK)
    {
        detectedAddr = (0x3F << 1);
    }
    I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DeviceAddress = detectedAddr;
    I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].BacklightVal = LCD_BACKLIGHT;

    // Chuẩn Hitachi HD44780: Chuyển từ chế độ 8-bit ban đầu sang 4-bit mode
    // Bước 1: Gửi 3 lần bán byte 0x30 (chỉ 1 xung EN mỗi lần)
    I2C_LCD_WriteNibble(I2C_LCD_InstanceIndex, 0x30, 0);
    HAL_Delay(5); // Chờ > 4.1ms
    I2C_LCD_WriteNibble(I2C_LCD_InstanceIndex, 0x30, 0);
    HAL_Delay(1); // Chờ > 100us
    I2C_LCD_WriteNibble(I2C_LCD_InstanceIndex, 0x30, 0);
    HAL_Delay(1);

    // Bước 2: Gửi bán byte 0x20 để chốt vào 4-bit mode (BẮT BUỘC chỉ 1 nibble!)
    I2C_LCD_WriteNibble(I2C_LCD_InstanceIndex, 0x20, 0);
    HAL_Delay(1);

    // Bước 3: Từ đây LCD đã ở 4-bit mode, cấu hình các tham số hiển thị
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS); // 0x28
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF); // 0x0C
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_ENTRYMODESET | LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT); // 0x06
    I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;

    // Xóa trắng màn hình và đưa con trỏ về gốc
    I2C_LCD_Clear(I2C_LCD_InstanceIndex);
}

void I2C_LCD_Clear(uint8_t I2C_LCD_InstanceIndex)
{
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_CLEARDISPLAY);
    HAL_Delay(2);
}

void I2C_LCD_Home(uint8_t I2C_LCD_InstanceIndex)
{
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_RETURNHOME);
    HAL_Delay(2);
}

void I2C_LCD_SetCursor(uint8_t I2C_LCD_InstanceIndex, uint8_t Col, uint8_t Row)
{
    int Row_Offsets[] = {0x00, 0x40, 0x14, 0x54};
    if (Row > I2C_LCD_CfgParam[I2C_LCD_InstanceIndex].I2C_LCD_nRow)
    {
    	Row = I2C_LCD_CfgParam[I2C_LCD_InstanceIndex].I2C_LCD_nRow - 1;
    }
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_SETDDRAMADDR | (Col + Row_Offsets[Row]));
}

void I2C_LCD_WriteChar(uint8_t I2C_LCD_InstanceIndex, char Ch)
{
    I2C_LCD_Data(I2C_LCD_InstanceIndex, Ch);
}

void I2C_LCD_WriteString(uint8_t I2C_LCD_InstanceIndex, char *Str)
{
    while (*Str)
    {
        I2C_LCD_Data(I2C_LCD_InstanceIndex, *Str++);
    }
}

void I2C_LCD_ShiftLeft(uint8_t I2C_LCD_InstanceIndex)
{
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}

void I2C_LCD_ShiftRight(uint8_t I2C_LCD_InstanceIndex)
{
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

void I2C_LCD_Backlight(uint8_t I2C_LCD_InstanceIndex)
{
	I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].BacklightVal = LCD_BACKLIGHT;
    I2C_LCD_ExpanderWrite(I2C_LCD_InstanceIndex, 0);
}

void I2C_LCD_NoBacklight(uint8_t I2C_LCD_InstanceIndex)
{
	I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].BacklightVal = LCD_NOBACKLIGHT;
    I2C_LCD_ExpanderWrite(I2C_LCD_InstanceIndex, 0);
}

void I2C_LCD_Display(uint8_t I2C_LCD_InstanceIndex)
{
	I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl |= LCD_DISPLAYON;
	I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_DISPLAYCONTROL | I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl);
}

void I2C_LCD_NoDisplay(uint8_t I2C_LCD_InstanceIndex)
{
	I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl &= ~LCD_DISPLAYON;
	I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_DISPLAYCONTROL | I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl);
}

void I2C_LCD_Cursor(uint8_t I2C_LCD_InstanceIndex)
{
	I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl |= LCD_CURSORON;
	I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_DISPLAYCONTROL | I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl);
}

void I2C_LCD_NoCursor(uint8_t I2C_LCD_InstanceIndex)
{
	I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl &= ~LCD_CURSORON;
	I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_DISPLAYCONTROL | I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl);
}

void I2C_LCD_Blink(uint8_t I2C_LCD_InstanceIndex)
{
	I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl |= LCD_BLINKON;
	I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_DISPLAYCONTROL | I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl);
}

void I2C_LCD_NoBlink(uint8_t I2C_LCD_InstanceIndex)
{
	I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl &= ~LCD_BLINKON;
	I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_DISPLAYCONTROL | I2C_LCD_InfoParam_g[I2C_LCD_InstanceIndex].DisplayCtrl);
}

void I2C_LCD_CreateCustomChar(uint8_t I2C_LCD_InstanceIndex, uint8_t CharIndex, const uint8_t* CharMap)
{
    CharIndex &= 0x07;  // Ensure index is in the range 0-7
    I2C_LCD_Cmd(I2C_LCD_InstanceIndex, LCD_SETCGRAMADDR | (CharIndex << 3));
    for (int i = 0; i < 8; i++)
    {
    	I2C_LCD_Send(I2C_LCD_InstanceIndex, CharMap[i], RS);
    }
}

void I2C_LCD_PrintCustomChar(uint8_t I2C_LCD_InstanceIndex, uint8_t CharIndex)
{
	I2C_LCD_Send(I2C_LCD_InstanceIndex, CharIndex, RS);
}
