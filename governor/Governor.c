/*Instructions to Run
On Your Computer: 
	arm-linux-androideabi-clang++ -static-libstdc++ Governor.cpp -o Governor 
	adb push Governor /data/local/Working_dir
On the Board: 
	chmod +x Governor.sh
	./Governor graph_alexnet_all_pipe_sync #NumberOFPartitions #TargetFPS #TargetLatency
*/


#include <stdio.h>      
#include <stdlib.h>     
#include <stdbool.h>
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
	bool last_increased_bigCPU;
	PipelineConfig *best_config;
	float best_bigCPU_freq;
	float best_littleCPU_freq;
} Policy;


void apply_policy(Policy *policy, PipelineConfig *config, stats_t *stats, float target_fps, float target_latency){

	float latency_diff = stats->latency - target_latency;
	float fps_diff = stats->fps - target_fps;

	if (latency_diff < 0){
		if (policy->last_increased_bigCPU){
			
		}
	}

}


/* Get feedback by parsing the results */
void parse_results(stats_t *ret){
	float fps;
	float latency;
	FILE *output_file;
    char *line = NULL;
    size_t len = 0;
    stats_t stats;
    
    if ((output_file = fopen("output.txt", "r")) == NULL) {
		printf("Error opening file\n");
		return;
	}

	/* Read Output.txt File and Extract Data */
	while (getline(&line, &len, output_file) != -1)
	{
		char temp[100];
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
		if ( strstr("stage2_inference_time:")!=NULL ){
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
	/* Check Throughput and Latency Constraints */
	if ( latency <= target_latency ){
		latency_condition=1; //Latency requirement was met.
	}
	if ( fps >= target_fps ){
		fps_condition=1; //FPS requirement was met.
	}
}


bool conditions_met(stats_t *s, float target_fps, float target_latency) {

	if (s->latency <= target_latency && s->fps >= target_fps){
		return true;
	}
	return false;
}