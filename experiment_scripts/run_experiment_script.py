import os
import argparse
from datetime import datetime

parser = argparse.ArgumentParser()
parser.add_argument("--graph", type=str, required=True)
parser.add_argument("--n_frames", type=int, required=True)
# parser.add_argument("--partition_point1", type=int, required=False) # NOTE: Currently not used but maybe helpful in the future
# parser.add_argument("--partition_point2", type=int, required=False)
# parser.add_argument("--order", type=str, required=False)
# parser.add_argument("--cpu_freq", type=int, required=False)
# parser.add_argument("--cpu_type", type=str, required=False)

LITTLE_FREQ_TABLE=[500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000]
BIG_FREQ_TABLE=[500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000, 1908000, 2016000, 2100000, 2208000]

args = parser.parse_args()

# setup system using shell scripts
print("Setting fan")
os.system(f"./set_fan.sh 1 0 1") # ./set_fan.sh <enable> <mode> <level>
print("Setting LD_LIBRARY_PATH env variable")
os.system(f"adb -d shell \"export LD_LIBRARY_PATH=/data/local/Working_dir\"")


def run_littlecpufreq_experiment():

    print(" --- LittleCPU Experiment ---")

    # Basically only LittleCPU
    partition_point1 = partition_point2 = 8
    order = "L-G-B"

    watts_read = []

    for little_freq in LITTLE_FREQ_TABLE:
        print(f"Setting little freq: {little_freq}")
        os.system(f"./set_freq.sh little {little_freq}")
        
        inference_command = f"./run_inference.sh {args.graph} {args.n_frames} {partition_point1} {partition_point2} {order}"

        os.system(inference_command)

        read_watts = input("What was the average watts you read from the power meter? ")
        watts_read.append(float(read_watts))
        
    print(" --- End of LittleCPU ---")
    print(watts_read)

    # save results to logfile with timestamp
    with open(f"littlecpufreq_experiment_{datetime.now().strftime("%Y%m%d_%H%M%S")}.txt", "w") as f:
        f.write(str(LITTLE_FREQ_TABLE) + "\n")
        f.write(str(watts_read) + "\n")

    return watts_read


def run_bigcpufreq_experiment():

    print(" --- bigCPU Experiment ---")

    partition_point1 = partition_point2 = 8
    order = "B-G-L"

    watts_read = []

    for big_freq in BIG_FREQ_TABLE:
        print(f"Setting big freq: {big_freq}")
        os.system(f"./set_freq.sh big {big_freq}")
        
        inference_command = f"./run_inference.sh {args.graph} {args.n_frames} {partition_point1} {partition_point2} {order}"

        os.system(inference_command)

        read_watts = input("What was the average watts you read from the power meter? ")
        watts_read.append(float(read_watts))
        
    print(" --- End of bigCPU ---")
    print(watts_read)

    with open(f"bigcpufreq_experiment_{datetime.now().strftime("%Y%m%d_%H%M%S")}.txt", "w") as f:
        f.write(str(BIG_FREQ_TABLE) + "\n")
        f.write(str(watts_read) + "\n")

    return watts_read


def run_gpu_power_experiment():

    print(" --- GPU Power Experiment ---")

    partition_point1 = 4
    partition_point2 = 6
    order = "G-B-L"

    little_freq_table_short=[500000, 1000000, 1512000, 1800000]
    big_freq_table_short=[500000, 1000000, 1512000, 1800000, 2208000]

    watts_read = []
    freqs = []

    for big_freq in big_freq_table_short:
        os.system(f"./set_freq.sh big {big_freq}")

        for little_freq in little_freq_table_short:
            print(f"Big freq: {big_freq}, little freq: {little_freq}")
            os.system(f"./set_freq.sh little {little_freq}")
        
            inference_command = f"./run_inference.sh {args.graph} {args.n_frames} {partition_point1} {partition_point2} {order}"

            os.system(inference_command)

        read_watts = input("What was the average watts you read from the power meter? ")
        freqs.append((big_freq, little_freq))
        watts_read.append(float(read_watts))
        
    print(" --- End of GPU ---")
    print(watts_read)

    with open(f"gpu_experiment_{datetime.now().strftime("%Y%m%d_%H%M%S")}.txt", "w") as f:
        f.write(str(freqs) + "\n")
        f.write(str(watts_read) + "\n")

    return watts_read


run_gpu_power_experiment()


os.system("./set_fan.sh 1 0 0") # turn fan off again