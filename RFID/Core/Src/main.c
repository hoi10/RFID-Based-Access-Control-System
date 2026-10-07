/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : RFID access control: RC522 + I2C LCD + servo + LEDs
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "I2C_LCD.h"
#include "SERVO.h"
#include "rc522.h"
#include "uart.h"
#include "DWT_Delay.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MyI2C_LCD I2C_LCD_1
#define SERVO_Motor1    0
#define MAX_AUTHORIZED_CARDS 5

#define LED_GREEN_PORT  GPIOA
#define LED_GREEN_PIN   GPIO_PIN_4
#define LED_RED_PORT    GPIOA
#define LED_RED_PIN     GPIO_PIN_5

#define BARRIER_OPEN_ANGLE   90.0f
#define BARRIER_CLOSE_ANGLE  0.0f
#define BARRIER_OPEN_MS      2500
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */
static uchar authorizedCards[MAX_AUTHORIZED_CARDS][4] = {
    {0x62, 0x99, 0x18, 0x02},
    {0x11, 0x22, 0x33, 0x44}
};
static uint8_t numAuthorizedCards = 2;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
static void Tone(uint32_t Frequency, uint32_t Duration);
static void noTone(void);
static void LED_AllOff(void);
static void LED_GreenOn(void);
static void LED_RedOn(void);
static void ShowIdleScreen(void);
static void Buzzer_Init(void);
static void Barrier_OpenThenClose(void);
static uchar CompareUID(uchar *scannedUID, uchar *storedUID);
static uchar IsAuthorizedCard(uchar *scannedUID);
static void RFID_ProcessCard(void);
static void DebugPrintUID(const uchar *uid, uint8_t len);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void DebugPrintUID(const uchar *uid, uint8_t len)
{
  static const char hexDigits[] = "0123456789ABCDEF";
    uart_write_str("UID:");
    for (uint8_t i = 0; i < len; i++)
    {
    char buffer[4] = {
      hexDigits[uid[i] >> 4],
      hexDigits[uid[i] & 0x0F],
      ' ',
      '\0'
    };
        uart_write_str(buffer);
    }
    uart_write_str("\r\n");
}

static void LED_AllOff(void)
{
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_RESET);
}

static void LED_GreenOn(void)
{
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_SET);
}

static void LED_RedOn(void)
{
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN, GPIO_PIN_SET);
}

static void ShowIdleScreen(void)
{
    LED_AllOff();
    I2C_LCD_Clear(MyI2C_LCD);
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 0);
    I2C_LCD_WriteString(MyI2C_LCD, "Quet the RFID");
    I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
    I2C_LCD_WriteString(MyI2C_LCD, "Moi quet the...");
}

static void Barrier_OpenThenClose(void)
{
    SERVO_MoveTo(SERVO_Motor1, BARRIER_OPEN_ANGLE);
    HAL_Delay(BARRIER_OPEN_MS);
    SERVO_MoveTo(SERVO_Motor1, BARRIER_CLOSE_ANGLE);
}

static uchar CompareUID(uchar *scannedUID, uchar *storedUID)
{
    for (uchar i = 0; i < 4; i++)
    {
        if (scannedUID[i] != storedUID[i])
        {
            return 0;
        }
    }
    return 1;
}

static uchar IsAuthorizedCard(uchar *scannedUID)
{
    for (uint8_t i = 0; i < numAuthorizedCards; i++)
    {
        if (CompareUID(scannedUID, authorizedCards[i]))
        {
            return 1;
        }
    }
    return 0;
}

static void RFID_ProcessCard(void)
{
    uchar status;
    uchar tagType[2];
  uchar serNum[5] = {0};

    status = MFRC522_Request(PICC_REQIDL, tagType);
    if (status != MI_OK)
    {
        return;
    }

    status = MFRC522_Anticoll(serNum);
    if (status != MI_OK)
    {
        return;
    }

    if (IsAuthorizedCard(serNum))
    {
        LED_GreenOn();
        uart_write_str("CARD OK\r\n");
        DebugPrintUID(serNum, 4);
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "Welcome");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Mo rao chan");
        Tone(659, 200);
        Tone(784, 200);
        noTone();
        Barrier_OpenThenClose();
        HAL_Delay(300);
        ShowIdleScreen();
    }
    else
    {
        LED_RedOn();
        uart_write_str("CARD DENIED\r\n");
        DebugPrintUID(serNum, 4);
        I2C_LCD_Clear(MyI2C_LCD);
        I2C_LCD_WriteString(MyI2C_LCD, "Sai The");
        I2C_LCD_SetCursor(MyI2C_LCD, 0, 1);
        I2C_LCD_WriteString(MyI2C_LCD, "Tu choi");
        Tone(392, 500);
        noTone();
        HAL_Delay(1500);
        ShowIdleScreen();
    }

    MFRC522_Halt();
}

static void Buzzer_Init(void)
{
    GPIO_InitTypeDef gpioInit = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpioInit.Pin = GPIO_PIN_2;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpioInit);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
    DWT_Delay_Init();
}

static void Tone(uint32_t Frequency, uint32_t Duration)
{
    if ((Frequency == 0) || (Duration == 0))
    {
        return;
    }

    uint32_t halfPeriodUs = 500000UL / Frequency;
    uint32_t cycles = (Frequency * Duration + 500UL) / 1000UL;

    for (uint32_t i = 0; i < cycles; i++)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);
        DWT_Delay_us(halfPeriodUs);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
        DWT_Delay_us(halfPeriodUs);
    }
}

static void noTone(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  USART1_Init();
  /* USER CODE BEGIN 2 */
    Buzzer_Init();
    Tone(1000, 150);
    noTone();
    SERVO_Init(SERVO_Motor1);
    SERVO_MoveTo(SERVO_Motor1, BARRIER_CLOSE_ANGLE);
    I2C_LCD_Init(MyI2C_LCD);
    I2C_LCD_Backlight(MyI2C_LCD);
    I2C_LCD_Display(MyI2C_LCD);
    LED_AllOff();
    MFRC522_Init();
    uart_write_str("RFID ready\r\n");
    ShowIdleScreen();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
        RFID_ProcessCard();
        HAL_Delay(80);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4|GPIO_PIN_5, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);

  /* LED xanh PA4, LED do PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* RC522 RST PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* RC522 CS PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif /* USE_FULL_ASSERT */
