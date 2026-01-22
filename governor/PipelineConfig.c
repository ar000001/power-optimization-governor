
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_PARTITION_POINT 8
#define NUM_BIG_FREQUENCIES 13
#define NUM_LITTLE_FREQUENCIES 9
#define BIG_CPU 1
#define LITTLE_CPU 2

const int LITTLE_FREQUENCY_TABLE[]={500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000};
const int BIG_FREQUENCY_TABLE[]={500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000, 1908000, 2016000, 2100000, 2208000};


typedef struct PipelineConfig {
    int partition_point1;
    int partition_point2;
    int big_frequency;
    int little_frequency;
    char order[6];
} PipelineConfig;


typedef enum {
    GPU = 0,
    BIG_CPU = 1,
    LITTLE_CPU = 2
} processor;


void run_inference(PipelineConfig *config, char *graph, int n_frames){
    
    char command[150];
    sprintf(command, "echo %d > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq", config->little_frequency);
    system(command);
    sprintf(command, "echo %d > /sys/devices/system/cpu/cpufreq/policy2/scaling_max_freq", config->big_frequency);
    system(command);

    sprintf(command, "./%s --threads=4 --threads2=2 --target=CL --n=%d --partition_point=%d --partition_point2=%d --order=%s > output.txt 2>&1",
        graph, n_frames, config->partition_point1, config->partition_point2, config->order);
    system(command);

}


inline void print_pipe_line_config(PipelineConfig *config){
    printf("Partition Point 1: %d\n", config->partition_point1);
    printf("Partition Point 2: %d\n", config->partition_point2);
    printf("Big Frequency: %d\n", config->big_frequency);
    printf("Little Frequency: %d\n", config->little_frequency);
    printf("Order: %s\n", config->order);
}


inline int set_partition_point1(PipelineConfig *config, int partition_point){

    if (partition_point > MAX_PARTITION_POINT || partition_point < 1)
        return -1;
    else
        config->partition_point1 = partition_point;
}


inline int set_partition_point2(PipelineConfig *config, int partition_point){

    if (partition_point > MAX_PARTITION_POINT || partition_point < 1)
        return -1;
    else
        config->partition_point2 = partition_point;
}


inline int set_order(PipelineConfig *config, char *order){
    if(validate_order(order) == -1)
        return -1;
    else
        strcpy(config->order, order);
}


//This function needs to be checked. Probably not correct.
int validate_order(char *order){

    char c1, c2, c3;
    int n = sscanf(order, "%c-%c-%c", &c1, &c2, &c3);
    if (n != 3)
        return -1;
    else
        return strstr("GBL", c1) && strstr("GBL", c2) && strstr("GBL", c3);
}


int set_frequency(PipelineConfig *config, int freq, processor cpu) {

    if (cpu == BIG_CPU)
        config->big_frequency = freq;
    else if (cpu == LITTLE_CPU)
        config->little_frequency = freq;

    return validate_frequency(freq, cpu);
}

int increment_frequency(int freq, processor cpu){

    if (cpu == BIG_CPU){
        for(int i = 0; i < NUM_BIG_FREQUENCIES-1; i++) {
            if (freq == BIG_FREQUENCY_TABLE[i])
                return BIG_FREQUENCY_TABLE[i+1];
        }
    } else if (cpu == LITTLE_CPU) {
        for(int i = 0; i < NUM_LITTLE_FREQUENCIES-1; i++) {
            if (freq == LITTLE_FREQUENCY_TABLE[i])
                return LITTLE_FREQUENCY_TABLE[i+1];
        }
    }

    return -1;
}

int decrement_frequency(int freq, processor cpu){

    if (cpu == BIG_CPU){
        for(int i=1; i < NUM_BIG_FREQUENCIES; i++) {
            if (freq == BIG_FREQUENCY_TABLE[i])
                return BIG_FREQUENCY_TABLE[i-1];
        }
    } else if (cpu == LITTLE_CPU){
        for(int i=1; i < NUM_LITTLE_FREQUENCIES; i++) {
            if (freq == LITTLE_FREQUENCY_TABLE[i])
                return LITTLE_FREQUENCY_TABLE[i-1];
        }
    }

    return -1;
}

int set_little_frequency(PipelineConfig *config, int freq) {
    if(validate_little_frequency(freq))
        config->little_frequency = freq;
    else
        return -1;
}


bool validate_frequency(int freq, processor cpu){
    
    if (cpu == BIG_CPU)
    for (int i = 0; i < NUM_BIG_FREQUENCIES; i++){
        if (freq == BIG_FREQUENCY_TABLE[i])
            return true;
    }
    return false;
}

bool validate_little_frequency(int freq){
    
    for (int i = 0; i < NUM_LITTLE_FREQUENCIES; i++){
        if (freq == LITTLE_FREQUENCY_TABLE[i])
            return true;
    }
    return false;
}