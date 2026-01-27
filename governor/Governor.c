#include <stdio.h>      
#include <stdlib.h>     
#include <stdbool.h>
#include <math.h>
#include "Governor.h"
#include "PipelineConfig.h"
#include "ApproximationModels.h"

// This config is a very well performing config.
// From this config on, we will try to find a better config, by tweaking values slightly.
PipelineConfig ROOT_CONFIG = {4, 6, 1800000, 1200000, "G-B-L"};


void apply_policy(Policy *policy, PipelineConfig *config, stats_t *stats, double target_fps, double target_latency){
	return;
}


/* Get feedback by parsing the results */
void parse_results(stats_t *ret){
	double fps;
	double latency;
	double stage1_inference_time;
	double stage2_inference_time;
	double stage3_inference_time;
	FILE *output_file;
    char *line = NULL;
    size_t len = 0;
    
    if ((output_file = fopen("last_run_output.txt", "r")) == NULL) {
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
				/* Checking the given word is double or not */
				if (sscanf(temp, "%lf", &fps) == 1){
					ret->fps = fps;
					printf("Throughput is: %lf FPS\n", fps);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Frame Latency */
		if ( strstr(line, "Frame latency is:")!=NULL ){

			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is double or not */
				if (sscanf(temp, "%lf", &latency) == 1){
					ret->latency = latency;
					printf("Latency is: %lf ms\n", latency);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Stage One Inference Time */
		if ( strstr(line, "stage1_inference_time:")!=NULL ){
			
			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is double or not */
				if (sscanf(temp, "%lf", &stage1_inference_time) == 1){
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
				/* Checking the given word is double or not */
				if (sscanf(temp, "%lf", &stage2_inference_time) == 1){
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
				/* Checking the given word is double or not */
				if (sscanf(temp, "%lf", &stage3_inference_time) == 1){
					ret->stage3_inference_time = stage3_inference_time;
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
	}
}


bool conditions_met(stats_t *s, double target_fps, double target_latency) {

	if (s->latency <= target_latency && s->fps >= target_fps){
		return true;
	}
	return false;
}


