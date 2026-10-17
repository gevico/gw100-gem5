> 本文件由 `HC32-Google-TPUv2v3-Norrie-Patil.pdf` 自动转换（docling 2.140.0，2026-10-17）。每页的整页图在 `images/` 目录；表格与数字请以 PDF 原文为准；`<!-- p.N -->` 标记对应 PDF 第 N 页。

<!-- p.1 -->

![第 1 页](images/page-001.jpg)

## Google's Training Chips Revealed: TPUv2 and TPUv3

Thomas Norrie, Nishant Patil, Doe Hyun Yoon, George Kurian, Sheng Li, James Laudon, Cliff Young, Norman P. Jouppi, and David Patterson

<!-- p.2 -->

![第 2 页](images/page-002.jpg)

## Thanks to the Team

<!-- p.3 -->

![第 3 页](images/page-003.png)

Production Pipeline

<!-- p.4 -->

![第 4 页](images/page-004.png)

## Capability

Production Pipeline Capability Production Pipeline

<!-- p.5 -->

![第 5 页](images/page-005.png)

<!-- p.6 -->

![第 6 页](images/page-006.png)

## Challenges of ML Training

More Computation More Memory Wider Operands More Programmability Harder Parallelization Backprop, transpose, derivatives Keep data around for backprop Need dynamic range (more than int8) User experimentation, optimizers Scale-up instead of scale-out

<!-- p.7 -->

![第 7 页](images/page-007.png)

## Our Constraints

Time A development day saved is a deployment day earned.

Staffing We have a lean team.

<!-- p.8 -->

![第 8 页](images/page-008.png)

## Key Goals (that is, where will we do a great job?)

```
Build it quickly Achieve high performance... ...at scale ...for new workloads out-of-the-box ...all while being cost effective
```

<!-- p.9 -->

![第 9 页](images/page-009.png)

## TPUv1 Recap

<!-- p.10 -->

![第 10 页](images/page-010.png)

## TPUv1 Recap

<!-- p.11 -->

![第 11 页](images/page-011.png)

## TPUv1 Recap

<!-- p.12 -->

![第 12 页](images/page-012.png)

## TPUv1 Recap

<!-- p.13 -->

![第 13 页](images/page-013.png)

## TPUv2 Changes

<!-- p.14 -->

![第 14 页](images/page-014.png)

## TPUv2 Changes

<!-- p.15 -->

![第 15 页](images/page-015.png)

## TPUv2 Changes

<!-- p.16 -->

![第 16 页](images/page-016.png)

## TPUv2 Changes

Single vector memory instead of buffers between fixed function units.

<!-- p.17 -->

![第 17 页](images/page-017.png)

## TPUv2 Changes

<!-- p.18 -->

![第 18 页](images/page-018.png)

## TPUv2 Changes

General purpose vector unit instead of a fixed function activation pipeline.

<!-- p.19 -->

![第 19 页](images/page-019.png)

## TPUv2 Changes

<!-- p.20 -->

![第 20 页](images/page-020.png)

## TPUv2 Changes

<!-- p.21 -->

![第 21 页](images/page-021.png)

## TPUv2 Changes

<!-- p.22 -->

![第 22 页](images/page-022.png)

## TPUv2 Changes

Connect DRAM into the memory system instead of directly into the matrix unit

<!-- p.23 -->

![第 23 页](images/page-023.png)

## TPUv2 Changes

<!-- p.24 -->

![第 24 页](images/page-024.png)

## TPUv2 Changes

Add interconnect for high-bandwidth scaling Redrawn with more detail...

<!-- p.25 -->

![第 25 页](images/page-025.png)

<!-- p.26 -->

![第 26 页](images/page-026.png)

<!-- p.27 -->

![第 27 页](images/page-027.png)

Scalar

Unit

Vector

Unit HBM

PCIe

Queues Core 0

Matrix

Multiply

Unit

Transpose /

Permute Unit

= Compute

= Memory

= Host

= Interconnect

Link Link Link Link Interconnect Router

Previous Slide Core 1

Matrix

Multiply

Unit

Transpose /

Permute Unit Scalar Unit Vector Unit HBM

PCIe

Queues

<!-- p.28 -->

![第 28 页](images/page-028.png)

## TPU Core

- VLIW Architecture
- Leverage known compiler techniques
- Linear Algebra ISA
- Scalar, vector, and matrix
- Built for generality

<!-- p.29 -->

![第 29 页](images/page-029.png)

## TPU Core: Scalar Unit

- 322b VLIW bundle
- 2 scalar slots
- 4 vector slots (2 for load/store)
- 2 matrix slots (push, pop)
- 1 misc slot
- 6 immediates
- Scalar Unit performs:
- Full VLIW bundle fetch and decode
- Scalar slot execution

<!-- p.30 -->

![第 30 页](images/page-030.png)

## TPU Core: Scalar Unit

<!-- p.31 -->

![第 31 页](images/page-031.png)

## TPU Core: Vector Unit (Lane)

<!-- p.32 -->

![第 32 页](images/page-032.png)

## TPU Core: Matrix Multiply Unit

- 128 x 128 systolic array
- Streaming LHS and results
- Stationary RHS (w/ optional transpose)
- Numerics
- bfloat16 multiply
- ■ {s, e, m} = {1, 8, 7}
- ■ The original!
- float32 accumulation

<!-- p.33 -->

![第 33 页](images/page-033.png)

<!-- p.34 -->

![第 34 页](images/page-034.png)

## TPU Core: Transpose, Reduction, Permute Unit

- Efficient common matrix transforms
- Transpose
- Reduction
- Permutation
- Generally, allow reshuffling of data across vector lanes

<!-- p.35 -->

![第 35 页](images/page-035.png)

## Memory System

- Loads and stores against SRAM scratchpads
- Provides predictable scheduling within the core
- Can stall on sync flags
- Accessible through asynchronous DMAs
- Indicate completion in sync flags

<!-- p.36 -->

![第 36 页](images/page-036.png)

## Memory System

- Loads and stores against SRAM scratchpads
- Provides predictable scheduling within the core
- Can stall on sync flags
- Accessible through asynchronous DMAs
- Indicate completion in sync flags

<!-- p.37 -->

![第 37 页](images/page-037.png)

## Memory System

- Loads and stores against SRAM scratchpads
- Provides predictable scheduling within the core
- Can stall on sync flags
- Accessible through asynchronous DMAs
- Indicate completion in sync flags

<!-- p.38 -->

![第 38 页](images/page-038.png)

## Memory System

<!-- p.39 -->

![第 39 页](images/page-039.png)

## Memory System

<!-- p.40 -->

![第 40 页](images/page-040.png)

## Interconnect

- On-die router with 4 links
- 500 Gbps per link
- Assembled into 2D torus
- Software view:
- Uses DMAs just like HBM
- Restricted to push DMAs
- Simply target another chip id

<!-- p.41 -->

![第 41 页](images/page-041.png)

## Floorplan

<!-- p.42 -->

![第 42 页](images/page-042.jpg)

## TPUv3

The Anti-Second System

<!-- p.43 -->

![第 43 页](images/page-043.png)

<!-- p.44 -->

![第 44 页](images/page-044.png)

<!-- p.45 -->

![第 45 页](images/page-045.png)

<!-- p.46 -->

![第 46 页](images/page-046.png)

<!-- p.47 -->

![第 47 页](images/page-047.png)

<!-- p.48 -->

![第 48 页](images/page-048.png)

<!-- p.49 -->

![第 49 页](images/page-049.png)

<!-- p.50 -->

![第 50 页](images/page-050.png)

## Retrospective: Key Goals

Build it quickly

Achieve high performance...

...at scale

...for new workloads

out-of-the-box

...all while being cost effective

<!-- p.51 -->

![第 51 页](images/page-051.png)

## Retrospective: Key Goals

## Build it quickly

Achieve high performance... ...at scale

...for new workloads out-of-the-box

...all while being cost effective

- Co-design: simplified hardware with software predictability (e.g., VLIW, scratchpads)
- Willingness to make tradeoffs

<!-- p.52 -->

![第 52 页](images/page-052.png)

## Retrospective: Key Goals

Build it quickly

Achieve high performance...

...at scale

...for new workloads out-of-the-box

...all while being cost effective

- Compute density with bfloat16 systolic array
- HBM to feed the compute
- XLA compiler optimizations

<!-- p.53 -->

![第 53 页](images/page-053.png)

## Retrospective: Key Goals

```
Build it quickly Achieve high performance... ...at scale ...for new workloads out-of-the-box ...all while being cost effective
```

- System-first approach
- Interconnect with a familiar interface for ease-of-use

<!-- p.54 -->

![第 54 页](images/page-054.png)

## Retrospective: Key Goals

Build it quickly

Achieve high performance... ...at scale

...for new workloads out-of-the-box

...all while being cost effective

- Flexible big data cores with principled linear algebra framework
- XLA compiler
- HBM capacity

<!-- p.55 -->

![第 55 页](images/page-055.png)

## Retrospective: Key Goals

Build it quickly

Achieve high performance...

...at scale

...for new workloads out-of-the-box

...all while being cost effective

- Matrix Unit efficiency
- Simplicity
- High performance for good perf/$

<!-- p.56 -->

![第 56 页](images/page-056.jpg)

System &amp;

Performance

<!-- p.57 -->

![第 57 页](images/page-057.png)

## Supercomputer with dedicated interconnect

- TPUv1: single-chip system-built as coprocessor to a CPU
- Works well for inference
- TPUv2, v3: ML Supercomputer
- Multi-chip scaling critical for practical training times
- Single TPUv2 chip would take 60 - 400 days for production workloads

<!-- p.58 -->

![第 58 页](images/page-058.png)

## TPU Training Pod Architecture

TPU Board

TPUs interconnected in 2D Torus

Dedicated network for

synchronous parallel training

<!-- p.59 -->

![第 59 页](images/page-059.png)

## TPU Training Pod Architecture

## TPU Board

TPUs interconnected in 2D Torus

Dedicated network for

synchronous parallel training

<!-- p.60 -->

![第 60 页](images/page-060.jpg)

## Supercomputer with dedicated interconnect

TPUv2 supercomputer (256 chips)

TPUv2 boards = 4 chips

<!-- p.61 -->

![第 61 页](images/page-061.jpg)

## Supercomputer with dedicated interconnect

TPUv2 supercomputer (256 chips)

TPUv3 supercomputer (1024 chips)

TPUv2 boards = 4 chips

TPUv3 boards = 4 chips

<!-- p.62 -->

![第 62 页](images/page-062.jpg)

## Supercomputer with dedicated interconnect

TPUv2 supercomputer (256 chips)

TPUv3 supercomputer (1024 chips)

## 11.5 petaflops

4 TB HBM

2-D torus

256 chips

- &gt; 100 petaflops 32 TB HBM

Liquid cooled

New chip + larger-scale system 1024 chips

<!-- p.63 -->

![第 63 页](images/page-063.png)

## 6 Production Applications

- MultiLayer Perceptrons (MLP)
- MLP0 is unpublished
- MLP1 is RankBrain [Cla15]

## · Convolutional Neural Networks (CNN)

- CNN0 is AlphaZero, which mastered the games chess, Go, and shogi [Sil18]
- CNN1 is an Google-internal model for image recognition

## · Recurrent Neural Networks (RNN)

- RNN0 is a Translation model [Che18]
- RNN1 is a Speech model [Chi18]

<!-- p.64 -->

![第 64 页](images/page-064.png)

## TPUv3 Supercomputer Scaling: 6 Production Apps

- MLP0 &amp; MLP1
- 40% &amp; 14% of perfect linear scale at 1024 chip-scale
- ■ Limited by embeddings

<!-- p.65 -->

![第 65 页](images/page-065.jpg)

## TPUv3 Supercomputer Scaling: 6 Production Apps

- MLP0 &amp; MLP1
- 40% &amp; 14% of perfect linear scaling
- CNN0
- 96% of perfect linear scaling!
- CNN1, RNN0, RNN1
- 3 production apps run at 99% of perfect linear scaling at 1024 chips!

<!-- p.66 -->

![第 66 页](images/page-066.png)

## TPUv3 Supercomputer Scaling: MLP0-next vs. MLP0

- Improved scaling for newer larger models and SW improvements for better quality
- MLP0-next: 67% of perfect linear scale at 1024 chips
- ■ Up from 40% from MLP0

<!-- p.67 -->

![第 67 页](images/page-067.png)

## TPUv3 vs TPUv2: Memory Bound or 2X MXUs Useful?

<!-- p.68 -->

![第 68 页](images/page-068.png)

## Speedup: MLPerf v0.5 (11/2018) - v0.6 (5/2019)

Production apps also sped up:

- CNN0 1.8x (more bfloat16 use)
- MLP0 1.6x (better partitioning and placement of embeddings)

Performance enables larger models for improved accuracy

<!-- p.69 -->

![第 69 页](images/page-069.png)

## Inference: TPUv2/v3 vs TPUv1

- Training chips can also do inference (looks like forward pass)
- Bfloat16 numerics in TPUv2/v3 vs int8 in TPUv1

<!-- p.70 -->

![第 70 页](images/page-070.jpg)

## Key Takeaways

- TPUv2/v3 supercomputers with 256-1024 chips run production applications at scale, powering many Google products
- TPUs are widely used to accelerate production and research
- Proven results from Model/HW/SW codesign, with more opportunities still available

Used across many products
