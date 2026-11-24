// The few lines every test program needs: CHECK and CHECK_NEAR count what fails.
#pragma once

#include <cmath>
#include <cstdio>

inline int failures = 0;

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; } \
    } while (0)
#define CHECK_NEAR(a, b, tolerance)                                                   \
    do {                                                                              \
        const double x_ = (a), y_ = (b);                                              \
        if (!(std::fabs(x_ - y_) <= (tolerance))) {                                   \
            std::printf("FAIL line %d: %s = %.9g, expected %.9g (+-%g)\n", __LINE__, #a, x_, y_, (double)(tolerance)); \
            failures++;                                                               \
        }                                                                             \
    } while (0)
