#pragma once
/* The single gate every transmission goes through. */
#include "../hal/radio.h"
#include "../subghz/protocol.h"

typedef enum {
    TX_OK,
    TX_NO_REGION, /* no region chosen in the settings yet */
    TX_FREQ_BLOCKED, /* frequency is outside the region's licence-free bands */
    TX_BAD_FREQ, /* the radio cannot tune there */
    TX_ROLLING, /* rolling codes are never transmitted */
    TX_FAILED,
} TxResult;

TxResult tx_check(uint32_t hz);
/* Configure the radio and start keying. Then queue pulses with
 * radio_tx_push(), call radio_tx_finish() and wait for !radio_tx_busy(). */
TxResult tx_begin(uint32_t hz, const SgPreset* preset);
void tx_end(void);
/* Queue `repeats` frames of a fixed code and start sending. */
TxResult tx_send_key(uint32_t hz, const SgPreset* preset, const SgDecoded* key, int repeats);
const char* tx_result_text(TxResult r);
