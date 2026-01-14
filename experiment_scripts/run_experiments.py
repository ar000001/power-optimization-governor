import os
import re
import argparse
from datetime import datetime

parser = argparse.ArgumentParser()
parser.add_argument("--graph", type=str, required=True)       # e.g. graph_alexnet_all_pipe_sync
parser.add_argument("--n_frames", type=int, required=True)    # >= 60
args = parser.parse_args()

LITTLE_FREQ_TABLE = [500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000]
BIG_FREQ_TABLE    = [500000, 667000, 1000000, 1200000, 1398000, 1512000, 1608000, 1704000, 1800000, 1908000, 2016000, 2100000, 2208000]

# smaller sweep for GPU grid (reasonable workload)
LITTLE_FREQ_SHORT = [500000, 1000000, 1512000, 1800000]
BIG_FREQ_SHORT    = [500000, 1000000, 1512000, 1800000, 2208000]

ALEXNET_TOTALPARTS = 8  # per course guide: AlexNet has 8 partitioning parts


def setup_board_once():
    # Keep your style
    os.system("./set_fan.sh 1 0 1")  # enable=1 mode=0 level=1
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

    # common pattern: "Frame rate is: 28.4"
    m = re.search(r"Frame rate is:\s*([0-9]+(?:\.[0-9]+)?)", txt)
    if m:
        fps = float(m.group(1))

    # common pattern: "Average latency is: 95" or "Latency is: 95"
    m = re.search(r"(Average\s+)?latency\s+is:\s*([0-9]+(?:\.[0-9]+)?)", txt, re.IGNORECASE)
    if m:
        lat = float(m.group(2))

    if fps is None or lat is None:
        print("ERROR: Could not parse FPS/latency from last_run_output.txt.")
        print("Search the file for the exact lines containing FPS/latency and update regex.")
        raise ValueError("Parse failed")

    return fps, lat


def run_one(graph, n_frames, pp1, pp2, order):
    os.system(f"./run_inference.sh {graph} {n_frames} {pp1} {pp2} {order}")
    fps, lat = parse_last_run_output("last_run_output.txt")
    watts = float(input("Average watts from power meter? "))
    return fps, lat, watts


def save_results(filename, header_lines, rows):
    with open(filename, "w") as f:
        for line in header_lines:
            f.write(line + "\n")
        # CSV-like
        f.write("big_freq,little_freq,pp1,pp2,order,fps,latency,watts\n")
        for r in rows:
            f.write(f"{r['big_freq']},{r['little_freq']},{r['pp1']},{r['pp2']},{r['order']},{r['fps']},{r['latency']},{r['watts']}\n")


# -------------------------
# (1) Little CPU only sweep
# -------------------------
def exp1_little_cpu():
    print("=== EXP1: Little CPU only ===")

    # entire network on L (GPU/B ignored)
    pp1 = pp2 = ALEXNET_TOTALPARTS
    order = "L-G-B"

    rows = []
    for lf in LITTLE_FREQ_TABLE:
        os.system(f"./set_freq.sh little {lf}")
        # optionally keep big low to reduce background power
        os.system(f"./set_freq.sh big {BIG_FREQ_TABLE[0]}")

        print(f"Little freq={lf}")
        fps, lat, watts = run_one(args.graph, args.n_frames, pp1, pp2, order)

        rows.append(dict(big_freq=BIG_FREQ_TABLE[0], little_freq=lf, pp1=pp1, pp2=pp2, order=order, fps=fps, latency=lat, watts=watts))

    out = f"exp1_little_cpu_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    save_results(out, ["EXP1 Little CPU only"], rows)
    print(f"Saved {out}")
    return out


# -------------------------
# (2) Big CPU only sweep
# -------------------------
def exp2_big_cpu():
    print("=== EXP2: Big CPU only ===")

    pp1 = pp2 = ALEXNET_TOTALPARTS
    order = "B-G-L"

    rows = []
    for bf in BIG_FREQ_TABLE:
        os.system(f"./set_freq.sh big {bf}")
        os.system(f"./set_freq.sh little {LITTLE_FREQ_TABLE[0]}")  # keep little low

        print(f"Big freq={bf}")
        fps, lat, watts = run_one(args.graph, args.n_frames, pp1, pp2, order)

        rows.append(dict(big_freq=bf, little_freq=LITTLE_FREQ_TABLE[0], pp1=pp1, pp2=pp2, order=order, fps=fps, latency=lat, watts=watts))

    out = f"exp2_big_cpu_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    save_results(out, ["EXP2 Big CPU only"], rows)
    print(f"Saved {out}")
    return out


# -------------------------
# (3) GPU-only run at different big/little CPU freqs
# -------------------------
def exp3_gpu_cpu_freq_grid():
    print("=== EXP3: GPU-only with CPU freq grid ===")

    # GPU-only: partition points at end and GPU first in order
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


# -------------------------
# (4) Partition points sweep at fixed freqs + fixed order
# -------------------------
def exp4_partition_points():
    print("=== EXP4: Partition points sweep ===")

    fixed_big = 1800000
    fixed_little = 1200000
    os.system(f"./set_freq.sh big {fixed_big}")
    os.system(f"./set_freq.sh little {fixed_little}")

    fixed_order = "B-L-G"  # choose one and keep it constant

    # pp1 <= pp2 <= 8
    candidates = [(2,4), (3,5), (4,6), (5,7), (6,8)]

    rows = []
    for pp1, pp2 in candidates:
        print(f"pp1={pp1} pp2={pp2} order={fixed_order}")
        fps, lat, watts = run_one(args.graph, args.n_frames, pp1, pp2, fixed_order)
        rows.append(dict(big_freq=fixed_big, little_freq=fixed_little, pp1=pp1, pp2=pp2, order=fixed_order, fps=fps, latency=lat, watts=watts))

    out = f"exp4_partition_points_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    save_results(out, [f"EXP4 partition points fixed freqs: big={fixed_big}, little={fixed_little}, order={fixed_order}"], rows)
    print(f"Saved {out}")
    return out


# -------------------------
# (5) Component order sweep at fixed freqs + fixed partition points
# -------------------------
def exp5_component_orders():
    print("=== EXP5: Component orders sweep ===")

    fixed_big = 1800000
    fixed_little = 1200000
    os.system(f"./set_freq.sh big {fixed_big}")
    os.system(f"./set_freq.sh little {fixed_little}")

    fixed_pp1 = 4
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

    # Run all required experiments for the assignment:
    f1 = exp1_little_cpu()
    f2 = exp2_big_cpu()
    f3 = exp3_gpu_cpu_freq_grid()
    f4 = exp4_partition_points()
    f5 = exp5_component_orders()

    os.system("./set_fan.sh 1 0 0")  # restore fan state
    print("DONE. Generated files:")
    print(f1, f2, f3, f4, f5)
