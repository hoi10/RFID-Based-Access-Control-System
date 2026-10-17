#include "uart.h"

#define UART_RX_BUFFER_SIZE 128U
#define UART_RX_BUFFER_MASK (UART_RX_BUFFER_SIZE - 1U)

static volatile uint8_t uartRxBuffer[UART_RX_BUFFER_SIZE];
static volatile uint8_t uartRxHead;
static volatile uint8_t uartRxTail;

void USART1_Init(void)
{
    // Enable GPIOA clock and USART1 clock
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    // Configure PA9 (TX) as Alternate function push-pull (0x0B)
    // Configure PA10 (RX) as Input floating (0x04)
    GPIOA->CRH &= ~(0xFFU << 4);
    GPIOA->CRH |= (0x4BU << 4);

    // Baud rate 9600 bps (Standard for Proteus Virtual Terminal)
    uint32_t pclk2 = SystemCoreClock;
    if (pclk2 == 0)
    {
        pclk2 = 72000000UL;
    }
    USART1->BRR = (pclk2 + (9600 / 2)) / 9600;

    // Enable Transmitter, Receiver and USART
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    NVIC_SetPriority(USART1_IRQn, 6);
    NVIC_EnableIRQ(USART1_IRQn);
}

void uart_write_char(char c)
{
    while (!(USART1->SR & USART_SR_TXE))
    {
    }
    USART1->DR = (uint8_t)c;
}

void uart_write_str(const char *str)
{
    while (*str)
    {
        uart_write_char(*str++);
    }
}

int uart_read_char(uint8_t *ch)
{
    if (uartRxTail != uartRxHead)
    {
        *ch = uartRxBuffer[uartRxTail];
        uartRxTail = (uint8_t)((uartRxTail + 1U) & UART_RX_BUFFER_MASK);
        return 1;
    }
    return 0;
}

void uart_rx_irq_handler(void)
{
    uint8_t ch = (uint8_t)(USART1->DR & 0xFFU);
    uint8_t nextHead = (uint8_t)((uartRxHead + 1U) & UART_RX_BUFFER_MASK);

    if (nextHead != uartRxTail)
    {
        uartRxBuffer[uartRxHead] = ch;
        uartRxHead = nextHead;
    }
}
