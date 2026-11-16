/* Decoder tests on real captures (see tools/fetch_test_vectors.sh).
 * Skipped when the captures have not been downloaded. */
#include "test.h"
#include "subghz/protocol.h"
#include "subghz/sub_file.h"

static const char* dir;
static SgPacketizer packetizer;
static SgDecoded found[512];
static int n_found;

static void on_packet(const SgPacket* pkt, void* ctx) {
    (void)ctx;
    SgDecoded d;
    if(sg_decode_packet(pkt, &d) && n_found < 512) found[n_found++] = d;
}
static void on_pulse(sg_pulse_t p, void* ctx) {
    (void)ctx;
    sg_packetizer_feed(&packetizer, p);
}

static bool load(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    n_found = 0;
    if(!f) return false;
    static SubParser parser;
    sub_parser_init(&parser, on_pulse, NULL);
    sg_packetizer_init(&packetizer, 0, on_packet, NULL);
    char buf[4096];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), f)) > 0) sub_parser_feed(&parser, buf, n);
    fclose(f);
    sub_parser_finish(&parser);
    sg_packetizer_flush(&packetizer);
    return true;
}

static int count(const char* proto, uint64_t key) {
    int c = 0;
    for(int i = 0; i < n_found; i++)
        if(strcmp(found[i].proto->name, proto) == 0 && (key == ~0ull || found[i].data == key)) c++;
    return c;
}

/* Every frame of the capture must decode to this protocol, and `key` must be
 * seen at least `min` times. */
static void expect(const char* file, const char* proto, uint64_t key, int min) {
    if(!load(file)) {
        printf("  missing %s\n", file);
        CHECK(false);
        return;
    }
    int hits = count(proto, key);
    if(hits < min) printf("  %s: %s key %llX seen %d times, expected >= %d\n", file, proto, (unsigned long long)key, hits, min);
    CHECK(hits >= min);
    int others = n_found - count(proto, ~0ull);
    if(others) printf("  %s: %d frames decoded as a different protocol\n", file, others);
    CHECK_EQ(others, 0);
}

static void test_static(void) {
    expect("princeton_raw.sub", "Princeton", 0x7C5703, 7);
    expect("princeton_raw.sub", "Princeton", 0x7C57C0, 7);
    expect("came_raw.sub", "CAME", 0x216, 50);
    expect("nice_flo_raw.sub", "Nice FLO", 0x5C14E4, 10);
    expect("holtek_ht12x_raw.sub", "Holtek_HT12X", 0xFFB, 10);
    expect("linear_raw.sub", "Linear", 0x316, 30);
    expect("gate_tx_raw.sub", "GateTX", 0x02EDCD, 14);
    expect("smc5326_raw.sub", "SMC5326", 0x17D5575, 18);
}

static void test_rolling(void) {
    expect("nice_flor_s_raw.sub", "Nice FloR-S", ~0ull, 6);
    expect("came_atomo_raw.sub", "CAME Atomo", ~0ull, 6);
    expect("somfy_telis_raw.sub", "Somfy Telis", ~0ull, 18);
    /* same remote throughout, rolling counter goes up by one per press */
    uint16_t prev = 0;
    for(int i = 0; i < n_found; i++) {
        CHECK_EQ(found[i].serial, 0x121300);
        CHECK(found[i].cnt == prev || found[i].cnt == prev + 1 || prev == 0);
        prev = found[i].cnt;
    }
    CHECK_EQ(prev, 0x20F);
}

static void test_mixed(void) {
    /* a long recording with many different remotes mixed together */
    CHECK(load("test_random_raw.sub"));
    CHECK(n_found >= 200);
    CHECK(count("KeeLoq", ~0ull) >= 15);
    CHECK(count("Princeton", 0x7C570C) >= 5);
    CHECK(count("CAME", 0x216) >= 10);
    /* KeeLoq: repeats of one press are identical, and one remote keeps its
     * serial while the hopping code changes */
    int same_serial_other_hop = 0;
    for(int i = 0; i < n_found; i++)
        for(int j = i + 1; j < n_found; j++)
            if(strcmp(found[i].proto->name, "KeeLoq") == 0 && found[j].proto == found[i].proto &&
               found[i].serial == found[j].serial && found[i].hop != found[j].hop)
                same_serial_other_hop++;
    CHECK(same_serial_other_hop > 0);
}

int main(int argc, char** argv) {
    dir = argc > 1 ? argv[1] : "tests/vectors";
    if(!load("princeton_raw.sub")) {
        printf("reference captures not found in %s - run tools/fetch_test_vectors.sh\n", dir);
        return 77;
    }
    RUN(test_static);
    RUN(test_rolling);
    RUN(test_mixed);
    TEST_END();
}
