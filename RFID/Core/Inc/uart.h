#ifndef __UART_H_
#define __UART_H_

#include "stm32f1xx.h"
#include <stdint.h>

void USART1_Init(void);
void uart_write_char(char c);
void uart_write_str(const char *str);
int  uart_read_char(uint8_t *ch);
void uart_rx_irq_handler(void);

#endif /* __UART_H_ */
