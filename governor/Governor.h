#ifndef GOVERNOR_H
#define GOVERNOR_H

#include <stdbool.h>
#include "PipelineConfig.h"

extern PipelineConfig ROOT_CONFIG;

typedef struct {
	float latency;
	float fps;
	float stage1_inference_time;
	float stage2_inference_time;
	float stage3_inference_time;
} stats_t;

typedef struct {
	bool increase_bigCPU;
	bool decrease_bigCPU;
	float latency_margin;
	float fps_margin;
} Policy;

void apply_policy(Policy *policy, PipelineConfig *config, stats_t *stats, float target_fps, float target_latency);

void parse_results(stats_t *ret);

bool conditions_met(stats_t *s, float target_fps, float target_latency);

float fx_freq_power_little_cpu(float khz);      

float fx_freq_power_big_cpu(float khz);

float fx_freq_latency_little_cpu(float khz);

float fx_freq_latency_big_cpu(float khz);

float fx_freq_fps_little_cpu(float khz);

float fx_freq_fps_big_cpu(float khz);

float fx_power_freq_little_cpu(float watts);

float fx_power_freq_big_cpu(float watts);

float fx_latency_freq_little_cpu(float latency);

float fx_latency_freq_big_cpu(float latency);

float fx_fps_freq_little_cpu(float fps);

float fx_fps_freq_big_cpu(float fps);

#endif
