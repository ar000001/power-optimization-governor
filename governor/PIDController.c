#include "PIDController.h"
#include "ApproximationModels.h"
#include "PipelineConfig.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>



static void pid_apply_partition_move(PipelineConfig *config, int dpp1, int dpp2) {
    if (!config) return;

    const int orig_pp1 = config->partition_point1;
    const int orig_pp2 = config->partition_point2;

    int target_pp1 = orig_pp1 + dpp1;
    int target_pp2 = orig_pp2 + dpp2;

    if (target_pp1 < 1) target_pp1 = 1;
    if (target_pp1 > TOTAL_LAYERS) target_pp1 = TOTAL_LAYERS;

    if (target_pp2 < 1) target_pp2 = 1;
    if (target_pp2 > TOTAL_LAYERS) target_pp2 = TOTAL_LAYERS;
    if (target_pp2 < target_pp1) target_pp2 = target_pp1;

    int best_pp1 = orig_pp1;
    int best_pp2 = orig_pp2;
    int best_cost = 1000000000;
    int best_orig_cost = 1000000000;

    for (int pp1 = 1; pp1 <= TOTAL_LAYERS; pp1++) {
        for (int pp2 = pp1; pp2 <= TOTAL_LAYERS; pp2++) {
            const int s1 = pp1;
            const int s2 = pp2 - pp1;
            const int s3 = TOTAL_LAYERS - pp2;

            if (s1 == 1 || s2 == 1 || s3 == 1) continue;

            if (dpp1 > 0 && pp1 <= orig_pp1) continue;
            if (dpp1 < 0 && pp1 >= orig_pp1) continue;
            if (dpp2 > 0 && pp2 <= orig_pp2) continue;
            if (dpp2 < 0 && pp2 >= orig_pp2) continue;

            const int cost = abs(pp1 - target_pp1) + abs(pp2 - target_pp2);
            const int orig_cost = abs(pp1 - orig_pp1) + abs(pp2 - orig_pp2);

            if (cost < best_cost || (cost == best_cost && orig_cost < best_orig_cost)) {
                best_cost = cost;
                best_orig_cost = orig_cost;
                best_pp1 = pp1;
                best_pp2 = pp2;
            }
        }
    }

    if (best_cost >= 1000000000) {
        config->partition_point1 = target_pp1;
        config->partition_point2 = target_pp2;
        enforce_no_single_layer_stages(config);
        return;
    }

    config->partition_point1 = best_pp1;
    config->partition_point2 = best_pp2;
}

static double pid_governor_constraint_violation(const PIDGovernor *gov, const stats_t *stats) {
    double fps_deficit = 0.0;
    double lat_excess = 0.0;

    if (stats->fps < gov->target_fps) {
        fps_deficit = (gov->target_fps - stats->fps) / gov->target_fps;
    }
    if (stats->latency > gov->target_latency) {
        lat_excess = (stats->latency - gov->target_latency) / gov->target_latency;
    }

    return fmax(fps_deficit, lat_excess);
}

static void pid_governor_maybe_update_best(PIDGovernor *gov, const PipelineConfig *config,
                                          const stats_t *stats, double estimated_power) {
    const bool meets_targets = (stats->fps >= gov->target_fps) && (stats->latency <= gov->target_latency);
    const double violation = pid_governor_constraint_violation(gov, stats);

    if (!gov->best_valid) {
        gov->best_config = *config;
        gov->best_estimated_power = estimated_power;
        gov->best_violation = violation;
        gov->best_meets_targets = meets_targets;
        gov->best_valid = true;
        return;
    }

    if (violation < gov->best_violation - 1e-12) {
        gov->best_config = *config;
        gov->best_estimated_power = estimated_power;
        gov->best_violation = violation;
        gov->best_meets_targets = meets_targets;
        return;
    }

    if (fabs(violation - gov->best_violation) <= 1e-12) {
        if (estimated_power < gov->best_estimated_power - 1e-12) {
            gov->best_config = *config;
            gov->best_estimated_power = estimated_power;
            gov->best_violation = violation;
            gov->best_meets_targets = meets_targets;
        }
    }
}

BottleneckStage detect_bottleneck(stats_t *stats, double *bottleneck_ratio) {
    double stage1 = stats->stage1_inference_time;
    double stage2 = stats->stage2_inference_time;
    double stage3 = stats->stage3_inference_time;
    
    double total = stage1 + stage2 + stage3;

    if (total <= 0.0) {
        if (bottleneck_ratio) {
            *bottleneck_ratio = 0.0;
        }
        return BOTTLENECK_NONE;
    }
    
    double max_time = stage1;
    BottleneckStage bottleneck = BOTTLENECK_STAGE1_GPU;
    
    if (stage2 > max_time) {
        max_time = stage2;
        bottleneck = BOTTLENECK_STAGE2_BIG;
    }
    if (stage3 > max_time) {
        max_time = stage3;
        bottleneck = BOTTLENECK_STAGE3_LITTLE;
    }
    
    double ratio = max_time / total;
    if (bottleneck_ratio) {
        *bottleneck_ratio = ratio;
    }

    if (ratio < BOTTLENECK_RATIO_THRESHOLD) {
        return BOTTLENECK_NONE;
    }
    
    return bottleneck;
}

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
    pid_init(&gov->fps_pid, 2.0, 0.5, 0.0, -2.0, 2.0);
    pid_init(&gov->latency_pid, 2.0, 0.3, 0.0, -2.0, 2.0);
    
    gov->target_fps = target_fps;
    gov->target_latency = target_latency;
    gov->power_reduction_rate = 0.05;
    gov->iteration = 0;
    gov->max_iterations = max_iterations;
    gov->converged = false;
    gov->partition_step_cooldown = 0;
    gov->has_prev = false;

    gov->has_last_config = false;
    gov->same_config_streak = 0;

    gov->best_valid = false;
    gov->best_meets_targets = false;
    gov->best_estimated_power = 0.0;
    gov->best_violation = 0.0;
}

void pid_governor_reset_best(PIDGovernor *gov) {
    gov->best_valid = false;
    gov->best_meets_targets = false;
    gov->best_estimated_power = 0.0;
    gov->best_violation = 0.0;
}

bool pid_governor_get_best(const PIDGovernor *gov, PipelineConfig *out_config,
                           double *out_estimated_power, bool *out_meets_targets) {
    if (!gov->best_valid) {
        return false;
    }

    if (out_config) {
        *out_config = gov->best_config;
    }
    if (out_estimated_power) {
        *out_estimated_power = gov->best_estimated_power;
    }
    if (out_meets_targets) {
        *out_meets_targets = gov->best_meets_targets;
    }

    return true;
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

    double combined_steps = fps_adjustment + latency_adjustment;
    int steps = (int) round(combined_steps);

    int old_big = config->big_frequency;
    int old_little = config->little_frequency;
    int old_big_idx = get_frequency_index(old_big, BIG_CPU);
    int old_little_idx = get_frequency_index(old_little, LITTLE_CPU);
    
    if (!latency_met) {
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

    int new_big_idx = get_frequency_index(config->big_frequency, BIG_CPU);
    int new_little_idx = get_frequency_index(config->little_frequency, LITTLE_CPU);

    printf("  [freq-adj] combined_steps=%.2f (fps=%.2f + lat=%.2f) | big: idx %d->%d (%d->%d kHz, %+d steps) | little: idx %d->%d (%d->%d kHz, %+d steps)\n",
           combined_steps, fps_adjustment, latency_adjustment,
           old_big_idx, new_big_idx, old_big, config->big_frequency, steps,
           old_little_idx, new_little_idx, old_little, config->little_frequency, steps);
}

void pid_governor_adjust_partition_points(PIDGovernor *gov, PipelineConfig *config,
                                          double fps_margin, double latency_margin,
                                          bool reduce_power, bool force_partition) {
    if (gov->partition_step_cooldown > 0) {
        gov->partition_step_cooldown--;
        return;
    }
    
    int pp1 = config->partition_point1;
    int pp2 = config->partition_point2;

    int prev_pp1 = pp1;
    int prev_pp2 = pp2;
    
    if (reduce_power) {
        double rel_margin = fmin(fps_margin / gov->target_fps, 
                                  latency_margin / gov->target_latency);
        
        if (rel_margin > 0.2 && pp1 > 1) {
            pid_apply_partition_move(config, -1, 0);
            if(prev_pp1 == config->partition_point1 && prev_pp2 == config->partition_point2) {
                gov->partition_step_cooldown = 3;
                printf("[PID] Tried to shift work from GPU to big CPU, but couldn't because risking bottleneck of GPU. Therefore, shifting from little CPU to big CPU\n");
                pid_apply_partition_move(config, 0, -1);
                printf("[PID] Partition: shifting work from big CPU to little CPU (pp2: %d -> %d)\n", 
                       pp2, config->partition_point2);
            }
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: shifting work from GPU to big CPU (pp1: %d -> %d)\n", 
                   pp1, config->partition_point1);
        } else if (rel_margin > 0.15 && pp2 > pp1) {
            pid_apply_partition_move(config, 0, -1);
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: shifting work from big CPU to little CPU (pp2: %d -> %d)\n", 
                   pp2, config->partition_point2);
        }

        enforce_no_single_layer_stages(config);

    } else {


        double fps_deficit = -fps_margin / gov->target_fps;
        double lat_deficit = -latency_margin / gov->target_latency;
        double deficit = fmax(fps_deficit, lat_deficit);


        if (force_partition) {

            if (deficit > 0.15 && pp1 < TOTAL_LAYERS && pp2 < TOTAL_LAYERS) {
                pid_apply_partition_move(config, +1, +1);
            } else if (pp2 < TOTAL_LAYERS) {
                pid_apply_partition_move(config, 0, +1);
            } else if (pp1 < TOTAL_LAYERS) {
                pid_apply_partition_move(config, +1, 0);
            } else {
                return;
            }
            enforce_no_single_layer_stages(config);
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: forced change (pp1: %d -> %d, pp2: %d -> %d, deficit: %.2f)\n",
                   pp1, config->partition_point1, pp2, config->partition_point2, deficit);
            return;
        }

        printf("[PID] Partition: changing partition points (fps_deficit: %.2f, lat_deficit: %.2f, deficit: %.2f)\n", 
               fps_deficit, lat_deficit, deficit);

        if (deficit > 0.2 && pp1 < TOTAL_LAYERS) {
            //shifting form big to GPU
            pid_apply_partition_move(config, +1, 0);
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: shifting work from big CPU to GPU (pp1: %d -> %d)\n", 
                    pp1, config->partition_point1);
        } else if (deficit > 0.15 && (pp1 < TOTAL_LAYERS || pp2 < TOTAL_LAYERS)) {
            pid_apply_partition_move(config, +1, +1);
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: shifting work from little CPU to GPU (pp1: %d -> %d, pp2: %d -> %d)\n", 
                    pp1, config->partition_point1, pp2, config->partition_point2);
        } else if (deficit > 0.1 && pp2 < TOTAL_LAYERS) {
            pid_apply_partition_move(config, 0, +1);
            gov->partition_step_cooldown = 3;
            printf("[PID] Partition: shifting work from little CPU to big CPU (pp2: %d -> %d)\n", 
                    pp2, config->partition_point2);
        }

        enforce_no_single_layer_stages(config);
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

    bool little_at_min = config->little_frequency <= LITTLE_FREQUENCY_TABLE[0];
    bool big_at_min = config->big_frequency <= BIG_FREQUENCY_TABLE[0];

    BottleneckStage bottleneck = detect_bottleneck(stats, NULL);
    
    double rel_fps_margin = margin_fps / gov->target_fps;
    double rel_lat_margin = margin_latency / gov->target_latency;
    double usable_margin = fmin(rel_fps_margin, rel_lat_margin);
    double margin_ratio = (usable_margin > 1e-6) ? fmax(rel_fps_margin, rel_lat_margin) / usable_margin : 0.0;
    bool margin_imbalanced = (margin_ratio > 3.0 || fmax(rel_fps_margin, rel_lat_margin) > 0.25) && (fmax(rel_fps_margin, rel_lat_margin) > 0.1);

    printf("  [power-reduce] usable_margin=%.1f%% (min of fps=%.1f%%, lat=%.1f%%), ratio=%.1f, imbalanced=%s\n", 
           usable_margin * 100.0, rel_fps_margin * 100.0, rel_lat_margin * 100.0, 
           margin_ratio, margin_imbalanced ? "YES" : "NO");

    bool fps_slack_latency_tight = (rel_lat_margin > 0.0) && (rel_fps_margin > rel_lat_margin) && (rel_lat_margin < 0.05) && (rel_fps_margin > 0.10);

    if (fps_slack_latency_tight) {
        double t1 = stats->stage1_inference_time;
        double t2 = stats->stage2_inference_time;
        double t3 = stats->stage3_inference_time;

        BottleneckStage max_stage = BOTTLENECK_STAGE1_GPU;
        double max_time = t1;
        if (t2 > max_time) {
            max_time = t2;
            max_stage = BOTTLENECK_STAGE2_BIG;
        }
        if (t3 > max_time) {
            max_time = t3;
            max_stage = BOTTLENECK_STAGE3_LITTLE;
        }

        BottleneckStage target_stage = (bottleneck != BOTTLENECK_NONE) ? bottleneck : max_stage;

        printf("  [power-reduce] fps slack / latency tight: targeting stage=%d (t1=%.3fms t2=%.3fms t3=%.3fms)\n",
               (int)target_stage, t1, t2, t3);

        if (target_stage == BOTTLENECK_STAGE2_BIG) {
            const int pp1 = config->partition_point1;

            if (pp1 < TOTAL_LAYERS) {
                pid_apply_partition_move(&test_config, +1, 0);

                if (test_config.partition_point1 != config->partition_point1 ||
                    test_config.partition_point2 != config->partition_point2) {
                    printf("  [power-reduce] latency-tight move: shifting work from big to GPU (pp1: %d -> %d)\n",
                           pp1, test_config.partition_point1);
                    *config = test_config;
                    gov->partition_step_cooldown = 2;
                    enforce_no_single_layer_stages(config);
                    return true;
                }
            }

            printf("  [power-reduce] latency-tight move: no partition change possible\n");
        }
    }

    bool fps_tight_latency_slack = (rel_fps_margin > 0.0) && (rel_lat_margin > rel_fps_margin) && (rel_fps_margin < 0.05) && (rel_lat_margin > 0.10);

    if (fps_tight_latency_slack) {
        double t1 = stats->stage1_inference_time;
        double t2 = stats->stage2_inference_time;
        double t3 = stats->stage3_inference_time;

        BottleneckStage max_stage = BOTTLENECK_STAGE1_GPU;
        double max_time = t1;
        if (t2 > max_time) {
            max_time = t2;
            max_stage = BOTTLENECK_STAGE2_BIG;
        }
        if (t3 > max_time) {
            max_time = t3;
            max_stage = BOTTLENECK_STAGE3_LITTLE;
        }

        BottleneckStage target_stage = (bottleneck != BOTTLENECK_NONE) ? bottleneck : max_stage;

        printf("  [power-reduce] fps tight / latency slack: targeting bottleneck stage=%d (t1=%.3fms t2=%.3fms t3=%.3fms)\n",
               (int)target_stage, t1, t2, t3);

        if (target_stage == BOTTLENECK_STAGE2_BIG) {
            int big_step = (int)(config->big_frequency * (gov->power_reduction_rate * 0.5));
            if (big_step < 1) big_step = 1;
            int requested_big = config->big_frequency - big_step;
            int new_big = snap_to_valid_frequency(requested_big, BIG_CPU);

            if (!big_at_min && new_big < config->big_frequency) {
                printf("  [power-reduce] targeted: reducing big freq %d -> %d (step=%d)\n",
                       config->big_frequency, new_big, big_step);
                test_config.big_frequency = new_big;
                reduced = true;
            }
        } else if (target_stage == BOTTLENECK_STAGE3_LITTLE) {
            int little_step = (int)(config->little_frequency * (gov->power_reduction_rate * 0.5));
            if (little_step < 1) little_step = 1;
            int requested_little = config->little_frequency - little_step;
            int new_little = snap_to_valid_frequency(requested_little, LITTLE_CPU);

            if (!little_at_min && new_little < config->little_frequency) {
                printf("  [power-reduce] targeted: reducing little freq %d -> %d (step=%d)\n",
                       config->little_frequency, new_little, little_step);
                test_config.little_frequency = new_little;
                reduced = true;
            }
        }
    }
    
    if (usable_margin > 0.05) {
        int big_step = (int)(config->big_frequency * gov->power_reduction_rate);
        int requested_big = config->big_frequency - big_step;
        int new_big = snap_to_valid_frequency(requested_big, BIG_CPU);
        
        if (!big_at_min && new_big < config->big_frequency) {
            printf("  [power-reduce] margin>5%%: reducing big freq %d -> %d (step=%d)\n",
                   config->big_frequency, new_big, big_step);
            test_config.big_frequency = new_big;
            reduced = true;
        } else if (big_at_min) {
            printf("  [power-reduce] margin>5%% but big freq already at minimum %d\n", BIG_FREQUENCY_TABLE[0]);
        } else {
            printf("  [power-reduce] margin>5%% but big freq not reduced (requested=%d, snapped=%d, step=%d)\n",
                   requested_big, new_big, big_step);
        }
    } else {
        printf("  [power-reduce] margin<=5%%, skipping big freq reduction\n");
    }
    
    if (usable_margin > 0.1) {
        int little_step = (int)(config->little_frequency * gov->power_reduction_rate);
        int requested_little = config->little_frequency - little_step;
        int new_little = snap_to_valid_frequency(requested_little, LITTLE_CPU);
        
        if (!little_at_min && new_little < config->little_frequency) {
            printf("  [power-reduce] margin>10%%: reducing little freq %d -> %d (step=%d)\n",
                   config->little_frequency, new_little, little_step);
            test_config.little_frequency = new_little;
            reduced = true;
        } else if (little_at_min) {
            printf("  [power-reduce] margin>10%% but little freq already at minimum %d\n", LITTLE_FREQUENCY_TABLE[0]);
        } else {
            printf("  [power-reduce] margin>10%% but little freq not reduced (requested=%d, snapped=%d, step=%d)\n",
                   requested_little, new_little, little_step);
        }
    } else {
        printf("  [power-reduce] margin<=10%%, skipping little freq reduction\n");
    }
    
    if (usable_margin > 0.15) {
        printf("  [power-reduce] margin>15%%: considering partition adjustment\n");
        pid_governor_adjust_partition_points(gov, &test_config, margin_fps, margin_latency, true, false);
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
        enforce_no_single_layer_stages(config);
    } else if (margin_imbalanced) {
        const double min_margin = fmin(rel_fps_margin, rel_lat_margin);

        if (min_margin >= 0.10) {
            printf("  [power-reduce] no reductions but margins imbalanced (%.1f%% vs %.1f%%), trying partition rebalance\n",
                   rel_fps_margin * 100.0, rel_lat_margin * 100.0);

            // Try to rebalance by adjusting partition points to trade excess margin for tighter constraint
            int pp1 = config->partition_point1;
            int pp2 = config->partition_point2;

            if (rel_fps_margin > rel_lat_margin) {
                // FPS has excess margin, latency is tight -> shift work to slower processors
                if (pp1 > 1) {
                    pid_apply_partition_move(&test_config, -1, 0);
                    printf("  [power-reduce] rebalance: shifting work from GPU to big (pp1: %d -> %d)\n",
                           pp1, test_config.partition_point1);
                    if (pp1 == config->partition_point1 && pp2 == config->partition_point2) {
                        gov->partition_step_cooldown = 3;
                        printf("[PID] Tried to shift work from GPU to big CPU, but couldn't because risking bottleneck of GPU. Therefore, shifting from little CPU to big CPU\n");
                        pid_apply_partition_move(&test_config, 0, -1);
                        printf("[PID] Partition: shifting work from big CPU to little CPU (pp2: %d -> %d)\n", 
                            pp2, test_config.partition_point2);
                    }
                    *config = test_config;
                    gov->partition_step_cooldown = 2;
                    return true;
                } else if (pp2 > pp1) {
                    pid_apply_partition_move(&test_config, 0, -1);
                    printf("  [power-reduce] rebalance: shifting work from big to little (pp2: %d -> %d)\n",
                           pp2, test_config.partition_point2);
                    *config = test_config;
                    gov->partition_step_cooldown = 2;
                    return true;
                }
            } else {
                // Latency has excess margin, FPS is tight -> shift work to faster processors
                if (pp2 < TOTAL_LAYERS) {
                    pid_apply_partition_move(&test_config, 0, +1);
                    printf("  [power-reduce] rebalance: shifting work from little to big (pp2: %d -> %d)\n",
                           pp2, test_config.partition_point2);
                    *config = test_config;
                    gov->partition_step_cooldown = 2;
                    return true;
                } else if (pp1 < TOTAL_LAYERS) {
                    pid_apply_partition_move(&test_config, +1, 0);
                    printf("  [power-reduce] rebalance: shifting work from big to GPU (pp1: %d -> %d)\n",
                           pp1, test_config.partition_point1);
                    *config = test_config;
                    gov->partition_step_cooldown = 2;
                    return true;
                }
            }
            printf("  [power-reduce] rebalance: no partition changes possible\n");
        } else {
            printf("  [power-reduce] margins imbalanced but not comfortably above thresholds (min=%.1f%%), skipping rebalance\n",
                   min_margin * 100.0);
        }
    } else {
        printf("  [power-reduce] no reductions possible\n");
    }
    
    return reduced;
}

PIDResult pid_governor_step(PIDGovernor *gov, PipelineConfig *config,
                            stats_t *stats, double *estimated_power) {
    gov->iteration++;

    enforce_no_single_layer_stages(config);

    if (gov->has_last_config && memcmp(&gov->last_config, config, sizeof(*config)) == 0) {
        gov->same_config_streak++;
    } else {
        gov->same_config_streak = 1;
        gov->last_config = *config;
        gov->has_last_config = true;
    }

    if (gov->same_config_streak >= 4) {
        gov->converged = true;
        if (gov->best_valid) {
            *config = gov->best_config;
        }
        *estimated_power = estimate_power(config);
        pid_governor_maybe_update_best(gov, config, stats, *estimated_power);
        printf("[PID] Converged: pipeline configuration unchanged for %d iterations\n", gov->same_config_streak);
        return PID_CONVERGED;
    }

    *estimated_power = estimate_power(config);
    const double total_inference_time = stats->stage1_inference_time + stats->stage2_inference_time + stats->stage3_inference_time;
    printf("[PID-LOG] iter=%d power=%.3fW stage1=%.3fms stage2=%.3fms stage3=%.3fms total=%.3fms\n",
           gov->iteration, *estimated_power,
           stats->stage1_inference_time, stats->stage2_inference_time, stats->stage3_inference_time,
           total_inference_time);
    pid_governor_maybe_update_best(gov, config, stats, *estimated_power);
    
    if (gov->iteration > gov->max_iterations) {
        printf("[PID] MAX_ITERATIONS reached (%d), stopping\n", gov->max_iterations);

        if (gov->best_valid) {
            *config = gov->best_config;
            *estimated_power = gov->best_estimated_power;
        }
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
            pid_governor_maybe_update_best(gov, config, stats, *estimated_power);
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

        if (!both_at_max && !latency_worsened) {
            pid_governor_apply_frequency_adjustment(gov, config, fps_adjustment, latency_adjustment, fps_met, latency_met);
        } else {
            gov->partition_step_cooldown = 0;
        }
        
        double fps_margin = stats->fps - gov->target_fps;
        double latency_margin = gov->target_latency - stats->latency;
        pid_governor_adjust_partition_points(gov, config, fps_margin, latency_margin, false, both_at_max);
        
        printf("[PID] Adjusting: fps_steps=%.2f, lat_steps=%.2f -> big_freq=%d, little_freq=%d, pp1=%d, pp2=%d\n",
               fps_adjustment, latency_adjustment, config->big_frequency, config->little_frequency,
               config->partition_point1, config->partition_point2);
    }

    enforce_no_single_layer_stages(config);
    
    *estimated_power = estimate_power(config);
    return PID_CONTINUE;
}
