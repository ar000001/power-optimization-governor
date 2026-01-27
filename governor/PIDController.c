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
    bool integral_clamped = false;
    if (pid->integral > integral_limit) { pid->integral = integral_limit; integral_clamped = true; }
    if (pid->integral < -integral_limit) { pid->integral = -integral_limit; integral_clamped = true; }
    
    double derivative = (error - pid->prev_error) / dt;
    pid->prev_error = error;
    
    double p_term = pid->Kp * error;
    double i_term = pid->Ki * pid->integral;
    double d_term = pid->Kd * derivative;
    double output = p_term + i_term + d_term;
    
    printf("  [PID-calc] error=%.4f | P=%.2f (Kp=%.1f) | I=%.2f (Ki=%.1f, integral=%.4f%s) | D=%.2f (Kd=%.1f, deriv=%.4f) | raw_steps=%.2f",
           error, p_term, pid->Kp, i_term, pid->Ki, pid->integral, 
           integral_clamped ? " CLAMPED" : "", d_term, pid->Kd, derivative, output);
    
    if (output > pid->output_max) { 
        printf(" -> clamped to max %.0f steps\n", pid->output_max);
        output = pid->output_max; 
    } else if (output < pid->output_min) { 
        printf(" -> clamped to min %.0f steps\n", pid->output_min);
        output = pid->output_min; 
    } else {
        printf("\n");
    }
    
    return output;
}

void pid_reset(PIDState *pid) {
    pid->integral = 0.0;
    pid->prev_error = 0.0;
}

void pid_governor_init(PIDGovernor *gov, double target_fps, double target_latency,
                       int max_iterations) {
    // Step-based output: bounds are in frequency table steps (e.g., ±3 steps max per iteration)
    // Kp/Ki/Kd are now dimensionless since error is normalized and output is in steps
    pid_init(&gov->fps_pid, 3.0, 0.5, 0.0, -2.0, 2.0);
    pid_init(&gov->latency_pid, 2.0, 0.3, 0.0, -2.0, 2.0);
    
    gov->target_fps = target_fps;
    gov->target_latency = target_latency;
    gov->power_reduction_rate = 0.05;
    gov->iteration = 0;
    gov->max_iterations = max_iterations;
    gov->converged = false;
    gov->total_layers = TOTAL_LAYERS;
    gov->partition_step_cooldown = 0;
    gov->has_prev = false;
}

int get_frequency_index(int freq, processor cpu) {
    const int *table;
    int num_freqs;
    
    if (cpu == BIG_CPU) {
        table = BIG_FREQUENCY_TABLE;
        num_freqs = NUM_BIG_FREQUENCIES;
    } else {
        table = LITTLE_FREQUENCY_TABLE;
        num_freqs = NUM_LITTLE_FREQUENCIES;
    }
    
    for (int i = 0; i < num_freqs; i++) {
        if (freq == table[i]) return i;
    }
    
    // If not exact match, find closest
    int closest_idx = 0;
    int min_diff = abs(freq - table[0]);
    for (int i = 1; i < num_freqs; i++) {
        int diff = abs(freq - table[i]);
        if (diff < min_diff) {
            min_diff = diff;
            closest_idx = i;
        }
    }
    return closest_idx;
}

int frequency_step(int current_freq, int steps, processor cpu) {
    const int *table;
    int num_freqs;
    
    if (cpu == BIG_CPU) {
        table = BIG_FREQUENCY_TABLE;
        num_freqs = NUM_BIG_FREQUENCIES;
    } else {
        table = LITTLE_FREQUENCY_TABLE;
        num_freqs = NUM_LITTLE_FREQUENCIES;
    }
    
    int current_idx = get_frequency_index(current_freq, cpu);
    int new_idx = current_idx + steps;
    
    // Clamp to valid range
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= num_freqs) new_idx = num_freqs - 1;
    
    return table[new_idx];
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
                                             double latency_adjustment,
                                             bool fps_met,
                                             bool latency_met) {
    double steps = (int) round(fps_adjustment + latency_adjustment);
    
    if (!latency_met) {
        int old_big = config->big_frequency;
        config->big_frequency = frequency_step(old_big, steps, BIG_CPU);
        if (config->big_frequency == old_big) {
            config->little_frequency = frequency_step(config->little_frequency, steps, LITTLE_CPU);
        }
        return;
    }

    if (!fps_met) {
        config->big_frequency = frequency_step(config->big_frequency, steps, BIG_CPU);
        config->little_frequency = frequency_step(config->little_frequency, steps, LITTLE_CPU);
    }

    printf("  [freq-adj] combined_steps=%.2f (fps=%.2f + lat=%.2f) | big: idx %d->%d (%d->%d kHz, %+d steps) | little: idx %d->%d (%d->%d kHz, %+d steps)\n",
           combined_steps, fps_adjustment, latency_adjustment,
           old_big_idx, new_big_idx, old_big, config->big_frequency, steps,
           old_little_idx, new_little_idx, old_little, config->little_frequency, steps);
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
            config->partition_point2 = (pp2 - 1 < pp1) ? pp1 : pp2 - 1;
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: shifting work from big CPU to little CPU (pp2: %d -> %d)\n", 
                   pp2, config->partition_point2);
        } else if (rel_margin > 0.2 && pp1 < pp2 - 1 /* Consider this to avoid a GPU bottleneck where overhead outweights GPU capacity: && pp1 > 2 */) {
            config->partition_point1 = (pp1 - 1 < 1) ? 1 : pp1 - 1;
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
                config->partition_point2 = (pp2 + 1 > TOTAL_LAYERS) ? TOTAL_LAYERS : pp2 + 1;
                gov->partition_step_cooldown = 3;
                printf("[PID] Partition: shifting work from little CPU to big CPU (pp2: %d -> %d)\n", 
                       pp2, config->partition_point2);
            } else if (pp1 > 0) {
                config->partition_point1 = pp1 + 1;
                config->partition_point2 = pp2 + 1;
                gov->partition_step_cooldown = 3;
                printf("[PID] Partition: shifting work from little CPU to GPU (pp1: %d -> %d, pp2: %d -> %d)\n", 
                       pp1, config->partition_point1, pp2, config->partition_point2);
            }
        }
    }
}

static bool try_reduce_power(PIDGovernor *gov, PipelineConfig *config, 
                             stats_t *stats, double margin_fps, double margin_latency) {
    printf("  [power-reduce] checking: fps_margin=%.2f (%.1f%%), lat_margin=%.2fms (%.1f%%)\n",
           margin_fps, 100.0 * margin_fps / gov->target_fps,
           margin_latency, 100.0 * margin_latency / gov->target_latency);
    
    if (margin_fps <= 0 && margin_latency <= 0) {
        printf("  [power-reduce] no margin available, cannot reduce\n");
        return false;
    }
    
    PipelineConfig test_config = *config;
    bool reduced = false;
    
    double usable_margin = fmin(margin_fps / gov->target_fps, 
                                 margin_latency / gov->target_latency);
    double average_margin = (margin_fps + margin_latency) / 2;

    printf("  [power-reduce] usable_margin=%.1f%% (min of fps/lat margins)\n", usable_margin * 100.0);
    
    if (usable_margin > 0.05) {
        int big_step = (int)(config->big_frequency * gov->power_reduction_rate);
        int new_big = snap_to_valid_frequency(config->big_frequency - big_step, BIG_CPU);
        
        if (new_big < config->big_frequency) {
            printf("  [power-reduce] margin>5%%: reducing big freq %d -> %d (step=%d)\n",
                   config->big_frequency, new_big, big_step);
            test_config.big_frequency = new_big;
            reduced = true;
        } else {
            printf("  [power-reduce] margin>5%% but big freq already at minimum %d\n", config->big_frequency);
        }
    } else {
        printf("  [power-reduce] margin<=5%%, skipping big freq reduction\n");
    }
    
    if (usable_margin > 0.1) {
        int little_step = (int)(config->little_frequency * gov->power_reduction_rate);
        int new_little = snap_to_valid_frequency(config->little_frequency - little_step, LITTLE_CPU);
        
        if (new_little < config->little_frequency) {
            printf("  [power-reduce] margin>10%%: reducing little freq %d -> %d (step=%d)\n",
                   config->little_frequency, new_little, little_step);
            test_config.little_frequency = new_little;
            reduced = true;
        } else {
            printf("  [power-reduce] margin>10%% but little freq already at minimum %d\n", config->little_frequency);
        }
    } else {
        printf("  [power-reduce] margin<=10%%, skipping little freq reduction\n");
    }
    
    if (usable_margin > 0.15) {
        printf("  [power-reduce] margin>15%%: considering partition adjustment\n");
        pid_governor_adjust_partition_points(gov, &test_config, margin_fps, margin_latency, true);
        if (test_config.partition_point1 != config->partition_point1 ||
            test_config.partition_point2 != config->partition_point2) {
            reduced = true;
        }
    } else {
        printf("  [power-reduce] margin<=15%%, skipping partition adjustment\n");
    }
    
    if (reduced) {
        printf("  [power-reduce] applied reductions to config\n");
        *config = test_config;
    } else {
        printf("  [power-reduce] no reductions possible\n");
    }
    
    return reduced;
}

PIDResult pid_governor_step(PIDGovernor *gov, PipelineConfig *config,
                            stats_t *stats, double *estimated_power) {
    gov->iteration++;
    
    if (gov->iteration > gov->max_iterations) {
        printf("[PID] MAX_ITERATIONS reached (%d), stopping\n", gov->max_iterations);
        return PID_MAX_ITERATIONS;
    }
    
    double fps_error = (gov->target_fps - stats->fps) / gov->target_fps;
    double latency_error = (stats->latency - gov->target_latency) / gov->target_latency;
    
    bool latency_worsened = false;
    if (gov->has_prev) latency_worsened = (stats->latency > gov->prev_latency + 1.0); // +1ms noise margin
    gov->prev_latency = stats->latency;
    gov->prev_fps = stats->fps;
    gov->has_prev = true;

    double dt = 1.0;
    
    bool fps_met = stats->fps >= gov->target_fps;
    bool latency_met = stats->latency <= gov->target_latency;
    
    printf("[PID] iter=%d fps=%.2f (target=%.2f, dev=%.4f) lat=%.2f (target=%.2f, dev=%.4f) pp1=%d pp2=%d\n",
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

        const int max_big_freq = BIG_FREQUENCY_TABLE[NUM_BIG_FREQUENCIES - 1];
        const int max_little_freq = LITTLE_FREQUENCY_TABLE[NUM_LITTLE_FREQUENCIES - 1];
        const bool big_at_max = (config->big_frequency >= max_big_freq);
        const bool little_at_max = (config->little_frequency >= max_little_freq);
        const bool both_at_max = (big_at_max && little_at_max);
        
        printf("  [PID] targets NOT met: fps_met=%s, latency_met=%s\n",
               fps_met ? "YES" : "NO", latency_met ? "YES" : "NO");
        
        if (!fps_met) {
            printf("  [PID-fps] computing adjustment (fps=%.2f < target=%.2f):\n", stats->fps, gov->target_fps);
            if (both_at_max) {
                printf("  [PID-fps] both freqs at max (big=%d, little=%d), skipping frequency increase\n",
                       config->big_frequency, config->little_frequency);
                fps_adjustment = 0.0;
            } else {
                fps_adjustment = pid_update(&gov->fps_pid, fps_error, dt);
            }
        } else {
            printf("  [PID-fps] target met, resetting PID state\n");
            pid_reset(&gov->fps_pid);
        }
        
        if (!latency_met) {
            printf("  [PID-lat] computing adjustment (lat=%.2f > target=%.2f):\n", stats->latency, gov->target_latency);
            if (both_at_max) {
                printf("  [PID-lat] both freqs at max (big=%d, little=%d), skipping frequency increase\n",
                       config->big_frequency, config->little_frequency);
                latency_adjustment = 0.0;
            } else {
                latency_adjustment = pid_update(&gov->latency_pid, latency_error, dt);
            }
        } else {
            printf("  [PID-lat] target met, resetting PID state\n");
            pid_reset(&gov->latency_pid);
        }

        if (!both_at_max || latency_worsened) {
            pid_governor_apply_frequency_adjustment(gov, config, fps_adjustment, latency_adjustment, fps_met, latency_met);
        } else {
            gov->partition_step_cooldown = 0;
        }
        
        double fps_margin = stats->fps - gov->target_fps;
        double latency_margin = gov->target_latency - stats->latency;
        pid_governor_adjust_partition_points(gov, config, fps_margin, latency_margin, false);
        
        printf("[PID] Adjusting: fps_steps=%.2f, lat_steps=%.2f -> big_freq=%d, little_freq=%d, pp1=%d, pp2=%d\n",
               fps_adjustment, latency_adjustment, config->big_frequency, config->little_frequency,
               config->partition_point1, config->partition_point2);
    }
    
    *estimated_power = estimate_power(config);
    return PID_CONTINUE;
}
