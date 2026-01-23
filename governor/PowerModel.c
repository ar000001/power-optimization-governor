
#include "PowerModel.h"
#include <math.h>

//best fit from experiments
static double power_little_cpu(double khz) {
    return 4.827e-14 * khz * khz + 2.292e-7 * khz + 1.855;
}

static double power_big_cpu(double khz) {
    return 6.998e-13 * khz * khz - 7.705e-7 * khz + 2.523;
}

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

double estimate_power(int big_freq, int little_freq, int pp1, int pp2) {

    
    double w_gpu = compute_weighted_fraction(0, pp1);
    double w_big = compute_weighted_fraction(pp1, pp2);
    double w_little = compute_weighted_fraction(pp2, TOTAL_LAYERS);
    
    double p_big_total = power_big_cpu((double)big_freq);
    double p_little_total = power_little_cpu((double)little_freq);
    
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
        double raw_overhead = 9.412e-07 * (double)big_freq 
                            - 3.230e-08 * (double)little_freq;
        double scaled_overhead = (0.63/1.57) * raw_overhead;
        
        //constant overhead for pipeline synchronization (found by trial and error)
        double base_overhead = 0.47;
        
        double stage_factor = (double)(active_stages - 1) / 2.0;
        power += (base_overhead + scaled_overhead) * stage_factor;
    }
    
    return power;
}
