import os
import argparse
import matplotlib.pyplot as plt
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
os.system(f"./set_fan.sh 1 0 1") # ./set_fan.sh <enable> <mode> <level>
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

        read_watts = input("What was the max watts you read from the power meter? ")
        watts_read.append((little_freq, float(read_watts)))
        
    print(" --- End of LittleCPU ---")
    print(watts_read)

    return watts_read


data = run_littlecpufreq_experiment()

# write data to text file
with open("littlecpufreq_experiment.txt", "w") as f:
    for item in data:
        f.write(f"{item[0]} {item[1]}\n")

plt.plot(data)
plt.show()

os.system("./set_fan.sh 1 0 0") # turn fan off again