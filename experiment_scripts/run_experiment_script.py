import os
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--graph", type=str, required=True)
parser.add_argument("--threads", type=int, required=True)
parser.add_argument("--threads2", type=int, required=True)
parser.add_argument("--target", type=str, required=True)
parser.add_argument("--n_frames", type=int, required=True)
parser.add_argument("--partition_point1", type=int, required=True)
parser.add_argument("--partition_point2", type=int, required=True)
parser.add_argument("--order", type=str, required=True)
parser.add_argument("--cpu_freq", type=int, required=True)
parser.add_argument("--cpu_type", type=str, required=True)

little_freq_table=[500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000]
big_freq_table=[500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000, 1908000, 2016000, 2100000, 2208000]

args = parser.parse_args()

if args.cpu_type == "little":
    freq_table = little_freq_table
elif args.cpu_type == "big":
    freq_table = big_freq_table
else:
    raise ValueError("Invalid CPU type. Either 'little' or 'big'")

if args.cpu_freq not in freq_table:
    raise ValueError(f"Invalid CPU frequency. \nAvailable frequencies: {freq_table}")

# setup system using shell scripts

os.system(f"./set_cpu_freq.sh {args.cpu_type} {args.cpu_freq}")
os.system(f"./set_fan.sh 1 0 1") # ./set_fan.sh <enable> <mode> <level>


def run_experiment():

    watts_read = []

    for little_freq in little_freq_table:
        for big_freq in big_freq_table:
            print(f"Running experiment with little freq: {little_freq}, big freq: {big_freq}")
            os.system(f"./set_cpu_freq.sh little {little_freq}")
            os.system(f"./set_cpu_freq.sh big {big_freq}")        

            inference_command = "./run_inference.sh {graph} {n_frames} {threads} {threads2} {order}".format(
                graph=args.graph,
                n_frames=args.n_frames,
                threads=args.threads,
                threads2=args.threads2,
                order=args.order
            )

            os.system(inference_command)

            read_watts = input("What was the max watts you read from the power meter? ")
            watts_read.append(float(read_watts))
            print(f"Max watts: {read_watts}")

    return watts_read

run_experiment()
