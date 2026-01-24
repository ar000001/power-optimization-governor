#ifndef GOVERNOR_H
#define GOVERNOR_H

#include <stdbool.h>
#include "PipelineConfig.h"

extern PipelineConfig ROOT_CONFIG;

typedef struct {
	double latency;
	double fps;
	double stage1_inference_time;
	double stage2_inference_time;
	double stage3_inference_time;
} stats_t;

typedef struct {
	bool increase_bigCPU;
	bool decrease_bigCPU;
	double latency_margin;
	double fps_margin;
} Policy;

void apply_policy(Policy *policy, PipelineConfig *config, stats_t *stats, double target_fps, double target_latency);

void parse_results(stats_t *ret);

bool conditions_met(stats_t *s, double target_fps, double target_latency);

#endif
