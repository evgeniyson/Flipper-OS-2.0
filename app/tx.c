#include "tx.h"
#include "rx.h"
#include "settings.h"
#include "../subghz/region.h"

TxResult tx_check(uint32_t hz) {
    if(!sg_freq_valid(hz)) return TX_BAD_FREQ;
    if(settings.region == SG_REGION_NONE) return TX_NO_REGION;
    if(!sg_region_tx_allowed((SgRegion)settings.region, hz)) return TX_FREQ_BLOCKED;
    return TX_OK;
}

TxResult tx_begin(uint32_t hz, const SgPreset* preset) {
    TxResult r = tx_check(hz);
    if(r != TX_OK) return r;
    rx_stop();
    radio_configure(preset);
    radio_set_frequency(hz);
    if(!radio_tx_start()) {
        radio_idle();
        radio_sleep();
        return TX_FAILED;
    }
    return TX_OK;
}

void tx_end(void) {
    radio_tx_stop();
    radio_idle();
    radio_sleep();
}

TxResult tx_send_key(uint32_t hz, const SgPreset* preset, const SgDecoded* key, int repeats) {
    static sg_pulse_t frame[160];
    if(!key->proto || key->proto->cls != SG_CLASS_STATIC) return TX_ROLLING;
    size_t n = sg_encode(key, frame, 160);
    if(n == 0) return TX_ROLLING;
    TxResult r = tx_begin(hz, preset);
    if(r != TX_OK) return r;
    for(int rep = 0; rep < repeats; rep++)
        for(size_t i = 0; i < n; i++)
            if(!radio_tx_push(frame[i])) break;
    radio_tx_finish();
    return TX_OK;
}

const char* tx_result_text(TxResult r) {
    switch(r) {
    case TX_OK:
        return "Sent";
    case TX_NO_REGION:
        return "Set region in Settings";
    case TX_FREQ_BLOCKED:
        return "Freq not allowed here";
    case TX_BAD_FREQ:
        return "Bad frequency";
    case TX_ROLLING:
        return "Rolling code: no TX";
    default:
        return "TX failed";
    }
}
