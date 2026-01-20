/*Instructions to Run
On Your Computer: 
	arm-linux-androideabi-clang++ -static-libstdc++ Governor.cpp -o Governor 
	adb push Governor /data/local/Working_dir
On the Board: 
	chmod +x Governor.sh
	./Governor graph_alexnet_all_pipe_sync #NumberOFPartitions #TargetFPS #TargetLatency
*/


#include <stdio.h>      /* printf */
#include <stdlib.h>     /* system, NULL, EXIT_FAILURE */
#include "PipelineConfig.h"

float stage_one_inference_time=0;
float stage_two_inference_time=0;
float stage_three_inference_time=0;

int partitions=0;
int target_fps=0;
int target_latency=0;



/* Get feedback by parsing the results */
void parse_results(){
	float fps;
	float latency;
	FILE *output_file;
    char *line = NULL;
    size_t len = 0;
    
    if ((output_file = fopen("output.txt", "r")) == NULL) {
		printf("Error opening file\n");
		return;
	}

	/* Read Output.txt File and Extract Data */
	while (getline(&line, &len, output_file) != -1)
	{
		char temp[100];
		/* Extract Frame Rate */
		if ( strstr(line, "Frame rate is:")!=NULL ){
			//cout<<"line is: "<<line<<std::endl;
			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &fps) == 1){
					printf("Throughput is: %f FPS\n", fps);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Frame Latency */
		if ( strstr(line, "Frame latency is:")!=NULL ){
			//cout<<"line is: "<<line<<std::endl;
			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &latency) == 1){
					printf("Latency is: %f ms\n", latency);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Stage One Inference Time */
		if ( strstr(line, "stage1_inference_time:")!=NULL ){
			//cout<<"line is: "<<line<<std::endl;
			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &stage_one_inference_time) == 1){
					//printf("StageOneInferenceTime is: %f ms\n", StageOneInferenceTime);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Stage Two Inference Time */
		if ( strstr("stage2_inference_time:")!=NULL ){
			temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &stage_two_inference_time) == 1){
					//printf("StageTwoInferenceTime is: %f ms\n", StageTwoInferenceTime);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
		/* Extract Stage Three Inference Time */
		if ( strstr(line, "stage3_inference_time:")!=NULL ){
            temp = strtok(line, " ");
			while (temp != NULL) {
				/* Checking the given word is float or not */
				if (sscanf(temp, "%f", &stage_three_inference_time) == 1){
					//printf("StageThreeInferenceTime is: %f ms\n", StageThreeInferenceTime);
					break;
				}
				temp = strtok(NULL, " ");
			}
		}
	}
	/* Check Throughput and Latency Constraints */
	if ( Latency <= Target_Latency ){
		LatencyCondition=1; //Latency requirement was met.
	}
	if ( FPS >= Target_FPS ){
		FPSCondition=1; //FPS requirement was met.
	}
}


void set_system_config(){
	/* Export OpenCL library path */
    system("export LD_LIBRARY_PATH=/data/local/Working_dir");
	setenv("LD_LIBRARY_PATH","/data/local/Working_dir",1);

	/* Setup Performance Governor (CPU) */
	system("echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor");
	system("echo performance > /sys/devices/system/cpu/cpufreq/policy2/scaling_governor");

}


int main (int argc, char *argv[])
{
	if ( argc < 5 ){
		printf("Wrong number of input arguments.\n");
		return -1;
	}

    char graph[100];
	sscanf(argv[1], "%s", graph);
	partitions=atoi(argv[2]);
	target_fps=atoi(argv[3]);
	target_latency=atoi(argv[4]);

	char command[100];

	/* Checking if processor is available */
	if (system(NULL)) {
        puts ("Ok");
    } else {
        exit (EXIT_FAILURE);
    }

    set_system_config();
	/* Initialize Little and Big CPU with Lowest Frequency */


	int N_Frames=10;
	/* Start with running half network on Little CPU and half network on Big CPU with GPU empty in the middle */
	int PartitionPoint1=partitions/2;
	int PartitionPoint2=partitions/2;
	string Order="L-G-B";
    
	while(true){
		char Run_Command[150];		
		sprintf(Run_Command, "./%s --threads=4 --threads2=2 --target=NEON --n=%d --partition_point=%d --partition_point2=%d --order=%s > output.txt 2>&1",
        graph, N_Frames, PartitionPoint1, PartitionPoint2, Order.c_str());
		system(Run_Command);
		ParseResults();
		if ( FPSCondition && LatencyCondition ){//Both Latency and Throughput Requirements are Met.
			printf("Solution Was Found.\n TargetBigFrequency:%d \t TargetLittleFrequency:%d \t PartitionPoint1:%d \t PartitionPoint2:%d \t Order:%s\n", 
			BigFrequencyTable[BigFrequencyCounter],LittleFrequencyTable[LittleFrequencyCounter], PartitionPoint1, PartitionPoint2, Order.c_str());
			break;	
		}

		printf("Target Perfromance Not Satisfied\n\n");

		if ( LittleFrequencyCounter < MaxLittleFrequencyCounter ){
			/* Push Frequency of Little Cluster Higher to Meet Target Performance */
			LittleFrequencyCounter=LittleFrequencyCounter+1;
			Command="echo " + to_string(LittleFrequencyTable[LittleFrequencyCounter]) + " > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq";
			system(Command.c_str());
			printf("Increasing Frequency of Little Cores to %d\n", LittleFrequencyTable[LittleFrequencyCounter]);
		}
		else{
			if ( BigFrequencyCounter < MaxBigFrequencyCounter ){
				/* Push Frequency of Small Cluster Higher to Meet Target Performance */
				BigFrequencyCounter=BigFrequencyCounter+1;
				Command="echo " + to_string(BigFrequencyTable[BigFrequencyCounter]) + " > /sys/devices/system/cpu/cpufreq/policy2/scaling_max_freq";
				system(Command.c_str());
				printf("Increasing Frequency of Big Cores to %d\n", BigFrequencyTable[BigFrequencyCounter]);
			}
			else{
				if ( StageOneInferenceTime < StageThreeInferenceTime ){
					if ( PartitionPoint2 < partitions ){
						/* Push Layers from Third Stage (Big CPU) to GPU to Meet Target Performance */
						PartitionPoint2=PartitionPoint2+1;
						printf("Reducing the Size of Pipeline Partition 3\n");
					}
					else{
						printf("No Solution Found\n");
						break;
					}
				}
				else{
					if ( PartitionPoint1 > 1 ){
						/* Push Layers from First Stage (Little CPU) to GPU to Meet Target Performance */
						PartitionPoint1=PartitionPoint1-1;
						printf("Reducing the Size of Pipeline Partition 1\n");
					}
					else{
						printf("No Solution Found\n");
						break;
					}
				}
			}
		}
	}
  
  return 0;
}