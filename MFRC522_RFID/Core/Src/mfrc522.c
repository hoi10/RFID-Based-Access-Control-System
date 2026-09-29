#include "mfrc522.h"

#define CommandReg      0x01
#define ComIEnReg       0x02
#define ComIrqReg       0x04
#define ErrorReg        0x06
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define ModeReg         0x11
#define TxControlReg    0x14
#define TxASKReg        0x15
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D

#define PCD_IDLE        0x00
#define PCD_TRANSCEIVE  0x0C
#define PCD_RESETPHASE  0x0F

#define PICC_REQIDL     0x26
#define PICC_ANTICOLL   0x93

#define MI_OK       0
#define MI_ERR      2

static SPI_HandleTypeDef *rfidSPI;

static void CS_Select(void)   { HAL_GPIO_WritePin(RFID_CS_GPIO_Port, RFID_CS_Pin, GPIO_PIN_RESET); }
static void CS_Unselect(void) { HAL_GPIO_WritePin(RFID_CS_GPIO_Port, RFID_CS_Pin, GPIO_PIN_SET); }

static void WriteRawRegister(uint8_t reg, uint8_t val)
{
  uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), val };
  uint8_t rx[2];

  CS_Select();
  HAL_SPI_TransmitReceive(rfidSPI, tx, rx, 2, 100);
  CS_Unselect();
}

static uint8_t ReadRawRegister(uint8_t reg)
{
  uint8_t tx[2] = { (uint8_t)(((reg << 1) & 0x7E) | 0x80), 0x00 };
  uint8_t rx[2] = { 0, 0 };

  CS_Select();
  HAL_SPI_TransmitReceive(rfidSPI, tx, rx, 2, 100);
  CS_Unselect();

  return rx[1];
}

static void SetBitMask(uint8_t reg, uint8_t mask)   { WriteRawRegister(reg, ReadRawRegister(reg) | mask); }
static void ClearBitMask(uint8_t reg, uint8_t mask) { WriteRawRegister(reg, ReadRawRegister(reg) & (~mask)); }

static void MFRC522_Reset(void)
{
  HAL_GPIO_WritePin(RFID_RST_GPIO_Port, RFID_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(RFID_RST_GPIO_Port, RFID_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(50);
  WriteRawRegister(CommandReg, PCD_RESETPHASE);
  HAL_Delay(50);
}

static void AntennaOn(void)
{
  uint8_t temp = ReadRawRegister(TxControlReg);
  if (!(temp & 0x03)) SetBitMask(TxControlReg, 0x03);
}

static uint8_t ToCard(uint8_t cmd, uint8_t *sendData, uint8_t sendLen,
                       uint8_t *backData, uint16_t *backLen)
{
  uint8_t status = MI_ERR;
  uint8_t irqEn = 0x00, waitIRq = 0x00, n;
  uint16_t i;

  if (cmd == PCD_TRANSCEIVE) { irqEn = 0x77; waitIRq = 0x30; }

  WriteRawRegister(ComIEnReg, irqEn | 0x80);
  ClearBitMask(ComIrqReg, 0x80);
  SetBitMask(FIFOLevelReg, 0x80);
  WriteRawRegister(CommandReg, PCD_IDLE);

  for (i = 0; i < sendLen; i++) WriteRawRegister(FIFODataReg, sendData[i]);
  WriteRawRegister(CommandReg, cmd);
  if (cmd == PCD_TRANSCEIVE) SetBitMask(BitFramingReg, 0x80);

  i = 2000;
  do {
    n = ReadRawRegister(ComIrqReg);
    i--;
  } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

  ClearBitMask(BitFramingReg, 0x80);

  if (i != 0 && !(ReadRawRegister(ErrorReg) & 0x1B))
  {
    status = MI_OK;
    if (n & irqEn & 0x01) status = MI_ERR;

    if (cmd == PCD_TRANSCEIVE)
    {
      n = ReadRawRegister(FIFOLevelReg);
      uint8_t lastBits = ReadRawRegister(ControlReg) & 0x07;
      *backLen = lastBits ? (n - 1) * 8 + lastBits : n * 8;
      if (n == 0) n = 1;
      if (n > 16) n = 16;
      for (i = 0; i < n; i++) backData[i] = ReadRawRegister(FIFODataReg);
    }
  }
  return status;
}

static uint8_t Request(uint8_t reqMode, uint8_t *TagType)
{
  uint16_t backBits;
  WriteRawRegister(BitFramingReg, 0x07);
  TagType[0] = reqMode;
  uint8_t status = ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);
  if ((status != MI_OK) || (backBits != 0x10)) status = MI_ERR;
  return status;
}

static uint8_t Anticoll(uint8_t *serNum)
{
  uint16_t backBits;
  uint8_t serNumCheck = 0, i;

  WriteRawRegister(BitFramingReg, 0x00);
  serNum[0] = PICC_ANTICOLL;
  serNum[1] = 0x20;

  uint8_t status = ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &backBits);

  if (status == MI_OK)
  {
    for (i = 0; i < 4; i++) serNumCheck ^= serNum[i];
    if (serNumCheck != serNum[4]) status = MI_ERR;
  }
  return status;
}

void MFRC522_Init(SPI_HandleTypeDef *hspi)
{
  rfidSPI = hspi;
  MFRC522_Reset();
  WriteRawRegister(TModeReg, 0x8D);
  WriteRawRegister(TPrescalerReg, 0x3E);
  WriteRawRegister(TReloadRegL, 30);
  WriteRawRegister(TReloadRegH, 0);
  WriteRawRegister(TxASKReg, 0x40);
  WriteRawRegister(ModeReg, 0x3D);
  AntennaOn();
}

uint8_t MFRC522_Check(uint8_t *id)
{
  uint8_t status;
  uint8_t tagType[2];

  status = Request(PICC_REQIDL, tagType);
  if (status == MI_OK) status = Anticoll(id);
  ClearBitMask(BitFramingReg, 0x80);
  return (status == MI_OK) ? 1 : 0;
}

uint8_t MFRC522_Compare(uint8_t *cardID, uint8_t *compareID)
{
  for (uint8_t i = 0; i < 5; i++)
    if (cardID[i] != compareID[i]) return 0;
  return 1;
}
