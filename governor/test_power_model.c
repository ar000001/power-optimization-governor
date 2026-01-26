/*
 * test_power_model.c - Validate power model against experimental measurements
 *
 * Build: gcc -O2 -Wall -o test_power_model test_power_model.c PowerModel.c -lm
 * Run:   ./test_power_model
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "PowerModel.h"

typedef struct {
    unsigned long big_freq;
    unsigned long little_freq;
    int pp1;
    int pp2;
    double measured_watts;
    const char *description;
} TestCase;

int main(void) {
    
    // Test cases from experimental data
    TestCase tests[] = {
        // exp1: Little CPU only (pp1=8, pp2=8 means GPU does nothing, order L-G-B)
        // Actually for L-G-B order, little does all work
        // Reinterpreted: pp1=0, pp2=0 for all-little in G-B-L order
        {500000, 500000, 0, 0, 1.98, "Little only @ 500MHz"},
        {500000, 1000000, 0, 0, 2.07, "Little only @ 1000MHz"},
        {500000, 1800000, 0, 0, 2.44, "Little only @ 1800MHz"},
        
        // exp2: Big CPU only
        // For B-G-L order with pp1=8,pp2=8: big does all
        // In G-B-L: pp1=0, pp2=8 means big does all
        {500000, 500000, 0, 8, 2.18, "Big only @ 500MHz"},
        {1000000, 500000, 0, 8, 2.61, "Big only @ 1000MHz"},
        {1800000, 500000, 0, 8, 3.22, "Big only @ 1800MHz"},
        {2208000, 500000, 0, 8, 4.45, "Big only @ 2208MHz"},
        
        // exp3: GPU only (pp1=8, pp2=8 in G-B-L order)
        {500000, 500000, 8, 8, 2.93, "GPU only (low CPU freq)"},
        {1000000, 1000000, 8, 8, 2.85, "GPU only (mid CPU freq)"},
        {1800000, 1800000, 8, 8, 3.14, "GPU only (high CPU freq)"},
        
        // exp4: Mixed workloads with G-B-L order
        {1800000, 1200000, 1, 7, 3.88, "G-B-L pp1=1,pp2=7"},
        {1800000, 1200000, 2, 7, 4.13, "G-B-L pp1=2,pp2=7"},
        {1800000, 1200000, 4, 7, 4.17, "G-B-L pp1=4,pp2=7"},
        {1800000, 1200000, 5, 7, 4.19, "G-B-L pp1=5,pp2=7"},
        {1800000, 1200000, 4, 6, 4.30, "G-B-L pp1=4,pp2=6"},
        {1800000, 1200000, 5, 6, 4.40, "G-B-L pp1=5,pp2=6"},
        {1800000, 1200000, 5, 8, 4.21, "G-B-L pp1=5,pp2=8 (no little)"},
    };
    
    int n_tests = sizeof(tests) / sizeof(tests[0]);
    double total_error = 0.0;
    double max_error = 0.0;
    
    printf("%-35s %8s %8s %8s %8s\n", 
           "Test Case", "Measured", "Estimate", "Error", "Error%");
    printf("%-35s %8s %8s %8s %8s\n",
           "-----------------------------------", "--------", "--------", "--------", "--------");
    
    for (int i = 0; i < n_tests; i++) {
        TestCase *t = &tests[i];
        double est = estimate_power(t->big_freq, t->little_freq, t->pp1, t->pp2);
        double err = est - t->measured_watts;
        double err_pct = 100.0 * fabs(err) / t->measured_watts;
        
        printf("%-35s %8.2f %8.2f %+8.2f %7.1f%%\n",
               t->description, t->measured_watts, est, err, err_pct);
        
        total_error += fabs(err);
        if (fabs(err) > max_error) max_error = fabs(err);
    }
    
    printf("\n");
    printf("Mean Absolute Error: %.3f W\n", total_error / n_tests);
    printf("Max Absolute Error:  %.3f W\n", max_error);
    
    // Demo: show power estimates for a sweep
    printf("\n=== Power Estimate Examples ===\n\n");
    printf("Frequency sweep (pp1=4, pp2=6, G-B-L order):\n");
    printf("%10s %10s %10s\n", "Big(MHz)", "Little(MHz)", "Est.Power(W)");
    
    unsigned long big_freqs[] = {500000, 1000000, 1512000, 1800000, 2208000};
    unsigned long little_freqs[] = {500000, 1000000, 1512000, 1800000};
    
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 4; j++) {
            double p = estimate_power(big_freqs[i], little_freqs[j], 4, 6);
            printf("%10.0f %10.0f %12.2f\n", 
                   (double)big_freqs[i]/1000, (double)little_freqs[j]/1000, p);
        }
    }
    
    printf("\nPartition point sweep (big=1800MHz, little=1200MHz):\n");
    printf("%5s %5s %10s %10s %10s %12s\n", 
           "pp1", "pp2", "GPU_frac", "Big_frac", "Little_frac", "Est.Power(W)");
    
    for (int pp1 = 0; pp1 <= 8; pp1 += 2) {
        for (int pp2 = pp1; pp2 <= 8; pp2 += 2) {
            double g, b, l;
            get_workload_fractions(pp1, pp2, &g, &b, &l);
            double p = estimate_power(1800000, 1200000, pp1, pp2);
            printf("%5d %5d %10.2f %10.2f %10.2f %12.2f\n", pp1, pp2, g, b, l, p);
        }
    }
    
    return 0;
}
