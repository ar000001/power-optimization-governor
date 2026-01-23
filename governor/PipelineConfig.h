#ifndef PIPELINECONFIG_H
#define PIPELINECONFIG_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define MAX_PARTITION_POINT 8
#define NUM_BIG_FREQUENCIES 13
#define NUM_LITTLE_FREQUENCIES 9

typedef enum {
    GPU = 0,
    BIG_CPU = 1,
    LITTLE_CPU = 2
} processor;

extern const int LITTLE_FREQUENCY_TABLE[];
extern const int BIG_FREQUENCY_TABLE[];

typedef struct PipelineConfig {
    int partition_point1;
    int partition_point2;
    int big_frequency;
    int little_frequency;
    char order[6];
} PipelineConfig;

void run_inference(PipelineConfig *config, char *graph, int n_frames);

void print_pipe_line_config(PipelineConfig *config);

int set_partition_point1(PipelineConfig *config, int partition_point);

int set_partition_point2(PipelineConfig *config, int partition_point);

int set_order(PipelineConfig *config, char *order);

int validate_order(char *order);

int set_frequency(PipelineConfig *config, int freq, processor cpu);

int increment_frequency(int freq, processor cpu);

int decrement_frequency(int freq, processor cpu);

int set_little_frequency(PipelineConfig *config, int freq);

bool validate_frequency(int freq, processor cpu);

bool validate_little_frequency(int freq);

#endif
