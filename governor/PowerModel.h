#ifndef POWERMODEL_H
#define POWERMODEL_H

#include <stdbool.h>

#define TOTAL_LAYERS 8
#define GPU_POWER 3.0

double estimate_power(int big_freq, int little_freq, int pp1, int pp2);

void get_workload_fractions(int pp1, int pp2, double *gpu_frac, double *big_frac, double *little_frac);

static inline double khz_to_mhz(int freq_khz) {
    return (double)freq_khz / 1000.0;
}

#endif