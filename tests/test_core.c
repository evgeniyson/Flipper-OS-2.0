#include "test.h"
#include "core/fmt.h"
#include "core/ringbuf.h"

static void check_fmt(const char* expect, const char* got) {
    t_checks++;
    if(strcmp(expect, got) != 0) {
        t_fail++;
        printf("  FAIL fmt: expected \"%s\" got \"%s\"\n", expect, got);
    }
}

static void test_fmt(void) {
    char a[64], b[64];
#define SAME(...) \
    do { \
        snprintf(a, sizeof(a), __VA_ARGS__); \
        fmt_snprintf(b, sizeof(b), __VA_ARGS__); \
        check_fmt(a, b); \
    } while(0)
    SAME("plain");
    SAME("%d %d %d", 0, -1, 2147483647);
    SAME("%5d|%-5d|%05d", 42, 42, 42);
    SAME("%05d", -42);
    SAME("%u %x %X %08lX", 4000000000u, 0xbeefu, 0xBEEFu, 0x1234ABCDul);
    SAME("%s|%6s|%-6s|", "ab", "ab", "ab");
    SAME("%c%c %%", 'o', 'k');
    SAME("%lld %llu %llX", -1234567890123LL, 12345678901234567890ULL, 0xDEADBEEFCAFEULL);
    SAME("%0*lX", 6, 0x95D5D4ul);
    SAME("%ld", -2147483647L - 1);
#undef SAME
    /* truncation: always terminated, returns the full length */
    char small[5];
    int n = fmt_snprintf(small, sizeof(small), "%s", "truncate me");
    CHECK_EQ(n, 11);
    CHECK(strcmp(small, "trun") == 0);
}

static void test_ringbuf(void) {
    static RingBuf rb;
    rb_reset(&rb);
    int32_t v;
    CHECK(!rb_pop(&rb, &v));
    for(int i = 0; i < (int)RB_SIZE; i++) rb_push(&rb, i);
    CHECK(!rb.overflow);
    rb_push(&rb, -1);
    CHECK(rb.overflow);
    CHECK_EQ(rb_count(&rb), RB_SIZE);
    for(int i = 0; i < (int)RB_SIZE; i++) {
        CHECK(rb_pop(&rb, &v));
        if(v != i) {
            CHECK_EQ(v, i);
            break;
        }
    }
    CHECK(!rb_pop(&rb, &v));
    /* wrap-around keeps order */
    for(int round = 0; round < 3; round++) {
        for(int i = 0; i < 3000; i++) rb_push(&rb, round * 10000 + i);
        for(int i = 0; i < 3000; i++) {
            rb_pop(&rb, &v);
            if(v != round * 10000 + i) CHECK_EQ(v, round * 10000 + i);
        }
    }
    CHECK_EQ(rb_count(&rb), 0);
}

int main(void) {
    RUN(test_fmt);
    RUN(test_ringbuf);
    TEST_END();
}
