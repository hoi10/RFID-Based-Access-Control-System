#include "rc522.h"
#include <stdio.h>
#include <string.h>

static uchar RC522_SPI_Transfer(uchar data)
{
    uchar received = 0U;

    for (uchar mask = 0x80U; mask != 0U; mask >>= 1)
    {
        /* 1. Giữ SCK = LOW, xuất bit dữ liệu ra MOSI (PB15) */
        HAL_GPIO_WritePin(MFRC522_SCK_PORT, MFRC522_SCK_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MFRC522_MOSI_PORT, MFRC522_MOSI_PIN,
                          (data & mask) ? GPIO_PIN_SET : GPIO_PIN_RESET);

        /* Thời gian thiết lập (Setup time) trước cạnh lên SCK */
        for (volatile uint8_t settle = 0U; settle < 8U; settle++)
        {
            __NOP();
        }

        /* 2. Cạnh lên SCK = HIGH (RC522 chốt MOSI, MISO đã ổn định) */
        HAL_GPIO_WritePin(MFRC522_SCK_PORT, MFRC522_SCK_PIN, GPIO_PIN_SET);

        for (volatile uint8_t settle = 0U; settle < 4U; settle++)
        {
            __NOP();
        }

        /* 3. Đọc MISO (PB14) khi SCK đang ở mức HIGH */
        received = (uchar)((received << 1) |
                           ((HAL_GPIO_ReadPin(MFRC522_MISO_PORT, MFRC522_MISO_PIN) == GPIO_PIN_SET) ? 1U : 0U));

        for (volatile uint8_t settle = 0U; settle < 4U; settle++)
        {
            __NOP();
        }

        /* 4. Cạnh xuống SCK = LOW (RC522 dịch bit MISO tiếp theo) */
        HAL_GPIO_WritePin(MFRC522_SCK_PORT, MFRC522_SCK_PIN, GPIO_PIN_RESET);
    }

    return received;
}

void Write_MFRC522(uchar addr, uchar val)
{
    HAL_GPIO_WritePin(MFRC522_CS_PORT, MFRC522_CS_PIN, GPIO_PIN_RESET);
    (void)RC522_SPI_Transfer((uchar)((addr << 1) & 0x7EU));
    (void)RC522_SPI_Transfer(val);
    HAL_GPIO_WritePin(MFRC522_CS_PORT, MFRC522_CS_PIN, GPIO_PIN_SET);
}

uchar Read_MFRC522(uchar addr)
{
    uchar val;

    HAL_GPIO_WritePin(MFRC522_CS_PORT, MFRC522_CS_PIN, GPIO_PIN_RESET);
    (void)RC522_SPI_Transfer((uchar)(((addr << 1) & 0x7EU) | 0x80U));
    val = RC522_SPI_Transfer(0x00U);
    HAL_GPIO_WritePin(MFRC522_CS_PORT, MFRC522_CS_PIN, GPIO_PIN_SET);

    return val;
}

static void Read_MFRC522_FIFO(uchar *buffer, uchar length)
{
    const uchar readAddr = (uchar)(((FIFODataReg << 1) & 0x7EU) | 0x80U);

    if ((buffer == NULL) || (length == 0U))
    {
        return;
    }
    HAL_GPIO_WritePin(MFRC522_CS_PORT, MFRC522_CS_PIN, GPIO_PIN_RESET);
    (void)RC522_SPI_Transfer(readAddr);
    for (uchar i = 0U; i < length; i++)
    {
        uchar nextAddr = (i < (uchar)(length - 1U)) ? readAddr : 0x00U;
        buffer[i] = RC522_SPI_Transfer(nextAddr);
    }
    HAL_GPIO_WritePin(MFRC522_CS_PORT, MFRC522_CS_PIN, GPIO_PIN_SET);
}

void SetBitMask(uchar reg, uchar mask)
{
    uchar tmp = Read_MFRC522(reg);
    Write_MFRC522(reg, (uchar)(tmp | mask));
}

void ClearBitMask(uchar reg, uchar mask)
{
    uchar tmp = Read_MFRC522(reg);
    Write_MFRC522(reg, (uchar)(tmp & (uchar)(~mask)));
}

void AntennaOn(void)
{
    uchar temp = Read_MFRC522(TxControlReg);
    if ((temp & 0x03U) != 0x03U)
    {
        SetBitMask(TxControlReg, 0x03U);
    }
}

void AntennaOff(void)
{
    ClearBitMask(TxControlReg, 0x03U);
}

void MFRC522_Reset(void)
{
    Write_MFRC522(CommandReg, PCD_RESETPHASE);
}

uchar MFRC522_ReadVersion(void)
{
    return Read_MFRC522(VersionReg);
}

// Timer reload cho REQA/WUPA, Anticoll, Select va HLTA (8 ticks = 4.0ms)
#define RC522_TIMER_RELOAD_FAST   0x0008U

static void MFRC522_SetTimerReload(uint16_t reloadTicks)
{
    Write_MFRC522(TReloadRegH, (uchar)((reloadTicks >> 8) & 0xFFU));
    Write_MFRC522(TReloadRegL, (uchar)(reloadTicks & 0xFFU));
}

static uint8_t MFRC522_WaitSoftResetDone(uint32_t timeoutMs)
{
    uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < timeoutMs)
    {
        if ((Read_MFRC522(CommandReg) & 0x10U) == 0U)
        {
            return 1U;
        }
    }
    return ((Read_MFRC522(CommandReg) & 0x10U) == 0U) ? 1U : 0U;
}

void MFRC522_Init(void)
{
    GPIO_InitTypeDef gpioInit = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_DISABLE();

    /* Cấu hình PB0 (RST), PB12 (CS), PB13 (SCK), PB15 (MOSI) */
    gpioInit.Pin = MFRC522_RST_PIN | MFRC522_CS_PIN | MFRC522_SCK_PIN | MFRC522_MOSI_PIN;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpioInit);

    /* Cấu hình PB14 (MISO) Input Pull-up */
    gpioInit.Pin = MFRC522_MISO_PIN;
    gpioInit.Mode = GPIO_MODE_INPUT;
    gpioInit.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpioInit);

    HAL_GPIO_WritePin(GPIOB, MFRC522_SCK_PIN | MFRC522_MOSI_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MFRC522_CS_PORT, MFRC522_CS_PIN, GPIO_PIN_SET);

    /* Hard Reset qua chân PB0 */
    HAL_GPIO_WritePin(MFRC522_RST_PORT, MFRC522_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(MFRC522_RST_PORT, MFRC522_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(50);

    /* Soft Reset và chờ bit PowerDown (bit 4 của CommandReg) về 0 */
    MFRC522_Reset();
    (void)MFRC522_WaitSoftResetDone(50U);

    /* Cấu hình tường minh tốc độ 106 kBd, tắt CRC cứng ban đầu, bật RxNoErr (0x08) và đặt độ rộng xung Miller */
    Write_MFRC522(TxModeReg, 0x00U);
    Write_MFRC522(RxModeReg, 0x08U);
    Write_MFRC522(ModWidthReg, 0x26U);

    /* Cấu hình Timer nội: TPrescaler = 0x0D3E (3390) -> 0.5ms/tick; TReload mặc định = 8 ticks (4ms) cho quét thẻ */
    Write_MFRC522(TModeReg, 0x8DU);
    Write_MFRC522(TPrescalerReg, 0x3EU);
    MFRC522_SetTimerReload(RC522_TIMER_RELOAD_FAST);

    /* Điều chế 100% ASK (bắt buộc cho ISO14443A) và giá trị khởi tạo CRC 0x6363 */
    Write_MFRC522(TxAutoReg, 0x40U);
    Write_MFRC522(ModeReg, 0x3DU);

    /* Độ lợi bộ thu 48dB (bits [6:4] = 111b, các bit dự trữ [3:0] = 0) */
    Write_MFRC522(RFCfgReg, 0x70U);

    /* Bật Anten và chờ trường RF ổn định */
    AntennaOn();
    HAL_Delay(5);
}

void MFRC522_CheckAndRecover(void)
{
    uchar txCtrl = Read_MFRC522(TxControlReg);
    uchar ver = Read_MFRC522(VersionReg);
    if (((txCtrl & 0x03U) != 0x03U) || (ver == 0x00U) || (ver == 0xFFU))
    {
        MFRC522_Init();
    }
}

static uchar MFRC522_TransceiveEx(uchar command,
                                  const uchar *sendData,
                                  uchar sendLen,
                                  uchar *backData,
                                  uint *backLen,
                                  uchar errMask)
{
    uchar status = MI_ERR;
    uchar irqEn = 0x00U;
    uchar waitIRq = 0x00U;
    uchar n = 0U;
    uint i;

    if (backLen != NULL)
    {
        *backLen = 0U;
    }

    switch (command)
    {
        case PCD_AUTHENT:
            irqEn = 0x12U;
            waitIRq = 0x10U;
            break;
        case PCD_TRANSCEIVE:
            irqEn = 0x77U;
            waitIRq = 0x30U; /* RxIRq | IdleIRq */
            break;
        default:
            break;
    }

    /* 1. Dung lenh cu, xoa co ngat va lam sach FIFO */
    Write_MFRC522(CommandReg, PCD_IDLE);
    Write_MFRC522(CommIEnReg, (uchar)(irqEn | 0x80U));
    Write_MFRC522(CommIrqReg, 0x7FU);
    Write_MFRC522(FIFOLevelReg, 0x80U);

    /* 2. Nap du lieu vao FIFO */
    for (i = 0U; i < sendLen; i++)
    {
        Write_MFRC522(FIFODataReg, sendData[i]);
    }

    /* 3. Phat lenh */
    Write_MFRC522(CommandReg, command);
    if (command == PCD_TRANSCEIVE)
    {
        SetBitMask(BitFramingReg, 0x80U);
    }

    /* 4. Cho hoan tat hoac timeout */
    i = 2500U;
    do
    {
        n = Read_MFRC522(CommIrqReg);
        i--;
    } while ((i != 0U) && ((n & 0x01U) == 0U) && ((n & waitIRq) == 0U));

    ClearBitMask(BitFramingReg, 0x80U);

    uchar capturedIrq = (uchar)(n | Read_MFRC522(CommIrqReg));
    uchar capturedErr = Read_MFRC522(ErrorReg);
    uchar capturedFifoLevel = (uchar)(Read_MFRC522(FIFOLevelReg) & 0x7FU);
    uchar capturedLastBits = (uchar)(Read_MFRC522(ControlReg) & 0x07U);
    uint16_t rxBits = 0U;

    if (capturedFifoLevel > 0U)
    {
        if (capturedLastBits != 0U)
        {
            rxBits = (uint16_t)(((uint16_t)(capturedFifoLevel - 1U) * 8U) + capturedLastBits);
        }
        else
        {
            rxBits = (uint16_t)((uint16_t)capturedFifoLevel * 8U);
        }
    }

    /* Doc du lieu tu FIFO */
    if ((command == PCD_TRANSCEIVE) && (backData != NULL) && (capturedFifoLevel > 0U))
    {
        uchar readBytes = capturedFifoLevel;
        if (readBytes > MAX_LEN)
        {
            readBytes = MAX_LEN;
        }
        Read_MFRC522_FIFO(backData, readBytes);
    }

    if (backLen != NULL)
    {
        *backLen = (uint)rxBits;
    }

    /* Don dep sau giao dich */
    Write_MFRC522(CommandReg, PCD_IDLE);
    SetBitMask(ControlReg, 0x80U);
    Write_MFRC522(FIFOLevelReg, 0x80U);
    Write_MFRC522(CommIrqReg, 0x7FU);

    /* Phan loai trang thai */
    if (i == 0U)
    {
        status = MI_ERR;
    }
    else if ((capturedErr & RC522_ERR_COLL) != 0U)
    {
        status = MI_COLLERR;
    }
    else if ((capturedErr & errMask) != 0U)
    {
        status = MI_ERR;
    }
    else if ((command == PCD_TRANSCEIVE) && (capturedFifoLevel == 0U) && (rxBits == 0U))
    {
        status = MI_NOTAGERR;
    }
    else if ((capturedIrq & 0x01U) != 0U)
    {
        status = MI_ERR;
    }
    else
    {
        status = MI_OK;
    }

    return status;
}

uchar MFRC522_CalculateCRC(const uchar *pIndata, uchar len, uchar *pOutData)
{
    uchar i;
    uchar n;

    if ((pIndata == NULL) || (pOutData == NULL) || (len == 0U))
    {
        return MI_ERR;
    }

    /* 1. Dừng lệnh hiện tại */
    Write_MFRC522(CommandReg, PCD_IDLE);

    /* 2. Xóa cờ CRCIRq cũ trong DivIrqReg (Set2 = 0, bit 2 CRCIRq = 1 -> ghi 0x04) */
    Write_MFRC522(DivIrqReg, 0x04U);

    /* 3. Xóa sạch FIFO trước khi nạp dữ liệu tính CRC */
    Write_MFRC522(FIFOLevelReg, 0x80U);

    for (i = 0U; i < len; i++)
    {
        Write_MFRC522(FIFODataReg, pIndata[i]);
    }

    /* 4. Kích hoạt lệnh tính CRC */
    Write_MFRC522(CommandReg, PCD_CALCCRC);

    /* 5. Chờ cờ CRCIRq mới bật lên trong thời gian cho phép */
    i = 0xFFU;
    do
    {
        n = Read_MFRC522(DivIrqReg);
        i--;
    } while ((i != 0U) && ((n & 0x04U) == 0U));

    /* Dừng lệnh PCD_CALCCRC */
    Write_MFRC522(CommandReg, PCD_IDLE);

    if ((n & 0x04U) == 0U)
    {
        return MI_ERR;
    }

    /* 6. Chỉ đọc CRCResultRegL/H khi CRCIRq của lần tính hiện tại đã bật */
    pOutData[0] = Read_MFRC522(CRCResultRegL);
    pOutData[1] = Read_MFRC522(CRCResultRegH);
    return MI_OK;
}

uchar MFRC522_Request(uchar reqMode, uchar *TagType)
{
    uchar status;
    uint backBits = 0U;
    uchar cmdBuf[2] = {reqMode, 0U};

    MFRC522_SetTimerReload(RC522_TIMER_RELOAD_FAST);

    /* Tắt CRC phần cứng cả phát và thu, đặt khung ngắn 7-bit (TxLastBits = 7) */
    ClearBitMask(TxModeReg, 0x80U);
    ClearBitMask(RxModeReg, 0x80U);
    Write_MFRC522(BitFramingReg, 0x07U);

    status = MFRC522_TransceiveEx(PCD_TRANSCEIVE, cmdBuf, 1U, TagType, &backBits,
                                  RC522_ERR_MASK_NO_CRC);

    if (status == MI_OK)
    {
        if (backBits != 0x10U)
        {
            status = MI_ERR;
        }
    }

    return status;
}

uchar MFRC522_AnticollCL(uchar cascadeCode, uchar *clFrame5)
{
    uchar status;
    uchar i;
    uchar bccCheck = 0U;
    uint unLen = 0U;
    uchar cmdBuf[2];
    uchar rxBuf[MAX_LEN] = {0};

    if (clFrame5 == NULL)
    {
        return MI_ERR;
    }

    MFRC522_SetTimerReload(RC522_TIMER_RELOAD_FAST);

    /* Tắt CRC phần cứng, truyền đủ 8-bit/byte, đặt ValuesAfterColl = 0 (xóa các bit sau va chạm) */
    ClearBitMask(TxModeReg, 0x80U);
    ClearBitMask(RxModeReg, 0x80U);
    Write_MFRC522(BitFramingReg, 0x00U);
    ClearBitMask(CollReg, 0x80U);

    cmdBuf[0] = cascadeCode;
    cmdBuf[1] = 0x20U;

    status = MFRC522_TransceiveEx(PCD_TRANSCEIVE, cmdBuf, 2U, rxBuf, &unLen,
                                  RC522_ERR_MASK_NO_CRC);
    if (status != MI_OK)
    {
        return status;
    }

    if (unLen != 40U)
    {
        return MI_ERR;
    }

    for (i = 0U; i < 4U; i++)
    {
        clFrame5[i] = rxBuf[i];
        bccCheck ^= rxBuf[i];
    }
    clFrame5[4] = rxBuf[4];

    if (bccCheck != clFrame5[4])
    {
        return MI_ERR;
    }

    return MI_OK;
}

uchar MFRC522_SelectCL(uchar cascadeCode, const uchar *clFrame5, uchar *sakOut)
{
    uchar i;
    uchar status;
    uint recvBits = 0U;
    uchar buffer[9];
    uchar rxBuf[MAX_LEN] = {0};
    uchar expectedCrc[2] = {0};

    if (clFrame5 == NULL)
    {
        return MI_ERR;
    }

    MFRC522_SetTimerReload(RC522_TIMER_RELOAD_FAST);
    Write_MFRC522(BitFramingReg, 0x00U);

    buffer[0] = cascadeCode;
    buffer[1] = 0x70U;
    for (i = 0U; i < 5U; i++)
    {
        buffer[i + 2U] = clFrame5[i];
    }

    if (MFRC522_CalculateCRC(buffer, 7U, &buffer[7]) != MI_OK)
    {
        return MI_ERR;
    }

    /* Kiểm tra đầy đủ cả CRCErr (0x04) với mặt nạ RC522_ERR_MASK_WITH_CRC (0x1F) */
    status = MFRC522_TransceiveEx(PCD_TRANSCEIVE, buffer, 9U, rxBuf, &recvBits,
                                  RC522_ERR_MASK_WITH_CRC);
    if (status != MI_OK)
    {
        return status;
    }

    /* Phản hồi SAK chuẩn gồm 1 byte SAK + 2 byte CRC_A = 24 bits (0x18) */
    if (recvBits != 0x18U)
    {
        return MI_ERR;
    }

    if (MFRC522_CalculateCRC(&rxBuf[0], 1U, expectedCrc) != MI_OK)
    {
        return MI_ERR;
    }

    if ((rxBuf[1] != expectedCrc[0]) || (rxBuf[2] != expectedCrc[1]))
    {
        return MI_ERR;
    }

    if (sakOut != NULL)
    {
        *sakOut = rxBuf[0];
    }

    return MI_OK;
}

uchar MFRC522_ReadCardCascade(uchar reqMode, MFRC522_CardInfo *cardInfo)
{
    static const uchar cascadeLevels[3] = {
        PICC_ANTICOLL_CL1,
        PICC_ANTICOLL_CL2,
        PICC_ANTICOLL_CL3
    };
    uchar status;
    uchar clFrame[5];
    uchar sak = 0U;
    uint8_t uidIndex = 0U;

    if (cardInfo == NULL)
    {
        return MI_ERR;
    }

    memset(cardInfo, 0, sizeof(*cardInfo));

    /* Bước 1: Gửi REQA (0x26) hoặc WUPA (0x52) đúng khung 7-bit */
    status = MFRC522_Request(reqMode, cardInfo->atqa);
    if (status != MI_OK)
    {
        return status;
    }

    /* Bước 2: Chạy qua các Cascade Level (CL1 -> CL2 -> CL3) theo bit 2 (0x04) của SAK */
    for (uint8_t level = 0U; level < 3U; level++)
    {
        status = MFRC522_AnticollCL(cascadeLevels[level], clFrame);
        if (status != MI_OK)
        {
            /* Đã nhận ATQA hợp lệ ở Bước 1 (có bằng chứng RF của thẻ), nếu lỗi ở Anticoll thì trả về MI_COLLERR hoặc MI_ERR */
            return (status == MI_COLLERR) ? MI_COLLERR : MI_ERR;
        }

        status = MFRC522_SelectCL(cascadeLevels[level], clFrame, &sak);
        if (status != MI_OK)
        {
            return (status == MI_COLLERR) ? MI_COLLERR : MI_ERR;
        }

        /* Kiểm tra bit Cascade (bit 2 = 0x04) trong SAK */
        if ((sak & 0x04U) != 0U)
        {
            /* UID chưa hoàn chỉnh: byte 0 bắt buộc là Cascade Tag (0x88) */
            if ((clFrame[0] != PICC_CASCADE_TAG) || (level >= 2U))
            {
                return MI_ERR;
            }
            cardInfo->uid[uidIndex++] = clFrame[1];
            cardInfo->uid[uidIndex++] = clFrame[2];
            cardInfo->uid[uidIndex++] = clFrame[3];
        }
        else
        {
            /* UID đã hoàn chỉnh ở Cascade Level hiện tại */
            cardInfo->uid[uidIndex++] = clFrame[0];
            cardInfo->uid[uidIndex++] = clFrame[1];
            cardInfo->uid[uidIndex++] = clFrame[2];
            cardInfo->uid[uidIndex++] = clFrame[3];
            cardInfo->uidLen = uidIndex;
            cardInfo->sak = sak;
            return MI_OK;
        }
    }

    return MI_ERR;
}

uchar MFRC522_Halt(void)
{
    uint unLen = 0U;
    uchar buff[4];
    uchar rxDummy[MAX_LEN] = {0};

    MFRC522_SetTimerReload(RC522_TIMER_RELOAD_FAST);
    Write_MFRC522(BitFramingReg, 0x00U);

    buff[0] = PICC_HALT;
    buff[1] = 0x00U;

    if (MFRC522_CalculateCRC(buff, 2U, &buff[2]) != MI_OK)
    {
        return MI_ERR;
    }

    uchar status = MFRC522_TransceiveEx(PCD_TRANSCEIVE, buff, 4U, rxDummy, &unLen, RC522_ERR_MASK_HALT);
    return (status == MI_NOTAGERR) ? MI_OK : status;
}

