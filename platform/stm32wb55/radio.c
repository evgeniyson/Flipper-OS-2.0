/* hal/radio.h for the Flipper Zero: the on-board CC1101 and an optional
 * external CC1101 module on the GPIO header, both on SPI1 (one at a time).
 *
 * Edge timing:
 *   on-board: GD0 is wired to TIM2 channel 2, used in PWM-input mode with the
 *             timer's digital filter (same scheme as the stock firmware)
 *   external: GD0 arrives on PB2, which has no timer function, so edges are
 *             time-stamped in an EXTI interrupt against free-running TIM2
 * Transmission: TIM2 marks the end of each pulse; its interrupt sets the level
 * for the next one and reloads the period.
 */
#include "board.h"
#include "../../core/ringbuf.h"
#include "../../drivers/cc1101.h"
#include "../../hal/radio.h"
#include "../../subghz/region.h"

typedef struct {
    Pin sck, miso, mosi, cs, gd0;
    SpiSpeed speed;
} RadioPins;

static const RadioPins pins[RADIO_COUNT] = {
    [RADIO_INTERNAL] = {{GPIOA, 5}, {GPIOB, 4}, {GPIOB, 5}, {GPIOD, 0}, {GPIOA, 1}, SPI_DIV16},
    /* wires and breadboards: keep the clock slow */
    [RADIO_EXTERNAL] = {{GPIOB, 3}, {GPIOA, 6}, {GPIOA, 7}, {GPIOA, 4}, {GPIOB, 2}, SPI_DIV32},
};

typedef enum { MODE_OFF, MODE_CAPTURE_TIMER, MODE_CAPTURE_EXTI, MODE_TX } Mode;

static RadioId current = RADIO_INTERNAL;
static RadioId attached = RADIO_COUNT; /* which pin set SPI1 is routed to */
static volatile Mode mode = MODE_OFF;
static RingBuf rx_ring, tx_ring;
static volatile uint32_t last_high, last_edge;
static volatile bool tx_finishing, tx_running;
static uint32_t rx_bw_hz = 650000;
static int band = -1;
static bool ext_seen;
static uint32_t ext_probe_at;

/* ------------------------------------------------------------ bus */

static void bus_attach(RadioId id) {
    if(attached == id) return;
    if(attached < RADIO_COUNT) {
        /* release the other pin set; its chip-select stays high */
        const RadioPins* old = &pins[attached];
        pin_init(old->sck, PIN_ANALOG, PULL_NONE, false, 0);
        pin_init(old->mosi, PIN_ANALOG, PULL_NONE, false, 0);
        pin_init(old->miso, PIN_ANALOG, PULL_NONE, false, 0);
    }
    const RadioPins* p = &pins[id];
    pin_write(p->cs, true);
    pin_init(p->cs, PIN_OUTPUT, PULL_NONE, false, 0);
    pin_init(p->sck, PIN_ALT, PULL_NONE, false, 5);
    pin_init(p->mosi, PIN_ALT, PULL_NONE, false, 5);
    /* pulled up so that a missing chip reads as "never ready" */
    pin_init(p->miso, PIN_ALT, PULL_UP, false, 5);
    spi_init(SPI1, p->speed);
    attached = id;
}

static void bus_select(bool active) {
    pin_write(pins[attached].cs, !active);
}
static bool bus_wait_ready(void) {
    for(int i = 0; i < 400; i++) { /* the crystal needs up to ~1 ms after sleep */
        if(!pin_read(pins[attached].miso)) return true;
        delay_us(10);
    }
    return false;
}
static uint8_t bus_xfer(uint8_t byte) {
    return spi_xfer(SPI1, byte);
}
static const Cc1101Bus bus = {bus_select, bus_wait_ready, bus_xfer, delay_us};

/* ------------------------------------------------------------ interrupts */

static void timer_reset(void) {
    NVIC_DisableIRQ(TIM2_IRQn);
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    RCC->APB1RSTR1 |= RCC_APB1RSTR1_TIM2RST;
    RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_TIM2RST;
    TIM2->PSC = CPU_HZ / 1000000u - 1; /* 1 us per count */
}

void TIM2_IRQHandler(void) {
    uint32_t sr = TIM2->SR;
    if(mode == MODE_CAPTURE_TIMER) {
        /* falling edge: channel 1 holds the length of the HIGH that just ended */
        if(sr & TIM_SR_CC1IF) {
            uint32_t high = TIM2->CCR1;
            last_high = high;
            if(high) rb_push(&rx_ring, (int32_t)high);
        }
        /* rising edge: channel 2 holds the whole period, the counter restarted */
        if(sr & TIM_SR_CC2IF) {
            uint32_t period = TIM2->CCR2;
            if(period > last_high) rb_push(&rx_ring, -(int32_t)(period - last_high));
            last_high = 0;
        }
        /* reading CCRx cleared the capture flags; drop only the over-capture
         * and update flags so that no new edge is lost */
        TIM2->SR = ~(TIM_SR_CC1OF | TIM_SR_CC2OF | TIM_SR_UIF | TIM_SR_TIF);
    } else if(mode == MODE_TX) {
        TIM2->SR = 0;
        int32_t pulse;
        if(rb_pop(&tx_ring, &pulse)) {
            uint32_t dur = pulse > 0 ? (uint32_t)pulse : (uint32_t)(-pulse);
            if(dur < 30) dur = 30; /* the period must outlast this handler */
            if(dur > 1000000) dur = 1000000;
            pin_write(pins[current].gd0, pulse > 0);
            TIM2->ARR = dur - 1;
        } else {
            pin_write(pins[current].gd0, false);
            if(tx_finishing) {
                TIM2->CR1 &= ~TIM_CR1_CEN;
                tx_running = false;
            } else {
                TIM2->ARR = 199; /* queue is empty: carrier off, look again shortly */
            }
        }
    } else {
        TIM2->SR = 0;
    }
}

void EXTI2_IRQHandler(void) {
    EXTI->PR1 = EXTI_PR1_PIF2;
    if(mode != MODE_CAPTURE_EXTI) return;
    uint32_t now = TIM2->CNT;
    uint32_t dur = now - last_edge;
    last_edge = now;
    /* the level after the edge tells which level just ended */
    if(dur) rb_push(&rx_ring, pin_read(pins[RADIO_EXTERNAL].gd0) ? -(int32_t)dur : (int32_t)dur);
}

static void stop_timing(void) {
    NVIC_DisableIRQ(TIM2_IRQn);
    NVIC_DisableIRQ(EXTI2_IRQn);
    EXTI->IMR1 &= ~EXTI_IMR1_IM2;
    TIM2->CR1 &= ~TIM_CR1_CEN;
    TIM2->DIER = 0;
    mode = MODE_OFF;
    tx_running = false;
    /* GD0 back to high impedance on our side */
    pin_init(pins[current].gd0, PIN_ANALOG, PULL_NONE, false, 0);
}

/* ------------------------------------------------------------ device selection */

void radio_init(void) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    (void)RCC->APB1ENR1;
    pin_write(PIN_RF_SW, false);
    pin_init(PIN_RF_SW, PIN_OUTPUT, PULL_NONE, false, 0);
    /* the external chip-select is only driven once that module is used */
    bus_attach(RADIO_INTERNAL);
    cc1101_reset(&bus);
    cc1101_sleep(&bus);
}

bool radio_present(RadioId id) {
    if(id == RADIO_EXTERNAL && id != current) {
        /* probing drives header pins: do it at most once a second */
        if(ext_probe_at && millis() - ext_probe_at < 1000) return ext_seen;
        ext_probe_at = millis() | 1;
    }
    if(mode != MODE_OFF) return id == current; /* busy: do not disturb the bus */
    RadioId back = attached;
    bus_attach(id);
    bool ok = cc1101_probe(&bus);
    if(ok) cc1101_sleep(&bus); /* the probe woke the chip up */
    if(id == RADIO_EXTERNAL) {
        ext_seen = ok;
        if(!ok && current != RADIO_EXTERNAL) pin_init(pins[id].cs, PIN_ANALOG, PULL_NONE, false, 0);
    }
    if(back < RADIO_COUNT) bus_attach(back);
    return ok;
}

bool radio_select(RadioId id) {
    if(id == current && attached == id) return true;
    if(!radio_present(id)) return false;
    stop_timing();
    /* park the radio we are leaving */
    cc1101_sleep(&bus);
    current = id;
    bus_attach(id);
    cc1101_reset(&bus);
    cc1101_sleep(&bus);
    band = -1;
    return true;
}

RadioId radio_current(void) {
    return current;
}

/* ------------------------------------------------------------ configuration */

/* The on-board antenna has three matching networks, chosen by one MCU pin
 * and the CC1101's GD2 output. */
static void set_path(int new_band) {
    if(current != RADIO_INTERNAL) return;
    static const struct {
        bool sw;
        uint8_t iocfg2;
    } paths[4] = {
        {true, CC1101_IOCFG_HW}, /* 300-348 MHz */
        {false, CC1101_IOCFG_HW | CC1101_IOCFG_INV}, /* 387-464 MHz */
        {true, CC1101_IOCFG_HW | CC1101_IOCFG_INV}, /* 779-928 MHz */
        {false, CC1101_IOCFG_HW}, /* isolated */
    };
    int idx = new_band >= 0 && new_band <= 2 ? new_band : 3;
    pin_write(PIN_RF_SW, paths[idx].sw);
    cc1101_write(&bus, CC1101_IOCFG2, paths[idx].iocfg2);
}

void radio_configure(const SgPreset* preset) {
    stop_timing();
    bus_attach(current);
    cc1101_reset(&bus);
    cc1101_load(&bus, preset->regs, preset->patable);
    for(const uint8_t* r = preset->regs; r[0] || r[1]; r += 2)
        if(r[0] == CC1101_MDMCFG4)
            rx_bw_hz = CC1101_XTAL_HZ / (8u * (4u + ((r[1] >> 4) & 3u)) * (1u << (r[1] >> 6)));
    band = -1;
    set_path(-1);
}

uint32_t radio_set_frequency(uint32_t hz) {
    cc1101_idle(&bus);
    uint32_t actual = cc1101_set_frequency(&bus, hz);
    int new_band = sg_freq_band(hz);
    if(new_band != band) {
        band = new_band;
        set_path(band);
    }
    return actual;
}

void radio_rx(void) {
    cc1101_rx(&bus);
}

void radio_idle(void) {
    stop_timing();
    cc1101_idle(&bus);
}

void radio_sleep(void) {
    stop_timing();
    set_path(-1);
    band = -1;
    cc1101_sleep(&bus);
}

int radio_rssi(void) {
    return cc1101_rssi_dbm(&bus);
}

int radio_measure(uint32_t hz) {
    radio_set_frequency(hz);
    cc1101_rx(&bus); /* includes the synthesizer calibration */
    /* the RSSI filter needs longer to settle the narrower the channel filter is */
    delay_us(rx_bw_hz >= 200000 ? 1000 : 2500);
    return cc1101_rssi_dbm(&bus);
}

/* ------------------------------------------------------------ capture */

void radio_capture_start(void) {
    stop_timing();
    rb_reset(&rx_ring);
    last_high = 0;
    timer_reset();
    if(current == RADIO_INTERNAL) {
        pin_init(pins[current].gd0, PIN_ALT, PULL_NONE, false, 1); /* TIM2_CH2 */
        TIM2->ARR = 0x7FFFFFFE;
        TIM2->CR1 = TIM_CR1_CKD_1; /* filter clock = timer clock / 4 */
        /* IC1 <- TI2 (falling), IC2 <- TI2 (rising, 16 us glitch filter) */
        TIM2->CCMR1 = (2u << TIM_CCMR1_CC1S_Pos) | (1u << TIM_CCMR1_CC2S_Pos) | (0xFu << TIM_CCMR1_IC2F_Pos);
        TIM2->CCER = TIM_CCER_CC1P | TIM_CCER_CC1E | TIM_CCER_CC2E;
        /* restart the counter on every rising edge */
        TIM2->SMCR = (6u << TIM_SMCR_TS_Pos) | (4u << TIM_SMCR_SMS_Pos) | TIM_SMCR_MSM;
        TIM2->EGR = TIM_EGR_UG; /* latch the prescaler */
        TIM2->SR = 0;
        TIM2->DIER = TIM_DIER_CC1IE | TIM_DIER_CC2IE;
        mode = MODE_CAPTURE_TIMER;
        NVIC_SetPriority(TIM2_IRQn, 0);
        NVIC_EnableIRQ(TIM2_IRQn);
        TIM2->CR1 |= TIM_CR1_CEN;
    } else {
        pin_init(pins[current].gd0, PIN_INPUT, PULL_NONE, false, 0);
        TIM2->ARR = 0xFFFFFFFFu;
        TIM2->EGR = TIM_EGR_UG;
        TIM2->SR = 0;
        TIM2->CR1 = TIM_CR1_CEN;
        last_edge = TIM2->CNT;
        SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~SYSCFG_EXTICR1_EXTI2) | SYSCFG_EXTICR1_EXTI2_PB;
        EXTI->RTSR1 |= EXTI_RTSR1_RT2;
        EXTI->FTSR1 |= EXTI_FTSR1_FT2;
        EXTI->PR1 = EXTI_PR1_PIF2;
        mode = MODE_CAPTURE_EXTI;
        EXTI->IMR1 |= EXTI_IMR1_IM2;
        NVIC_SetPriority(EXTI2_IRQn, 0);
        NVIC_EnableIRQ(EXTI2_IRQn);
    }
}

void radio_capture_stop(void) {
    if(mode == MODE_CAPTURE_TIMER || mode == MODE_CAPTURE_EXTI) stop_timing();
}

size_t radio_capture_read(sg_pulse_t* out, size_t max) {
    size_t n = 0;
    while(n < max && rb_pop(&rx_ring, &out[n])) n++;
    return n;
}

bool radio_capture_overflowed(void) {
    bool o = rx_ring.overflow;
    rx_ring.overflow = false;
    return o;
}

uint32_t radio_capture_quiet_us(void) {
    if(mode == MODE_CAPTURE_TIMER) {
        /* the counter restarts on each rising edge */
        uint32_t cnt = TIM2->CNT, high = last_high;
        return cnt > high ? cnt - high : 0;
    }
    if(mode == MODE_CAPTURE_EXTI) return TIM2->CNT - last_edge;
    return 0;
}

/* ------------------------------------------------------------ transmit */

bool radio_tx_start(void) {
    stop_timing();
    rb_reset(&tx_ring);
    tx_finishing = false;
    /* in asynchronous mode the chip reads its data from GD0 while transmitting */
    pin_write(pins[current].gd0, false);
    pin_init(pins[current].gd0, PIN_OUTPUT, PULL_NONE, false, 0);
    if(!cc1101_tx(&bus)) {
        cc1101_idle(&bus);
        pin_init(pins[current].gd0, PIN_ANALOG, PULL_NONE, false, 0);
        return false;
    }
    timer_reset();
    TIM2->ARR = 199;
    TIM2->CR1 = TIM_CR1_URS;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM2->DIER = TIM_DIER_UIE;
    mode = MODE_TX;
    tx_running = true;
    NVIC_SetPriority(TIM2_IRQn, 0);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 |= TIM_CR1_CEN;
    return true;
}

bool radio_tx_push(sg_pulse_t pulse) {
    if(mode != MODE_TX || rb_count(&tx_ring) >= RB_SIZE) return false;
    rb_push(&tx_ring, pulse);
    return true;
}

size_t radio_tx_space(void) {
    return RB_SIZE - rb_count(&tx_ring);
}

void radio_tx_finish(void) {
    tx_finishing = true;
}

bool radio_tx_busy(void) {
    return mode == MODE_TX && tx_running;
}

void radio_tx_stop(void) {
    if(mode != MODE_TX) return;
    pin_write(pins[current].gd0, false);
    stop_timing();
    cc1101_idle(&bus);
}

/* Called on fatal errors and before power-off: make sure nothing keeps transmitting. */
void radio_panic_stop(void) {
    NVIC_DisableIRQ(TIM2_IRQn);
    NVIC_DisableIRQ(EXTI2_IRQn);
    if(RCC->APB1ENR1 & RCC_APB1ENR1_TIM2EN) TIM2->CR1 &= ~TIM_CR1_CEN;
    if(attached >= RADIO_COUNT) return; /* radio not set up yet */
    pin_write(pins[current].gd0, false);
    mode = MODE_OFF;
    cc1101_strobe(&bus, CC1101_SIDLE);
    cc1101_strobe(&bus, CC1101_SPWD);
}
