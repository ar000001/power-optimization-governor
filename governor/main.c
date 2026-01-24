
#include <stdio.h>
#include <stdlib.h>

#include "Governor.h"
#include "PipelineConfig.h"
#include "ApproximationModels.h"


int total_parts=0;
int target_fps=0;
int target_latency=0;


void set_system_config(){
	/* Export OpenCL library path */
    system("export LD_LIBRARY_PATH=/data/local/Working_dir");
	setenv("LD_LIBRARY_PATH","/data/local/Working_dir",1);

	/* Setup Performance Governor (CPU) */
	system("echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor");
	system("echo performance > /sys/devices/system/cpu/cpufreq/policy2/scaling_governor");
}


int main (int argc, char *argv[]) {
	if ( argc < 5 ){
		printf("Wrong number of input arguments.\n");
        printf("Usage: ./governor <graph> <total_parts> <target_fps> <target_latency>\n");
		return -1;
	}

    char graph[100];
	sscanf(argv[1], "%s", graph);
	total_parts=atoi(argv[2]);
	target_fps=atoi(argv[3]);
	target_latency=atoi(argv[4]);

	short latency_condition=0;
	short fps_condition=0;

	PipelineConfig config = ROOT_CONFIG;
    Policy policy = {false, false, -100, 1};

	char command[100];

	/* Checking if processor is available */
	if (system(NULL)) {
        puts ("Ok");
    } else {
        exit (EXIT_FAILURE);
    }

    approximate_target_space((double)target_fps, (double)target_latency, &config);

    double p = estimate_power(&config);
    printf("[smoke-test] estimated power: %f\n", p);

    

    /*
    set_system_config();

    stats_t stats;

	while(1){
		run_inference(&config, graph, 60);
		parse_results(&stats);
		apply_policy(&policy,&config, &stats, target_fps, target_latency);
		if (conditions_met(&stats, target_fps, target_latency) == 0){
			//NOTE: might be changed later on, such that if a config is found, it
			// still continues to try to find a better config.
			print_pipe_line_config(&config);
			break;
		}
	}
    */
  
  	return 0;
}