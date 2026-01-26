
#include "ApproximationModels.h"
#include "PipelineConfig.h"
#include <stdio.h>
#include <math.h>



void get_workload_fractions(int pp1, int pp2,
                            double *gpu_frac,
                            double *big_frac,
                            double *little_frac) {    
    int gpu_layers = pp1;
    int big_layers = pp2 - pp1;
    int little_layers = TOTAL_LAYERS - pp2;
    
    *gpu_frac = (double)gpu_layers / TOTAL_LAYERS;
    *big_frac = (double)big_layers / TOTAL_LAYERS;
    *little_frac = (double)little_layers / TOTAL_LAYERS;
}



//From literature:
// Colburn, Shane & Chu, Yi & Shlizerman, Eli & Majumdar, Arka. (2018). An Optical Frontend for a Convolutional Neural Network. 10.48550/arXiv.1901.03661. 
// a rough approximation of the different workloads of the layer. Crucial bc in AlexNet the distribution is very different.
static const double LAYER_WEIGHTS[TOTAL_LAYERS] = {
    0.20, 0.25, 0.15, 0.15, 0.10, 0.08, 0.05, 0.02
};

static double compute_weighted_fraction(int start_layer, int end_layer) {
    
    double sum = 0.0;
    for (int i = start_layer; i < end_layer; i++) {
        sum += LAYER_WEIGHTS[i];
    }
    return sum;
}

double estimate_power(PipelineConfig *config) {
    
    double w_gpu = compute_weighted_fraction(0, config->partition_point1);
    double w_big = compute_weighted_fraction(config->partition_point1, config->partition_point2);
    double w_little = compute_weighted_fraction(config->partition_point2, TOTAL_LAYERS);
    
    double p_big_total = fx_power_bcpu((double)config->big_frequency);
    double p_little_total = fx_power_lcpu((double)config->little_frequency);
    
    double power = 0.0;
    
    if (w_gpu > 0) {
        power += GPU_POWER * w_gpu; //GPU power is constant
    }
    
    if (w_big > 0) {
        power += p_big_total * w_big; //Big CPU power is frequency dependent (using best fit above)
    }
    
    if (w_little > 0) {
        power += p_little_total * w_little; //Little CPU power is frequency dependent 
    }
    
    int active_stages = (w_gpu > 0 ? 1 : 0) + 
                        (w_big > 0 ? 1 : 0) + 
                        (w_little > 0 ? 1 : 0);
    
    if (active_stages > 1) {
        // From experiment 3: 9.412e-07 * big_freq - 3.230e-08 * little_freq
        // Power variation from lowest to highest freq pairs:: 0.454 to 2.02 -> 1.57
        // Observed power variation in exp3: 0.63W
        double raw_overhead = 9.412e-07 * (double)config->big_frequency 
                            - 3.230e-08 * (double)config->little_frequency;
        double scaled_overhead = (0.63/1.57) * raw_overhead;
        
        //constant overhead for pipeline synchronization (found by trial and error)
        double base_overhead = 0.47;
        
        double stage_factor = (double)(active_stages - 1) / 2.0;
        power += (base_overhead + scaled_overhead) * stage_factor;
    }
    
    return power;
}


void get_frequency_neighbors(double frequency, processor cpu, int *left, int *right) {
    
    if (cpu == BIG_CPU) {

        if (frequency > BIG_FREQUENCY_TABLE[NUM_BIG_FREQUENCIES-1]) {
            *left = BIG_FREQUENCY_TABLE[NUM_BIG_FREQUENCIES-1];
            *right = BIG_FREQUENCY_TABLE[NUM_BIG_FREQUENCIES-1];
            return;
        }

        for(int i = 1; i < NUM_BIG_FREQUENCIES-1; i++) {
            if (frequency > BIG_FREQUENCY_TABLE[i-1] && frequency < BIG_FREQUENCY_TABLE[i+1]) {
                *left = BIG_FREQUENCY_TABLE[i-1];
                *right = BIG_FREQUENCY_TABLE[i+1];
                return;
            }
        }
    } else if (cpu == LITTLE_CPU) {

        if (frequency > LITTLE_FREQUENCY_TABLE[NUM_LITTLE_FREQUENCIES-1]) {
            *left = LITTLE_FREQUENCY_TABLE[NUM_LITTLE_FREQUENCIES-1];
            *right = LITTLE_FREQUENCY_TABLE[NUM_LITTLE_FREQUENCIES-1];
            return;
        }

        for(int i = 1; i < NUM_LITTLE_FREQUENCIES-1; i++) {
            if (frequency > LITTLE_FREQUENCY_TABLE[i-1] && frequency < LITTLE_FREQUENCY_TABLE[i+1]) {
                *left = LITTLE_FREQUENCY_TABLE[i-1];
                *right = LITTLE_FREQUENCY_TABLE[i+1];
                return;
            }
        }
    }
}

typedef struct {
    double fps;
    double latency;
} GridPoint;

// Grid data: grid[big_freq_idx][little_freq_idx]
static GridPoint measurement_grid[NUM_BIG_FREQUENCIES][NUM_LITTLE_FREQUENCIES];
static double grid_fps_min, grid_fps_max;
static double grid_latency_min, grid_latency_max;
static int grid_loaded = 0;

static int freq_to_big_idx(int freq) {
    for (int i = 0; i < NUM_BIG_FREQUENCIES; i++) {
        if (BIG_FREQUENCY_TABLE[i] == freq) return i;
    }
    return -1;
}

static int freq_to_little_idx(int freq) {
    for (int i = 0; i < NUM_LITTLE_FREQUENCIES; i++) {
        if (LITTLE_FREQUENCY_TABLE[i] == freq) return i;
    }
    return -1;
}

int load_measurement_grid(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "load_measurement_grid: cannot open %s\n", filepath);
        return -1;
    }
    
    char line[256];
    int line_num = 0;
    grid_fps_min = 1e9;
    grid_fps_max = -1e9;
    grid_latency_min = 1e9;
    grid_latency_max = -1e9;
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        if (line_num <= 2) continue;  // skip header lines
        
        int big_freq, little_freq, pp1, pp2;
        char order[16];
        double fps, latency;
        
        if (sscanf(line, "%d,%d,%d,%d,%[^,],%lf,%lf", 
                   &big_freq, &little_freq, &pp1, &pp2, order, &fps, &latency) != 7) {
            continue;
        }
        
        int big_idx = freq_to_big_idx(big_freq);
        int little_idx = freq_to_little_idx(little_freq);
        
        if (big_idx < 0 || little_idx < 0) continue;
        
        measurement_grid[big_idx][little_idx].fps = fps;
        measurement_grid[big_idx][little_idx].latency = latency;
        
        if (fps < grid_fps_min) grid_fps_min = fps;
        if (fps > grid_fps_max) grid_fps_max = fps;
        if (latency < grid_latency_min) grid_latency_min = latency;
        if (latency > grid_latency_max) grid_latency_max = latency;
    }
    
    fclose(fp);
    grid_loaded = 1;
    printf("load_measurement_grid: loaded from %s (fps: %.2f-%.2f, latency: %.2f-%.2f)\n",
           filepath, grid_fps_min, grid_fps_max, grid_latency_min, grid_latency_max);
    return 0;
}

void approximate_target_space(double target_fps, double target_latency, PipelineConfig *config) {
    if (!grid_loaded) {
        fprintf(stderr, "approximate_target_space: grid not loaded, call load_measurement_grid first\n");
        config->big_frequency = -1;
        config->little_frequency = -1;
        return;
    }
    
    double best_error = 1e9;
    int best_big_idx = -1;
    int best_little_idx = -1;
    
    double norm_target_fps = (target_fps - grid_fps_min) / (grid_fps_max - grid_fps_min);
    double norm_target_latency = (target_latency - grid_latency_min) / (grid_latency_max - grid_latency_min);
    
    if (norm_target_fps < 0) norm_target_fps = 0;
    if (norm_target_fps > 1) norm_target_fps = 1;
    if (norm_target_latency < 0) norm_target_latency = 0;
    if (norm_target_latency > 1) norm_target_latency = 1;
    
    for (int big_idx = 0; big_idx < NUM_BIG_FREQUENCIES; big_idx++) {
        for (int little_idx = 0; little_idx < NUM_LITTLE_FREQUENCIES; little_idx++) {
            GridPoint point = measurement_grid[big_idx][little_idx];
            
            double norm_fps = (point.fps - grid_fps_min) / (grid_fps_max - grid_fps_min);
            double norm_latency = (point.latency - grid_latency_min) / (grid_latency_max - grid_latency_min);
            
            double fps_error = norm_target_fps - norm_fps;
            double latency_error = norm_latency - norm_target_latency;
            
            if (fps_error < 0) fps_error = 0;
            if (latency_error < 0) latency_error = 0;
            
            double total_error = fps_error * fps_error + latency_error * latency_error;
            
            if (total_error < best_error) {
                best_error = total_error;
                best_big_idx = big_idx;
                best_little_idx = little_idx;
            }
        }
    }
    
    if (best_big_idx >= 0 && best_little_idx >= 0) {
        config->big_frequency = BIG_FREQUENCY_TABLE[best_big_idx];
        config->little_frequency = LITTLE_FREQUENCY_TABLE[best_little_idx];
        printf("approximate_target_space: target_fps=%.2f, target_latency=%.2f -> big_freq=%d, little_freq=%d (error=%.4f, grid_fps=%.2f, grid_latency=%.2f)\n",
               target_fps, target_latency, config->big_frequency, config->little_frequency, best_error,
               measurement_grid[best_big_idx][best_little_idx].fps, measurement_grid[best_big_idx][best_little_idx].latency);
    } else {
        config->big_frequency = -1;
        config->little_frequency = -1;
    }
}