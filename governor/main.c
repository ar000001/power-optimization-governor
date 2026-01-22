
#include <stdio.h>
#include <stdlib.h>

#include "Governor.h"
#include "PipelineConfig.h"


int partitions=0;
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
		return -1;
	}

    char graph[100];
	sscanf(argv[1], "%s", graph);
	partitions=atoi(argv[2]);
	target_fps=atoi(argv[3]);
	target_latency=atoi(argv[4]);

	short latency_condition=0;
	short fps_condition=0;

	PipelineConfig config = ROOT_CONFIG;

	char command[100];

	/* Checking if processor is available */
	if (system(NULL)) {
        puts ("Ok");
    } else {
        exit (EXIT_FAILURE);
    }

    set_system_config();

	stats_t stats;

	while(1){
		run_inference(&config, graph, partitions);
		parse_results(&stats);
		apply_policy(&config, &stats, target_fps, target_latency);
		if (conditions_met(&stats, target_fps, target_latency) == 0){
			//NOTE: might be changed later on, such that if a config is found, it
			// still continues to try to find a better config.
			print_pipe_line_config(&config);
			break;
		}
	}

  
  	return 0;
}