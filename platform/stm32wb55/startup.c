/* Reset entry, vector table and fault handling. */
#include "board.h"
#include "../../core/fmt.h"
#include <string.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _stack_top;
int main(void);
void fault_report(const uint32_t* frame);

/* Requests carried across a system reset. Doing these things straight after
 * reset means the chip is in a known state: no peripheral, interrupt or
 * watchdog of the running firmware is still active. */
#define BOOT_DFU      0xDF00B007u
#define BOOT_SHUTDOWN 0x0FF0B007u
/* Not initialised on purpose: keeps its value across a system reset. */
static volatile uint32_t boot_request __attribute__((section(".noinit")));

/* Hand control to the ST USB bootloader in system memory. Called straight
 * after reset, before any peripheral has been touched, so the bootloader
 * finds the chip in the state it expects. */
void dfu_enter(void) {
    /* erratum: the option-byte error flag is always set after reset and makes
     * the bootloader refuse to program; writing 1 clears it */
    FLASH->SR = FLASH_SR_OPTVERR;
    SYSCFG->MEMRMP = SYSCFG_MEMRMP_MEM_MODE_0; /* system flash at address 0 */
    const uint32_t* vectors = (const uint32_t*)0x1FFF0000u;
    uint32_t sp = vectors[0], pc = vectors[1];
    __asm volatile("msr msp, %0\n bx %1\n" : : "r"(sp), "r"(pc));
    for(;;) {
    }
}

void dfu_request_and_reset(void) {
    boot_request = BOOT_DFU;
    NVIC_SystemReset();
    for(;;) {
    }
}

void shutdown_request_and_reset(void) {
    boot_request = BOOT_SHUTDOWN;
    NVIC_SystemReset();
    for(;;) {
    }
}

/* Deepest sleep of the MCU; BACK (PC13, wake-up pin 2, active low) restarts it. */
static void shutdown_enter(void) {
    PWR->PUCRC |= PWR_PUCRC_PC13;
    PWR->PDCRA |= PWR_PDCRA_PA8; /* keep the vibration motor, speaker and IR LED off */
    PWR->PDCRB |= PWR_PDCRB_PB8 | PWR_PDCRB_PB9;
    PWR->CR3 |= PWR_CR3_EWUP2 | PWR_CR3_APC;
    PWR->C2CR3 |= PWR_C2CR3_EWUP2;
    PWR->CR4 = (PWR->CR4 | PWR_CR4_WP2) & ~PWR_CR4_C2BOOT;
    PWR->SCR = PWR_SCR_CWUF2;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_LPMS) | PWR_CR1_LPMS_2;
    PWR->C2CR1 = (PWR->C2CR1 & ~PWR_C2CR1_LPMS) | PWR_C2CR1_LPMS_2;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    for(;;) __WFI();
}

void Reset_Handler(void) {
    /* 1. the running firmware asked for DFU before resetting */
    uint32_t request = boot_request;
    boot_request = 0;
    if(request == BOOT_DFU) dfu_enter();
    if(request == BOOT_SHUTDOWN) shutdown_enter();

    /* 2. LEFT held during reset: always available, whatever state the rest of
     * the firmware is in. PB11 is pulled up and reads low when pressed. */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    (void)RCC->AHB2ENR;
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << 22)) | (1u << 22);
    GPIOB->MODER &= ~(3u << 22);
    int low = 0;
    for(volatile int i = 0; i < 4000; i++) /* a few ms at the 4 MHz reset clock */
        low = (GPIOB->IDR & (1u << 11)) ? 0 : low + 1;
    if(low > 2000) {
        /* put the pin back the way reset left it */
        GPIOB->MODER |= 3u << 22;
        GPIOB->PUPDR &= ~(3u << 22);
        RCC->AHB2ENR &= ~RCC_AHB2ENR_GPIOBEN;
        dfu_enter();
    }

    /* 3. C runtime */
    for(uint32_t *src = &_sidata, *dst = &_sdata; dst < &_edata;) *dst++ = *src++;
    for(uint32_t* dst = &_sbss; dst < &_ebss;) *dst++ = 0;
    main();
    for(;;) {
    }
}

/* ------------------------------------------------------------ faults */

void fault_report(const uint32_t* frame) {
    char l1[28], l2[28];
    /* stacked frame: r0 r1 r2 r3 r12 lr pc xpsr */
    fmt_snprintf(l1, sizeof(l1), "PC %08lX LR %08lX", (unsigned long)frame[6], (unsigned long)frame[5]);
    fmt_snprintf(l2, sizeof(l2), "CFSR %08lX", (unsigned long)SCB->CFSR);
    panic(l1, l2);
}

__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile("tst lr, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "b fault_report\n");
}

void Default_Handler(void) {
    char l2[28];
    fmt_snprintf(l2, sizeof(l2), "IRQ %ld", (long)(SCB->ICSR & 0x1FF) - 16);
    panic("Unexpected interrupt", l2);
}

void SysTick_Handler(void);
void TIM2_IRQHandler(void);
void EXTI2_IRQHandler(void);

/* every slot defaults to Default_Handler; the few used ones are overridden */
#pragma GCC diagnostic ignored "-Woverride-init"
#define VECTORS (16 + 63)
__attribute__((section(".isr_vector"), used)) static void (*const vector_table[VECTORS])(void) = {
    [0] = (void (*)(void))&_stack_top,
    [1] = Reset_Handler,
    [2] = Default_Handler, /* NMI */
    [3] = HardFault_Handler,
    [4] = HardFault_Handler, /* MemManage */
    [5] = HardFault_Handler, /* BusFault */
    [6] = HardFault_Handler, /* UsageFault */
    [11] = Default_Handler, /* SVCall */
    [12] = Default_Handler, /* DebugMon */
    [14] = Default_Handler, /* PendSV */
    [15] = SysTick_Handler,
    [16 ... VECTORS - 1] = Default_Handler,
    [16 + EXTI2_IRQn] = EXTI2_IRQHandler,
    [16 + TIM2_IRQn] = TIM2_IRQHandler,
};
