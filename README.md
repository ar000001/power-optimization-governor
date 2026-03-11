# governor
Governor for the power optimization of inference on AlexNet on the Khadas VIM3 development board using software pipelining. The Khadas VIM3 development board is based on the Amlogic A311D heterogeneous multiprocessor system-on-chip (HMPSoC)).
This project is born from a course at Universiteit van Amsterdam. 

## Overview

This governor uses a PID controller to optimize pipeline configurations (CPU frequencies, partition points, and processing order) to meet target FPS and latency requirements while minimizing power consumption.

## Setup
As this is an application specific controller, it was only implemented for a specific system setup. The system used here is based on the Amlogic A311D HMPSoC. Furthermore, it requires the help of software pipelining. In this project I used the [ARMCL-pipe-all library](https://github.com/Ehsan-aghapour/ARMCL-pipe-all/) software pipeline module developed by Ehsan Aghapour. This module allows to split a given NN architecture into multiple pipeline stages. For the communication between the board and my development laptop I used the android debug bridge tool.

## Building

As the governor uses the ARMv7 ISA it is most likely necessary to crosscompile the source code into that ISA.
In the folder `governor` execute the following command:

```bash
make -C ./src clean all CC="$PATH_TO_ANDORIDNDK/android-ndk-r21e/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi24-clang"
```

For that, you need to install the [Android NDK r21e](https://github.com/android/ndk/wiki/unsupported-downloads).
This will compile the governor executable from the source files.

You also need to compile the ARMCL-Pipe-All library in similar fashion, because the governor relies on executing AlexNet using that exact library. For that, please refer to Ehsan's repository.

When everything is finished compiling, you should push all the binaries onto the board using adb:

```bash
adb -d root
adb -d push governor PATH_ON_BOARD
adb -d push ARMCL-Pipe-All_PATH ARMCL-Pipe-All_PATH_ON_BOARD
```

Make sure the files are in the same directory or change the location of the graph binaries in the source code.

## Usage

To  run the governor on the board, use the following commands:

First, connect to the board:

```bash
adb -d root
adb -d shell
cd GOVERNOR_DIR_ON_BOARD
```

```bash
./governor <graph> <total_parts> <target_fps> <target_latency>
```

for example

```bash
./governor graph_alexnet_all_pipe_sync 8 16 200
```

### Arguments

- graph: Path to the graph configuration file
- total_parts: Total number of parts to process
- target_fps: Target frames per second
- target_latency: Target latency in milliseconds
