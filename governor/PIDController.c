#include "PIDController.h"
#include "ApproximationModels.h"
#include <stdio.h>
#include <math.h>

void pid_init(PIDState *pid, double Kp, double Ki, double Kd,
              double output_min, double output_max) {
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->integral = 0.0;
    pid->prev_error = 0.0;
    pid->output_min = output_min;
    pid->output_max = output_max;
}

double pid_update(PIDState *pid, double error, double dt) {
    pid->integral += error * dt;
    
    double integral_limit = (pid->output_max - pid->output_min) / 2.0;
    if (pid->integral > integral_limit) pid->integral = integral_limit;
    if (pid->integral < -integral_limit) pid->integral = -integral_limit;
    
    double derivative = (error - pid->prev_error) / dt;
    pid->prev_error = error;
    
    double output = pid->Kp * error + pid->Ki * pid->integral + pid->Kd * derivative;
    
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;
    
    return output;
}

void pid_reset(PIDState *pid) {
    pid->integral = 0.0;
    pid->prev_error = 0.0;
}

void pid_governor_init(PIDGovernor *gov, double target_fps, double target_latency,
                       int max_iterations) {
    pid_init(&gov->fps_pid, 50000.0, 5000.0, 1000.0, 
             -500000.0, 500000.0);
    
    pid_init(&gov->latency_pid, 30000.0, 3000.0, 500.0,
             -500000.0, 500000.0);
    
    gov->target_fps = target_fps;
    gov->target_latency = target_latency;
    gov->power_reduction_rate = 0.05;
    gov->iteration = 0;
    gov->max_iterations = max_iterations;
    gov->converged = false;
    gov->total_layers = TOTAL_LAYERS;
    gov->partition_step_cooldown = 0;
}

int snap_to_valid_frequency(int freq, processor cpu) {
    const int *table;
    int num_freqs;
    
    if (cpu == BIG_CPU) {
        table = BIG_FREQUENCY_TABLE;
        num_freqs = NUM_BIG_FREQUENCIES;
    } else {
        table = LITTLE_FREQUENCY_TABLE;
        num_freqs = NUM_LITTLE_FREQUENCIES;
    }
    
    if (freq <= table[0]) return table[0];
    if (freq >= table[num_freqs - 1]) return table[num_freqs - 1];
    
    for (int i = 0; i < num_freqs - 1; i++) {
        if (freq >= table[i] && freq <= table[i + 1]) {
            if (freq - table[i] < table[i + 1] - freq) {
                return table[i];
            } else {
                return table[i + 1];
            }
        }
    }
    
    return table[num_freqs - 1];
}

void pid_governor_apply_frequency_adjustment(PIDGovernor *gov,
                                             PipelineConfig *config,
                                             double fps_adjustment,
                                             double latency_adjustment) {
    double combined_adjustment = fps_adjustment + latency_adjustment;
    
    double big_adjustment = combined_adjustment * 0.6;
    double little_adjustment = combined_adjustment * 0.4;
    
    int new_big_freq = config->big_frequency + (int)big_adjustment;
    int new_little_freq = config->little_frequency + (int)little_adjustment;
    
    config->big_frequency = snap_to_valid_frequency(new_big_freq, BIG_CPU);
    config->little_frequency = snap_to_valid_frequency(new_little_freq, LITTLE_CPU);
}

void pid_governor_adjust_partition_points(PIDGovernor *gov, PipelineConfig *config,
                                          double fps_margin, double latency_margin,
                                          bool reduce_power) {
    if (gov->partition_step_cooldown > 0) {
        gov->partition_step_cooldown--;
        return;
    }
    
    int pp1 = config->partition_point1;
    int pp2 = config->partition_point2;
    
    if (reduce_power) {
        double rel_margin = fmin(fps_margin / gov->target_fps, 
                                  latency_margin / gov->target_latency);
        
        if (rel_margin > 0.15 && pp2 < gov->total_layers) {
            config->partition_point2 = pp2 + 1;
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: shifting work to little CPU (pp2: %d -> %d)\n", 
                   pp2, config->partition_point2);
        } else if (rel_margin > 0.2 && pp1 < pp2 - 1) {
            config->partition_point1 = pp1 + 1;
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: shifting work from GPU to big CPU (pp1: %d -> %d)\n", 
                   pp1, config->partition_point1);
        }
    } else {
        double fps_deficit = -fps_margin / gov->target_fps;
        double lat_deficit = -latency_margin / gov->target_latency;
        double deficit = fmax(fps_deficit, lat_deficit);
        
        if (deficit > 0.1) {
            if (pp2 > pp1 + 1) {
                config->partition_point2 = pp2 - 1;
                gov->partition_step_cooldown = 3;
                printf("[PID] Partition: shifting work to big CPU (pp2: %d -> %d)\n", 
                       pp2, config->partition_point2);
            } else if (pp1 > 0) {
                config->partition_point1 = pp1 - 1;
                gov->partition_step_cooldown = 3;
                printf("[PID] Partition: shifting work to GPU (pp1: %d -> %d)\n", 
                       pp1, config->partition_point1);
            }
        }
    }
}

static bool try_reduce_power(PIDGovernor *gov, PipelineConfig *config, 
                             stats_t *stats, double margin_fps, double margin_latency) {
    if (margin_fps <= 0 && margin_latency <= 0) {
        return false;
    }
    
    PipelineConfig test_config = *config;
    bool reduced = false;
    
    double usable_margin = fmin(margin_fps / gov->target_fps, 
                                 margin_latency / gov->target_latency);
    
    if (usable_margin > 0.05) {
        int big_step = (int)(config->big_frequency * gov->power_reduction_rate);
        int new_big = snap_to_valid_frequency(config->big_frequency - big_step, BIG_CPU);
        
        if (new_big < config->big_frequency) {
            test_config.big_frequency = new_big;
            reduced = true;
        }
    }
    
    if (usable_margin > 0.1) {
        int little_step = (int)(config->little_frequency * gov->power_reduction_rate);
        int new_little = snap_to_valid_frequency(config->little_frequency - little_step, LITTLE_CPU);
        
        if (new_little < config->little_frequency) {
            test_config.little_frequency = new_little;
            reduced = true;
        }
    }
    
    if (usable_margin > 0.15) {
        pid_governor_adjust_partition_points(gov, &test_config, margin_fps, margin_latency, true);
        if (test_config.partition_point1 != config->partition_point1 ||
            test_config.partition_point2 != config->partition_point2) {
            reduced = true;
        }
    }
    
    if (reduced) {
        *config = test_config;
    }
    
    return reduced;
}

PIDResult pid_governor_step(PIDGovernor *gov, PipelineConfig *config,
                            stats_t *stats, double *estimated_power) {
    gov->iteration++;
    
    if (gov->iteration > gov->max_iterations) {
        return PID_MAX_ITERATIONS;
    }
    
    double fps_error = gov->target_fps - stats->fps;
    double latency_error = stats->latency - gov->target_latency;
    
    double dt = 1.0;
    
    bool fps_met = stats->fps >= gov->target_fps;
    bool latency_met = stats->latency <= gov->target_latency;
    
    printf("[PID] iter=%d fps=%.2f (target=%.2f, err=%.2f) lat=%.2f (target=%.2f, err=%.2f) pp1=%d pp2=%d\n",
           gov->iteration, stats->fps, gov->target_fps, fps_error,
           stats->latency, gov->target_latency, latency_error,
           config->partition_point1, config->partition_point2);
    
    if (fps_met && latency_met) {
        double margin_fps = stats->fps - gov->target_fps;
        double margin_latency = gov->target_latency - stats->latency;
        
        bool reduced = try_reduce_power(gov, config, stats, margin_fps, margin_latency);
        
        if (!reduced) {
            gov->converged = true;
            *estimated_power = estimate_power(config);
            printf("[PID] Converged at iteration %d: big_freq=%d, little_freq=%d, pp1=%d, pp2=%d, power=%.3fW\n",
                   gov->iteration, config->big_frequency, config->little_frequency,
                   config->partition_point1, config->partition_point2, *estimated_power);
            return PID_CONVERGED;
        }
        
        printf("[PID] Targets met, reducing power: big_freq=%d, little_freq=%d, pp1=%d, pp2=%d\n",
               config->big_frequency, config->little_frequency,
               config->partition_point1, config->partition_point2);
    } else {
        double fps_adjustment = 0.0;
        double latency_adjustment = 0.0;
        
        if (!fps_met) {
            fps_adjustment = pid_update(&gov->fps_pid, fps_error, dt);
        } else {
            pid_reset(&gov->fps_pid);
        }
        
        if (!latency_met) {
            latency_adjustment = pid_update(&gov->latency_pid, latency_error, dt);
        } else {
            pid_reset(&gov->latency_pid);
        }
        
        pid_governor_apply_frequency_adjustment(gov, config, fps_adjustment, latency_adjustment);
        
        double fps_margin = stats->fps - gov->target_fps;
        double latency_margin = gov->target_latency - stats->latency;
        pid_governor_adjust_partition_points(gov, config, fps_margin, latency_margin, false);
        
        printf("[PID] Adjusting: fps_adj=%.0f, lat_adj=%.0f -> big_freq=%d, little_freq=%d, pp1=%d, pp2=%d\n",
               fps_adjustment, latency_adjustment, config->big_frequency, config->little_frequency,
               config->partition_point1, config->partition_point2);
    }
    
    *estimated_power = estimate_power(config);
    return PID_CONTINUE;
}
