/* Unit tests of the Sub-GHz engine on synthetic signals. */
#include "test.h"
#include "subghz/analyzer.h"
#include "subghz/presets.h"
#include "subghz/protocol.h"
#include "subghz/region.h"
#include "subghz/sub_file.h"

/* ---------------------------------------------------------------- helpers */

static uint32_t rng_state = 12345;
static uint32_t rng(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

#define MAXP 8192
static sg_pulse_t wave[MAXP];
static size_t wave_n;

static void wave_reset(void) {
    wave_n = 0;
}
static void emit(int32_t v) {
    if(wave_n && (wave[wave_n - 1] > 0) == (v > 0))
        wave[wave_n - 1] += v;
    else if(wave_n < MAXP)
        wave[wave_n++] = v;
}
/* scale every duration by 1 +- pct% and add +-abs_us of noise */
static void jitter(int pct, int abs_us) {
    for(size_t i = 0; i < wave_n; i++) {
        int32_t v = wave[i] < 0 ? -wave[i] : wave[i];
        int32_t scale = 100 + (int32_t)(rng() % (2 * pct + 1)) - pct;
        v = v * scale / 100 + (int32_t)(rng() % (2 * abs_us + 1)) - abs_us;
        if(v < 30) v = 30;
        wave[i] = wave[i] < 0 ? -v : v;
    }
}

static SgDecoded results[64];
static int n_results, n_packets;
static SgPacket last_packet;

static void on_packet(const SgPacket* pkt, void* ctx) {
    (void)ctx;
    n_packets++;
    last_packet = *pkt;
    SgDecoded d;
    if(sg_decode_packet(pkt, &d) && n_results < 64) results[n_results++] = d;
}

static void run_wave(void) {
    static SgPacketizer z;
    n_results = n_packets = 0;
    sg_packetizer_init(&z, 0, on_packet, NULL);
    for(size_t i = 0; i < wave_n; i++) sg_packetizer_feed(&z, wave[i]);
    sg_packetizer_flush(&z);
}

/* ---------------------------------------------------------------- packetizer */

static void test_packetizer(void) {
    wave_reset();
    emit(-20000);
    for(int i = 0; i < 20; i++) {
        emit(400);
        emit(-400);
    }
    emit(-9000); /* merges with the last low: a 9400us gap */
    for(int i = 0; i < 10; i++) {
        emit(300);
        emit(-900);
    }
    run_wave();
    CHECK_EQ(n_packets, 2);
    CHECK_EQ(last_packet.n, 19); /* trailing low folded into the gap */
    CHECK_EQ(last_packet.gap_before, 9400);
    CHECK_EQ(last_packet.gap_after, 0); /* flushed: unknown */

    /* short bursts are noise */
    wave_reset();
    for(int i = 0; i < 5; i++) {
        emit(100);
        emit(-100);
    }
    emit(-10000);
    run_wave();
    CHECK_EQ(n_packets, 0);

    /* an endless burst is cut, reported as truncated and never decoded */
    wave_reset();
    for(int i = 0; i < 3000; i++) {
        emit(200);
        emit(-200);
    }
    emit(-10000);
    run_wave();
    CHECK_EQ(n_packets, 1);
    CHECK(last_packet.truncated);
    CHECK_EQ(n_results, 0);
}

/* ---------------------------------------------------------------- static codes */

static void roundtrip(const char* name, uint64_t key, uint8_t bits, uint16_t te, int jit_pct) {
    const SgProtocol* proto = sg_protocol_by_name(name);
    CHECK(proto != NULL);
    if(!proto) return;
    SgDecoded in = {.proto = proto, .data = key, .bits = bits, .te = te};
    sg_pulse_t frame[160];
    size_t n = sg_encode(&in, frame, 160);
    CHECK(n > 0);
    wave_reset();
    emit(-30000);
    for(int rep = 0; rep < 5; rep++)
        for(size_t i = 0; i < n; i++) emit(frame[i]);
    emit(-30000);
    /* CAME 12 bit and Holtek HT12X differ only in the header length (47 vs 36
     * te), so they cannot tolerate as much independent timing noise. */
    if(bits == 12 && te < 500 && jit_pct > 5) jit_pct = 5;
    if(jit_pct) jitter(jit_pct, 15);
    run_wave();
    /* the first or last frame may lack its header or guard; the rest must decode */
    if(n_results < 3) {
        printf("  %s %ubit key %llX te %u jitter %d%%: only %d frames decoded\n", name, bits,
               (unsigned long long)key, te, jit_pct, n_results);
        CHECK(n_results >= 3);
    }
    for(int i = 0; i < n_results; i++) {
        if(results[i].proto != proto || results[i].data != key || results[i].bits != bits) {
            printf("  %s: got %s %ubit %llX\n", name, results[i].proto->name, results[i].bits,
                   (unsigned long long)results[i].data);
            CHECK(false);
        }
    }
}

static void test_static_roundtrip(void) {
    for(int jit = 0; jit <= 10; jit += 5) {
        for(int k = 0; k < 20; k++) {
            uint64_t r = ((uint64_t)rng() << 20) ^ rng();
            roundtrip("Princeton", r & 0xFFFFFF, 24, 390, jit);
            roundtrip("Princeton", r & 0xFFFFFF, 24, 250, jit);
            roundtrip("SMC5326", r & 0x1FFFFFF, 25, 300, jit);
            roundtrip("Linear", r & 0x3FF, 10, 500, jit);
            roundtrip("Holtek_HT12X", r & 0xFFF, 12, 320, jit);
            roundtrip("GateTX", r & 0xFFFFFF, 24, 350, jit);
            roundtrip("CAME", r & 0xFFF, 12, 320, jit);
            roundtrip("CAME", r & 0xFFFFFF, 24, 320, jit);
            roundtrip("Nice FLO", r & 0xFFF, 12, 700, jit);
            roundtrip("Nice FLO", r & 0xFFFFFF, 24, 700, jit);
        }
    }
    /* edge patterns */
    roundtrip("Princeton", 0x000000, 24, 390, 0);
    roundtrip("Princeton", 0xFFFFFF, 24, 390, 0);
    roundtrip("CAME", 0x000, 12, 320, 0);
    roundtrip("CAME", 0xFFF, 12, 320, 0);
}

static void test_no_tx_for_rolling(void) {
    /* rolling-code and sensor protocols must never produce a waveform */
    for(size_t i = 0; i < sg_protocol_count; i++) {
        const SgProtocol* p = sg_protocols[i];
        SgDecoded d = {.proto = p, .data = 0x123456, .bits = 24, .te = 400};
        sg_pulse_t out[400];
        size_t n = sg_encode(&d, out, 400);
        if(p->cls == SG_CLASS_STATIC)
            CHECK(n > 0);
        else
            CHECK_EQ(n, 0);
    }
}

/* ---------------------------------------------------------------- rolling codes */

static void synth_keeloq(uint32_t hop, uint32_t serial, uint8_t btn, int te) {
    uint64_t v = ((uint64_t)((uint32_t)btn << 28 | serial) << 32) | hop;
    for(int i = 0; i < 11; i++) {
        emit(te);
        emit(-te);
    }
    emit(te);
    emit(-10 * te);
    for(int i = 0; i < 66; i++) {
        bool one = i < 64 ? (v >> i) & 1 : 0;
        emit(one ? te : 2 * te);
        emit(one ? -2 * te : -te);
    }
    emit(-40 * te);
}

static void test_keeloq(void) {
    wave_reset();
    emit(-50000);
    for(int i = 0; i < 4; i++) synth_keeloq(0xDEADBEEF, 0x0ABCDEF, 0x9, 400);
    jitter(8, 20);
    run_wave();
    CHECK(n_results >= 3);
    for(int i = 0; i < n_results; i++) {
        CHECK(strcmp(results[i].proto->name, "KeeLoq") == 0);
        CHECK_EQ(results[i].serial, 0x0ABCDEF);
        CHECK_EQ(results[i].btn, 0x9);
        CHECK_EQ(results[i].hop, 0xDEADBEEF);
        CHECK(results[i].proto->encode == NULL);
    }
}

static void synth_somfy(uint8_t ctrl, uint16_t code, uint32_t addr) {
    uint8_t b[7] = {0xA7, (uint8_t)(ctrl << 4), (uint8_t)(code >> 8), (uint8_t)code,
                    (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    uint8_t ck = 0;
    for(int i = 0; i < 7; i++) ck ^= (uint8_t)(b[i] ^ (b[i] >> 4));
    b[1] |= ck & 0xF;
    for(int i = 1; i < 7; i++) b[i] ^= b[i - 1];
    for(int i = 0; i < 2; i++) {
        emit(2560);
        emit(-2560);
    }
    emit(4550);
    emit(-640);
    for(int k = 0; k < 56; k++) {
        bool bit = (b[k / 8] >> (7 - k % 8)) & 1;
        emit(bit ? -640 : 640); /* rising edge in the middle = 1 */
        emit(bit ? 640 : -640);
    }
    emit(-30000);
}

static void test_somfy(void) {
    wave_reset();
    emit(-50000);
    for(int i = 0; i < 3; i++) synth_somfy(0x2, 0x1234, 0xABCDEF);
    jitter(6, 20);
    run_wave();
    CHECK(n_results >= 2);
    for(int i = 0; i < n_results; i++) {
        CHECK(strcmp(results[i].proto->name, "Somfy Telis") == 0);
        CHECK_EQ(results[i].btn, 0x2);
        CHECK_EQ(results[i].cnt, 0x1234);
        CHECK_EQ(results[i].serial, 0xABCDEF);
    }
}

/* ---------------------------------------------------------------- weather */

static void ppm_bits(uint64_t v, int bits, int pulse, int zero, int one) {
    for(int i = bits - 1; i >= 0; i--) {
        emit(pulse);
        emit(-((v >> i) & 1 ? one : zero));
    }
}
static void pwm_bits(uint64_t v, int bits, int te_s, int te_l) {
    for(int i = bits - 1; i >= 0; i--) {
        bool one = (v >> i) & 1;
        emit(one ? te_l : te_s);
        emit(-(one ? te_s : te_l));
    }
}

static void test_nexus(void) {
    /* id 0x5A, battery ok, channel 2, -5.3 C, 47 % */
    uint64_t v = ((uint64_t)0x5A << 28) | (1ull << 27) | (1ull << 24) | ((uint64_t)((-53) & 0xFFF) << 12) |
                 (0xFull << 8) | 47;
    wave_reset();
    emit(-50000);
    for(int i = 0; i < 12; i++) {
        ppm_bits(v, 36, 500, 1000, 2000);
        emit(500);
        emit(-4000);
    }
    emit(-50000);
    jitter(6, 20);
    run_wave();
    CHECK_EQ(n_results, 1);
    if(n_results) {
        const SgDecoded* d = &results[0];
        CHECK(strcmp(d->proto->name, "Nexus-TH") == 0);
        CHECK_EQ(d->id, 0x5A);
        CHECK_EQ(d->channel, 2);
        CHECK_EQ(d->temp_c10, -53);
        CHECK_EQ(d->humidity, 47);
        CHECK(!d->battery_low);
    }
}

static void test_gt_wt02(void) {
    /* id 0x34, battery ok, channel 1, +23.7 C, 35 %: example from the protocol notes */
    uint64_t v = 0x3400ED4760ull >> 3; /* 37 bits, left aligned in 40 */
    wave_reset();
    emit(-50000);
    for(int i = 0; i < 4; i++) {
        ppm_bits(v, 37, 500, 2000, 4000);
        emit(500);
        emit(-9000);
    }
    jitter(5, 20);
    run_wave();
    CHECK(n_results >= 3);
    if(n_results) {
        const SgDecoded* d = &results[0];
        CHECK(strcmp(d->proto->name, "GT-WT02") == 0);
        CHECK_EQ(d->id, 0x34);
        CHECK_EQ(d->channel, 1);
        CHECK_EQ(d->temp_c10, 237);
        CHECK_EQ(d->humidity, 35);
        CHECK(!d->battery_low);
    }
}

static void test_acurite(void) {
    /* channel A, id 0x0ABC, battery ok, 55 %, 21.5 C */
    uint32_t temp = 215 + 1000;
    uint8_t b[7] = {(uint8_t)(0xC0 | 0x0A), 0xBC, 0x44, 55, (uint8_t)(temp >> 7), (uint8_t)(temp & 0x7F), 0};
    for(int i = 0; i < 6; i++) b[6] = (uint8_t)(b[6] + b[i]);
    uint64_t v = 0;
    for(int i = 0; i < 7; i++) v = v << 8 | b[i];
    wave_reset();
    emit(-50000);
    for(int rep = 0; rep < 3; rep++) {
        for(int i = 0; i < 4; i++) {
            emit(600);
            emit(-600);
        }
        pwm_bits(v, 56, 200, 400);
    }
    emit(-50000);
    jitter(5, 15);
    run_wave();
    CHECK_EQ(n_results, 1);
    if(n_results) {
        const SgDecoded* d = &results[0];
        CHECK(strcmp(d->proto->name, "Acurite 592TXR") == 0);
        CHECK_EQ(d->id, 0x0ABC);
        CHECK_EQ(d->channel, 1);
        CHECK_EQ(d->temp_c10, 215);
        CHECK_EQ(d->humidity, 55);
    }
}

/* ---------------------------------------------------------------- analyzer */

static void analyse_wave(SgAnalysis* a) {
    run_wave();
    CHECK(n_packets >= 1);
    sg_analyze(&last_packet, a);
}

static void test_analyzer(void) {
    static SgAnalysis a;
    char hex[160];

    /* PWM 1:3 with an unusual length no decoder knows */
    wave_reset();
    emit(-20000);
    pwm_bits(0x11A5F3C7ull, 29, 250, 750);
    emit(250);
    emit(-20000);
    jitter(5, 10);
    analyse_wave(&a);
    CHECK_EQ(a.enc, SG_ENC_PWM);
    CHECK_EQ(a.n_bits, 29);
    sg_bits_to_hex(a.bits, a.n_bits, hex, sizeof(hex));
    CHECK(strcmp(hex, "8D2F9E38") == 0); /* 0x11A5F3C7 << 3: left aligned */
    CHECK(a.te > 220 && a.te < 280);
    CHECK_EQ(a.n_hi, 2);

    /* PPM */
    wave_reset();
    emit(-20000);
    ppm_bits(0xC3A55Aull, 24, 400, 800, 1600);
    emit(400);
    emit(-20000);
    jitter(5, 10);
    analyse_wave(&a);
    CHECK_EQ(a.enc, SG_ENC_PPM);
    CHECK_EQ(a.n_bits, 24);
    sg_bits_to_hex(a.bits, a.n_bits, hex, sizeof(hex));
    CHECK(strcmp(hex, "C3A55A") == 0);

    /* Manchester, HIGH->LOW = 1 */
    wave_reset();
    emit(-20000);
    uint32_t word = 0xB38F0C5A;
    for(int i = 31; i >= 0; i--) {
        bool bit = (word >> i) & 1;
        emit(bit ? 500 : -500);
        emit(bit ? -500 : 500);
    }
    emit(-20000);
    jitter(4, 10);
    analyse_wave(&a);
    CHECK_EQ(a.enc, SG_ENC_MANCHESTER);
    sg_bits_to_hex(a.bits, 32, hex, sizeof(hex));
    CHECK(strcmp(hex, "B38F0C5A") == 0);
    CHECK(a.baud > 1800 && a.baud < 2200);

    /* bit difference between two presses of a remote */
    uint8_t x[4] = {0xDE, 0xAD, 0xBE, 0xEF}, y[4] = {0xDE, 0xAD, 0xBF, 0xE0}, mask[4];
    CHECK_EQ(sg_bits_diff(x, y, 32, mask), 5);
    CHECK_EQ(mask[2], 0x01);
    CHECK_EQ(mask[3], 0x0F);
    CHECK_EQ(sg_bits_diff(x, y, 23, mask), 0);
}

/* ---------------------------------------------------------------- .sub files */

static char file_buf[65536];
static size_t file_len;
static int file_write(void* ctx, const char* data, size_t len) {
    (void)ctx;
    if(file_len + len > sizeof(file_buf)) return -1;
    memcpy(file_buf + file_len, data, len);
    file_len += len;
    return (int)len;
}

static sg_pulse_t parsed[MAXP];
static size_t parsed_n;
static void on_pulse(sg_pulse_t p, void* ctx) {
    (void)ctx;
    if(parsed_n < MAXP) parsed[parsed_n++] = p;
}

static void test_sub_file(void) {
    /* RAW: write, then read back in awkward chunk sizes */
    wave_reset();
    for(int i = 0; i < 1500; i++) emit((i & 1 ? -1 : 1) * (int32_t)(100 + rng() % 30000));
    SubWriter w;
    file_len = 0;
    CHECK(sub_writer_begin_raw(&w, file_write, NULL, 433920000, "FuriHalSubGhzPresetOok650Async"));
    for(size_t i = 0; i < wave_n; i++) sub_writer_pulse(&w, wave[i]);
    CHECK(sub_writer_end(&w));
    CHECK_EQ(w.total, wave_n);

    for(size_t chunk = 1; chunk <= 4096; chunk = chunk * 7 + 1) {
        static SubParser p;
        parsed_n = 0;
        sub_parser_init(&p, on_pulse, NULL);
        for(size_t off = 0; off < file_len; off += chunk)
            sub_parser_feed(&p, file_buf + off, off + chunk > file_len ? file_len - off : chunk);
        sub_parser_finish(&p);
        CHECK(p.info.filetype_ok);
        CHECK(p.info.is_raw);
        CHECK_EQ(p.info.frequency, 433920000);
        CHECK(strcmp(p.info.preset, "FuriHalSubGhzPresetOok650Async") == 0);
        CHECK_EQ(parsed_n, wave_n);
        CHECK(memcmp(parsed, wave, wave_n * sizeof(sg_pulse_t)) == 0);
    }

    /* key file */
    SgDecoded d = {.proto = sg_protocol_by_name("Princeton"), .data = 0x95D5D4, .bits = 24, .te = 400};
    file_len = 0;
    CHECK(sub_write_key(file_write, NULL, 433920000, "FuriHalSubGhzPresetOok650Async", &d));
    static SubParser p;
    sub_parser_init(&p, NULL, NULL);
    sub_parser_feed(&p, file_buf, file_len);
    sub_parser_finish(&p);
    SgDecoded back;
    CHECK(!p.info.is_raw);
    CHECK(sub_info_to_decoded(&p.info, &back));
    CHECK(back.proto == d.proto);
    CHECK_EQ(back.data, 0x95D5D4);
    CHECK_EQ(back.bits, 24);
    CHECK_EQ(back.te, 400);
    CHECK_EQ(back.serial, 0x95D5D);
    CHECK_EQ(back.btn, 4);

    /* CRLF line ends, comments and a custom preset */
    const char* text = "Filetype: Flipper SubGhz RAW File\r\nVersion: 1\r\n# comment\r\n"
                       "Frequency: 868350000\r\nPreset: FuriHalSubGhzPresetCustom\r\n"
                       "Custom_preset_module: CC1101\r\nCustom_preset_data: 02 0D 10 67 00 00 C0\r\n"
                       "Protocol: RAW\r\nRAW_Data: 100 -200 300\r\nRAW_Data: -400 500";
    parsed_n = 0;
    sub_parser_init(&p, on_pulse, NULL);
    sub_parser_feed(&p, text, strlen(text));
    sub_parser_finish(&p);
    CHECK_EQ(p.info.frequency, 868350000);
    CHECK_EQ(p.info.custom_len, 7);
    CHECK_EQ(p.info.custom[3], 0x67);
    CHECK_EQ(parsed_n, 5);
    CHECK_EQ(parsed[3], -400);
    CHECK_EQ(parsed[4], 500);
}

/* ---------------------------------------------------------------- radio maths */

static void test_presets(void) {
    uint32_t actual;
    CHECK_EQ(sg_cc1101_rx_bw_bits(270000, &actual), 0x60);
    CHECK(actual > 270000 && actual < 271000);
    CHECK_EQ(sg_cc1101_rx_bw_bits(650000, &actual), 0x10);
    CHECK_EQ(sg_cc1101_rx_bw_bits(58000, &actual), 0xF0);
    CHECK_EQ(sg_cc1101_deviation_bits(2380, &actual), 0x04);
    CHECK_EQ(sg_cc1101_deviation_bits(47607, &actual), 0x47);
    uint8_t e, m;
    sg_cc1101_data_rate_bits(3794, &e, &m, &actual);
    CHECK_EQ(e, 0x7);
    CHECK_EQ(m, 0x32);
    /* 433.92 MHz -> 0x10B071 per the datasheet formula */
    CHECK_EQ(sg_cc1101_freq_word(433920000), 0x10B071);
    uint32_t hz = sg_cc1101_freq_hz(0x10B071);
    CHECK(hz > 433919000 && hz < 433921000);

    CHECK(sg_preset_by_sub_name("FuriHalSubGhzPresetOok650Async") != NULL);
    CHECK(sg_preset_by_sub_name("nope") == NULL);

    SgCustomPreset c;
    SgCustomParams want = {.fsk = true, .rx_bw_hz = 100000, .deviation_hz = 20000, .data_rate = 9600};
    sg_preset_build_custom(&c, &want);
    CHECK(c.actual.rx_bw_hz > 95000 && c.actual.rx_bw_hz < 105000);
    CHECK(c.actual.deviation_hz > 19000 && c.actual.deviation_hz < 21000);
    CHECK(c.actual.data_rate > 9500 && c.actual.data_rate < 9700);
    CHECK_EQ(c.preset.patable[0], 0xC0);
    int pairs = 0;
    while(c.preset.regs[2 * pairs] || c.preset.regs[2 * pairs + 1]) pairs++;
    CHECK(pairs > 10 && pairs * 2 + 2 <= SG_CUSTOM_REGS_MAX);
}

static void test_region(void) {
    CHECK(sg_freq_valid(433920000));
    CHECK(sg_freq_valid(315000000));
    CHECK(sg_freq_valid(868350000));
    CHECK(!sg_freq_valid(350000000));
    CHECK(!sg_freq_valid(500000000));
    CHECK_EQ(sg_freq_band(315000000), 0);
    CHECK_EQ(sg_freq_band(433920000), 1);
    CHECK_EQ(sg_freq_band(915000000), 2);

    /* nothing may be transmitted until a region is chosen */
    for(size_t i = 0; i < sg_freq_list_count; i++) CHECK(!sg_region_tx_allowed(SG_REGION_NONE, sg_freq_list[i]));
    CHECK(sg_region_tx_allowed(SG_REGION_EU_RU, 433920000));
    CHECK(sg_region_tx_allowed(SG_REGION_EU_RU, 868350000));
    CHECK(!sg_region_tx_allowed(SG_REGION_EU_RU, 315000000));
    CHECK(!sg_region_tx_allowed(SG_REGION_EU_RU, 915000000));
    CHECK(!sg_region_tx_allowed(SG_REGION_EU_RU, 433000000));
    CHECK(sg_region_tx_allowed(SG_REGION_US_CA_AU, 315000000));
    CHECK(!sg_region_tx_allowed(SG_REGION_US_CA_AU, 868350000));
    CHECK(sg_region_tx_allowed(SG_REGION_JP, 315000000));
    CHECK(!sg_region_tx_allowed(SG_REGION_JP, 433920000));
    CHECK(!sg_region_tx_allowed(SG_REGION_COUNT, 433920000));
    for(size_t i = 0; i < sg_freq_list_count; i++) CHECK(sg_freq_valid(sg_freq_list[i]));
}

static void test_describe(void) {
    SgDecoded d = {.proto = sg_protocol_by_name("Princeton"), .data = 0x95D5D4, .bits = 24, .te = 400};
    sg_static_fill_fields(&d);
    char text[200];
    sg_decoded_describe(&d, text, sizeof(text));
    CHECK(strstr(text, "Princeton 24bit") != NULL);
    CHECK(strstr(text, "Key: 95D5D4") != NULL);
    CHECK(strstr(text, "Btn: 4") != NULL);
    /* tiny buffer must not overflow */
    char tiny[8];
    sg_decoded_describe(&d, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));
}

int main(void) {
    RUN(test_packetizer);
    RUN(test_static_roundtrip);
    RUN(test_no_tx_for_rolling);
    RUN(test_keeloq);
    RUN(test_somfy);
    RUN(test_nexus);
    RUN(test_gt_wt02);
    RUN(test_acurite);
    RUN(test_analyzer);
    RUN(test_sub_file);
    RUN(test_presets);
    RUN(test_region);
    RUN(test_describe);
    TEST_END();
}
