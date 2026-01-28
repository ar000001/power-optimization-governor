#ifndef POWERMODEL_H
#define POWERMODEL_H

#include <stdbool.h>
#include <math.h>

#include "PipelineConfig.h"

#define GPU_POWER 3.0

typedef struct {
	double (*fx_freq_power_lcpu)(double);
	double (*fx_freq_power_bcpu)(double);
	double (*fx_latency_freq_lcpu)(double);
	double (*fx_latency_freq_bcpu)(double);
	double (*fx_fps_freq_lcpu)(double);
	double (*fx_fps_freq_bcpu)(double);
} ApproximationModel; // Currently not used but might be used another time when the code is cleaned up.

double estimate_power(PipelineConfig *config);

void get_workload_fractions(int pp1, int pp2, double *gpu_frac, double *big_frac, double *little_frac);

static inline double khz_to_mhz(int freq_khz) {
    return (double)freq_khz / 1000.0;
}

//best fit from experiments
static inline double fx_power_lcpu(double khz) {
    return 4.827e-14 * khz * khz + 2.292e-7 * khz + 1.855;
}

static inline double fx_power_bcpu(double khz) {
    return 6.998e-13 * khz * khz - 7.705e-7 * khz + 2.523;
}


static inline double fx_latency_lcpu(double khz){
    double ghz = khz / 1e6;
	return 3.902e+02 / ghz + 153.954;
}

static inline double fx_latency_bcpu(double khz){
	double ghz = khz / 1e6;
	return 1.986e+02 / ghz + 12.009;
}

static inline double fx_fps_lcpu(double khz){
    double ghz = khz / 1e6;
	return 1.0 / (3.287e-01 / ghz + 0.202);
}

static inline double fx_fps_bcpu(double khz){
	double ghz = khz / 1e6;
	return 1.0 / (1.882e-01 / ghz + 0.019);
}

static inline double fx_power_freq_lcpu(double watts){
	return -1.74e+06 * (watts*watts) + 1.042e+07 * watts - 1.329e+07;
}

static inline double fx_power_freq_bcpu(double watts){
	return -3.409e+05 * (watts*watts) + 2.991e+06 * watts - 4.385e+06;
}

static inline double fx_latency_freq_lcpu(double latency){
	if (latency <= 153.954) return LITTLE_FREQUENCY_TABLE[NUM_LITTLE_FREQUENCIES-1]; // infeasible, return max freq
	return (3.902e+02 / (latency - 153.954))*1e6;
}

static inline double fx_latency_freq_bcpu(double latency){
	if (latency <= 12.009) return BIG_FREQUENCY_TABLE[NUM_BIG_FREQUENCIES-1]; // infeasible, return max freq
	return (1.986e+02 / (latency - 12.009))*1e6;
}

static inline double fx_fps_freq_lcpu(double fps){
	double denom = 1.0/fps - 0.202;
	if (denom <= 0) return LITTLE_FREQUENCY_TABLE[NUM_LITTLE_FREQUENCIES-1]; // infeasible, return max freq
	return (3.287e-01 / denom)*1e6;
}

static inline double fx_fps_freq_bcpu(double fps){
	double denom = 1.0/fps - 0.019;
	if (denom <= 0) return BIG_FREQUENCY_TABLE[NUM_BIG_FREQUENCIES-1]; // infeasible, return max freq
	return (1.882e-01 / denom)*1e6;
}

int load_measurement_grid(const char *filepath);
void approximate_target_space(double target_fps, double target_latency, PipelineConfig *config);

#endif