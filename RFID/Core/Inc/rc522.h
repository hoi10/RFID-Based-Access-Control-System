#ifndef RC522_H
#define RC522_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define uchar unsigned char
#define uint  unsigned int

#define MAX_LEN 16

/* Sơ đồ chân phần cứng cố định cho MFRC522 */
#define MFRC522_CS_PORT      GPIOB
#define MFRC522_CS_PIN       GPIO_PIN_12
#define MFRC522_SCK_PORT     GPIOB
#define MFRC522_SCK_PIN      GPIO_PIN_13
#define MFRC522_MISO_PORT    GPIOB
#define MFRC522_MISO_PIN     GPIO_PIN_14
#define MFRC522_MOSI_PORT    GPIOB
#define MFRC522_MOSI_PIN     GPIO_PIN_15
#define MFRC522_RST_PORT     GPIOB
#define MFRC522_RST_PIN      GPIO_PIN_0

/* MFRC522 Commands (Chapter 10 of Datasheet) */
#define PCD_IDLE              0x00
#define PCD_AUTHENT           0x0E
#define PCD_RECEIVE           0x08
#define PCD_TRANSMIT          0x04
#define PCD_TRANSCEIVE        0x0C
#define PCD_RESETPHASE        0x0F
#define PCD_CALCCRC           0x03

/* PICC Commands (ISO/IEC 14443-3 Type A) */
#define PICC_REQIDL           0x26
#define PICC_REQALL           0x52
#define PICC_ANTICOLL         0x93
#define PICC_SElECTTAG        0x93
#define PICC_ANTICOLL_CL1     0x93
#define PICC_ANTICOLL_CL2     0x95
#define PICC_ANTICOLL_CL3     0x97
#define PICC_CASCADE_TAG      0x88
#define PICC_AUTHENT1A        0x60
#define PICC_AUTHENT1B        0x61
#define PICC_READ             0x30
#define PICC_WRITE            0xA0
#define PICC_DECREMENT        0xC0
#define PICC_INCREMENT        0xC1
#define PICC_RESTORE          0xC2
#define PICC_TRANSFER         0xB0
#define PICC_HALT             0x50

/* Return Status Codes */
#define MI_OK                 0U
#define MI_NOTAGERR           1U
#define MI_ERR                2U
#define MI_COLLERR            3U

/* ErrorReg (0x06) Bit Masks (Section 9.3.1.7, Table 33) */
#define RC522_ERR_PROTOCOL    0x01U
#define RC522_ERR_PARITY      0x02U
#define RC522_ERR_CRC         0x04U
#define RC522_ERR_COLL        0x08U
#define RC522_ERR_BUFFER_OVFL 0x10U
#define RC522_ERR_TEMP        0x40U
#define RC522_ERR_WR          0x80U

/* Mặt nạ kiểm tra lỗi theo từng loại giao dịch */
#define RC522_ERR_MASK_NO_CRC    0x1BU /* BufferOvfl | CollErr | ParityErr | ProtocolErr */
#define RC522_ERR_MASK_WITH_CRC  0x1FU /* BufferOvfl | CollErr | CRCErr | ParityErr | ProtocolErr */
#define RC522_ERR_MASK_HALT      0xDFU /* WrErr | TempErr | BufferOvfl | CollErr | CRCErr | ParityErr | ProtocolErr */

/* MFRC522 Registers (Chapter 9 of Datasheet) */
/* Page 0: Command and Status */
#define Reserved00            0x00
#define CommandReg            0x01
#define CommIEnReg            0x02
#define DivlEnReg             0x03
#define CommIrqReg            0x04
#define DivIrqReg             0x05
#define ErrorReg              0x06
#define Status1Reg            0x07
#define Status2Reg            0x08
#define FIFODataReg           0x09
#define FIFOLevelReg          0x0A
#define WaterLevelReg         0x0B
#define ControlReg            0x0C
#define BitFramingReg         0x0D
#define CollReg               0x0E
#define Reserved01            0x0F
/* Page 1: Command */
#define Reserved10            0x10
#define ModeReg               0x11
#define TxModeReg             0x12
#define RxModeReg             0x13
#define TxControlReg          0x14
#define TxAutoReg             0x15
#define TxSelReg              0x16
#define RxSelReg              0x17
#define RxThresholdReg        0x18
#define DemodReg              0x19
#define Reserved11            0x1A
#define Reserved12            0x1B
#define MifareReg             0x1C
#define Reserved13            0x1D
#define Reserved14            0x1E
#define SerialSpeedReg        0x1F
/* Page 2: Configuration */
#define Reserved20            0x20
#define CRCResultRegH         0x21
#define CRCResultRegL         0x22
#define Reserved21            0x23
#define ModWidthReg           0x24
#define Reserved22            0x25
#define RFCfgReg              0x26
#define GsNReg                0x27
#define CWGsPReg              0x28
#define ModGsPReg             0x29
#define TModeReg              0x2A
#define TPrescalerReg         0x2B
#define TReloadRegH           0x2C
#define TReloadRegL           0x2D
#define TCounterValueRegH     0x2E
#define TCounterValueRegL     0x2F
/* Page 3: Test Registers */
#define Reserved30            0x30
#define TestSel1Reg           0x31
#define TestSel2Reg           0x32
#define TestPinEnReg          0x33
#define TestPinValueReg       0x34
#define TestBusReg            0x35
#define AutoTestReg           0x36
#define VersionReg            0x37
#define AnalogTestReg         0x38
#define TestDAC1Reg           0x39
#define TestDAC2Reg           0x3A
#define TestADCReg            0x3B
#define Reserved31            0x3C
#define Reserved32            0x3D
#define Reserved33            0x3E
#define Reserved34            0x3F

typedef struct
{
    uint8_t uidLen;      /* 4, 7, hoac 10 bytes */
    uchar   uid[10];     /* Byte UID thuc te */
    uchar   atqa[2];     /* 2 byte ATQA */
    uint8_t sak;         /* Byte SAK cuoi cung */
} MFRC522_CardInfo;

void  Write_MFRC522(uchar addr, uchar val);
uchar Read_MFRC522(uchar addr);
void  SetBitMask(uchar reg, uchar mask);
void  ClearBitMask(uchar reg, uchar mask);
void  AntennaOn(void);
void  AntennaOff(void);
void  MFRC522_Reset(void);
void  MFRC522_Init(void);
uchar MFRC522_ReadVersion(void);
void  MFRC522_CheckAndRecover(void);

uchar MFRC522_CalculateCRC(const uchar *pIndata, uchar len, uchar *pOutData);

uchar MFRC522_Request(uchar reqMode, uchar *TagType);
uchar MFRC522_AnticollCL(uchar cascadeCode, uchar *clFrame5);
uchar MFRC522_SelectCL(uchar cascadeCode, const uchar *clFrame5, uchar *sakOut);
uchar MFRC522_ReadCardCascade(uchar reqMode, MFRC522_CardInfo *cardInfo);
uchar MFRC522_Halt(void);

#endif /* RC522_H */
