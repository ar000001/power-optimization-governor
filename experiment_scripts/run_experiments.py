import os
import re
import argparse
from datetime import datetime

parser = argparse.ArgumentParser()
parser.add_argument("--graph", type=str, required=True)
parser.add_argument("--n_frames", type=int, required=True)
args = parser.parse_args()

LITTLE_FREQ_TABLE = [500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000]
BIG_FREQ_TABLE    = [500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000, 1908000, 2016000, 2100000, 2208000]

# smaller sweep for GPU
LITTLE_FREQ_SHORT = [500000, 1000000, 1512000, 1800000]
BIG_FREQ_SHORT    = [500000, 1000000, 1512000, 1800000, 2208000]

ALEXNET_TOTALPARTS = 8 

DATA_PATH = "data/"


def setup_board_once():
    os.system("./set_fan.sh 1 0 1")
    os.system("adb -d root")


def parse_last_run_output(output_path="last_run_output.txt"):
    """
    Parse FPS and latency from Pipe-All output.
    If parse fails, open last_run_output.txt and adjust regex patterns accordingly.
    """

    with open(output_path, "r", encoding="utf-8", errors="ignore") as f:
        txt = f.read()

    fps = None
    lat = None

    m = re.search(r"Frame rate is:\s*([0-9]+(?:\.[0-9]+)?)", txt)
    if m:
        fps = float(m.group(1))

    m = re.search(r"Frame latency is:\s*([0-9]+(?:\.[0-9]+)?)", txt, re.IGNORECASE)
    if m:
        lat = float(m.group(1))

    if fps is None or lat is None:
        print("ERROR: Could not parse FPS/latency from last_run_output.txt.")
        print("Search the file for the exact lines containing FPS/latency and update regex.")
        raise ValueError("Parse failed")

    return fps, lat


def run_one(graph, n_frames, pp1, pp2, order):
    os.system(f"./run_inference.sh {graph} {n_frames} {pp1} {pp2} {order}")
    fps, lat = parse_last_run_output("last_run_output.txt")
    elapsed_time_ms = float(input("Elapsed time of measurement (ms)? "))
    mwh = float(input("Recorded energy (mWh)? "))
    elapsed_time_s = (elapsed_time_ms * n_frames) / 1000.0
    watts = (mwh / 1000.0) / (elapsed_time_s / 3600.0)
    print(f"Calculated power: {watts:.2f} W")
    return fps, lat, watts


def save_results(filename, header_lines, rows):
    with open(DATA_PATH+filename, "w") as f:
        for line in header_lines:
            f.write(line + "\n")
        f.write("big_freq,little_freq,pp1,pp2,order,fps,latency,watts\n")
        for r in rows:
            f.write(f"{r['big_freq']},{r['little_freq']},{r['pp1']},{r['pp2']},{r['order']},{r['fps']},{r['latency']},{r['watts']}\n")


def exp1_little_cpu():
    print("=== EXP1: Little CPU only ===")

    pp1 = pp2 = ALEXNET_TOTALPARTS
    order = "L-G-B"

    rows = []
    for lf in LITTLE_FREQ_TABLE:
        os.system(f"./set_freq.sh little {lf}")
        os.system(f"./set_freq.sh big {BIG_FREQ_TABLE[0]}")

        print(f"Little freq={lf}")
        fps, lat, watts = run_one(args.graph, args.n_frames, pp1, pp2, order)

        rows.append(dict(big_freq=BIG_FREQ_TABLE[0], little_freq=lf, pp1=pp1, pp2=pp2, order=order, fps=fps, latency=lat, watts=watts))

    out = f"exp1_little_cpu_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    save_results(out, ["EXP1 Little CPU only"], rows)
    print(f"Saved {out}")
    return out


def exp2_big_cpu():
    print("=== EXP2: Big CPU only ===")

    pp1 = pp2 = ALEXNET_TOTALPARTS
    order = "B-G-L"

    rows = []
    for bf in BIG_FREQ_TABLE:
        os.system(f"./set_freq.sh big {bf}")
        os.system(f"./set_freq.sh little {LITTLE_FREQ_TABLE[0]}")

        print(f"Big freq={bf}")
        fps, lat, watts = run_one(args.graph, args.n_frames, pp1, pp2, order)

        rows.append(dict(big_freq=bf, little_freq=LITTLE_FREQ_TABLE[0], pp1=pp1, pp2=pp2, order=order, fps=fps, latency=lat, watts=watts))

    out = f"exp2_big_cpu_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    save_results(out, ["EXP2 Big CPU only"], rows)
    print(f"Saved {out}")
    return out


def exp3_gpu_cpu_freq_grid():
    print("=== EXP3: GPU-only with CPU freq grid ===")

    pp1 = pp2 = ALEXNET_TOTALPARTS
    order = "G-B-L"

    rows = []
    for bf in BIG_FREQ_SHORT:
        os.system(f"./set_freq.sh big {bf}")
        for lf in LITTLE_FREQ_SHORT:
            os.system(f"./set_freq.sh little {lf}")
            print(f"GPU-only, big={bf}, little={lf}")
            fps, lat, watts = run_one(args.graph, args.n_frames, pp1, pp2, order)

            rows.append(dict(big_freq=bf, little_freq=lf, pp1=pp1, pp2=pp2, order=order, fps=fps, latency=lat, watts=watts))

    out = f"exp3_gpu_grid_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    save_results(out, ["EXP3 GPU-only, CPU freq grid"], rows)
    print(f"Saved {out}")
    return out


def exp4_partition_points():
    print("=== EXP4: Partition points sweep ===")

    fixed_big = 1800000
    fixed_little = 1200000
    os.system(f"./set_freq.sh big {fixed_big}")
    os.system(f"./set_freq.sh little {fixed_little}")

    fixed_order = "G-B-L"

    # NOTE: The idea here is that the little cpu is fixed on the last layer, bc
    # it is small and therefore will most probably always only handle the last layer.
    # The more important part is to see how the big cpu handles the different partition points.
    # And more or less the tradeoffs between bigCPU and GPU.
    big_pp = 7
    little_pp = 7
    candidates = []
    
    for big_pp in range(1, 7):
        candidates.append((big_pp, little_pp))

    rows = []
    for pp1, pp2 in candidates:
        print(f"pp1={pp1} pp2={pp2} order={fixed_order}")
        fps, lat, watts = run_one(args.graph, args.n_frames, pp1, pp2, fixed_order)
        rows.append(dict(big_freq=fixed_big, little_freq=fixed_little, pp1=pp1, pp2=pp2, order=fixed_order, fps=fps, latency=lat, watts=watts))

    out = f"exp4_partition_points_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    save_results(out, [f"EXP4 partition points fixed freqs: big={fixed_big}, little={fixed_little}, order={fixed_order}"], rows)
    print(f"Saved {out}")
    return out


def exp5_component_orders():
    print("=== EXP5: Component orders sweep ===")

    fixed_big = 1800000
    fixed_little = 1200000
    os.system(f"./set_freq.sh big {fixed_big}")
    os.system(f"./set_freq.sh little {fixed_little}")

    fixed_pp1 = 3
    fixed_pp2 = 6

    orders = ["B-L-G", "B-G-L", "L-B-G", "L-G-B", "G-B-L", "G-L-B"]

    rows = []
    for order in orders:
        print(f"order={order} pp1={fixed_pp1} pp2={fixed_pp2}")
        fps, lat, watts = run_one(args.graph, args.n_frames, fixed_pp1, fixed_pp2, order)
        rows.append(dict(big_freq=fixed_big, little_freq=fixed_little, pp1=fixed_pp1, pp2=fixed_pp2, order=order, fps=fps, latency=lat, watts=watts))

    out = f"exp5_orders_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    save_results(out, [f"EXP5 orders fixed freqs: big={fixed_big}, little={fixed_little}, pp1={fixed_pp1}, pp2={fixed_pp2}"], rows)
    print(f"Saved {out}")
    return out


if __name__ == "__main__":
    setup_board_once()

    #f1 = exp1_little_cpu()
    #f2 = exp2_big_cpu()
    f3 = exp3_gpu_cpu_freq_grid()
    #f4 = exp4_partition_points()
    #f5 = exp5_component_orders()

    os.system("./set_fan.sh 1 0 0")
    print("DONE. Generated files:")
    print(f3)
