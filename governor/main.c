
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include "Governor.h"
#include "PipelineConfig.h"
#include "ApproximationModels.h"
#include "PIDController.h"


int total_parts=0;
int target_fps=0;
int target_latency=0;

static time_t get_file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return st.st_mtime;
}

static void print_best_so_far(PIDGovernor *pid_gov) {
    PipelineConfig best_config;
    double best_power;
    bool best_meets;

    if (pid_governor_get_best(pid_gov, &best_config, &best_power, &best_meets)) {
        printf("  Best-so-far config: big_freq=%d, little_freq=%d, pp1=%d, pp2=%d, order=%s, meets_targets=%s\n",
               best_config.big_frequency, best_config.little_frequency,
               best_config.partition_point1, best_config.partition_point2, best_config.order,
               best_meets ? "YES" : "NO");
        printf("  Best-so-far estimated power: %.3f W\n", best_power);
    } else {
        printf("  No best-so-far config cached.\n");
    }
}


void set_system_config(){
	/* Export OpenCL library path */
    system("export LD_LIBRARY_PATH=/data/local/Working_dir");
    system("adb -d root");
	setenv("LD_LIBRARY_PATH","/data/local/Working_dir",1);

	/* Setup Performance Governor (CPU) */
	system("adb -d shell \"echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor\"");
	system("adb -d shell \"echo performance > /sys/devices/system/cpu/cpufreq/policy2/scaling_governor\"");

    system("./set_fan.sh 1 0 1");
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

    if (load_measurement_grid("../experiment_scripts/data/exp6_freq_sweep_fixed_order_partition_20260123_145108.csv") != 0) {
        fprintf(stderr, "Failed to load measurement grid\n");
        return -1;
    }

    approximate_target_space((double)target_fps, (double)target_latency, &config);

    double p = estimate_power(&config);
    printf("[smoke-test] estimated power: %f\n", p);

    set_system_config();

    PIDGovernor pid_gov;
    pid_governor_init(&pid_gov, (double)target_fps, (double)target_latency, 20);

    stats_t stats;
    double estimated_power = 0.0;
    PIDResult result;

    printf("\n[PID Governor] Starting optimization for target_fps=%d, target_latency=%d\n", 
           target_fps, target_latency);
    printf("[PID Governor] Initial config: big_freq=%d, little_freq=%d, pp1=%d, pp2=%d\n",
           config.big_frequency, config.little_frequency,
           config.partition_point1, config.partition_point2);

    while (1) {
        time_t mtime_before = get_file_mtime("last_run_output.txt");
        run_inference(&config, graph, 100);
        time_t mtime_after = get_file_mtime("last_run_output.txt");

        if (mtime_after == mtime_before) {
            printf("\n[PID Governor] Inference interrupted (Ctrl-C detected). Exiting.\n");
            print_best_so_far(&pid_gov);
            system("./set_fan.sh 1 0 0");
            return 1;
        }

        parse_results(&stats);

        result = pid_governor_step(&pid_gov, &config, &stats, &estimated_power);
        
        if (result == PID_CONVERGED) {
            printf("\n[PID Governor] Optimization complete!\n");
            printf("  Final config: big_freq=%d, little_freq=%d, pp1=%d, pp2=%d, order=%s\n",
                   config.big_frequency, config.little_frequency,
                   config.partition_point1, config.partition_point2, config.order);
            printf("  Estimated power: %.3f W\n", estimated_power);
            printf("  Achieved: fps=%.2f, latency=%.2f ms\n", stats.fps, stats.latency);
            print_best_so_far(&pid_gov);
            print_pipe_line_config(&config);
            break;
        } else if (result == PID_MAX_ITERATIONS) {
            printf("\n[PID Governor] Max iterations reached without full convergence.\n");
            print_best_so_far(&pid_gov);

            printf("  Returned config: big_freq=%d, little_freq=%d, pp1=%d, pp2=%d\n",
                   config.big_frequency, config.little_frequency,
                   config.partition_point1, config.partition_point2);
            printf("  Returned estimated power: %.3f W\n", estimated_power);
            printf("  Last stats: fps=%.2f, latency=%.2f ms\n", stats.fps, stats.latency);
            break;
        }

        printf("\n\n");
    }
  
  	return 0;
}