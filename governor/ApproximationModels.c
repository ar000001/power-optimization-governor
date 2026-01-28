
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

 static const double MEASUREMENT_FPS_LUT[NUM_BIG_FREQUENCIES][NUM_LITTLE_FREQUENCIES] = {
     {3.346040, 4.377780, 4.232640, 4.188820, 4.164900, 4.196390, 4.166370, 4.184710, 4.185660},
     {6.569120, 6.577360, 6.575900, 6.575030, 6.582240, 6.579170, 6.574160, 6.576520, 6.570770},
     {9.877850, 9.931090, 9.963640, 9.928120, 9.766520, 9.966470, 9.956640, 9.933330, 9.764850},
     {9.938270, 11.743700, 11.755700, 11.762500, 11.772200, 11.766100, 11.571500, 11.776600, 11.771600},
     {9.861600, 12.761500, 13.409400, 13.448400, 13.471400, 13.465500, 13.474800, 13.495300, 13.230400},
     {9.783870, 12.748500, 14.389800, 14.358400, 14.379900, 14.384800, 14.369200, 14.383900, 14.360500},
     {9.737340, 12.719600, 14.874200, 14.869000, 14.875300, 14.874200, 14.879500, 14.899700, 14.911800},
     {9.812670, 12.559400, 15.526300, 15.591400, 15.598100, 15.609200, 15.804800, 15.560600, 15.654300},
     {9.776630, 12.600500, 16.281500, 16.207100, 16.253500, 16.310500, 16.250000, 16.313600, 16.280000},
     {9.789500, 12.658100, 16.992300, 17.015500, 17.040700, 16.994200, 16.981700, 16.995000, 17.030200},
     {9.714770, 12.694000, 16.849700, 17.660300, 17.640500, 17.657800, 17.713800, 17.665300, 17.661800},
     {9.785530, 12.613700, 16.769600, 18.169300, 18.154200, 18.102600, 18.195500, 18.174000, 18.246400},
     {9.619060, 12.620400, 17.003200, 18.552900, 18.821400, 18.744800, 18.802800, 18.734700, 18.857800},
 };

 static const double MEASUREMENT_LATENCY_LUT[NUM_BIG_FREQUENCIES][NUM_LITTLE_FREQUENCIES] = {
     {651.768, 510.188, 525.308, 530.112, 532.572, 528.319, 531.156, 529.349, 529.002},
     {355.303, 354.730, 355.089, 355.080, 353.552, 354.020, 352.801, 353.841, 354.739},
     {250.991, 247.759, 246.918, 246.894, 250.285, 246.503, 245.870, 246.674, 250.765},
     {234.940, 216.349, 215.716, 212.617, 212.502, 211.894, 214.453, 212.208, 214.084},
     {228.154, 198.901, 191.970, 190.714, 190.310, 190.088, 190.168, 189.635, 192.677},
     {222.743, 194.187, 182.428, 182.818, 180.735, 180.848, 180.997, 180.607, 181.011},
     {221.628, 192.198, 176.529, 176.542, 176.410, 176.402, 176.320, 175.850, 175.317},
     {218.008, 190.353, 171.368, 170.386, 169.927, 170.118, 168.280, 170.506, 168.967},
     {214.374, 187.621, 165.345, 165.826, 165.090, 164.170, 164.868, 164.178, 164.775},
     {212.481, 183.785, 160.459, 159.896, 159.471, 159.666, 159.882, 159.626, 159.237},
     {211.987, 182.962, 159.399, 156.075, 156.037, 155.831, 155.094, 155.601, 155.644},
     {210.192, 179.552, 157.880, 152.843, 153.066, 153.402, 152.695, 152.450, 151.823},
     {211.836, 178.035, 155.403, 150.661, 149.117, 149.765, 149.291, 149.708, 149.143},
 };

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
    (void)filepath;

    grid_fps_min = 1e9;
    grid_fps_max = -1e9;
    grid_latency_min = 1e9;
    grid_latency_max = -1e9;

    for (int big_idx = 0; big_idx < NUM_BIG_FREQUENCIES; big_idx++) {
        for (int little_idx = 0; little_idx < NUM_LITTLE_FREQUENCIES; little_idx++) {
            double fps = MEASUREMENT_FPS_LUT[big_idx][little_idx];
            double latency = MEASUREMENT_LATENCY_LUT[big_idx][little_idx];

            measurement_grid[big_idx][little_idx].fps = fps;
            measurement_grid[big_idx][little_idx].latency = latency;

            if (fps < grid_fps_min) grid_fps_min = fps;
            if (fps > grid_fps_max) grid_fps_max = fps;
            if (latency < grid_latency_min) grid_latency_min = latency;
            if (latency > grid_latency_max) grid_latency_max = latency;
        }
    }

    grid_loaded = 1;
    printf("load_measurement_grid: loaded embedded LUT (fps: %.2f-%.2f, latency: %.2f-%.2f)\n",
           grid_fps_min, grid_fps_max, grid_latency_min, grid_latency_max);
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