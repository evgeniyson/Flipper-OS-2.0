#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int t_fail, t_checks;

#define CHECK(cond) \
    do { \
        t_checks++; \
        if(!(cond)) { \
            t_fail++; \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

#define CHECK_EQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        t_checks++; \
        if(_a != _b) { \
            t_fail++; \
            printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); \
        } \
    } while(0)

#define RUN(fn) \
    do { \
        int _before = t_fail; \
        fn(); \
        printf("%s %s\n", t_fail == _before ? "ok  " : "FAIL", #fn); \
    } while(0)

#define TEST_END() \
    do { \
        printf("%d checks, %d failed\n", t_checks, t_fail); \
        return t_fail ? 1 : 0; \
    } while(0)
