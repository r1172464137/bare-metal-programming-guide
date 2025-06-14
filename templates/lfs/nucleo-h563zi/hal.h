// Copyright (c) 2022-2023 Cesanta Software Limited
// All rights reserved
//
// Datasheet: RM0481, devboard manual: UM3115
// https://www.st.com/resource/en/reference_manual/rm0481-stm32h563h573-and-stm32h562-armbased-32bit-mcus-stmicroelectronics.pdf
// Alternate functions: https://www.st.com/resource/en/datasheet/stm32h563vi.pdf

#pragma once

#include <stm32h563xx.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UART_DEBUG
#define UART_DEBUG USART3
#endif

#define BIT(x) (1UL << (x))
#define CLRSET(R, CLEARMASK, SETMASK) (R) = ((R) & ~(CLEARMASK)) | (SETMASK)
#define PIN(bank, num) ((((bank) - 'A') << 8) | (num))
#define PINNO(pin) (pin & 255)
#define PINBANK(pin) (pin >> 8)

// System clock (11.4, Figure 48; 11.4.5, Figure 51; 11.4.8
// SYS_FREQUENCY <= 250 MHz; (CLOCK_FREQUENCY / HPRE) ; hclk = SYS_FREQUENCY
// APB clocks <= 250 MHz. Configure flash latency (WS) in accordance to hclk
// freq (7.3.4, Table 37)
enum {
  HPRE = 7,   // register value, divisor value = BIT(value - 7) = / 1
  PPRE1 = 4,  // register values, divisor value = BIT(value - 3) = / 2
  PPRE2 = 4,
  PPRE3 = 4,
};
// Make sure your chip package uses the internal LDO, otherwise set PLL1_N = 200
enum { PLL1_HSI = 64, PLL1_M = 32, PLL1_N = 250, PLL1_P = 2 };
#define SYS_FREQUENCY \
  ((PLL1_HSI * PLL1_N / PLL1_M / PLL1_P / (BIT(HPRE - 7))) * 1000000)
#define AHB_FREQUENCY SYS_FREQUENCY
#define APB2_FREQUENCY (AHB_FREQUENCY / (BIT(PPRE2 - 3)))
#define APB1_FREQUENCY (AHB_FREQUENCY / (BIT(PPRE1 - 3)))

static inline void spin(volatile uint32_t n) {
  while (n--) (void) 0;
}

enum { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_AF, GPIO_MODE_ANALOG };
enum { GPIO_OTYPE_PUSH_PULL, GPIO_OTYPE_OPEN_DRAIN };
enum { GPIO_SPEED_LOW, GPIO_SPEED_MEDIUM, GPIO_SPEED_HIGH, GPIO_SPEED_INSANE };
enum { GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN };

#define GPIO(N) ((GPIO_TypeDef *) ((GPIOA_BASE_NS) + 0x400 * (N)))

static GPIO_TypeDef *gpio_bank(uint16_t pin) {
  return GPIO(PINBANK(pin));
}
static inline void gpio_toggle(uint16_t pin) {
  GPIO_TypeDef *gpio = gpio_bank(pin);
  uint32_t mask = BIT(PINNO(pin));
  gpio->BSRR = mask << (gpio->ODR & mask ? 16 : 0);
}
static inline int gpio_read(uint16_t pin) {
  return gpio_bank(pin)->IDR & BIT(PINNO(pin)) ? 1 : 0;
}
static inline void gpio_write(uint16_t pin, bool val) {
  GPIO_TypeDef *gpio = gpio_bank(pin);
  gpio->BSRR = BIT(PINNO(pin)) << (val ? 0 : 16);
}
static inline void gpio_init(uint16_t pin, uint8_t mode, uint8_t type,
                             uint8_t speed, uint8_t pull, uint8_t af) {
  GPIO_TypeDef *gpio = gpio_bank(pin);
  uint8_t n = (uint8_t) (PINNO(pin));
  RCC->AHB2ENR |= BIT(PINBANK(pin));  // Enable GPIO clock
  CLRSET(gpio->OTYPER, 1UL << n, ((uint32_t) type) << n);
  CLRSET(gpio->OSPEEDR, 3UL << (n * 2), ((uint32_t) speed) << (n * 2));
  CLRSET(gpio->PUPDR, 3UL << (n * 2), ((uint32_t) pull) << (n * 2));
  CLRSET(gpio->AFR[n >> 3], 15UL << ((n & 7) * 4),
         ((uint32_t) af) << ((n & 7) * 4));
  CLRSET(gpio->MODER, 3UL << (n * 2), ((uint32_t) mode) << (n * 2));
}
static inline void gpio_input(uint16_t pin) {
  gpio_init(pin, GPIO_MODE_INPUT, GPIO_OTYPE_PUSH_PULL, GPIO_SPEED_HIGH,
            GPIO_PULL_NONE, 0);
}
static inline void gpio_output(uint16_t pin) {
  gpio_init(pin, GPIO_MODE_OUTPUT, GPIO_OTYPE_PUSH_PULL, GPIO_SPEED_HIGH,
            GPIO_PULL_NONE, 0);
}

static inline bool uart_init(USART_TypeDef *uart, unsigned long baud) {
  uint8_t af = 7;           // Alternate function
  uint16_t rx = 0, tx = 0;  // pins
  uint32_t freq = 0;        // Bus frequency. UART1 is on APB2, rest on APB1

  if (uart == USART1) {
    freq = APB2_FREQUENCY, RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    tx = PIN('A', 9), rx = PIN('A', 10);
  } else if (uart == USART2) {
    freq = APB1_FREQUENCY, RCC->APB1LENR |= RCC_APB1LENR_USART2EN;
    tx = PIN('A', 2), rx = PIN('A', 3);
  } else if (uart == USART3) {
    freq = APB1_FREQUENCY, RCC->APB1LENR |= RCC_APB1LENR_USART3EN;
    tx = PIN('D', 8), rx = PIN('D', 9);
  } else {
    return false;
  }
  gpio_init(tx, GPIO_MODE_AF, GPIO_OTYPE_PUSH_PULL, GPIO_SPEED_HIGH, 0, af);
  gpio_init(rx, GPIO_MODE_AF, GPIO_OTYPE_PUSH_PULL, GPIO_SPEED_HIGH, 0, af);
  uart->CR1 = 0;                            // Disable UART
  uart->BRR = freq / baud;                  // Set baud rate
  uart->CR1 = USART_CR1_RE | USART_CR1_TE;  // Set mode to TX & RX
  uart->CR1 |= USART_CR1_UE;                // Enable UART
  return true;
}
static inline void uart_write_byte(USART_TypeDef *uart, uint8_t byte) {
  uart->TDR = byte;
  while ((uart->ISR & BIT(7)) == 0) spin(1);
}
static inline void uart_write_buf(USART_TypeDef *uart, const char *buf,
                                  size_t len) {
  while (len-- > 0) uart_write_byte(uart, *(uint8_t *) buf++);
}
static inline int uart_read_ready(USART_TypeDef *uart) {
  return uart->ISR & BIT(5);  // If RXNE bit is set, data is ready
}
static inline uint8_t uart_read_byte(USART_TypeDef *uart) {
  return (uint8_t) (uart->RDR & 255);
}

static inline void rng_init(void) {
  RCC->CCIPR5 |= RCC_CCIPR5_RNGSEL_0;  // RNG clock source pll1_q_ck
  RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;   // Enable RNG clock
  RNG->CR |= RNG_CR_RNGEN;             // Enable RNG
}
static inline uint32_t rng_read(void) {
  while ((RNG->SR & RNG_SR_DRDY) == 0) spin(1);
  return RNG->DR;
}

static inline bool ldo_is_on(void) {
  return (PWR->SCCR & PWR_SCCR_LDOEN) == PWR_SCCR_LDOEN;
}

// Hw pull-ups on PHY RXD0,1,DV to enable autonegotiation
static inline void ethernet_init(void) {
  // Initialise Ethernet. Enable MAC GPIO pins, see UM3115 section 10.7
  uint16_t pins[] = {PIN('A', 1),  PIN('A', 2),  PIN('A', 7),
                     PIN('B', 15), PIN('C', 1),  PIN('C', 4),
                     PIN('C', 5),  PIN('G', 11), PIN('G', 13)};
  for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
    gpio_init(pins[i], GPIO_MODE_AF, GPIO_OTYPE_PUSH_PULL, GPIO_SPEED_INSANE,
              GPIO_PULL_NONE, 11);  // 11 is the Ethernet function
  }
  NVIC_EnableIRQ(ETH_IRQn);           // Setup Ethernet IRQ handler
  RCC->APB3ENR |= RCC_APB3ENR_SBSEN;  // Enable SBS clock
  CLRSET(SBS->PMCR, SBS_PMCR_ETH_SEL_PHY, SBS_PMCR_ETH_SEL_PHY_2);  // RMII
  RCC->AHB1ENR |= RCC_AHB1ENR_ETHEN | RCC_AHB1ENR_ETHRXEN | RCC_AHB1ENR_ETHTXEN;
}

#define UUID ((uint32_t *) UID_BASE)  // Unique 96-bit chip ID. TRM 59.1

// Helper macro for MAC generation, byte reads not allowed
#define GENERATE_LOCALLY_ADMINISTERED_MAC() \
  {2,                                       \
   UUID[0] & 255,                           \
   (UUID[0] >> 10) & 255,                   \
   (UUID[0] >> 19) & 255,                   \
   UUID[1] & 255,                           \
   UUID[2] & 255}

static inline void system_init(void) {
  SCB->CPACR |= ((3UL << 20U) | (3UL << 22U));  // Enable FPU
  __DSB();
  __ISB();
}

extern unsigned char _end[];  // End of data section, start of heap. See link.ld
static unsigned char *s_current_heap_end = _end;

static inline size_t hal_ram_used(void) {
  return (size_t) (s_current_heap_end - _end);
}

static inline size_t hal_ram_free(void) {
  unsigned char endofstack;
  return (size_t) (&endofstack - s_current_heap_end);
}

static inline void clock_init(void) {
  // Set flash latency. RM0481, section 7.11.1, section 7.3.4 table 37
  CLRSET(FLASH->ACR, (FLASH_ACR_WRHIGHFREQ_Msk | FLASH_ACR_LATENCY_Msk),
         FLASH_ACR_LATENCY_5WS | FLASH_ACR_WRHIGHFREQ_1);

  if (ldo_is_on()) {
    PWR->VOSCR = PWR_VOSCR_VOS_0 | PWR_VOSCR_VOS_1;  // Select VOS0
  } else {
    PWR->VOSCR = PWR_VOSCR_VOS_1;  // Select VOS1
  }
  uint32_t f = PWR->VOSCR;  // fake read to wait for bus clocking
  while ((PWR->VOSSR & PWR_VOSSR_ACTVOSRDY) == 0) spin(1);
  (void) f;
  RCC->CR = RCC_CR_HSION;                          // Clear HSI clock divisor
  while ((RCC->CR & RCC_CR_HSIRDY) == 0) spin(1);  // Wait until done
  RCC->CFGR2 = (PPRE3 << 12) | (PPRE2 << 8) | (PPRE1 << 4) | (HPRE << 0);
  RCC->PLL1DIVR =
      ((PLL1_P - 1) << 9) | ((PLL1_N - 1) << 0);  // Set PLL1_P PLL1_N
  // Enable P and Q divider outputs; set PLL1_M, select HSI as source,
  // !PLL1VCOSEL, PLL1RGE=0
  RCC->PLL1CFGR =
      RCC_PLL1CFGR_PLL1QEN | RCC_PLL1CFGR_PLL1PEN | (PLL1_M << 8) | (1 << 0);
  RCC->CR |= RCC_CR_PLL1ON;                         // Enable PLL1
  while ((RCC->CR & RCC_CR_PLL1RDY) == 0) spin(1);  // Wait until done
  RCC->CFGR1 |= (3 << 0);                           // Set clock source to PLL1
  while ((RCC->CFGR1 & (7 << 3)) != (3 << 3)) spin(1);  // Wait until done
}
