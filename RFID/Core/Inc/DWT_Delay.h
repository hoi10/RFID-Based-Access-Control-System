/*
 * File: DWT_Delay.h
 * Driver Name: [[ DWT Delay ]]
 * SW Layer:   ECUAL
 * Created on: Jun 28, 2020
 * Author:     Khaled Magdy
 * -------------------------------------------
 * For More Information, Tutorials, etc.
 * Visit Website: www.DeepBlueMbedded.com
 *
 */

#ifndef DWT_DELAY_H_
#define DWT_DELAY_H_

#include "stm32f1xx_hal.h"


uint32_t DWT_Delay_Init(void);


// This Function Provides Delay In Microseconds Using DWT (with safe fallback)
__STATIC_INLINE void DWT_Delay_us(volatile uint32_t au32_microseconds)
{
  uint32_t au32_initial_ticks = DWT->CYCCNT;
  uint32_t au32_ticks = (HAL_RCC_GetHCLKFreq() / 1000000);
  uint32_t total_ticks = au32_microseconds * au32_ticks;

  // Nếu DWT->CYCCNT không chạy (ví dụ chip clone STM32 hoặc không cắm debugger),
  // fallback sang vòng lặp NOP để không bao giờ bị treo vĩnh viễn.
  if (DWT->CYCCNT == 0)
  {
    volatile uint32_t count = au32_microseconds * 8;
    while (count--)
    {
      __NOP();
    }
    return;
  }

  while ((DWT->CYCCNT - au32_initial_ticks) < total_ticks);
}

// This Function Provides Delay In Milliseconds
__STATIC_INLINE void DWT_Delay_ms(volatile uint32_t au32_milliseconds)
{
  HAL_Delay(au32_milliseconds);
}

#endif /* DWT_DELAY_H_ */
