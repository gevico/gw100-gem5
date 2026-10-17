> 本文件由 `HC37-Google-Ironwood.pdf` 自动转换（docling 2.140.0，2026-10-17）。每页的整页图在 `images/` 目录；表格与数字请以 PDF 原文为准；`<!-- p.N -->` 标记对应 PDF 第 N 页。

<!-- p.1 -->

![第 1 页](images/page-001.jpg)

## Ironwood: Delivering Best in Class perf, perf/TCO and perf/Watt for Reasoning Model Training and Serving

Norman P. Jouppi and Sridhar Lakshmanamurthy With contributions from many others Hot Chips August 26, 2025

<!-- p.2 -->

![第 2 页](images/page-002.png)

## Breakthrough Innovations in Ironwood TPU Systems

- 9216 Ironwood chips use optical circuit switches (OCS) to share memory
- Directly addressable shared HBM memory capacity of 1.77 PB
- 42.5 Exaflops of ML Compute using FP8 precision
- Emphasis on RAS (reliability, availability, and serviceability)
- Industry-leading compute power efficiency, 2x perf/W over previous generation
- 3rd generation of liquid cooling infrastructure
- 4th-generation Sparsecore for embeddings and collectives offload
- Deployment at hyperscale underway

<!-- p.3 -->

![第 3 页](images/page-003.png)

## TPUv4: 4096 Chips Share Memory Using OCS

Resources allocated

on a per job basis Any slice size or shape can be created, limited only by pool size

<!-- p.4 -->

![第 4 页](images/page-004.png)

## 9216 Ironwood Chips Sharing Memory Using OCS

Shared-memory Superpod

<!-- p.5 -->

![第 5 页](images/page-005.png)

## Racks Are Connected With Optical Circuit Switches (OCS)

- Different ranks of OCS connect different dimensions and indices

<!-- p.6 -->

![第 6 页](images/page-006.png)

## Directly-addressable Shared HBM Memory of 1.77 PB

- Sets a new record for shared-memory multiprocessors
- Enables low-overhead high-bandwidth sharing of data
- Accelerates fine-grained parallel processing
- Enables higher degrees of parallelism with high perf/TCO
- Able to support huge models efficiently

<!-- p.7 -->

![第 7 页](images/page-007.jpg)

## 42.5 Exaflops of ML Compute using FP8 Precision

<!-- p.8 -->

![第 8 页](images/page-008.png)

## Emphasis on RAS (Reliability, Availability, and Serviceability)

- Enables productive scaling to extreme sizes

<!-- p.9 -->

![第 9 页](images/page-009.jpg)

## Industry-leading Compute Power Efficiency

<!-- p.10 -->

![第 10 页](images/page-010.jpg)

## 3rd Generation of Liquid Cooling Infrastructure

- For details please see Jorge Padilla's 2025 Hot Chips tutorial slides

<!-- p.11 -->

![第 11 页](images/page-011.png)

## 4th Gen SparseCore: Accelerate Embeddings &amp; Collectives

- 2.4x FLOPS vs. 3rd Gen SparseCore
- Offload collectives for pretraining, RL-based fine tuning
- Execute in parallel with main TensorCore computation
- Embedding processing for large recommendation models
- SparseCores leverage non-coherent shared memory across a pod
- Massive memory parallelism (millions of outstanding references between nodes in the pod) is exploited with multithreading

<!-- p.12 -->

![第 12 页](images/page-012.png)

## Large Scale Pre-training &amp; MW Scale Load Swing

Full

Power

Idle Time

- Unprecedented load swings (MW scale in sec/ms) real with large-scale pretraining
- Ironwood supports both HW and SW features to smooth out these swings
- Deploys a full-stack approach to proactive power shaping - Google Project Smoothie

<!-- p.13 -->

![第 13 页](images/page-013.jpg)

## Deployment at Hyperscale Underway

<!-- p.14 -->

![第 14 页](images/page-014.png)

## Ironwood Chip

- First dual compute die TPU, 4614 TFLOPS of FP8
- &gt;10x compute compared to TPU v5p
- Capable of large scale pretraining of foundation models
- 8 stacks of HBM3E, peak BW 7.3 TB/s, capacity 192 GiB
- Optimized for serving latest generation thinking models
- 1.2 TBps I/O to gluelessly scale-up to 9216 chips
- Industry leading cold plate thermal solution
- Integrated root-of-trust (iROT) for secure computing
- Functional BIST &amp; silent data corruption (SDC) mitigation
- Logic repair to improve yield
- Dynamic voltage/frequency scaling for efficient perf/W
- AI based ALU circuits, floor plan optimization

<!-- p.15 -->

![第 15 页](images/page-015.png)

## Ironwood Architecture

<!-- p.16 -->

![第 16 页](images/page-016.png)

## Ironwood Architecture

<!-- p.17 -->

![第 17 页](images/page-017.png)

## Ironwood Architecture

<!-- p.18 -->

![第 18 页](images/page-018.png)

## Ironwood Architecture

<!-- p.19 -->

![第 19 页](images/page-019.png)

## Ironwood Architecture

<!-- p.20 -->

![第 20 页](images/page-020.png)

## Ironwood Architecture

Host &amp; Mgmt Plane: iROT, PM etc.

<!-- p.21 -->

![第 21 页](images/page-021.png)

## Confidential Computing Support

- Key requirement for Internal &amp; Cloud Customers
- Training and serving LLMs
- Ironwood supports integrated root-of-trust (iROT) controller
- Hardware support for secure boot
- Hardware support for secure test and debug
- Ironwood also adds hardware support for PCIe Data Object Exchange (DOE) and Component Measurement and Authentication (CMA)

<!-- p.22 -->

![第 22 页](images/page-022.jpg)

## Ironwood Tray

- 4 Ironwood TPUs per tray
- Liquid cooled
- 4 chips and VRs with parallel water flow
- Flow rate controlled by valve
- Similar to variable fan speeds for air
- Yields more efficient water cooling
- PCIe Gen5x16 per TPU for host I/O
- On back side of board
- 18 OSFP 1 connectors for off-board interconnect
- 16 on top side shown in photo

<!-- p.23 -->

![第 23 页](images/page-023.jpg)

## The Rack

<!-- p.24 -->

![第 24 页](images/page-024.png)

## Optimal ML Performance: From Chip to Data Center

- Maximize ML throughput under dynamically varying power budgets
- Achieve optimal energy efficiency and Perf/W

Chip/System/Rack Power Management Capabilities

The "Knobs"

Datacenter Power Awareness &amp; Controls Optimal Perf/Power within datacenter power capacity

The "Brain"

<!-- p.25 -->

![第 25 页](images/page-025.png)

## Optimal ML Performance: From Chip to Data Center

- Maximize ML throughput under dynamically varying power budgets
- Achieve optimal energy efficiency and Perf/W

Chip/System/Rack Power Management Capabilities

The "Knobs"

Datacenter Power Awareness &amp; Controls Optimal Perf/Power within datacenter power capacity

The "Brain"

<!-- p.26 -->

![第 26 页](images/page-026.png)

## Proprietary + Confidential Ironwood Continues Our Lead in Scale-up and Scale-out

- 9216 Ironwood chips share memory per superpod with optical circuit switches (OCS)
- Huge boost for scale-up
- Directly addressable shared HBM memory capacity of 1.77 PB
- Enables super low-latency and high BW sharing of data
- 42.5 Exaflops of ML Compute per superpod using FP8 precision
- Zettaflops across multiple superpods with scale-out
- Emphasis on RAS (reliability, availability, and serviceability)
- Enables productive scaling to extreme sizes
- Industry-leading compute power efficiency, 2x perf/W over previous generation
- Who wants nuclear reactors?
- 3rd generation of liquid cooling infrastructure
- 8+ years of production experience, over 1GW in production
- 4th-generation Sparsecore for embeddings and collectives offload
- Accelerate recommendation models and overlap collectives with computation
- Available soon in Google Cloud
