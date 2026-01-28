
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "PipelineConfig.h"

#define MAX_PARTITION_POINT 8
#define NUM_BIG_FREQUENCIES 13
#define NUM_LITTLE_FREQUENCIES 9

const int LITTLE_FREQUENCY_TABLE[]={500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000};
const int BIG_FREQUENCY_TABLE[]={500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000, 1908000, 2016000, 2100000, 2208000};


void run_inference(PipelineConfig *config, char *graph, int n_frames){
    
    char command[256];
    sprintf(command, "./set_freq.sh little %d", config->little_frequency);
    system(command);
    sprintf(command, "./set_freq.sh big %d", config->big_frequency);
    system(command);

    sprintf(command, "./run_inference.sh %s %d %d %d %s > output.txt 2>&1",
        graph, n_frames, config->partition_point1, config->partition_point2, config->order);
    system(command);

}


void print_pipe_line_config(PipelineConfig *config){
    printf("Partition Point 1: %d\n", config->partition_point1);
    printf("Partition Point 2: %d\n", config->partition_point2);
    printf("Big Frequency: %d\n", config->big_frequency);
    printf("Little Frequency: %d\n", config->little_frequency);
    printf("Order: %s\n", config->order);
}


int set_partition_point1(PipelineConfig *config, int partition_point){

    if (partition_point > MAX_PARTITION_POINT || partition_point < 1)
        return -1;
    else
        config->partition_point1 = partition_point;

    enforce_no_single_layer_stages(config);
    return 0;
}


int set_partition_point2(PipelineConfig *config, int partition_point){

    if (partition_point > MAX_PARTITION_POINT || partition_point < 1)
        return -1;
    else
        config->partition_point2 = partition_point;

    enforce_no_single_layer_stages(config);
    return 0;
}


int set_order(PipelineConfig *config, char *order){
    if(validate_order(order) == -1)
        return -1;
    else
        strcpy(config->order, order);
    return 0;
}


//This function needs to be checked. Probably not correct.
int validate_order(char *order){

    char c1, c2, c3;
    int n = sscanf(order, "%c-%c-%c", &c1, &c2, &c3);
    if (n != 3)
        return -1;
    else
        return strchr("GBL", c1) && strchr("GBL", c2) && strchr("GBL", c3) ? 0 : -1;
}


int set_frequency(PipelineConfig *config, int freq, processor cpu) {

    if (cpu == BIG_CPU)
        config->big_frequency = freq;
    else if (cpu == LITTLE_CPU)
        config->little_frequency = freq;

    return validate_frequency(freq, cpu);
}


void increment_frequency(PipelineConfig *config, int freq, processor cpu){

    if (cpu == BIG_CPU){
        for(int i = 0; i < NUM_BIG_FREQUENCIES-1; i++) {
            if (freq == BIG_FREQUENCY_TABLE[i])
                config->big_frequency = BIG_FREQUENCY_TABLE[i+1];
        }
    } else if (cpu == LITTLE_CPU) {
        for(int i = 0; i < NUM_LITTLE_FREQUENCIES-1; i++) {
            if (freq == LITTLE_FREQUENCY_TABLE[i])
                config->little_frequency = LITTLE_FREQUENCY_TABLE[i+1];
        }
    }
}


void decrement_frequency(PipelineConfig *config, int freq, processor cpu){

    if (cpu == BIG_CPU){
        for(int i=1; i < NUM_BIG_FREQUENCIES; i++) {
            if (freq == BIG_FREQUENCY_TABLE[i])
                config->big_frequency = BIG_FREQUENCY_TABLE[i-1];
        }
    } else if (cpu == LITTLE_CPU){
        for(int i=1; i < NUM_LITTLE_FREQUENCIES; i++) {
            if (freq == LITTLE_FREQUENCY_TABLE[i])
                config->little_frequency = LITTLE_FREQUENCY_TABLE[i-1];
        }
    }
}

bool validate_frequency(int freq, processor cpu){
    
    if (cpu == BIG_CPU) {
        for (int i = 0; i < NUM_BIG_FREQUENCIES; i++){
            if (freq == BIG_FREQUENCY_TABLE[i])
                return true;
        }
    } else if (cpu == LITTLE_CPU) {
        for (int i = 0; i < NUM_LITTLE_FREQUENCIES; i++){
            if (freq == LITTLE_FREQUENCY_TABLE[i])
                return true;
        }
    }
    return false;
}

void enforce_no_single_layer_stages(PipelineConfig *config) {

    if (!config) return;

    int pp1_cur = config->partition_point1;
    int pp2_cur = config->partition_point2;

    if (pp1_cur < 1) pp1_cur = 1;
    if (pp1_cur > TOTAL_LAYERS) pp1_cur = TOTAL_LAYERS;

    if (pp2_cur < 1) pp2_cur = 1;
    if (pp2_cur > TOTAL_LAYERS) pp2_cur = TOTAL_LAYERS;
    if (pp2_cur < pp1_cur) pp2_cur = pp1_cur;

    int best_pp1 = pp1_cur;
    int best_pp2 = pp2_cur;
    int best_cost = 1e9;

    for (int pp1 = 1; pp1 <= TOTAL_LAYERS; pp1++) {
        for (int pp2 = pp1; pp2 <= TOTAL_LAYERS; pp2++) {
            int s1 = pp1;
            int s2 = pp2 - pp1;
            int s3 = TOTAL_LAYERS - pp2;

            if (s1 == 1 || s2 == 1 || s3 == 1) continue;

            int cost = abs(pp1 - pp1_cur) + abs(pp2 - pp2_cur);
            if (cost < best_cost) {
                best_cost = cost;
                best_pp1 = pp1;
                best_pp2 = pp2;
            }
        }
    }

    config->partition_point1 = best_pp1;
    config->partition_point2 = best_pp2;

    if (best_pp1 != pp1_cur || best_pp2 != pp2_cur) {
        int s1 = best_pp1;
        int s2 = best_pp2 - best_pp1;
        int s3 = TOTAL_LAYERS - best_pp2;
        printf("[partition-fix] adjusted partition points to avoid 1-layer stage: pp1=%d->%d pp2=%d->%d (stages=%d,%d,%d)\n",
               pp1_cur, best_pp1, pp2_cur, best_pp2, s1, s2, s3);
    }
}

void calc_partition_sizes(PipelineConfig *config, int total_parts, int *out_g, int *out_b, int *out_l){
    
    int first_stage = config->partition_point1;
    int second_stage = config->partition_point2-first_stage;
    int third_stage = total_parts - config->partition_point2;

    char first, second, third;

    sscanf(config->order, "%c-%c-%c", &first, &second, &third);
    if (first == 'G') {
        *out_g = first_stage;
        if (second == 'B') {
            *out_b = second_stage;
            *out_l = third_stage;
        } else {
            *out_l = second_stage;
            *out_b = third_stage;
        }
    } else if (first == 'B') {
        *out_b = first_stage;
        if (second == 'G') {
            *out_g = second_stage;
            *out_l = third_stage;
        } else {
            *out_l = second_stage;
            *out_g = third_stage;
        }
    } else {
        *out_l = first_stage;
        if (second == 'B') {
            *out_b = second_stage;
            *out_g = third_stage;
        } else {
            *out_g = second_stage;
            *out_b = third_stage;
        }
    }
}