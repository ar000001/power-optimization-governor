
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
    sprintf(command, "adb -d shell \"echo %d > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq\"", config->little_frequency);
    system(command);
    sprintf(command, "adb -d shell \"echo %d > /sys/devices/system/cpu/cpufreq/policy2/scaling_max_freq\"", config->big_frequency);
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
    return 0;
}


int set_partition_point2(PipelineConfig *config, int partition_point){

    if (partition_point > MAX_PARTITION_POINT || partition_point < 1)
        return -1;
    else
        config->partition_point2 = partition_point;
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
                config->big_frequency = freq;
        }
    } else if (cpu == LITTLE_CPU){
        for(int i=1; i < NUM_LITTLE_FREQUENCIES; i++) {
            if (freq == LITTLE_FREQUENCY_TABLE[i])
                config->little_frequency = freq;
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

void calc_partition_sizes(PipelineConfig *config, int total_parts, int *out_g, int *out_b, int *out_l){
    
    int first_stage = config->partition_point1;
    int second_stage = config->partition_point2-first_stage;
    int third_stage = total_parts - second_stage;

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