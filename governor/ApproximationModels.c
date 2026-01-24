
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

// Experiment 6 data: EXP6_GRID[big_freq_idx][little_freq_idx]
// big_freq order:    500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000, 1908000, 2016000, 2100000, 2208000
// little_freq order: 500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000
static const GridPoint EXP6_GRID[NUM_BIG_FREQUENCIES][NUM_LITTLE_FREQUENCIES] = {
    // big_freq = 500000
    {{3.34604, 651.768}, {4.37778, 510.188}, {4.23264, 525.308}, {4.18882, 530.112}, {4.1649, 532.572}, {4.19639, 528.319}, {4.16637, 531.156}, {4.18471, 529.349}, {4.18566, 529.002}},
    // big_freq = 667000
    {{6.56912, 355.303}, {6.57736, 354.73}, {6.5759, 355.089}, {6.57503, 355.08}, {6.58224, 353.552}, {6.57917, 354.02}, {6.57416, 352.801}, {6.57652, 353.841}, {6.57077, 354.739}},
    // big_freq = 1000000
    {{9.87785, 250.991}, {9.93109, 247.759}, {9.96364, 246.918}, {9.92812, 246.894}, {9.76652, 250.285}, {9.96647, 246.503}, {9.95664, 245.87}, {9.93333, 246.674}, {9.76485, 250.765}},
    // big_freq = 1200000
    {{9.93827, 234.94}, {11.7437, 216.349}, {11.7557, 215.716}, {11.7625, 212.617}, {11.7722, 212.502}, {11.7661, 211.894}, {11.5715, 214.453}, {11.7766, 212.208}, {11.7716, 214.084}},
    // big_freq = 1398000
    {{9.8616, 228.154}, {12.7615, 198.901}, {13.4094, 191.97}, {13.4484, 190.714}, {13.4714, 190.31}, {13.4655, 190.088}, {13.4748, 190.168}, {13.4953, 189.635}, {13.2304, 192.677}},
    // big_freq = 1512000
    {{9.78387, 222.743}, {12.7485, 194.187}, {14.3898, 182.428}, {14.3584, 182.818}, {14.3799, 180.735}, {14.3848, 180.848}, {14.3692, 180.997}, {14.3839, 180.607}, {14.3605, 181.011}},
    // big_freq = 1608000
    {{9.73734, 221.628}, {12.7196, 192.198}, {14.8742, 176.529}, {14.869, 176.542}, {14.8753, 176.41}, {14.8742, 176.402}, {14.8795, 176.32}, {14.8997, 175.85}, {14.9118, 175.317}},
    // big_freq = 1704000
    {{9.81267, 218.008}, {12.5594, 190.353}, {15.5263, 171.368}, {15.5914, 170.386}, {15.5981, 169.927}, {15.6092, 170.118}, {15.8048, 168.28}, {15.5606, 170.506}, {15.6543, 168.967}},
    // big_freq = 1800000
    {{9.77663, 214.374}, {12.6005, 187.621}, {16.2815, 165.345}, {16.2071, 165.826}, {16.2535, 165.09}, {16.3105, 164.17}, {16.25, 164.868}, {16.3136, 164.178}, {16.28, 164.775}},
    // big_freq = 1908000
    {{9.7895, 212.481}, {12.6581, 183.785}, {16.9923, 160.459}, {17.0155, 159.896}, {17.0407, 159.471}, {16.9942, 159.666}, {16.9817, 159.882}, {16.995, 159.626}, {17.0302, 159.237}},
    // big_freq = 2016000
    {{9.71477, 211.987}, {12.694, 182.962}, {16.8497, 159.399}, {17.6603, 156.075}, {17.6405, 156.037}, {17.6578, 155.831}, {17.7138, 155.094}, {17.6653, 155.601}, {17.6618, 155.644}},
    // big_freq = 2100000
    {{9.78553, 210.192}, {12.6137, 179.552}, {16.7696, 157.88}, {18.1693, 152.843}, {18.1542, 153.066}, {18.1026, 153.402}, {18.1955, 152.695}, {18.174, 152.45}, {18.2464, 151.823}},
    // big_freq = 2208000
    {{9.61906, 211.836}, {12.6204, 178.035}, {17.0032, 155.403}, {18.5529, 150.661}, {18.8214, 149.117}, {18.7448, 149.765}, {18.8028, 149.291}, {18.7347, 149.708}, {18.8578, 149.143}}
};

// Min/max values from exp6 for normalization
#define EXP6_FPS_MIN 3.34604
#define EXP6_FPS_MAX 18.8578
#define EXP6_LATENCY_MIN 149.117
#define EXP6_LATENCY_MAX 651.768

void approximate_target_space(double target_fps, double target_latency, PipelineConfig *config) {
    //pipeline config needs to come with partition points set and will be returned with little and big frequencies set.    

    double best_error = 1e9;
    int best_big_idx = -1;
    int best_little_idx = -1;
    
    double norm_target_fps = (target_fps - EXP6_FPS_MIN) / (EXP6_FPS_MAX - EXP6_FPS_MIN);
    double norm_target_latency = (target_latency - EXP6_LATENCY_MIN) / (EXP6_LATENCY_MAX - EXP6_LATENCY_MIN);
    
    if (norm_target_fps < 0) norm_target_fps = 0;
    if (norm_target_fps > 1) norm_target_fps = 1;
    if (norm_target_latency < 0) norm_target_latency = 0;
    if (norm_target_latency > 1) norm_target_latency = 1;
    
    for (int big_idx = 0; big_idx < NUM_BIG_FREQUENCIES; big_idx++) {
        for (int little_idx = 0; little_idx < NUM_LITTLE_FREQUENCIES; little_idx++) {
            GridPoint point = EXP6_GRID[big_idx][little_idx];
            
            double norm_fps = (point.fps - EXP6_FPS_MIN) / (EXP6_FPS_MAX - EXP6_FPS_MIN);
            double norm_latency = (point.latency - EXP6_LATENCY_MIN) / (EXP6_LATENCY_MAX - EXP6_LATENCY_MIN);
            
            double fps_error = norm_target_fps - norm_fps;      // positive if we're below target
            double latency_error = norm_latency - norm_target_latency;  // positive if we're above target
            
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
               EXP6_GRID[best_big_idx][best_little_idx].fps, EXP6_GRID[best_big_idx][best_little_idx].latency);
    } else {
        config->big_frequency = -1;
        config->little_frequency = -1;
    }
}