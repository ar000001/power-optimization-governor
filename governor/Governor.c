#include <stdio.h>      
#include <stdlib.h>     
#include <stdbool.h>
#include <math.h>
#include "PipelineConfig.h"

// This config is a very well performing config.
// From this config on, we will try to find a better config, by tweaking values slightly.
PipelineConfig ROOT_CONFIG = {4, 6, 1800000, 1200000, "G-B-L"};

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
	float (*fx_freq_power_little_cpu)(float);
	float (*fx_freq_power_big_cpu)(float);
	float (*fx_freq_latency_little_cpu)(float);
	float (*fx_freq_latency_big_cpu)(float);
	float (*fx_freq_fps_little_cpu)(float);
	float (*fx_freq_fps_big_cpu)(float);
	float latency_margin;
	float fps_margin;
} Policy;


void apply_policy(Policy *policy, PipelineConfig *config, stats_t *stats, float target_fps, float target_latency){

	float latency_diff = stats->latency - target_latency;
	float fps_diff = stats->fps - target_fps;

	if (latency_diff > 0){ //target not met

		if (policy->increase_bigCPU)
			increment_frequency(config->big_frequency, BIG_CPU);
		 else 
			increment_frequency(config->little_frequency, LITTLE_CPU);

		policy->increase_bigCPU = !policy->increase_bigCPU;
		policy->decrease_bigCPU = false; //if there has been an increase, the next decrease must be of littleCPU

	} else if (latency_diff  < policy->latency_margin) { //target met with margin
		if (policy->decrease_bigCPU)
			decrement_frequency(config->big_frequency, BIG_CPU);
		 else 
			decrement_frequency(config->little_frequency, LITTLE_CPU);

		policy->decrease_bigCPU = !policy->decrease_bigCPU;

	}

}


/* Get feedback by parsing the results */
void parse_results(stats_t *ret){
	float fps;
	float latency;
	float stage1_inference_time;
	float stage2_inference_time;
	float stage3_inference_time;
	FILE *output_file;
    char *line = NULL;
    size_t len = 0;
    
    if ((output_file = fopen("output.txt", "r")) == NULL) {
		printf("Error opening file\n");
		return;
	}

	/* Read Output.txt File and Extract Data */
	while (getline(&line, &len, output_file) != -1)
	{
		char *temp;
		/* Extract Frame Rate */
		if ( strstr(line, "Frame rate is:")!=NULL ){

			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &fps) == 1){
					ret->fps = fps;
					printf("Throughput is: %f FPS\n", fps);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Frame Latency */
		if ( strstr(line, "Frame latency is:")!=NULL ){

			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &latency) == 1){
					ret->latency = latency;
					printf("Latency is: %f ms\n", latency);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Stage One Inference Time */
		if ( strstr(line, "stage1_inference_time:")!=NULL ){
			
			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &stage1_inference_time) == 1){
					ret->stage1_inference_time = stage1_inference_time;
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Stage Two Inference Time */
		if ( strstr(line, "stage2_inference_time:")!=NULL ){
			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &stage2_inference_time) == 1){
					ret->stage2_inference_time = stage2_inference_time;
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Stage Three Inference Time */
		if ( strstr(line, "stage3_inference_time:")!=NULL ){
            temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &stage3_inference_time) == 1){
					ret->stage3_inference_time = stage3_inference_time;
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
	}
}


bool conditions_met(stats_t *s, float target_fps, float target_latency) {

	if (s->latency <= target_latency && s->fps >= target_fps){
		return true;
	}
	return false;
}


float fx_freq_power_little_cpu(float khz){      
	return 4.827e-14 * pow(khz, 2) + 2.292e-7 * khz + 1.855;
}

float fx_freq_power_big_cpu(float khz){      
	return 6.998e-13 * pow(khz, 2) - 7.705e-7 * khz + 2.523;
}

float fx_freq_latency_little_cpu(float khz){
    float ghz = khz / 1e6;
	return -1.951e+01 * exp(-1.412e+02 * ghz) + 521.364;
}

float fx_freq_latency_big_cpu(float khz){
	float ghz = khz / 1e6;
	return 8.573e+02 * exp(-2.059e+00 * ghz) + 100.429;
}

float fx_freq_fps_little_cpu(float khz){
    float ghz = khz / 1e6;
	return 6.027e+03 * exp(1.836e-04 * ghz) + -6026.159;
}

float fx_freq_fps_big_cpu(float khz){
	float ghz = khz / 1e6;
	return 3.016e+04 * exp(1.385e-04 * ghz) + -30158.352;
}