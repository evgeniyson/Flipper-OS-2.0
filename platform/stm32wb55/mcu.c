/* Clock, time base, GPIO and the SPI / I2C / UART buses. */
#include "board.h"

/* ------------------------------------------------------------ GPIO */

void pin_init(Pin p, PinMode mode, PinPull pull, bool open_drain, uint8_t alt) {
    uint32_t port_index = ((uint32_t)p.port - GPIOA_BASE) / 0x400u; /* A..E = 0..4, H = 7 */
    RCC->AHB2ENR |= 1u << port_index;
    (void)RCC->AHB2ENR;
    uint32_t shift = p.pin * 2u;
    p.port->AFR[p.pin >> 3] = (p.port->AFR[p.pin >> 3] & ~(0xFu << ((p.pin & 7u) * 4u))) |
                              ((uint32_t)alt << ((p.pin & 7u) * 4u));
    p.port->PUPDR = (p.port->PUPDR & ~(3u << shift)) | ((uint32_t)pull << shift);
    p.port->OTYPER = (p.port->OTYPER & ~(1u << p.pin)) | ((open_drain ? 1u : 0u) << p.pin);
    p.port->OSPEEDR = (p.port->OSPEEDR & ~(3u << shift)) | (2u << shift);
    p.port->MODER = (p.port->MODER & ~(3u << shift)) | ((uint32_t)mode << shift);
}

/* ------------------------------------------------------------ clock and time */

static volatile uint32_t tick_ms;

void SysTick_Handler(void) {
    tick_ms++;
}

uint32_t millis(void) {
    return tick_ms;
}

/* Busy wait on the CPU cycle counter: works with interrupts disabled too. */
void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (CPU_HZ / 1000000u);
    while(DWT->CYCCNT - start < ticks) {
    }
}

void delay_ms(uint32_t ms) {
    while(ms--) delay_us(1000);
}

static bool wait_flag(volatile uint32_t* reg, uint32_t mask, uint32_t value) {
    for(uint32_t i = 0; i < 2000000u; i++)
        if((*reg & mask) == value) return true;
    return false;
}

void clock_init(void) {
    /* 32 MHz crystal -> PLL (/2 x8 /2) -> 64 MHz */
    RCC->CR |= RCC_CR_HSEON;
    bool ok = wait_flag(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY);

    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_3WS;
    ok = ok && wait_flag(&FLASH->ACR, FLASH_ACR_LATENCY, FLASH_ACR_LATENCY_3WS);

    if(ok) {
        RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_0 | RCC_PLLCFGR_PLLSRC_1 /* HSE */
                       | (1u << RCC_PLLCFGR_PLLM_Pos) /* /2 */
                       | (8u << RCC_PLLCFGR_PLLN_Pos) /* x8 */
                       | (1u << RCC_PLLCFGR_PLLR_Pos) /* /2 */
                       | RCC_PLLCFGR_PLLREN;
        RCC->CR |= RCC_CR_PLLON;
        ok = wait_flag(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY);
    }
    if(ok) {
        /* the radio core's clock may not exceed 32 MHz, even though it is never started */
        RCC->EXTCFGR = (RCC->EXTCFGR & ~RCC_EXTCFGR_C2HPRE) | RCC_EXTCFGR_C2HPRE_3;
        wait_flag(&RCC->EXTCFGR, RCC_EXTCFGR_C2HPREF, RCC_EXTCFGR_C2HPREF);
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_0 | RCC_CFGR_SW_1;
        ok = wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_0 | RCC_CFGR_SWS_1);
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* priorities: radio timing first, the millisecond tick last */
    NVIC_SetPriority(SysTick_IRQn, 8);
    SysTick->LOAD = CPU_HZ / 1000u - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

    if(!ok) panic("Clock start failed", "32 MHz crystal / PLL");
}

/* ------------------------------------------------------------ SPI */

void spi_init(SPI_TypeDef* spi, SpiSpeed speed) {
    if(spi == SPI1)
        RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    else
        RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    (void)RCC->APB2ENR;
    uint32_t cr1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | ((uint32_t)speed << SPI_CR1_BR_Pos);
    if((spi->CR1 & ~SPI_CR1_SPE) == cr1 && (spi->CR1 & SPI_CR1_SPE)) return; /* already set up */
    spi->CR1 &= ~SPI_CR1_SPE;
    spi->CR1 = cr1; /* mode 0: clock idles low, data sampled on the first edge */
    spi->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH; /* 8 bit frames */
    spi->CR1 |= SPI_CR1_SPE;
}

uint8_t spi_xfer(SPI_TypeDef* spi, uint8_t byte) {
    uint32_t guard = 200000;
    while(!(spi->SR & SPI_SR_TXE) && --guard) {
    }
    *(volatile uint8_t*)&spi->DR = byte;
    guard = 200000;
    while(!(spi->SR & SPI_SR_RXNE) && --guard) {
    }
    return *(volatile uint8_t*)&spi->DR;
}

/* ------------------------------------------------------------ I2C */

void i2c_init(void) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    (void)RCC->APB1ENR1;
    pin_init(PIN_I2C_SCL, PIN_ALT, PULL_UP, true, 4);
    pin_init(PIN_I2C_SDA, PIN_ALT, PULL_UP, true, 4);
    I2C1->CR1 = 0;
    I2C1->TIMINGR = 0x10707DBC; /* 100 kHz from 64 MHz */
    I2C1->CR1 = I2C_CR1_PE;
}

/* Wait for one of the flags in `mask`; false on NACK or timeout. */
static bool i2c_wait(uint32_t mask) {
    for(uint32_t i = 0; i < 400000u; i++) {
        uint32_t isr = I2C1->ISR;
        if(isr & I2C_ISR_NACKF) return false;
        if(isr & mask) return true;
    }
    return false;
}

static bool i2c_finish(bool ok) {
    if(ok) ok = i2c_wait(I2C_ISR_STOPF);
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
    if(!ok) {
        /* leave the bus in a clean state for the next attempt */
        I2C1->CR1 &= ~I2C_CR1_PE;
        (void)I2C1->CR1;
        I2C1->CR1 |= I2C_CR1_PE;
    }
    return ok;
}

bool i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value) {
    I2C1->CR2 = addr | (2u << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;
    bool ok = i2c_wait(I2C_ISR_TXIS);
    if(ok) {
        I2C1->TXDR = reg;
        ok = i2c_wait(I2C_ISR_TXIS);
    }
    if(ok) I2C1->TXDR = value;
    return i2c_finish(ok);
}

bool i2c_read(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
    I2C1->CR2 = addr | (1u << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    bool ok = i2c_wait(I2C_ISR_TXIS);
    if(ok) {
        I2C1->TXDR = reg;
        ok = i2c_wait(I2C_ISR_TC);
    }
    if(ok) {
        I2C1->CR2 = addr | I2C_CR2_RD_WRN | ((uint32_t)len << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND |
                    I2C_CR2_START;
        for(size_t i = 0; i < len && ok; i++) {
            ok = i2c_wait(I2C_ISR_RXNE);
            if(ok) data[i] = (uint8_t)I2C1->RXDR;
        }
    }
    return i2c_finish(ok);
}

/* ------------------------------------------------------------ UART (debug log) */

void uart_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    pin_init(PIN_UART_TX, PIN_ALT, PULL_UP, false, 7);
    USART1->BRR = CPU_HZ / 115200u;
    USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
}

void uart_write(const char* text) {
    for(; *text; text++) {
        uint32_t guard = 100000;
        while(!(USART1->ISR & USART_ISR_TXE) && --guard) {
        }
        USART1->TDR = (uint8_t)*text;
    }
}

/* ------------------------------------------------------------ watchdog */

void watchdog_start(void) {
    IWDG->KR = 0xCCCC; /* start (also turns on its 32 kHz oscillator) */
    IWDG->KR = 0x5555; /* unlock the settings */
    IWDG->PR = 6; /* /256: 125 Hz */
    IWDG->RLR = 2000; /* 16 s */
    for(uint32_t i = 0; i < 200000u && IWDG->SR; i++) {
    }
    IWDG->KR = 0xAAAA;
}

void watchdog_feed(void) {
    IWDG->KR = 0xAAAA;
}
