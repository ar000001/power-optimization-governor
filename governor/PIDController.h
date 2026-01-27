#ifndef PIDCONTROLLER_H
#define PIDCONTROLLER_H

#include <stdbool.h>
#include "PipelineConfig.h"
#include "Governor.h"

#define TOTAL_LAYERS 8

typedef struct {
    double Kp;
    double Ki;
    double Kd;
    double integral;
    double prev_error;
    double output_min;
    double output_max;
} PIDState;

typedef struct {
    PIDState fps_pid;
    PIDState latency_pid;
    double target_fps;
    double target_latency;
    double prev_latency;
    double prev_fps;
    bool has_prev;
    double power_reduction_rate;
    int iteration;
    int max_iterations;
    bool converged;
    int total_layers;
    int partition_step_cooldown;    
} PIDGovernor;

void pid_init(PIDState *pid, double Kp, double Ki, double Kd, 
              double output_min, double output_max);

double pid_update(PIDState *pid, double error, double dt);

void pid_reset(PIDState *pid);

void pid_governor_init(PIDGovernor *gov, double target_fps, double target_latency,
                       int max_iterations);

typedef enum {
    PID_CONTINUE,
    PID_CONVERGED,
    PID_MAX_ITERATIONS
} PIDResult;

PIDResult pid_governor_step(PIDGovernor *gov, PipelineConfig *config, 
                            stats_t *stats, double *estimated_power);

void pid_governor_apply_frequency_adjustment(PIDGovernor *gov, 
                                             PipelineConfig *config,
                                             double fps_adjustment,
                                             double latency_adjustment);

int snap_to_valid_frequency(int freq, processor cpu);

int get_frequency_index(int freq, processor cpu);

int frequency_step(int current_freq, int steps, processor cpu);

void pid_governor_adjust_partition_points(PIDGovernor *gov, PipelineConfig *config,
                                          double fps_margin, double latency_margin,
                                          bool reduce_power);

#endif
