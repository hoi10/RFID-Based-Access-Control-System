#include "SERVO.h"
#include "SERVO_cfg.h"

extern TIM_HandleTypeDef htim1;

typedef struct
{
    uint16_t Period_Min;
    uint16_t Period_Max;
} SERVO_info;

static SERVO_info gs_SERVO_info[SERVO_NUM] = {0};

static TIM_HandleTypeDef *SERVO_GetTimerHandle(TIM_TypeDef *timerInstance)
{
    if (timerInstance == TIM1)
    {
        return &htim1;
    }
    return NULL;
}

void SERVO_Init(uint16_t au16_SERVO_Instance)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    TIM_HandleTypeDef *htim = NULL;
    uint32_t PSC_Value = 0;
    uint32_t ARR_Value = 0;

    // Bat clock GPIO cho chan PWM
    if (SERVO_CfgParam[au16_SERVO_Instance].SERVO_GPIO == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (SERVO_CfgParam[au16_SERVO_Instance].SERVO_GPIO == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }

    GPIO_InitStruct.Pin = SERVO_CfgParam[au16_SERVO_Instance].SERVO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SERVO_CfgParam[au16_SERVO_Instance].SERVO_GPIO, &GPIO_InitStruct);

    // Xung PWM 50Hz chuan: 1 tick = 1us (72MHz / 72 = 1MHz), chu ky 20ms (ARR = 19999)
    PSC_Value = (uint32_t)((SERVO_CfgParam[au16_SERVO_Instance].TIM_CLK / 1000000U) - 1U);
    ARR_Value = 20000U - 1U;

    __HAL_RCC_TIM1_CLK_ENABLE();

    htim = SERVO_GetTimerHandle(SERVO_CfgParam[au16_SERVO_Instance].TIM_Instance);
    if (htim == NULL)
    {
        return;
    }

    htim->Instance = SERVO_CfgParam[au16_SERVO_Instance].TIM_Instance;
    htim->Init.Prescaler = PSC_Value;
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    htim->Init.Period = ARR_Value;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.RepetitionCounter = 0;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(htim);

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    HAL_TIM_ConfigClockSource(htim, &sClockSourceConfig);
    HAL_TIM_PWM_Init(htim);

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(htim, &sMasterConfig);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, SERVO_CfgParam[au16_SERVO_Instance].PWM_TIM_CH);

    // Tinh toan do rong xung: 1.0ms = 1000us (0 do), 2.0ms = 2000us (180 do)
    gs_SERVO_info[au16_SERVO_Instance].Period_Min = (uint16_t)((ARR_Value + 1U) * (SERVO_CfgParam[au16_SERVO_Instance].MinPulse / 20.0f));
    gs_SERVO_info[au16_SERVO_Instance].Period_Max = (uint16_t)((ARR_Value + 1U) * (SERVO_CfgParam[au16_SERVO_Instance].MaxPulse / 20.0f));

    HAL_TIM_PWM_Start(htim, SERVO_CfgParam[au16_SERVO_Instance].PWM_TIM_CH);
}

// Dieu khien servo quay den goc chi dinh (0 den 180 do)
void SERVO_MoveTo(uint16_t au16_SERVO_Instance, float af_Angle)
{
    uint16_t au16_Pulse = 0;
    TIM_HandleTypeDef *htim = NULL;

    htim = SERVO_GetTimerHandle(SERVO_CfgParam[au16_SERVO_Instance].TIM_Instance);
    if (htim == NULL)
    {
        return;
    }

    if (af_Angle < 0.0f)
    {
        af_Angle = 0.0f;
    }
    else if (af_Angle > 180.0f)
    {
        af_Angle = 180.0f;
    }

    au16_Pulse = (uint16_t)(((af_Angle * (gs_SERVO_info[au16_SERVO_Instance].Period_Max - gs_SERVO_info[au16_SERVO_Instance].Period_Min)) / 180.0f)
                 + gs_SERVO_info[au16_SERVO_Instance].Period_Min);

    __HAL_TIM_SET_COMPARE(htim, SERVO_CfgParam[au16_SERVO_Instance].PWM_TIM_CH, au16_Pulse);
}

// Dung phat xung PWM
void SERVO_Stop(uint16_t au16_SERVO_Instance)
{
    TIM_HandleTypeDef *htim = NULL;

    htim = SERVO_GetTimerHandle(SERVO_CfgParam[au16_SERVO_Instance].TIM_Instance);
    if (htim == NULL)
    {
        return;
    }

    __HAL_TIM_SET_COMPARE(htim, SERVO_CfgParam[au16_SERVO_Instance].PWM_TIM_CH, 0U);
}
