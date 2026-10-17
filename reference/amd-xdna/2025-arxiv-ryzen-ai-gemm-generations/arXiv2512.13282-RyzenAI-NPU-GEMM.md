> 本文件由 `arXiv2512.13282-RyzenAI-NPU-GEMM.pdf` 自动转换（docling 2.140.0，2026-10-17）。图片在 `images/` 目录；表格与数字请以 PDF 原文为准；`<!-- p.N -->` 标记对应 PDF 第 N 页。

<!-- p.1 -->

## Striking the Balance: GEMM Performance Optimization Across Generations of Ryzen™ AI NPUs

Endri Taka ∗

The University of Texas at Austin Austin, TX, United States endri.taka@utexas.edu

Pranathi Vasireddy Advanced Micro Devices, Inc.

Longmont, CO, United States pvasired@amd.com

## Abstract

The high computational and memory demands of modern deep learning (DL) workloads have led to the development of specialized hardware devices from cloud to edge, such as AMD's Ryzen™ AI XDNA™ NPUs. Optimizing general matrix multiplication (GEMM) algorithms for these architectures is critical for improving DL workload performance. To this end, this paper presents a common systematic methodology to optimize GEMM workloads across the two current NPU generations, namely XDNA and XDNA2. Our implementations exploit the unique architectural features of AMD's NPUs and address key performance bottlenecks at the system level. Endto-end performance evaluation across various GEMM sizes demonstrates state-of-the-art throughput of up to 6.76 TOPS (XDNA) and 38.05 TOPS (XDNA2) for 8-bit integer (int8) precision. Similarly, for brain floating-point (bf16) precision, our GEMM implementations attain up to 3.14 TOPS (XDNA) and 14.71 TOPS (XDNA2). This work provides significant insights into key performance aspects of optimizing GEMM workloads on Ryzen AI NPUs.

## Keywords

Computer Architecture, Deep Learning, Hardware Acceleration, Matrix Multiplication, Neural Processing Unit, Ryzen™ AI

## ACM Reference Format:

Endri Taka, Andre Roesti, Joseph Melber, Pranathi Vasireddy, Kristof Denolf, and Diana Marculescu. 2025. Striking the Balance: GEMM Performance Optimization Across Generations of Ryzen™ AI NPUs. In . ACM, New York, NY, USA, 12 pages. https://doi.org/10.1145/nnnnnnn.nnnnnnn

## 1 Introduction

The widespread adoption of deep learning (DL) applications, combined with their intensive computational requirements, has driven

∗ Work performed during an internship at AMD, Inc.

Permission to make digital or hard copies of all or part of this work for personal or classroom use is granted without fee provided that copies are not made or distributed for profit or commercial advantage and that copies bear this notice and the full citation on the first page. Copyrights for components of this work owned by others than the author(s) must be honored. Abstracting with credit is permitted. To copy otherwise, or republish, to post on servers or to redistribute to lists, requires prior specific permission and/or a fee. Request permissions from permissions@acm.org.

Conference'17, July 2017, Washington, DC, USA

© 2025 Copyright held by the owner/author(s). Publication rights licensed to ACM. ACM ISBN 978-x-xxxx-xxxx-x/YY/MM

[https://doi.org/10.1145/nnnnnnn.nnnnnnn](https://doi.org/10.1145/nnnnnnn.nnnnnnn)

Andre Roesti

Advanced Micro Devices, Inc. Longmont, CO, United States andre.roesti@amd.com

## Kristof Denolf

Advanced Micro Devices, Inc. Longmont, CO, United States kristof.denolf@amd.com Joseph Melber Advanced Micro Devices, Inc. Longmont, CO, United States joseph.melber@amd.com

## Diana Marculescu

The University of Texas at Austin Austin, TX, United States dianam@utexas.edu

the emergence of specialized hardware platforms. While cloud data centers continue to provide large-scale acceleration [1, 11, 28, 38, 48, 57], the increasing demand for energy-efficiency, low-latency, as well as enhanced privacy and security has motivated the integration of DL accelerators on the edge [37, 40, 42, 47, 51]. To this end, AMD released the Ryzen™ AI processors [51], featuring multi-core CPUs, an integrated GPU, and a new neural processing unit (NPU). The NPU features the XDNA™ architecture and serves as a dedicated DL accelerator integrated with x86 processors. The AMD NPU is an evolution of the AI Engine (AIE) architecture, utilized in Versal™ adaptive system-on-chip (SoC) platforms [2, 33] and Alveo™ V70 accelerator cards [3]. The NPU architecture leverages the characteristics of modern DL workloads, where compilers can determine most of the control flow statically. In particular, AMD NPUs provide an explicit data movement architecture, which both reduces hardware complexity and enables high performance, while offering substantially higher energy-efficiency compared to dynamically scheduled architectures, such as CPUs and GPUs [51].

As modern DL workloads are dominated by general matrix multiplication (GEMM) operations, several prior works have aimed to design and optimize GEMM workloads on the Versal platforms [30, 43, 54-56, 59-62]. The Versal SoC devices integrate an FPGA fabric, which acts as another level of memory hierarchy and is utilized for efficient exploitation of data reuse in GEMM. Furthermore, the flexibility of the FPGA allows the design of customized tiling schemes [55, 59] and tailored data layout transformations [56].

In this work, we conduct a comprehensive study to optimize GEMM workloads on Ryzen AI NPUs. Due to the absence of FPGA fabric, GEMM mapping and optimization on the AMD NPUs is a fundamentally different problem compared to Versal devices. This highlights the necessity for a separate methodology to address several new design challenges introduced by the NPUs. Having identified this important research gap, we propose a unified systematic framework to maximize GEMM performance across the two current generations, namely XDNA and XDNA2. We extensively focus on the end-to-end GEMM performance and introduce a novel procedure to enable system-level optimization. Within the framework, matrices are retained in regular order (row- and column-major) in main memory (DRAM). This facilitates seamless integration with tensor libraries for DL, such as GGML [34], while also enabling the implementation of high-performance GEMM libraries, similar to GPUs [16, 20, 49, 50]. Moreover, our GEMM designs efficiently leverage several architectural features of the NPUs (e.g., on-the-fly tensor transformations), while also addressing critical performance bottlenecks (e.g., via sophisticated data movement design between the NPU and DRAM). The key contributions of this paper are:

<!-- p.2 -->

- A systematic methodology to optimize GEMM workloads on Ryzen AI NPUs through analytical modeling along with hardware profiling. Our methodology is general in scope; in this paper we apply it across the two current NPU generations. Observing the inverse relationship between compute and off-chip memory, our approach is based on identifying the balanced point that maximizes performance.
- A sophisticated GEMM implementation that enables sufficient contiguous DRAM accesses to maximize performance. Our design leverages the multi-dimensional tensor addressing feature across the entire NPU hierarchy to transform matrices into the tiled layout expected by NPU cores, enabling matrices to remain in standard layout in DRAM (i.e., row- and column- major order) without explicit pre-tiling.
- A thorough experimental evaluation on two mini PCs, each incorporating an NPU from a different generation, which exhibits GEMM performance up to 6.76 TOPS (XDNA) and 38.05 TOPS (XDNA2) for 8-bit integer (int8), as well as 3.14 TOPS (XDNA) and 14.71 TOPS (XDNA2) for brain floatingpoint (bf16). Furthermore, we present roofline sweeps to provide a holistic view of performance across hundreds of GEMM sizes for each data type.
- We provide essential insights regarding key performance considerations for GEMM optimization on AMD's NPU architecture.

## 2 RelatedWork

Having identified the importance of GEMM in DL, several prior works have proposed frameworks that aim to optimize GEMM workloads on Versal devices. Some prior works [56, 59-62] explore end-to-end GEMM performance including DRAM, while others [30, 43, 54, 55] focus exclusively on the AIE computation part (i.e., excluding off-chip considerations). One common design choice across all of these works is to partition GEMM across the reduction dimension, driven by the limited number of ports in the AIE-FPGA interface. For instance, MaxEVA [54, 55] utilizes 20% of the AIE cores to perform adder-tree reduction, limiting the GEMM compute efficiency to 80%. In a similar manner, GAMA [43] exploits the cascade interface to transfer partial accumulations across AIE cores, observing an average performance degradation of 7% due to cascade stalls. In contrast, as we demonstrate in this work, all cores on AMD NPUs can perform GEMM computation in an independent fashion, thereby maximizing the compute efficiency.

Some prior works have also explored mapping GEMM on AMD NPUs. In [52], the authors explore fine-tuning GPT-2 on the Ryzen AI processors. Specifically, they offload the time-intensive GEMM computation on XDNA and attain 2.8× speedup compared to a CPU-only implementation. Furthermore, they utilize a GEMM implementation similar to the non-optimized programming example in [18]. In [32], a task-based programming model for dataflow accelerators is presented, demonstrating a GEMM implementation on XDNA. In particular, they attain up to 5.04 TOPS and 1.95 TOPS

Figure 1: Architecture of Ryzen AI NPUs.

![Image](images/image_000000_09cf3bc28819bddae5bd466e76ae76ca1116555d478445e1700fc5e0dd361494.png)

for int8 and bf16 data types, respectively, which closely matches the performance in [18]. In contrast, our optimized GEMM designs achieve superior performance of up to 6.76 TOPS (34% higher) and 3.14 TOPS (62% higher) on XDNA for int8 and bf16, respectively.

Related workloads were covered in prior publications. The authors in [61] map a ResNet layer on XDNA, utilizing only 20% of the XDNA cores. In [31], a compiler framework for dynamic attention folding on AMD NPUs is presented. Moreover, in [39] the authors perform characterization of generative AI workloads on Ryzen AI processors. Finally, other works leverage the NPUs to perform stencil applications [58], accelerate Fortran intrinsics [27], and implement a variant of the fast Fourier transform (FFT) [46].

## 3 NPUArchitecture, Features &amp; Programming

## 3.1 NPUArchitecture Overview

The architecture of both XDNA and XDNA2 is illustrated in Fig. 1. The NPU is a modular and scalable architecture, comprising a 2D array of identical compute tiles (CompTiles) [24, 51]. CompTiles include the processing cores which operate out of a local memory (L1). The NPU core is a very long instruction word (VLIW) processor with single instruction multiple data (SIMD) datapath, supporting both fixed-point and floating-point operations. NPUs incorporate a second level of on-chip memory (L2) via the memory tiles (MemTiles). These are arranged in a single row, located below the array of CompTiles (Fig. 1). Finally, NPUs include a last row of interface tiles (ShimTiles) to provide communication with DRAM.

Data movement between the levels of memory hierarchy is facilitated by direct memory access (DMA) engines, which are integrated across all NPU tiles. DMAs move data between the NPU tiles by utilizing the configurable interconnects (switches), shown as black squares in Fig. 1. The DMA engines of ShimTiles read/write data to DRAM via the NPU network-on-chip (NoC) and SoC-level fabric of the Ryzen AI chips [51, 53]. Task scheduling and data movement are orchestrated by an on-chip command processor. This processor controls the data movement between ShimTiles and DRAM at runtime, allowing the NPU to focus exclusively on the main computation.

<!-- p.3 -->

Figure 2: Data movement across the memory hierarchy in Ryzen AI NPUs: input buffer A (a) and output buffer C (b).

![Image](images/image_000001_a98fb2deeee24527efd6afd20d0493f3d7c6048824995f6b656284774c5a9d83.png)

The command processor is also responsible for (re-)configuring the NPU compute kernels, switches, and DMA transfers.

The XDNA has 20 compute cores, which are organized as a 4×5 array (rows × columns) of CompTiles [51], while the XDNA2 contains 32 cores as a 4×8 array [4]. Both NPU generations have 64 KB of memory per L1 tile and 512 KB of memory per L2 tile [24, 31, 51]. XDNA and XDNA2 natively support int8, int16 and bf16 precisions. Additionally, XDNA2 has hardware support for block floating-point (bfp16) datatype [5], where a block of eight numbers shares one common exponent [15, 29]. XDNA2 offers increased theoretical peak compute capabilities, delivering up to 50 TOPS [4, 10], compared to the 10 TOPS of XDNA [51].

## 3.2 Data Movement Architecture

Ryzen AI NPUs incorporate a dedicated data movement architecture that allows programmers to explicitly configure data transfers between all levels of the memory hierarchy. Data moves from a source DMA channel to a destination DMA channel through a circuit- or packet-switched stream, using one or more configurable switches between them. The source channel is a memory-map to stream (MM2S) channel which reads data from memory and pushes it to the stream switches. Conversely, the destination channel receives data from the stream switches and writes it to the memory-mapped space (S2MM). Each CompTile and ShimTile includes two MM2S and two S2MM DMA channels, while MemTiles incorporate six MM2S and six S2MM channels [24].

Fig. 2a shows an example of moving an input buffer 𝐴 from DRAM to an L2 MemTile, and eventually to L1, where it can be utilized by the corresponding core. In a similar fashion, Fig. 2b depicts the data movement of an output buffer from L1 to L2, and eventually to DRAM. The synchronization of data buffers between the DMAs and the corresponding module (e.g., the NPU core or DRAM) is managed by hardware lock units [24, 51].

DMAs run independently and in parallel with computation on cores. They are programmed by configuring a sequence of buffer descriptors (BDs). A BD contains all the required information associated with a specific DMA transfer, such as the amount of data to read/write, the memory addresses involved, and the locks to acquire/release before and after each transfer [8, 24]. BDs support both linear memory addressing and multi-dimensional address generation, enabling on-the-fly data layout transformations required for DL tensors. CompTiles and ShimTiles support each 3D tensor addressing, while MemTiles incorporate 4D addressing. This important DMA addressing feature is extensively exploited by our GEMM implementation across all tiles in the NPU architecture (refer to Sec. 4.3), allowing tensors to be stored in regular order in DRAM.

## 3.3 Programming Tools

In this work, we use IRON, an open-source close-to-metal toolchain for developing programs on Ryzen AI NPUs [17]. As a low-level toolkit, IRON enables fine-grained control of the NPU architectural attributes, such as explicit data movement and complex access patterns supported in DMAs, while also providing convenient programming abstractions [35]. These characteristics render IRON a compelling choice for GEMM implementation, where explicit control of the NPU architectural features is critical to performance.

IRON is based on a multi-level intermediate representation (MLIR) [41] dialect, named "AIE", and offers a user-friendly interface to this dialect through Python bindings [45]. Hence, Python scripts can be used to generate code that describes both the data movement across the NPU hierarchy and the compute kernels that run on the cores. The single-core compute kernels can be written in high-level C++ [5], or low-level SIMD intrinsics [7]. Finally, two options are available for single-core kernel compilation: the proprietary xchesscc compiler [8, 17] and the open-source Peano tool [9].

## 4 GEMMDesign &amp;Optimization

## 4.1 Multi-Level Tiling Method

Fig. 3a illustrates the multi-level tiling scheme we use to partition the input and output matrices. The inner-most (first) tiling level is defined by the supported shapes of the AIE API library for the single-core GEMM kernel [5], and is expressed via the parameters 𝑟 × 𝑠 × 𝑡 . The AIE API provides multiple optimized modes for each supported precision and enables portability across NPU generations. The second tiling level is determined by the GEMM kernel that is supported out of local L1 memory, denoted as 𝑚ct × 𝑘ct × 𝑛ct. Since the NPU is a multi-core architecture, we partition GEMM across multiple cores to exploit spatial-level parallelism and maximize performance. The third tiling level achieves this partitioning and corresponds to the GEMM size operating on the entire NPU array (parameters and mapping delineated in the next section). Finally, the outer-most (fourth) tiling level is dictated by the final GEMM sizes of input matrices 𝐴 (typically activations) and 𝐵 (typically weights), and output matrix 𝐶, expressed as 𝑀 × 𝐾 × 𝑁 .

## 4.2 GEMMMapping Strategy on NPUs

4.2.1 NPU ArrayMapping &amp; Core Design. Our mapping strategy is to parallelize GEMM in space across the 𝑀 and 𝑁 dimensions, while reduction across the 𝐾 dimension is performed in time. In this manner, all NPU cores perform the same GEMM computation on different data and operate independently. This leads to maximized performance, since no data communication occurs between the cores, as opposed to Versal devices, where the reduction dimension is partitioned across multiple cores (e.g., [43, 54]). By using the broadcast feature of the NPU architecture, we can attain spatial parallelization and exploit the inherent data reuse of the GEMM algorithm. This parallelization is expressed via the number of single-core tiles in the 𝑀 and 𝑁 dimensions, and is defined by the parameters𝑚rows and 𝑛cols, respectively (Fig 3a). In this work, we map the parameters𝑚rows and 𝑛cols in a straightforward fashion to the number of rows and columns of the two NPU architectures, respectively. In particular, we broadcast each input 𝐴 tile across one row in the NPU array (e.g., 𝐴0 in Fig. 3b and Fig. 3c for XDNA and XDNA2, respectively). Similarly, each 𝐵 tile is broadcast across one column of NPU cores. Note that due to the absence of a ShimTile in the last column of XDNA, we choose to map GEMM across 4 rows and 4 columns for this architecture, similar to [18, 32, 52]. This enables a symmetric 4 × 4 (𝑚rows × 𝑛cols) GEMM implementation, as depicted in Fig. 3b. For XDNA2, we utilize the entire 4 × 8 array, which results in an asymmetric mapping (Fig. 3c).

<!-- p.4 -->

Figure 3: Proposed GEMM multi-level tiling scheme (a), and GEMM mapping strategy on XDNA (b) and XDNA2 (c).

![Image](images/image_000002_90536df6ee6f9020749c41f635a628cb6cf7f0ea7c64c36c6eaec57c2def93de.png)

Each core performs a GEMM of 𝑚ct × 𝑘ct × 𝑛ct size. To handle the reduction across 𝐾, the single-core kernel also loads previous partial results of 𝑚ct × 𝑛ct size, performs accumulation and stores the updated results back to the output tiles. For example, the upperleft core in Fig. 3b, sequentially loads tiles 𝐴0 and 𝐵0, 𝐴0 ′ and 𝐵0 ′ , etc., in order to perform reduction. In this fashion, the output 𝐶 tiles remain stationary in the L1 memory of each core (output stationary mapping). When all tiles complete their accumulation (𝐾/𝑘ct tiles in total), the output tile is transferred to an L2 MemTile, and eventually to DRAM. The MemTile design is detailed in Sec. 4.2.2. Subsequently, a fast vectorized kernel initializes the output 𝐶 tile to zero, preparing it for the next accumulation in GEMM tiling.

To overlap GEMM computation with DMA transfers of matrix tiles, we employ double-buffering on both L1 and L2 for the input tiles 𝐴 and 𝐵. However, we retain the output 𝐶 tiles as a single buffer on each core. Since the output tiles are transferred only once for each complete reduction, when 𝐾/𝑘ct is sufficiently large, the infrequent added latency of a single-buffered output transfer becomes negligible. At the same time, this design choice frees up valuable L1 memory, thereby enabling higher flexibility in tiling parameter optimization. The larger tile sizes enabled by this additional memory ultimately lead to higher end-to-end GEMM performance in the general case (refer to Sec. 5.3.2).

4.2.2 MemoryTileDesign. The input tiles are temporarily stored in L2 MemTiles before being broadcast to each NPU core. Note that we load multiple input tiles across the reduction dimension 𝐾 into MemTiles (e.g., 𝐴0 and 𝐴0 ′ in Fig. 3b and 3c). This is an important aspect of the mapping; loading multiple tiles allows us to access a larger amount of contiguous data from DRAM, where matrices are stored in regular order (row- and column-major). Such long contiguous reads result in increased DRAM bandwidth (BW) utilization, which is essential for maximizing GEMM performance at the system level. In this work, we retain matrices 𝐴 and 𝐶 in row-major order, while matrix 𝐵 is either in row- or column-major order. To this end, we introduce another parameter, 𝑘mt, which specifies the size of the tiles loaded into L2. In particular, for matrix 𝐴, each L2 MemTile loads a tile of 𝑚ct × 𝑘mt size. Similarly, when 𝐵 is in column-major order, each L2 MemTile loads a tile of 𝑘mt × 𝑛ct size. Storing matrix 𝐴 in row-major and 𝐵 in column-major provides sufficient contiguous DRAM access for both matrices, which leads to higher GEMM performance (Sec. 5.2.3). However, when 𝐵 is in row-major, MemTiles load the same tile as CompTiles (i.e., 𝑘ct × 𝑛ct), since contiguous data are accessible across the 𝑛ct dimension.

Due to the 4 × 4 symmetric design of XDNA, each MemTile holds the same amount of input tiles, as illustrated in Fig. 3b. Specifically, each MemTile broadcasts 𝐵 tiles within its own column. The 𝐴 tiles are broadcast across the four rows; we map them in a regular fashion to the four MemTiles: the MemTile in column 0 holds 𝐴0 (which will be broadcast across row 0), the MemTile in column 1 holds 𝐴1 (which will be broadcast across row 1), etc. In contrast, for XDNA2, we map the four 𝐴 tiles to eight MemTiles in an alternating pattern across even columns due to its 4 × 8 asymmetric design. The mapping for 𝐵 remains the same. In this design, even MemTiles hold more tiles than their odd counterparts, as depicted in the logical view of Fig. 3c. This particular mapping aids the IRON tool to leverage the NPU architectural feature of directly accessing the memory of the neighboring MemTile [24]. Therefore, when buffer sizes exceed the capacity of a specific MemTile, IRON physically allocates buffers to a neighboring MemTile.

Observe that four output 𝐶 tiles are aggregated across each column into a MemTile (Fig. 3b and 3c). This is because four 𝐶 tiles need to be transferred concurrently, while ShimTiles provide only two S2MM channels. Hence, we exploit the six S2MM channels available in each MemTile [24] to temporarily store the four output tiles, before they are transferred to DRAM via ShimTiles.

We define the native GEMM size, as (𝑚ct · 𝑚rows) × 𝑘mt × (𝑛ct · 𝑛cols). This corresponds to the GEMM size that operates natively on the entire NPU array, while also ensuring high performance. Moreover, note that although we arbitrarily map matrix 𝐴 across rows and matrix 𝐵 across columns, the reverse mapping is equally feasible, yielding symmetrical solutions across the 𝑀 and 𝑁 dimensions. Finally, although an input stationary mapping can also be employed, it would not be adequate to efficiently support arbitrary GEMM dimensions. Specifically, partial results would need to be temporarily stored in MemTiles, and subsequently reloaded to CompTiles for exploitation of data reuse in GEMM. This would require three input channels for efficient GEMM computation, while CompTiles provide two inputs channels.

<!-- p.5 -->

Figure 4: On-the-fly DMA transformations for matrix A.

![Image](images/image_000003_54498a535b5552b0d64ca518b5986039bc6722a1751cac90c9aabe981dfafa31.png)

## 4.3 On-The-Fly Tensor Transformations

We extensively exploit the multi-dimensional addressing feature of DMAs to reorganize data into tiled layouts, as needed by the NPU cores. This enables matrices to be stored in standard order in DRAM (row- and column-major). The single-core GEMM kernels assume that matrices are pre-tiled [5]. In particular, the kernels running on each core expect 𝑟 × 𝑠 × 𝑡-sized tiles and both data within tiles as well as the tiles themselves to be in row-major order, as illustrated in the upper part of Fig. 4, for the case of matrix 𝐴. The aforementioned distribution of tiles across multiple cores, along with the requirement for contiguous DRAM accesses necessitates multiple data layout transformations, as explained below.

As matrices are transferred from DRAM to NPU, DMAs apply a series of transformations depending on the addressing capabilities of each NPU tile. Fig. 4 shows the transformations of each DMA channel associated with the transfer of matrix 𝐴 tiles (see Fig. 2a for DMA channels). Initially, a ShimTile MM2S channel reads a tile of 𝑚ct × 𝐾 size from DRAM. Since ShimTiles support 3D addressing, the row-major 𝑚ct × 𝐾 tile is transformed into multiple smaller 𝑚ct × 𝑘mt tiles via the MM2S channel (parameters: 𝑚ct, 𝑘mt, 𝐾). Before each 𝑚ct × 𝑘mt tile is stored in the MemTile, another 3D transformation occurs at the S2MMMemTile channel, partitioning it into several 𝑚ct × 𝑘ct tiles (parameters: 𝑚ct, 𝑘ct, 𝑘mt).

Each MemTile holds a𝑚ct × 𝑘mt tile, which it sequentially transmits to the corresponding CompTiles as a series of smaller 𝑚ct × 𝑘ct tiles. Since the CompTiles expect pre-tiled data, the data layout of each of the smaller tiles requires transformation. Describing this transfer requires five parameters, namely 𝑟 , 𝑠, 𝑚ct, 𝑘ct, 𝑘mt. However, MemTiles only support 4D addressing. To circumvent this, we decompose the transformation into two separate transformations, by utilizing the hardware's data layout transformation features in both the MM2S MemTile (output) channel and the S2MM CompTile (input) channel. First, the MM2S MemTile channel partitions data into several𝑚ct × 𝑠 tiles, as depicted in Fig. 4 (parameters: 𝑠,𝑚ct, 𝑘ct, 𝑘mt). This enables address linearization within the 𝑟 × 𝑠 tile, thereby allowing a subsequent 3D transformation in the S2MM CompTile channel to reorganize data into the required layout (effective parameters: 𝑟 · 𝑠, 𝑚ct, 𝑘ct).

Figure 5: Simplified view of outer-most (fourth) GEMM tiling level, determined by NPU-DRAM transfers (𝑚rows, 𝑛cols = 4).

![Image](images/image_000004_f3a2bbedb5bf1cc2d80cf354557e47e5420ecbf40aa1527888eb8fdd5a032cac.png)

Address generation in DMAs occurs at 32-bit granularity [8, 24]. DMAs alone cannot perform layout transformations at smallerprecision data types (e.g., individual elements of int8 or bf16 data types). However, shuffle instructions running on the AIE cores can be utilized to support this operation and enable the fine-grained data swizzling. In our application, this becomes relevant in use cases where an int8 or bf16 matrix 𝐵 is stored in column-major order in DRAM. To this end, we modify the GEMM kernel to utilize shuffling instructions, by using the AIE API transpose function [6], such that both data within tiles and the tiles themselves are in column-major order. Subsequently, we apply similar transformations to matrix 𝐵 as demonstrated for matrix 𝐴, across the NPU hierarchy (Fig. 4).

Furthermore, when matrix 𝐵 is in row-major, only one 4D transformation is required in the MemTiles (parameters: 𝑠, 𝑡 , 𝑘ct, 𝑛ct), since each MemTile holds a 𝑘ct × 𝑛ct tile (Sec. 4.2). In a similar fashion, matrix 𝐶 tiles (row-major) entail a single 4D transformation in the MemTiles (parameters: 𝑟 , 𝑡 , 𝑚ct, 𝑛ct).

## 4.4 Data Movement Between NPU&amp;DRAM

The outer-most (fourth) level of GEMM tiling encompasses data movement between the NPU and DRAM. The NPU interfaces with DRAM via ShimTiles and the control of data movement is orchestrated by the on-chip command processor. ShimTiles are equipped with an input task queue to facilitate DMA transfers, where tasks are submitted sequentially. When a task terminates, it issues a task-completion token, which the command processor uses to synchronize between multiple DRAM transfers. Each ShimTile has access to 16 BDs [24], which are used to specify DMA transfers. When a complex data movement pattern requires more than 16 BDs on a ShimTile, we can reuse (reconfigure) BDs. Before reconfiguring a BD, it is necessary to properly synchronize and ensure that the previous transfer associated with the BD has completed [21].

<!-- p.6 -->

Fig. 5 illustrates a simplified view of the data movement between the NPU and DRAM. Each BD is utilized to describe data movements in a fine-grained manner. In particular, for matrix 𝐴, each BD defines one ShimTile DMA transfer of 𝑚ct × 𝐾 size. Similarly, for matrix 𝐵, each BD defines a transfer of 𝐾 × 𝑛ct. For matrix 𝐶, each BD describes a transfer of (𝑚ct · 𝑚rows) × 𝑛ct, since𝑚rows output tiles are aggregated per column. The command processor program in our implementation maps and inserts BDs into ShimTile task queues as determined by the GEMM mapping strategy (e.g., 𝐵𝐷𝐴0, 𝐵𝐷𝐵0, and 𝐵𝐷𝐶0 to ShimTile 0, 𝐵𝐷𝐴1, 𝐵𝐷𝐵1, and 𝐵𝐷𝐶2 to ShimTile 1, etc). Moreover, 𝐵𝐷𝐴4, 𝐵𝐷𝐵0, and 𝐵𝐷𝐶1 are also pushed into the queue of ShimTile 0, as dictated by GEMM tiling. Similarly, our program enqueues BDs describing the GEMM transfers into the input task queues of each ShimTile, in a sequential fashion. We note that the simplified example in Fig. 5 is directly applicable to XDNA (𝑛cols = 4, see Sec. 4.2), while the BD mapping for XDNA2 can be determined in straightforward fashion.

Depending on the target GEMM dimensions, (i.e., 𝑀, 𝐾, 𝑁 ), more than the maximum of 16 BDs in each ShimTile might be required. BD reconfiguration is needed in this situation, which might result in performance degradation (Sec. 5.3.3). To address this challenge, we propose the following procedure, which enables DMA data transfers to overlap with BD reconfiguration. Initially, we submit to the task queue five BDs for each of the three 𝐴, 𝐵, and 𝐶 DMA transfers. This efficiently utilizes 15 out of the 16 BDs available in each ShimTile. Each DMA transfer begins immediately once its associated BD reaches the front of the queue. For instance, 𝐵𝐷𝐴0 and 𝐵𝐷𝐵0 transfers start first in Fig 5. Subsequently, the command processor waits for a task-completion token for each output transfer in a sequential manner (e.g., first for 𝐵𝐷𝐶0, then for 𝐵𝐷𝐶1, etc). Notice that the command processor only needs to wait for completion of each BD associated with the output matrix (e.g., 𝐵𝐷𝐶0), since once it completes, the corresponding input BDs (e.g., 𝐵𝐷𝐴0 and 𝐵𝐷𝐵0) have also finished. Therefore, once each output BD completes, the three retired BDs can be safely reconfigured, and the next three BDs are inserted into the queue (if available). This ensures that 15 BDs are in the queue in the steady-state operation, allowing DMA data movement to overlap efficiently with BD reconfiguration. This process is repeated iteratively until GEMM tiling is completed.

The fine-grained BD description of NPU-DRAM data movement discussed above allows supporting very large GEMM dimensions. GEMM dimensionality is limited by the NPU registers bitwidth utilized in multi-dimensional tensor addressing [8]. For instance, when storing 𝐵 in column-major, the GEMM programming example in [18] allows only a reduction 𝐾 dimension of up to ∼4K for bf16 on XDNA2, while our approach allows sizes &gt; 64K in all dimensions.

## 4.5 Analytical Modeling Optimization

To maximize GEMM performance, we propose an optimization methodology based on analytical modeling. First, we focus on singlecore performance to gain insights for the on-chip compute part, and then extend our methodology to optimize the system-level performance by incorporating off-chip DRAM BW constraints.

4.5.1 Single-Core GEMMOptimization. Our model utilizes architectural parameters, such as the peak compute throughput of the cores (𝑝𝑒𝑎𝑘\_𝑀𝐴𝐶𝑠 in MACs/cycle) and the DMA BW (𝐷𝑀𝐴\_𝐵𝑊 in Bytes/cycle), in order to identify the optimal 𝑚ct, 𝑘ct, 𝑛ct parameters that maximize performance. We define the efficiency (𝑒 𝑓 𝑓 ) as the fraction of the attained compute throughput to the peak throughput of the core. Eq. 1 expresses the compute cycles of the GEMM kernel (𝐶𝑐𝑜𝑚𝑝 ), while Eq. 2 and 3 formulate the number of cycles needed for the DMA transfers of 𝐴 (𝐶𝐴𝑐𝑜𝑚𝑚) and 𝐵 (𝐶𝐵𝑐𝑜𝑚𝑚) tiles, respectively. Here, 𝑡𝑦(·) indicates the data type size (in Bytes) of the matrices.

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

Next, we define the constraint in Eq. 4 to ensure that the single-core GEMM remains compute bound (not bounded by the DMA BW for 𝐴 and 𝐵 tiles). Furthermore, Eq. 5 restricts the GEMM buffers to fit within the 64KB L1 memory capacity (with 1KB reserved for stack). Finally, we impose the straightforward constraint that 𝑚ct, 𝑘ct, 𝑛ct need to be multiples of 𝑟 , 𝑠, 𝑡 , respectively (not shown).

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

The solution of 𝑚ct, 𝑘ct, 𝑛ct can be formulated as an integer programming (IP) optimization problem utilizing the aforementioned constraints. The IP is solved exhaustively by setting the maximization of the number of MACs (𝑚ct · 𝑘ct · 𝑛ct) as the main objective. This increases data reuse in GEMM, thereby maximizing the overall efficiency. Furthermore, due to the output stationary GEMM mapping, we impose a second objective to minimize the output 𝐶 tile (i.e., the product 𝑚ct · 𝑛ct). This is essential in reducing the number of loads/stores for accumulations and decreasing memory stalls caused by bank conflicts. Evidently, the two optimization objectives lead to increased 𝑘ct and reduced 𝑚ct, 𝑛ct (sufficiently large to not become DMA BW bound). Moreover, notice that we do not impose DMA constraints on the output 𝐶 buffer, as opposed to 𝐴 and 𝐵 (Eq. 2, 3), while also retaining 𝐶 as a single buffer (Eq. 5). This significantly increases the search space, thus increasing performance in the general GEMM case (Sec. 5.3.2), which is an essential aspect of the system-level optimization discussed below.

4.5.2 System-Level NPUArray Optimization. First, we analytically express the DRAM accesses for each matrix in GEMM. Eq. 6 captures the DRAM reads needed for matrix 𝐴 (𝐴𝑚𝑒𝑚). The first term, 𝑚ct · 𝑚rows · 𝐾 · 𝑡𝑦(𝐴), represents the DRAM reads (in Bytes) during GEMM tiling along the 𝐾 dimension. This is due to the output stationary mapping and because 𝐴 is broadcast across rows (Sec. 4.2). The second term, 𝑁/(𝑛ct ·𝑛cols), describes the repeat factor of the aforementioned read accesses due to tiling along the 𝑁 dimension. In a similar manner, the third term, 𝑀/(𝑚ct ·𝑚rows), captures the repeat across the 𝑀 dimension.

<!-- p.7 -->

<!-- formula-not-decoded -->

Similarly, Eq. 7 represents the DRAM reads for matrix 𝐵 (𝐵𝑚𝑒𝑚), while Eq. 8 shows the DRAM writes for the output matrix 𝐶 (𝐶𝑚𝑒𝑚). We note that these equations provide an elegant and compact representation of data reuse and tiling scheme in GEMM.

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

Furthermore, Eq. 9 models the GEMM compute time on the NPU (𝑇𝑐𝑜𝑚𝑝 ), while Eq. 10 expresses the total DRAM access time (𝑇𝑚𝑒𝑚). Here, 𝑝𝑒𝑎𝑘\_𝑇𝑂𝑃𝑆 denotes the theoretical peak throughput of the NPU array, calculated at the maximum operating frequency. Furthermore, 𝐷𝑅𝐴𝑀\_𝐵𝑊 is the effective DRAM BW achieved during GEMM execution on the NPU. Note that, since all NPU cores execute the same GEMM kernel independently, the single-core efficiency 𝑒 𝑓 𝑓 , as defined in Sec. 4.5.1, directly corresponds to the entire NPU array efficiency in Eq. 9.

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

As discussed previously (Sec. 4.5.1), minimizing𝑚ct and 𝑛ct, as well as maximizing 𝑘ct is essential to attain high efficiency, and thus maximized GEMM performance (equivalent to minimized 𝑇𝑐𝑜𝑚𝑝 ). However, from Eq. 6 and 7, we notice that 𝑛ct and𝑚ct parameters appear on the denominator of DRAM accesses for 𝐴 and 𝐵, respectively. Therefore, the DRAM access time, 𝑇𝑚𝑒𝑚, increases as 𝑚ct and 𝑛ct decrease. This highlights the inverse relationship between compute and memory time: as one increases, the other decreases. Thus, the optimal GEMM performance is attained at the balanced point where they intersect (i.e., 𝑇𝑐𝑜𝑚𝑝 ≈𝑇𝑚𝑒𝑚).

In order to find the aforementioned optimal balanced point we empirically set starting values for𝑚ct, 𝑘ct, 𝑛ct, and 𝑒𝑓 𝑓 parameters, and perform the iterative procedure explained below (starting values reduce iterations to typically &lt;5). These starting values are set based on the results of the single-core kernel optimization (Sec. 4.5.1), and the effective 𝐷𝑅𝐴𝑀\_𝐵𝑊 during GEMM execution (measured via micro-benchmarking, refer to Sec. 5.2.1). First, we measure the actual GEMM performance on the NPU device as well as the single kernel efficiency for the starting values, verifying that GEMM is memory bound (𝑇𝑐𝑜𝑚𝑝 &lt; 𝑇𝑚𝑒𝑚, due to low𝑚ct, 𝑛ct, and high 𝑘ct). In each iteration, we decrease the parameter 𝑘ct (as a multiple of 𝑠), and solve exhaustively an IP similar to the previous Sec. (4.5.1). However, for that specific iteration, we fix the 𝑘ct parameter and the objective is to maximize the product of𝑚ct · 𝑛ct. This leads to maximized 𝑚ct, 𝑛ct values, given the DMA BW and L1 memory constraints, ensuring a highly optimized GEMM kernel (maximized number of MACs). This is essential in identifying the optimal balanced point, because it results in the smallest possible increment of 𝑇𝑐𝑜𝑚𝑝 in each iteration, while also ensuring maximized possible GEMM performance given the specific parameters of that iteration. We note here that 𝑒𝑓 𝑓 is estimated based on each previous point measurements of each iteration. We iteratively measure GEMM performance on the NPU device of the top-ranked solution for each IP, verifying that in each step performance is higher compared to the previous. While GEMM performance is increasing in each step, at some point we observe lower performance, and the iteration stops. Evidently, at this specific point GEMM has become compute bound (𝑇𝑐𝑜𝑚𝑝 &gt; 𝑇𝑚𝑒𝑚), while also performance is lower. Therefore, the balanced point where GEMM performance is maximized has been identified (𝑚ct, 𝑘ct, 𝑛ct parameters of the previous iteration).

Table 1: Single-core GEMM results for XDNA &amp; XDNA2.

| Dev.                                                                                           | Precision Kernel Size In-Out 𝑚ct × 𝑘ct ×                                                                            | Throughput MACs/cycle                           | L1 Core Mem. (KB)                                                                       |
|------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------|-------------------------------------------------|-----------------------------------------------------------------------------------------|
| XDNA int8-int8 int8-int16 int8-int32 bf16-bf16 XDNA2 int8-int8 int8-int16 int8-int32 bf16-bf16 | 𝑛ct 64 × 232 × 64 64 × 216 × 64 48 × 280 × 48 64 × 104 × 64 64 × 232 × 64 64 × 216 × 64 48 × 280 × 48 48 × 152 × 48 | 233.0 217.6 192.0 112.6 450.6 419.8 384.0 158.1 | 62.0 (97%) 62.0 (97%) 61.5 (96%) 60.0 (94%) 62.0 (97%) 62.0 (97%) 61.5 (96%) 61.5 (96%) |

## 5 Evaluation

For the experimental evaluation, we use two representative mini PCs, corresponding to two NPU generations. The Minisforum UM790 Pro [44], equipped with Ryzen 9 7940HS processor (Phoenix Point) [12], is used for XDNA. For XDNA2, we use the ASRock 4×4 Box [36], featuring the AMD Ryzen AI 7 350 processor (Krackan Point) [13]. Both mini PCs have dual-channel DDR5-5600 MT/s DRAM. The CPUs run Ubuntu 24.04 LTS and serve as the host. In particular, CPUs allocate buffers in DRAM using Xilinx Runtime (XRT) [26], configure the NPU, invoke the NPU for GEMM execution, and process the completion notification. Finally, throughout all experiments, NPUs are configured at their maximum performance level (i.e., turbo mode [19], utilizing XDNA driver commands [14]).

## 5.1 Single-Core GEMMPerformance

We exploit the AIE API [5] to design highly optimized single-core GEMM kernels. The kernels are compiled using the xchesscc tool, employing various compiler directives (e.g., software pipelining and loop unrolling/flattening) to attain high efficiency. Performance is assessed via hardware profiling utilizing the NPU trace unit [23, 24], thereby enabling cycle accurate measurements. For int8, besides full output precision (int32), we also perform precision reduction to 16- and 8-bits, which is a common technique to increase GEMM performance in AIE architectures [25, 30, 43, 60]. Moreover, for bf16, we retain the output precision to bf16. Finally, for XDNA2 we emulate the bf16 precision utilizing the bfp16 hardware datapath, which leads to increased GEMM performance (emulation attained via a specific xchesscc flag at compile time, as mentioned in [5]).

In Table 1, we present the top-ranked solutions of the single-core optimization procedure described in Sec. 4.5.1. For int8 precision, we attain very high throughput, ranging from 192.0-233.0 MACs/cycle and from 384.0-450.6 MACs/cycle for XDNA and XDNA2, respectively. Similarly for bf16, we achieve 112.6 MACs/cycle for XDNA, and 158.1 MACs/cycle for XDNA2. Observe that in both cases, XDNA2 attains higher throughput compared to XDNA, since it has higher peak compute throughput capabilities per core. We note that, since performance is measured via hardware tracing, the results in Table 1 include the inevitable memory stalls due to bank conflicts, thus reflecting the actual NPU performance (when not bounded by DRAM BW). Finally, very high L1 memory usage is achieved across all solutions, ranging from 94-97%.

<!-- p.8 -->

## 5.2 GEMMPerformance on NPUArray

In this section, we present GEMM performance on the entire NPU array. Performance is evaluated using wall-clock time, thereby capturing the actual performance observed by the users (includes OS overheads, NPU dispatch time, etc.) [22]. All reported results represent the average of 100 runs.

5.2.1 Optimal Balanced GEMMKernel. As shown in the previous section, GEMM kernels can attain very high throughput. However, when using the optimum compute kernel sizes of Table 1, we observe that GEMM performance on the NPU array remains low. For instance, for int8-int16 precision, we obtain only 17.86 TOPS on XDNA2 at ∼4K square GEMM size. However, the peak compute capability of this kernel on the XDNA2 array is 48.36 TOPS, when calculated at maximum operating frequency (identified through XDNA driver commands [14], i.e., 1 GHz and 1.8 GHz for XDNA and XDNA2, respectively). This implies that GEMM is memory bound at this specific kernel size, due to low values of 𝑚ct and 𝑛ct (inverse relationship of compute and memory). Therefore, we leverage the optimization methodology described in Sec. 4.5.2 in order to identify the optimal balanced kernel (i.e., where compute and memory become balanced). First, we use micro-benchmarking to estimate the effective DRAM BW that is available to the NPU when running GEMM workloads. For micro-benchmarking, we imitate GEMM transfers from DRAM to NPU array and vice-versa, observing ∼15 GB/s and ∼50 GB/s for XDNA and XDNA2, respectively. Afterwards, we exploit these values to select starting points for the iterative optimization procedure of Sec. 4.5.2.

Tables 2 and 3 present the two top-ranked solutions, for XDNA and XDNA2, respectively. First, notice that kernel sizes have lower 𝑘ct and higher𝑚ct , 𝑛ct compared to kernels in Table 1. This decreases their compute throughput; for example, 96×112×96 for int8-int16 (Table 2) achieves 192.0 MACs/cycle, compared to 217.6 MACs/cycle of 64×216×64 (Table 1). However, GEMM performance on the NPU array increases, since compute and memory become balanced. For example, when using the 96×112×96 kernel, GEMM performance on XDNA for ∼4K GEMM size is 5.85 TOPS (Table 2) compared to 4.03 TOPS of 64×216×64 (not shown in Table 1).

Second, observe that when further increasing the product of 𝑚ct · 𝑛ct (thus decreasing 𝑘ct to fit in L1), GEMM performance on NPU array drops. For instance, for int8-int8, kernel 160×64×144 has higher 𝑚ct · 𝑛ct product compared to 144×72×144 (22.5K vs. 20.3K), but performance is lower: 36.13 vs. 37.35 TOPS (Table 3). This is because at this point GEMM has become compute bound, but compute has also become lower (throughput drops to 322.6 from 343.0 MACs/cycle). The peak compute throughput on the entire XDNA2 array when attaining single-core throughput of 322.6 MACs/cycle is 37.16 TOPS. However, the actual GEMM performance is 36.13 TOPS for ∼4K GEMM size, as shown in Table 3. This ∼3% difference is mainly attributed to DMA transfers of 𝐶 tiles (single output buffer), occurring at the end of each complete reduction across 𝐾 (every 𝐾/𝑘ct tiles). Another smaller contribution comes from the vectorized zeroing kernel executing every 𝐾/𝑘ct tiles (Sec. 4.2), which is typically &lt;10% of GEMM kernel time (thus, &lt;0.15% for the ∼4K GEMM of this particular example). Both output DMA transfers and zeroing kernel cycles have been verified using NPU tracing. Note that as 𝐾 becomes higher, these effects are further amortized. Other contributions include the DRAM transfer time for initial 𝐴 and 𝐵 tiles, along with final output 𝐶 tiles, as well as overheads due to wall-clock time measurements (e.g., NPU dispatch time) [22]. In summary, the bolded solutions across all precisions in Tables 2 and 3 represent the optimal balanced point between compute and memory, where GEMM performance is maximized.

We note that results in Tables 2, 3 correspond to matrix 𝐵 in column-major (results for 𝐵 in row-major are presented in Sec. 5.2.3). Across all results, the contiguous parameter 𝑘mt, which is utilized to increase the effective DRAM BW (and hence GEMM performance), is configured as shown below (Sec. 5.2.2). Finally, the optimization procedure requires less than 30 minutes to identify the optimum solution for each data type (evaluated on the corresponding mini PCs). The overall execution time is dominated by the compilation time of xchesscc and IRON (up to 5 minutes per iteration), while the exhaustive search takes less than 1 s in all cases.

5.2.2 Contiguous 𝑘mt Parameter. In this section, we present the impact of the contiguous 𝑘mt parameter on GEMM performance. Fig. 6 depicts the GEMM performance when varying the parameter 𝑘mt and utilizing the optimal balanced kernels for two arbitrary data types (∼4K GEMM size and 𝐵 in column-major). For instance, for XDNA, when 𝑘mt is equal to 𝑘ct (= 56 in Fig. 6a), GEMM performance is very low (i.e., 1.27 TOPS). As 𝑘mt increases (in multiples of 𝑘ct) performance improves. This is because it enables higher effective DRAM BW, by traversing more contiguous elements for both 𝐴 (row-major) and 𝐵 (column-major) matrices. However, at some point, this enhancement becomes saturated (i.e., higher 𝑘mt provides marginal improvement). Therefore, we empirically select the smaller value where GEMM performance becomes saturated for each data type (i.e., 𝑘mt=224 in Fig. 6a). Note that selecting the smallest value while also maintaining high GEMM performance is essential, since it reduces any potential zero padding needed to align the final GEMM dimensions with the native GEMM size (Sec. 4.2). For example, for the bf16-bf16 case, the native GEMM size operating natively on the entire 4×4 XDNA array is 384×224×384. We note here that this results in reduced L2 memory utilization, ranging from 47-61% across all data types (Tables 2 &amp; 3). While maximized L2 memory usage can be attained for higher 𝑘mt values (e.g., 96% for 𝑘mt=560 in Fig. 6a), this only leads to marginal performance improvement in GEMM (&lt; 1%).

Similarly, for XDNA2, for int8-int16, we set 𝑘mt=432 (Fig. 6b). In this case, the native GEMM size on the XDNA2 array becomes 512×432×896. We note here that the three latest points of Fig. 6b are enabled via the neighboring memory sharing in MemTiles, as a direct result of the GEMM mapping strategy on XDNA2 (Sec. 4.2.2). For all other data types, we set the parameter 𝑘mt in a similar fashion. Specifically, for XDNA, we set 𝑘mt equal to 448 for int8-int8 and int8-int16, while for int8-int32 we set it to 352. On XDNA2, we use 432 for int8-int8, and 384 for both int8-int32 and bf16-bf16.

It is important to mention here that the non-optimized GEMM example in [18], cannot support sufficient contiguous elements. For a fair comparison, we utilize our optimized balanced kernels and modify their implementation to support single output 𝐶 buffers (allowing it to fit in L1). For instance, for the data types shown in Fig. 6, we attain 2.4× and 3.6× higher performance for XDNA and XDNA2, respectively. These results highlight the importance of accessing sufficient contiguous elements in GEMM performance.

<!-- p.9 -->

Table 2: Evaluation of two top-ranked solutions for XDNA across various data types (𝐵 column-major).

| Precision Kernel Size In-Out 𝑚ct × 𝑘ct × 𝑛ct          | Product 𝑚ct · 𝑛ct   | Thrghpt. MACs/cyc     | L1 Core Mem. (KB)     | L2 Total Mem. (KB)   | Peak Comp. TOPS   | GEMM Size 𝑀 × 𝐾 × 𝑁                   | Actual NPU TOPS   |
|-------------------------------------------------------|---------------------|-----------------------|-----------------------|----------------------|-------------------|---------------------------------------|-------------------|
| int8-int8 112 × 112 × 112 12.3K 112 × 104 × 128 14.0K | 212.5 207.4         | 61.3 (96%) 62.8 (98%) |                       | 980 (48%) 1004 (49%) | 6.80 6.63         | 4032 × 4032 × 4032 4032 × 4160 × 4096 | 6.52 6.48         |
| int8-int16 96 × 112 × 96 80 × 104 × 128               | 9.0K 10.0K          | 192.0 186.9           | 60.0 (94%) 62.3 (97%) | 960 (47%) 996 (49%)  | 6.14 5.98         | 4224 × 4032 × 4224 4160 × 4160 × 4096 | 5.85 5.75         |
| int8-int32 80 × 88 × 96 64 × 80 × 128                 | 7.5K 8.0K           | 146.0 133.1           | 60.3 (94%) 62.0 (97%) | 964 (47%) 992 (48%)  | 4.67 4.26         | 4160 × 4224 × 4224 4096 × 4160 × 4096 | 4.42 4.09         |
| bf16-bf16 96 × 56 × 96 96 × 48 × 112                  | 9.0K 10.5K          | 99.8 97.3             | 60.0 (94%) 60.0 (94%) | 960 (47%) 960 (47%)  | 3.19 3.11         | 4224 × 4032 × 4224 4224 × 4032 × 4032 | 3.12 3.02         |

Table 3: Evaluation of two top-ranked solutions for XDNA2 across various data types (𝐵 column-major).

| Precision In-Out                        | Kernel Size 𝑚ct × 𝑘ct × 𝑛ct   | Product 𝑚ct · 𝑛ct   | Thrghpt. MACs/cyc     | L1 Core Mem. (KB)     | L2 Total Mem. (KB)    | Peak Comp. TOPS   | GEMM Size 𝑀 × 𝐾 × 𝑁                   | Actual NPU TOPS   |
|-----------------------------------------|-------------------------------|---------------------|-----------------------|-----------------------|-----------------------|-------------------|---------------------------------------|-------------------|
| int8-int8 144 × 72 × 144 160 × 64 × 144 | 20.3K 22.5K                   | 343.0 322.6         | 60.8 (95%) 60.5 (95%) |                       | 2106 (51%) 2064 (50%) | 39.52 37.16       | 4032 × 4320 × 4608 4480 × 4224 × 4608 | 37.35 36.13       |
| int8-int16 128 × 72 160 × 64            | × 112 × 96                    | 14.0K 15.0K         | 307.2 271.4           | 61.8 (97%) 62.0 (97%) | 2084 (51%) 2016 (49%) | 35.39 31.26       | 4096 × 4320 × 4480 4480 × 4224 × 4608 | 30.77 29.59       |
| int8-int32 96 × 64 × 128 × 56 ×         | 96 80                         | 9.0K 10.0K          | 256.0 209.9           | 60.0 (94%) 62.3 (97%) | 2016 (49%) 2036 (50%) | 29.49 24.18       | 4224 × 4224 × 4608 4096 × 4032 × 4480 | 24.74 21.67       |
| bf16-bf16 112 × 48 × 160 × ×            | 96 40 80                      | 10.5K 12.5K         | 137.2 124.1           | 60.0 (94%) 62.5 (98%) | 2496 (61%) 2400 (59%) | 15.81 14.30       | 4032 × 4224 × 4608 4480 × 4160 × 4480 | 14.52 13.67       |

Figure 6: GEMM performance while varying parameter 𝑘mt for bf16-bf16 96×56×96 (a) and int8-int16 128×72×112 (b).

![Image](images/image_000005_490c33b4559fb16aa5216c284ff4478e778260423822689a30341b0752e3b610.png)

5.2.3 GEMM Performance Sweeps. In Fig. 7 and 8, we show roofline GEMM performance sweeps (in linear scale). Each point represents a matrix size that is a multiple of the native GEMM size (using the optimal balanced kernel). We select more than 400 points for each case (separately for 𝐵 in column- and row-major), up to 8Ksized matrices, without favoring any particular 𝑀, 𝐾, 𝑁 dimension. First, notice that when arithmetic intensity (ARI) is low (i.e., small matrix sizes), performance is limited. In this case, GEMM is severely memory bound. However, as ARI increases, GEMM performance improves, and becomes more stabilized after a specific value. In all cases, storing matrix 𝐵 in column-major provides, on average, higher performance compared to row-major. This is because of accessing sufficient contiguous data for both 𝐴 and 𝐵 matrices (determined by 𝑘mt parameter). However, for 𝐵 in row-major, the contiguous access is limited to the 𝑛ct parameter, while only for 𝐴 (row-major), 𝑘mt contiguous data are traversed. To this end, for XDNA, we observe, on average, 4.8%, 4.4%, and 0.57% higher performance, for int8-int8, int8-int16, and bf16-bf16, respectively.

Moreover, we notice that for XDNA2 the difference between column- and row-major is higher. In particular, we observe, on average, 19.1%, 25.2%, and 8.7% higher performance, for int8-int8, int8-int16, and bf16-bf16, respectively. This difference between XDNA and XDNA2 is presumably attributed to complex interaction between the NPU NoC, the SoC-level fabric and DRAM [51, 53], which affects the effective DRAM BW that NPU perceives (although both mini PC devices are equipped with the same DRAM). Also, we note that for both XDNA and XDNA2, the difference between column- and row-major for bf16 is lower compared to int8. This is because of accessing a larger number of bytes for bf16 across the 𝑛ct dimension (when 𝐵 is in row-major), which increases GEMM performance in this case, thereby lowering their difference.

For XDNA, we note that performance gets stabilized after a specific ARI value for 𝐵 in both row- and column-major formats (almost resembling a line in Fig. 7). Moreover, for XDNA2, we observe that for 𝐵 in column-major, GEMM performance also resembles a stable line. However, for 𝐵 in row-major it displays a more scattered distribution (Fig. 8). This is because XDNA2 has higher reliance on the effective DRAM BW (due to attaining significantly higher absolute TOPS values), which is substantially increased and stabilized when accessing sufficient contiguous data across both 𝐴 and 𝐵 matrices. For example, for int8-int16 on XDNA2, we measure a variability of only 5% for 𝐵 in column-major, while for row-major the variability is 19% (ARI &gt; 1600). Finally, across all points in GEMM sweeps, XDNA attains up to 6.76 (int8-int8), 6.05 (int8-int16), 4.57 (int8int32), and 3.14 (bf16-bf16) TOPS. Similarly, XDNA2 achieves up to 38.05 (int8-int8), 31.52 (int8-int16), 25.31 (int8-int32), and 14.71 (bf16-bf16) TOPS (int8-int32 sweep omitted for brevity).

<!-- p.10 -->

Figure 7: Roofline GEMM performance sweeps for various matrix sizes on XDNA.

![Image](images/image_000006_47d732d8c3406590b9b28288259045cdd85c198f7f9bc0ebd65dc577b301c86c.png)

Figure 8: Roofline GEMM performance sweeps for various matrix sizes on XDNA2.

![Image](images/image_000007_fe49146b8b235a83e8057237f1ab9d3375b8548b1c9ee6815a3430eee2e40ea9.png)

## 5.3 Insights &amp;Discussion

## 5.3.1 PerformanceAcrossMultipleGEMMSizes inDLWork-

loads. Modern DL workloads perform GEMMs with a wide range of sizes across their layers. Our employed output stationary mapping allows arbitrary GEMM dimensions to be supported, by applying zero-padding to align with the native GEMM size (Sec. 4.2). Zero-padding can be applied efficiently by utilizing the NPU's architectural support for on-the-fly zero-padding in MemTile channels [24]; leveraging this feature is left for future work.

Switching between different GEMM sizes can incur critical performance inefficiencies. To this end, one approach could be to reconfigure the NPU array with a dedicated GEMM design for each size. When quantifying the reconfiguration latency of the entire GEMM design, we measure a delay of 3.4 ms and 4.9 ms on XDNA and XDNA2, respectively. However, this reconfiguration latency is comparable to the GEMM execution time (e.g., a ∼4K square GEMM for int8-int16 on XDNA2 takes 5.2 ms). This underscores that reconfiguring the entire design can impose substantial overheads.

When retaining the same GEMM design on the NPU, only two parameters require reconfiguration across different problem sizes (𝑀, 𝐾, 𝑁): (i) the total number of output tiles 𝑀 · 𝑁/(𝑚ct · 𝑛ct), and (ii) the number of tiles across the reduction dimension 𝐾/𝑘ct [52]. According to our measurements, this negligible reconfiguration does not incur any noticeable performance overhead at the system level. Hence, identifying the optimal parameters (i.e., 𝑚ct, 𝑘ct, 𝑛ct, 𝑘mt, Sec. 4), and reusing them across different GEMM sizes is essential for high-performance DL deployment. Finally, we note that the system-level GEMM results presented are specific to the mini PCs used in this evaluation. However, our optimization methodology is generalizable to any NPU device in the current two generations.

5.3.2 Single Output Buffer. Due to output stationary mapping, we retain the output 𝐶 tiles as single buffers and apply doublebuffering only for inputs 𝐴 and 𝐵 (Sec. 4.2). This is crucial in maximizing performance in the general GEMM case, because it enables significantly higher flexibility in single-core parameter optimization. In particular, it allows increased values for 𝑚ct × 𝑘ct × 𝑛ct parameters, compared to utilizing double-buffering (constrained by L1 memory size). This results in identifying a balanced kernel that has higher performance, thereby enabling higher GEMM performance at the system level. For example, when using the optimization methodology described in Sec. 4.5.2 and apply double-buffering for 𝐶, we identify the 112×48×96 size as the optimal balanced kernel for int8-int16 data type on XDNA2. For ∼4K square GEMM, this kernel provides 26.1 TOPS. However, the 128×72×112 kernel of Table 3 (single 𝐶 buffer), provides 30.77 TOPS, representing an 18% performance improvement. Similarly, on XDNA, double buffer on 𝐶 provides 2.76 TOPS (80×40×96 kernel), while single buffer offers 3.12 TOPS (96×56×96 kernel on Table 2, 13% higher). The transfer of the output 𝐶 tiles in the single buffer case gets amortized as the reduction 𝐾 dimension becomes sufficiently high (typically &lt;5% degradation in GEMM performance when 𝐾/𝑘ct&gt;20).

5.3.3 NPU-DRAMDataMovement &amp; BDReconfiguration. The procedure delineated in Sec. 4.4 enables efficient overlapping of NPU-DRAM data movement with BD reconfiguration. To quantify its impact on performance, we modify our design to synchronize and reconfigure BDs sequentially (without overlap). In this case, for int8-int16 on XDNA2, we notice only 22.21 TOPS at ∼4K square GEMM, while the overlapped design of Table 3 exhibits 30.77 TOPs (28% decrease for non-overlapped design). Similarly, for XDNA, for int8-int16 (Table 2), we observe a 27% degradation in performance.

<!-- p.11 -->

This highlights the critical role of overlapping DMA transfers with BD reconfiguration in attaining high GEMM performance.

5.3.4 Future Research. XDNA2 incorporates hardware support for bfp16 precision, where multiple numbers share one common exponent. This incurs additional challenges for data layout transformations exploiting the multi-dimensional DMA addressing features of the NPUs (Sec. 4.3). However, this is beyond the scope of this paper and will therefore be addressed in future work. Furthermore, our proposed optimization methodology can be also be exploited for special cases of GEMM such as general matrix-vector multiplication (GEMV), which we also leave as future work.

## 6 Conclusion

In this work, we propose a novel optimization methodology to maximize GEMM performance on Ryzen AI NPUs. We observe the inverse relationship between compute and off-chip memory and determine the optimal balanced point, where performance is maximized. To identify this optimal performance point, we exploit analytical modeling and hardware profiling techniques. Our methodology attains state-of-the-art GEMM performance and is generalizable across the current AMD Ryzen AI NPU generations.

## References

- [1] 2022. Introducing Amazon EC2 Inf2 Instances Featuring AWS Inferentia2. https: //d1.awsstatic.com/events/Summits/reinvent2022/CMP334\_22986.pdf .
- [2] Sagheer Ahmad, Sridhar Subramanian, Vamsi Boppana, Shankar Lakka, Fu-Hing Ho, Tomai Knopp, Juanjo Noguera, Gaurav Singh, and Ralph Wittig. 2019. Xilinx First 7nm Device: Versal AI Core (VC1902). In 2019 IEEE Hot Chips 31 Symposium (HCS). 1-28. https://doi.org/10.1109/HOTCHIPS.2019.8875639
- [3] AMD. 2023. AMD Highlights Future of High-Performance and Adaptive Computing During Opening Keynote of CES 2023. https: //www.amd.com/en/newsroom/press-releases/2023-1-4-amd-highlightsfuture-of-high-performance-and-adap.html.
- [4] [AMD. 2024. AMD Announces Expanded Consumer and Commercial AI PC Portfolio at CES. https://www.amd.com/content/dam/amd/en/documents/products/ processors/ryzen/ai/iron- for- ryzen-ai- tutorial-micro-2024.pdf .](https://www.amd.com/content/dam/amd/en/documents/products/processors/ryzen/ai/iron-for-ryzen-ai-tutorial-micro-2024.pdf)
- [5] [AMD. 2025. AI Engine API User Guide: Matrix Multiplication. https://download.amd.com/docnav/aiengine/xilinx2025\_1/aiengine\_api/ aie\_api/doc/group\_\_group\_\_mmul.html.](https://download.amd.com/docnav/aiengine/xilinx2025_1/aiengine_api/aie_api/doc/group__group__mmul.html)
- [6] AMD. 2025. AI Engine API User Guide: Reshaping. https://xilinx.github.io/aie\_ api/group\_\_group\_\_reshape.html.
- [7] [AMD. 2025. AI Engine-ML Intrinsics User Guide. https://download.amd.com/ docnav/aiengine/xilinx2025\_1/aiengine\_ml\_intrinsics/intrinsics/index.html.](https://download.amd.com/docnav/aiengine/xilinx2025_1/aiengine_ml_intrinsics/intrinsics/index.html)
- [8] AMD. 2025. AI Engine-ML Kernel and Graph Programming Guide (UG1603). https://docs.amd.com/r/en-US/ug1603-ai-engine-ml-kernel-graph? tocId=U3UXOInouDrMPWWdkItJdg.
- [9] AMD. 2025. AIEngine Fork of LLVM (Peano). https://github.com/Xilinx/llvm-aie.
- [10] [AMD. 2025. AMD Announces Expanded Consumer and Commercial AI PC Portfolio at CES. https://www.amd.com/en/newsroom/press-releases/2025-1-6amd-announces- expanded- consumer- and- commercial- ai- .html.](https://www.amd.com/en/newsroom/press-releases/2025-1-6-amd-announces-expanded-consumer-and-commercial-ai-.html)
- [11] [AMD. 2025. AMD CDNA™ 4 Architecture. https://www.amd.com/content/ dam/amd/en/documents/instinct- tech-docs/white-papers/amd- cdna-4architecture-whitepaper.pdf .](https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/white-papers/amd-cdna-4-architecture-whitepaper.pdf)
- [12] [AMD. 2025. AMD Ryzen™ 9 7940HS. https://www.amd.com/en/products/ processors/laptop/ryzen/7000- series/amd- ryzen-9-7940hs.html.](https://www.amd.com/en/products/processors/laptop/ryzen/7000-series/amd-ryzen-9-7940hs.html)
- [13] [AMD. 2025. AMD Ryzen™ Al 7 350. https://www.amd.com/en/products/ processors/laptop/ryzen/ai-300- series/amd- ryzen- ai-7-350.html.](https://www.amd.com/en/products/processors/laptop/ryzen/ai-300-series/amd-ryzen-ai-7-350.html)
- [14] AMD. 2025. AMD XDNA™ Driver for Linux. https://github.com/amd/xdnadriver.
- [15] AMD. 2025. BFP16 (Block floating point) Quantization. https://quark.docs.amd. com/latest/pytorch/tutorial\_bfp16.html.
- [16] AMD. 2025. hipBLAS Documentation. https://rocm.docs.amd.com/projects/ hipBLAS/en/latest/index.html.
- [17] AMD. 2025. IRONAPI andMLIR-based AI Engine Toolchain. https://github.com/ Xilinx/mlir-aie.
- [18] [AMD. 2025. Matrix Multiplication - Whole Array Design. https: //github.com/Xilinx/mlir-aie/blob/main/programming\_examples/basic/ matrix\_multiplication/whole\_array/README.md.](https://github.com/Xilinx/mlir-aie/blob/main/programming_examples/basic/matrix_multiplication/whole_array/README.md)
- [19] AMD. 2025. NPUManagement Interface. https://ryzenai.docs.amd.com/en/latest/ xrt\_smi.html.
- [20] AMD. 2025. rocBLAS Documentation. https://rocm.docs.amd.com/projects/ rocBLAS/en/latest/index.html.
- [21] [AMD. 2025. Runtime Data Movement. https://github.com/Xilinx/mlir-aie/blob/ main/programming\_guide/section-2/section-2d/DMATasks.md.](https://github.com/Xilinx/mlir-aie/blob/main/programming_guide/section-2/section-2d/DMATasks.md)
- [22] [AMD. 2025. Section 4a - Timers. https://github.com/Xilinx/mlir-aie/tree/main/ programming\_guide/section-4/section-4a.](https://github.com/Xilinx/mlir-aie/tree/main/programming_guide/section-4/section-4a)
- [23] [AMD. 2025. Section 4b - Trace. https://github.com/Xilinx/mlir-aie/tree/main/ programming\_guide/section-4/section-4b.](https://github.com/Xilinx/mlir-aie/tree/main/programming_guide/section-4/section-4b)
- [24] AMD. 2025. Versal Adaptive SoC AIE-ML Architecture Manual (AM020). https: //docs.amd.com/r/en-US/am020-versal-aie-ml.
- [25] AMD. 2025. Vitis Tutorials: AI Engine Development (XD100). https://docs.amd.com/r/en-US/Vitis-Tutorials-AI-Engine-Development/AIEngine-Development.
- [26] AMD. 2025. Xilinx Runtime (XRT). https://github.com/Xilinx/XRT.
- [27] Nick Brown and Gabriel Rodriguez-Canal. 2025. Seamless Acceleration of Fortran Intrinsics via AMD AI Engines. In Proceedings of the 2025 ACM/SIGDA International Symposium on Field Programmable Gate Arrays (Monterey, CA, USA) (FPGA '25). Association for Computing Machinery, New York, NY, USA, 185. https://doi.org/10.1145/3706628.3708854
- [28] Joel Coburn, Chunqiang Tang, Sameer AbuAsal, Neeraj Agrawal, RavitejaChinta, Harish Dixit, Brian Dodds, Saritha Dwarakapuram, Amin Firoozshahian, Cao Gao, Kaustubh Gondkar, Tyler Graf, Junhan Hu, Jian Huang, Sterling Hughes, Adam Hutchin, Bhasker Jakka, Guoqiang Jerry Chen, Indu Kalyanaraman, Ashwin Kamath, Pankaj Kansal, Erum Kazi, Roman Levenstein, Mahesh Maddury, Alex Mastro, Siji Medaiyese, Pritesh Modi, Jack Montgomery, Satish Nadathur, Amit Nagpal, Ashwin Narasimha, Maxim Naumov, Eleanor Ozer, Jongsoo Park, Poorvaja Ramani, Harikrishna Reddy, David Reiss, Deboleena Roy, Sathish Sekar, Arushi Sharma, Pavan Shetty, Aravind Sukumaran-Rajam, Eran Tal, Mike Tsai, Shreya Varshini, Richard Wareing, Olivia Wu, Xiaolong Xie, Jinghan Yang, Hangchen Yu, Tanmay Zargar, Zitong Zeng, Feixiong Zhang, Ajit Matthews, Xun Jiao, Jiyuan Zhang, Emmanuel Menage, Truls Edvard Stokke, and Mohammed Sourouri. 2025. Meta's Second Generation AI Chip: Model-Chip Co-Design and Productionization Experiences. In Proceedings of the 52nd Annual International Symposium on Computer Architecture (ISCA '25). Association for Computing Machinery, New York, NY, USA, 1689-1702. https://doi.org/10.1145/3695053.3731409
- [29] Bita Darvish Rouhani, Daniel Lo, Ritchie Zhao, Ming Liu, Jeremy Fowers, Kalin Ovtcharov, Anna Vinogradsky, Sarah Massengill, Lita Yang, Ray Bittner, Alessandro Forin, Haishan Zhu, Taesik Na, Prerak Patel, Shuai Che, Lok Chand Koppaka, Xia Song, Subhojit Som, Kaustav Das, Saurabh T, Steve Reinhardt, Sitaram Lanka, Eric Chung, and Doug Burger. 2020. Pushing the Limits of Narrow Precision Inferencing at Cloud Scale with Microsoft Floating Point. In Advances in Neural Information Processing Systems, H. Larochelle, M. Ranzato, R. Hadsell, M.F. Balcan, and H. Lin (Eds.), Vol. 33. Curran Associates, Inc., 10271-10281. https://proceedings.neurips.cc/paper\_files/paper/2020/file/ 747e32ab0fea7fbd2ad9ec03daa3f840-Paper.pdf
- [30] Xiaodong Deng, Shijie Wang, Tianyi Gao, Jing Liu, Longjun Liu, and Nanning Zheng. 2024. AMA: An Analytical Approach to Maximizing the Efficiency of Deep Learning on Versal AI Engine. In 2024 34th International Conference on Field-Programmable Logic and Applications (FPL). 227-235. https://doi.org/10. 1109/FPL64840.2024.00039
- [31] Aadesh Deshmukh, Venkata Yaswanth Raparti, and Samuel Hsu. 2025. ZenAttention: A Compiler Framework for Dynamic Attention Folding on AMD NPUs. arXiv:2508.17593 [cs.DC] https://arxiv.org/abs/2508.17593
- [32] Shihan Fang, Hongzheng Chen, Niansong Zhang, Jiajie Li, HanMeng, Adrian Liu, and Zhiru Zhang. 2025. Dato: A Task-Based Programming Model for Dataflow Accelerators. arXiv:2509.06794 [cs.PL] https://arxiv.org/abs/2509.06794
- [33] Ross Freeman, James V. Barnett, and Bernard V. Vonderschmitt. 2021. Xilinx Edge Processors. In 2021 IEEE Hot Chips 33 Symposium (HCS). 1-21. https: //doi.org/10.1109/HCS52781.2021.9567521
- [34] GGML. 2025. GGML: Tensor Library for Machine Learning. https://github.com/ ggml-org/ggml.
- [35] Erika Hunhoff, Joseph Melber, Kristof Denolf, Andra Bisca, Samuel Bayliss, Stephen Neuendorffer, Jeff Fifield, Jack Lo, Pranathi Vasireddy, Phil James-Roxby, and Eric Keller. 2025. Efficiency, Expressivity, and Extensibility in a Closeto-Metal NPU Programming Interface. In 2025 IEEE 33rd Annual International Symposium on Field-Programmable Custom Computing Machines (FCCM). 85-94. https://doi.org/10.1109/FCCM62733.2025.00043
- [36] ASRock Industrial. 2025. 4X4 BOX-AI350. https://www.asrockind.com/en-gb/ 4X4%20BOX-AI350.
- [37] [Intel. 2025. Heterogeneous AI Powerhouse: Unveiling the Hardware and Software Foundation of Intel® Core™ Ultra Processors for the Edge. https://www.intel.com/content/www/us/en/contentdetails/819442/heterogeneous-ai-powerhouse-unveiling-the-hardwareand- software- foundation-of- intel- core-ultra-processors- for- the- edge.html.](https://www.intel.com/content/www/us/en/content-details/819442/heterogeneous-ai-powerhouse-unveiling-the-hardware-and-software-foundation-of-intel-core-ultra-processors-for-the-edge.html)
- [38] Norman P. Jouppi, Doe Hyun Yoon, Matthew Ashcraft, Mark Gottscho, Thomas B. Jablin, George Kurian, James Laudon, Sheng Li, Peter Ma, Xiaoyu Ma, Thomas

<!-- p.12 -->

Norrie, Nishant Patil, Sushma Prasad, Cliff Young, Zongwei Zhou, and David Patterson. 2021. Ten Lessons From Three Generations Shaped Google's TPUv4i: Industrial Product. In 2021 ACM/IEEE 48th Annual International Symposium on Computer Architecture (ISCA). 1-14. https://doi.org/10.1109/ISCA52012.2021. 00010

- [39] Rachid Karami, Rajeev Patwari, Hyoukjun Kwon, and Ashish Sirasao. 2025. Exploring the Dynamic Scheduling Space of Real-Time Generative AI Applications on Emerging Heterogeneous Systems. arXiv:2507.14715 [cs.LG] https://arxiv.org/abs/2507.14715
- [40] Tomai Knopp, Jeffrey Chu, and Sagheer Ahmad. 2024. AMD Versal™AI Edge Series Gen 2 for Vision and Automotive. In 2024 IEEE Hot Chips 36 Symposium (HCS). 1-28. https://doi.org/10.1109/HCS61935.2024.10665274
- [41] Chris Lattner, Mehdi Amini, Uday Bondhugula, Albert Cohen, Andy Davis, Jacques Pienaar, River Riddle, Tatiana Shpeisman, Nicolas Vasilache, and Oleksandr Zinenko. 2021. MLIR: Scaling Compiler Infrastructure for Domain Specific Computation. In 2021 IEEE/ACM International Symposium on Code Generation and Optimization (CGO). 2-14. https://doi.org/10.1109/CGO51591.2021.9370308
- [42] Eric Mahurin. 2023. Qualocmm® Hexagon™ NPU. In 2023 IEEE Hot Chips 35 Symposium (HCS). 1-19. https://doi.org/10.1109/HCS59251.2023.10254715
- [43] Kaustubh Mhatre, Endri Taka, and Aman Arora. 2025. GAMA: High-Performance GEMM Acceleration on AMD Versal ML-Optimized AI Engines. In 2025 35th International Conference on Field-Programmable Logic and Applications (FPL). https://arxiv.org/abs/2504.09688
- [44] Minisforum. 2025. Minisforum UM790 Pro. https://store.minisforum.com/ products/minisforum-um790-pro.
- [45] [MLIR. 2025. MLIRPythonBindings. https://mlir.llvm.org/docs/Bindings/Python/.](https://mlir.llvm.org/docs/Bindings/Python/)
- [46] Ai Nozaki, Takuya Kojima, Hiroshi Nakamura, and Hideki Takase. 2024. A Study on Number Theoretic Transform Acceleration on AMD AI Engine. In 2024 IEEE 17th International Symposium on Embedded Multicore/Many-core Systems-on-Chip (MCSoC). 325-331. https://doi.org/10.1109/MCSoC64144.2024.00060
- [47] [Nvidia. 2022. NVIDIA Jetson AGXOrin Series . https://www.nvidia.com/content/ dam/en- zz/Solutions/gtcf21/jetson-orin/nvidia- jetson-agx-orin- technicalbrief.pdf .](https://www.nvidia.com/content/dam/en-zz/Solutions/gtcf21/jetson-orin/nvidia-jetson-agx-orin-technical-brief.pdf)
- [48] Nvidia. 2023. Confidential Compute on NVIDIA Hopper H100. https://resources. nvidia.com/en-us-hopper-architecture/nvidia-h100-tensor-c.
- [49] Nvidia. 2025. cuBLAS Documentation. https://docs.nvidia.com/cuda/cublas/ index.html.
- [50] Nvidia. 2025. NVIDIACUTLASSDocumentation. https://docs.nvidia.com/cutlass/ index.html.
- [51] Alejandro Rico, Satyaprakash Pareek, Javier Cabezas, David Clarke, Baris Ozgul, Francisco Barat, Yao Fu, Stephan Münz, Dylan Stuart, Patrick Schlangen, Pedro Duarte, Sneha Date, Indrani Paul, Jian Weng, Sonal Santan, Vinod Kathail, Ashish Sirasao, and Juanjo Noguera. 2024. AMD XDNA NPU in Ryzen AI Processors. IEEE Micro 44, 6 (2024), 73-82. https://doi.org/10.1109/MM.2024.3423692
- [52] André Rösti and Michael Franz. 2025. Unlocking the AMD neural processing unit for ML training on the client using bare-metal-programming tools. In 2025 IEEE 33rd Annual International Symposium on Field-Programmable Custom Computing Machines (FCCM). IEEE, 271-271.
- [53] Mahesh Subramon, David Kramer, and Indrani Paul. 2023. AMD Ryzen™ 7040 Series: Technology Overview. In 2023 IEEE Hot Chips 35 Symposium (HCS). 1-27. https://doi.org/10.1109/HCS59251.2023.10254701
- [54] Endri Taka, Aman Arora, Kai-Chiang Wu, and Diana Marculescu. 2023. MaxEVA: Maximizing the Efficiency of Matrix Multiplication on Versal AI Engine. In 2023 International Conference on Field Programmable Technology (ICFPT). 96-105. https://doi.org/10.1109/ICFPT59805.2023.00016
- [55] Endri Taka, Dimitrios Gourounas, Andreas Gerstlauer, Diana Marculescu, and Aman Arora. 2024. Efficient Approaches for GEMM Acceleration on Leading AI-Optimized FPGAs. In 2024 IEEE 32nd Annual International Symposium on Field-Programmable Custom Computing Machines (FCCM). 54-65. https://doi. org/10.1109/FCCM60383.2024.00015
- [56] Chengyue Wang, Xiaofan Zhang, Jason Cong, and James C. Hoe. 2025. Reconfigurable Stream Network Architecture. In Proceedings of the 52nd Annual International Symposium on Computer Architecture (ISCA '25). Association for Computing Machinery, New York, NY, USA, 1848-1866. https://doi.org/10.1145/ 3695053.3731088
- [57] Sherry Xu and Chandru Ramakrishnan. 2024. Inside Maia 100. In 2024 IEEE Hot Chips 36 Symposium (HCS). 1-17. https://doi.org/10.1109/HCS61935.2024. 10665248
- [58] Zhenyu Xu, Miaoxiang Yu, Yazhe Zhang, Jillian Cai, Qing Yang, and Tao Wei. 2025. Tile-Level Pipeline for Linear Scalable Stencil Computation on AMD AI Engines. In Proceedings of the 2025 ACM/SIGDA International Symposium on Field Programmable Gate Arrays (Monterey, CA, USA) (FPGA '25). Association for Computing Machinery, New York, NY, USA, 172-178. https://doi.org/10.1145/ 3706628.3708822
- [59] Jinming Zhuang, Jason Lau, Hanchen Ye, Zhuoping Yang, Yubo Du, Jack Lo, Kristof Denolf, Stephen Neuendorffer, Alex Jones, Jingtong Hu, Deming Chen, Jason Cong, and Peipei Zhou. 2023. CHARM: Composing Heterogeneous AcceleRators for Matrix Multiply on Versal ACAP Architecture. In Proceedings of the

2023 ACM/SIGDA International Symposium on Field Programmable Gate Arrays (Monterey, CA, USA) (FPGA '23). Association for Computing Machinery, New York, NY, USA, 153-164. https://doi.org/10.1145/3543622.3573210

- [60] JinmingZhuang, JasonLau, HanchenYe, ZhuopingYang, Shixin Ji, JackLo, Kristof Denolf, Stephen Neuendorffer, Alex Jones, Jingtong Hu, Yiyu Shi, Deming Chen, Jason Cong, and Peipei Zhou. 2024. CHARM 2.0: Composing Heterogeneous Accelerators for Deep Learning on Versal ACAP Architecture. ACM Trans. Reconfigurable Technol. Syst. 17, 3, Article 51 (Sept. 2024), 31 pages. https: //doi.org/10.1145/3686163
- [61] Jinming Zhuang, Shaojie Xiang, Hongzheng Chen, Niansong Zhang, Zhuoping Yang, Tony Mao, Zhiru Zhang, and Peipei Zhou. 2025. ARIES: An Agile MLIRBased Compilation Flow for Reconfigurable Devices with AI Engines. In Proceedings of the 2025 ACM/SIGDA International Symposium on Field Programmable Gate Arrays (Monterey, CA, USA) (FPGA '25). Association for Computing Machinery, New York, NY, USA, 92-102. https://doi.org/10.1145/3706628.3708870
- [62] Jinming Zhuang, Zhuoping Yang, and Peipei Zhou. 2023. High Performance, Low Power Matrix Multiply Design on ACAP: from Architecture, Design Challenges and DSE Perspectives. In 2023 60th ACM/IEEE Design Automation Conference (DAC). 1-6. https://doi.org/10.1109/DAC56929.2023.10247981
