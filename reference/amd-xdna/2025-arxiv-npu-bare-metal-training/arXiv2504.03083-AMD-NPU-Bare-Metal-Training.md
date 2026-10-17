> 本文件由 `arXiv2504.03083-AMD-NPU-Bare-Metal-Training.pdf` 自动转换（docling 2.140.0，2026-10-17）。图片在 `images/` 目录；表格与数字请以 PDF 原文为准；`<!-- p.N -->` 标记对应 PDF 第 N 页。

<!-- p.1 -->

## Unlocking the AMD Neural Processing Unit for ML Training on the Client Using Bare-Metal-Programming Tools

Andr´e R¨osti University of California, Irvine Irvine, CA, USA aroesti@uci.edu Michael Franz University of California, Irvine Irvine, CA, USA franz@uci.edu

Abstract-There has been a growing interest in executing machine learning (ML) workloads on the client side for reasons of customizability, privacy, performance, and availability. In response, hardware manufacturers have begun to incorporate so-called Neural Processing Units (NPUs) into their processors for consumer devices. Such dedicated hardware optimizes both power efficiency and throughput for common machine learning tasks. AMD's NPU, part of their Ryzen AI processors, is one of the first such accelerators integrated into a chip with an x86 processor. AMD supports bare-metal programming of their NPU rather than limiting programmers to pre-configured libraries.

In this paper, we explore the potential of using a bare-metal toolchain to accelerate the weight fine-tuning of a large language model, GPT-2, entirely on the client side using the AMD NPU. Fine-tuning on the edge allows for private customization of a model to a specific use case. To the best of our knowledge, this is the first time such an accelerator has been used to perform training on the client side. We offload time-intensive matrix multiplication operations from the CPU onto the NPU, achieving a speedup of over 2.8× for these operations. This improves end-to-end performance of the model in terms of throughput (1.7× and 1.2× speedup in FLOPS/s on mains and battery power, respectively) and energy efficiency (1.4× improvement in FLOPS/Ws on battery power). We detail our implementation approach and present an in-depth exploration of the NPU hardware and bare-metal tool-flow.

## I. INTRODUCTION

Ever-more powerful hardware has long been a principal enabler of breakthroughs in machine learning (ML); [1] stronger computational capabilities, i.e. FLOP/s (floating point operations per second), along with greater data transfer bandwidths, enable more complex and better-trained models within a given training time budget. As we enter an era of personal computing where artificial intelligence (AI) applications such as chatbots pervade many workflows, there is an increased interest in running these applications entirely on the client side for customizability, privacy, performance, and availability. While most such applications at the edge only perform inference, customization also requires training (fine-tuning) models. Given that edge devices, such as laptops, often run on battery power, such use cases demand excellent energy efficiency. More FLOP/W (floating point operations per Watt) enable running or training larger parts of a model on the enduser's device without compromising on battery life.

While graphics processing units (GPUs) have evolved to meet the ever-increasing demand for computation capabilities (FLOP/s), their power consumption is generally high. Recognizing shifting demands, various manufacturers have begun shipping dedicated neural processing units (NPUs) as part of their processors. These systems aim to strike a new balance between throughput and power consumption, optimizing FLOP/Ws (floating point operations per Watt-second) jointly. Among these, AMD's Ryzen AI is the first system-on-a-chip incorporating a dedicated NPU alongside an x86 CPU and integrated GPU. The AMD NPU's architecture, called XDNA, is comprised of a spatial array of so-called AI Engines, each of which can independently perform computation.

AMD provides multiple compiler tool-flows to program the NPU. Similar to competitor's offerings, AMD's principal production tool-flow, called Ryzen AI Software, provides a library of highly optimized computation kernels that run on the NPU. A lower-level tool-flow, IRON [2], enables bare-bones hardware access using an MLIR dialect and Python bindings. Development at this level is more labor intensive, but allows programmers to harness every hardware feature and tailor their application implementation for the device.

In this paper, we explore the use of the AMD NPU and the IRON tool-flow to accelerate client-side inference and finetuning (training) of a large language model, GPT-2. GPT-2 is a good example for the kind of workload that end-users may want to run and train locally; fine-tuning it enables a customized user experience without compromising privacy. We demonstrate that this is feasible using current hardware and tool-flows, by implementing a specialized matrixmultiplication kernel for the NPU that is tailored to the requirements of this application, and then modifying a frameworkfree implementation of GPT-2, called llm.c [3], to efficiently use this kernel. Our carefully obtained measurements show that our approach significantly improves throughput and power efficiency over the original implementation.

## II. RELATED WORK

The rise of GPUs has made it possible to train larger ML models [1], [4], [5], waking us from the "AI winter" of the 1990s. Today, due to the waning of Moore's law [6], we must rely on hardware specialization, i.e., dedicating circuitry to common domain-specific functions, for more processing power. Google's tensor processing unit (TPU) [7], a matrix multiplication accelerator card, is an early example of such specialized hardware. Both research [8]-[10] and industry [11], [12] frequently use field-programmable gate arrays (FPGAs) to explore specialized neural network hardware designs. Accelerating generalized matrix-matrix multiplication (GEMM) workloads [13] is particularly interesting, as it is at the core of many ML applications. AMD's NPU [14]- [16] and competitor products, like Apple's Neural Engine [17] or Qualcomm's NPU [18], are further instances of hardware specialization. The AMD NPU's XDNA architecture [16] is more flexible than application-specific integrated circuits (ASICS) and more performant than general-purpose CPUs. Other works have explored different points along this flexibility-performance trade-off, for example in coarsegrained reconfigurable architectures (CGRAs) [19].

<!-- p.2 -->

The task of optimally mapping the work of an application onto an accelerator is non-trivial [20], [21]. GEMM algorithms have received particular attention [22], [23] in the application mapping sphere; most approaches tile the input matrices into submatrices to parallelize work. Parameters of the design space, like tile size, can be automatically evaluated in a process called auto-tuning [24]. Prior work has also explored mapping GEMM algorithms [25]-[27] onto the AI Engines in AMD Versal devices; these findings also translate to the NPU, which is equipped with the same AI Engines.

This paper focuses on mapping the GPT-2 [28] large language model (LLM) onto the AMD NPU. GPT-2, along with its successor [29] and competitor [30] models, follows a transformer architecture [31].

To implement these ML models, programmers traditionally have used frameworks [32], [33] such as PyTorch and TensorFlow, which provide the most common data structures (e.g., computation graphs) and algorithms (such as gradient descent). The generality of these frameworks can add overheads and preclude specializations needed for optimal performance. Because of this, programmers have begun to move towards using minimal, purpose-built "disposable" frameworks [34]. Examples of this include llama2.c [35], an implementation of LLaMA 2 inference in one file of pure C code, llama.cpp [36], which is the primary testing ground of the GGML framework, and llm.c [3], the subject of this paper. Previous work has ported llama.cpp onto the AMD NPU [37] but performed inference only (no training). We believe we are the first to map a training workload (fine-tuning) of a similar model (GPT-2) onto the AMD NPU.

## III. BACKGROUND

This paper combines three ingredients: (1) powerful hardware (AMD's NPU), (2) an interesting application (GPT-2), and (3) a productive tool-flow (IRON) that allows us to port our application onto the hardware.

Fig. 1. Overview of the XDNA architecture, showing the VLIW processor compute cores (L1), also called AI Engines, memory cores (L2), shim cores and the dedicated command processor. The small grey boxes between arrows are configurable interconnect switch boxes.

![Image](images/image_000000_b639b1f1bc6ecb2a69987822c411938838bbcec4b775abdf2001a5d69a05bcbd.png)

## A. The Hardware: AMD XDNA NPU in Ryzen AI

XDNA and its successor XDNA 2 are spatial computing architectures that arrange compute cores in a two-dimensional grid. This paper focuses on the first iteration of the firstgeneration XDNA architecture codenamed Phoenix. On this NPU, cores are arranged as a grid of four rows and five columns. Four of these columns have a shim core that allows directly interfacing with main memory. Cores in the fifth column requiring access to main memory must route their requests via the shims in the first four columns. For simplicity and to enable regularity in our design, we will focus on the 4×4 partition of the main grid that has shim cores throughout the rest of this paper. We will identify each core by its zeroindexed (x, y) coordinates from the bottom left.

Figure 1, shows the three types of cores (compute cores, memory cores, and shim cores) and their arrangement. 1 Each compute core has 64KB of local memory and can execute code in parallel to the other cores. Memory cores each add another 512KB of memory and enable data reuse and distribution. The shim cores enable moving data from the CPU and GPU in and out of the accelerator. A dedicated command processor with access to all cores and switch boxes can be used to reconfigure the NPU at runtime.

To program the NPU, the programmer loads computation kernels onto the AI Engine cores and configures the data movement between cores and the CPU.

1) Data Movement: The grey boxes in figure 1 label the memory levels (L1, L2, L3) of all cores. L1 and L2 memories are local to cores; L3 corresponds to the unified main memory shared between the CPU, NPU, and integrated GPU, which can be directly accessed by each of these components. Cores at each level have a number of data movement accelerators (DMAs), which are simple processors that can copy data to and from the interconnect and local core memories, and acquire and release hardware semaphore locks for synchronization. A unique feature of the XDNA architecture is that the programmer must explicitly describe all data movement. This is done by (i) setting up circuit- or packet-switched routes between the cores (streams) through the switch boxes and (ii) programming the DMAs of each core. DMAs are independent of compute cores, so data movement can occur in parallel with computation.

1 AMD calls these "compute tiles", "memory tiles" and "shim tiles". We use the term "core" to avoid confusion with the matrix tiling discussed later.

<!-- p.3 -->

2) Compute Cores: Compute cores ("AI Engines") are very large instruction word (VLIW) processors with parallel issue slots for matrix multiplication operations, vector addition operations, vector shuffling and shifting, two slots for loading from memory and one slot for storing to memory.

There are no caches present in this hardware, and instructions have fixed latencies. The hardware has minimal stalling infrastructure, and the compiler must schedule instructions to avoid hazards.

Due to the lack of stalling, back-to-back vector operations (without compiler-inserted no-ops) in the generated assembly directly indicate 100% vector hardware utilization.

The vector processing units in the XDNA architecture can perform 128 fused-multiply add (FMA) operations for the bfloat16 input type and float32 output type in every clock cycle [38]. At a 1 GHz clock frequency, this equals 256 GFLOP/s per AI Engine core, for a total of 4 TFLOP/s of bfloat16 processing power for the 4 × 4 partition of compute cores this paper targets. When processing int8 data types, AMD advertises up to 10 TOP/s for the Phoenix-generation XDNA NPU.

## B. The Application: GPT-2

Figure 2 shows the computation graph of the 124M parameter variant of the GPT-2 large language model (GPT-2 small). GPT-2 gained widespread recognition for being one of the first models to produce plausible responses to questionanswering, machine translation, reading comprehension, and summarization prompts. Even though more recent releases have far surpassed GPT-2's performance, the architecture of newer large language models has remained largely identical. The model's small size enables training locally on end-user devices, making it an interesting case study for us.

In the process of porting GPT-2 to the AMD NPU, we will focus our attention on offloading general matrix-matrix multiplication (GEMM) of the form AB = C, where the A matrix has M rows and K columns, the B matrix has K rows and N columns, and the resulting output C is of size M×N. We refer to these dimensions as the "problem size" and denote it as M ×K ×N.

## C. The tool-flow: IRON

One of AMD's primary tool-flows to program the Ryzen AI processors is called Ryzen AI Software [39]; Using it,

## GPT-2 (124M) Floating Point Operations

12x

Fig. 2. Computation graph of GPT-2, including floating point operation count (1MFLOP = 1e6FLOP). The fine-tunable weights (as named in llm.c) are annotated in grey italics. The arrows on the left and right show the FLOP count in the forward and backward pass, respectively. The operations within the grey box are repeated twelve times.

![Image](images/image_000001_3412c77be05fa33a8af2bd787f2112b320fc137e87799b4eef7cc1d0b87070f8.png)

![Image](images/image_000002_f3ae10cf0f2b4d0c2fda6c65d5d6cb33bfee437f653368e4b0e271bac5069c5a.png)

Fig. 3. Available tools (blue) and the intermediate outputs (brown) they produce in the open-source IRON tool-flow frameworks like PyTorch and TensorFlow can automatically offload some common operations in models with minimal programmer effort. A similar tool-flow exists for AMD's accelerator and FPGA cards (Alveo, Versal), Vitis AI Software [40]. Alternatively, a Vitis-based flow allows programming the Versal AIE devices at a lower level using C/C++ code.

<!-- p.4 -->

Figure 3 shows the components of IRON [2], an open-source tool-flow that allows low-level programming of AMD's NPU. In IRON, programmers use a Python script to describe the layout of their NPU design; the data movement can either be described by manually configuring DMAs, switch boxes, and locks, or via a higher-level ObjectFIFO abstraction. The script also specifies what code each compute core should execute by reference to an object file compiled from C++ source code. The C++ source code uses the AIE API [41] and can be compiled with a choice of either an LLVMbased open-source compiler, or AMD's proprietary xchessccc compiler. Running the Python script generates an intermediate representation (IR) in MLIR [42] of the design for the MLIRAIE compiler. Compiling this IR results in two outputs: A final.xclbin file, containing the static configuration of all cores and switch boxes of the NPU, and an insts.txt file, which contains instructions to be executed by the dedicated command processor for re-configuring the NPU at runtime.

## IV. DESIGN: OVERVIEW

We considered two approaches for our design: (a) an endto-end implementation of the entire model on the NPU ("dataflow"), or (b) a hybrid NPU-CPU implementation, which runs only the most compute-intensive operations on the NPU and leaves all other operations to be performed on the CPU ("layerby-layer"). We choose to follow the more straightforward layer-by-layer approach, which allows incrementally offloading parts of a model.

We base our implementation on llm.c [3], an optimized implementation of GPT-2 written in pure C without external libraries. Both llm.c and our bare-metal NPU implementation forgo the use of general-purpose frameworks. Since abstractions rarely are free, both llm.c's and our low-level approach promises maximized hardware utilization.

Profiling the baseline implementation reveals matrix multiplication as the main bottleneck, both in the forward and backward passes (see "CPU" in figure 8 in the evaluation section). We therefore choose to offload this operation onto the NPU. To this end, we devise a parametrized design that can be used to generate NPU programs for multiple matrix sizes at build time, while being tailored to the sizes used in the 124M-parameter GPT-2 model at runtime.

## V. DESIGN: CPU SIDE

For the CPU side of our implementation, we modify the original llm.c, replacing matrix multiplications with an invocation of our NPU design.

A challenge of the layer-by-layer approach is that it requires reconfiguring the accelerator between invocations of different kernels. We have observed that this reconfiguration can be a major contributor to overall overheads, especially for small matrix sizes where it is not amortized as well. Therefore, we make every effort to minimize the required NPU reconfiguration.

## A. Initialization

The Xilinx Run Time (XRT) is the host programming interface that allows us to interface with the NPU during execution. At the beginning of our program, we use XRT to initialize the program memories of each AI Engine core and configure the interconnect switch boxes of the L1 and L2 memory levels. To ensure this configuration occurs only once, we designed our NPU kernels so that the data transfer always remains the same at these levels, irrespective of problem size. We then preload one instruction stream for the NPU command processor per problem size. These instruction streams encode the data movement between L3 and L2. The instruction streams are generated and compiled at build time. Lastly, we initialize shared XRT buffers to pass input and output matrices between CPU and NPU. We allocate one set of shared buffers for each problem size we aim to support.

The result of initialization is a partially initialized NPU (level L2 and up) and a hash map that stores the XRT data structures (instruction streams, shared XRT buffers) for each problem size for later use.

## B. Matrix Multiplication Invocation

We start each invocation of an offloadable matrix multiplication by copying the input buffers to the corresponding shared XRT buffers. This is necessary for our minimally-invasive modular implementation, which works irrespective of the call site; zero-copy buffers could be implemented by replacing the buffers used throughout the original implementation with shared XRT buffers.

In the original llm.c implementation, weights are stored in column-major order, whereas activations are laid out in a row-major order. This leads to inconsistent data layouts across invocations when the derivatives are calculated during backpropagation. Since our NPU design always expects the same data layout, we added code to additionally perform a transpose on the CPU as we copy the input buffers where needed. We optimized this transpose by parallelizing it across all available CPU cores.

We could alternatively use the data layout transformation features of the DMAs in the NPU to perform this transpose; however, this would require reconfiguration of nearly all DMAs between invocations, which is impractically slow. Yet another approach might be to rewrite llm.c entirely to use all row-major data structures; however, this would significantly affect the cache locality, and hence the performance, of the CPU algorithms that we do not currently offload.

Once the input buffers are set up, we issue the instruction stream for the given problem size to the command processor. This configures the L3 to L2 DMAs, which immediately start tiling data from the shared input buffers. The instruction stream also contains instructions to write two runtime parameters into the registers of the AI Engine cores: the number of matrix tiles to accumulate and the number of tiles in the output matrix.

<!-- p.5 -->

The CPU then waits for NPU execution to complete; this happens as soon as the last L3 shim has copied the last output tile back into the shared output buffer. The CPU then copies the results out of the shared output buffer into the application.

Note that this design is carefully engineered to minimize NPU reconfiguration (only L3 is reconfigured), which is critical to attaining workable performance.

## VI. DESIGN: NPU SIDE

At a high level, the NPU side of our design tiles the input matrices A (of size M×K) and B (of size K×N), which it reads from shared buffers in L3, into sub-matrices of size m×k and k×n, respectively, on the L2 memory cores. These cores then repeatedly distribute tiles to the 16 computation cores (L1), each of which multiplies and accumulates its inputs inplace. Memory cores (L2) join the output tiles produced by the computation cores (L1) and route them back to an output buffer in the main memory (L3).

The sizes of the input matrices and sub-matrix tiles are compile-time parameters of our design. We generate one variation of our design for each of the 12 differently-sized matrix multiplications in 124M GPT-2 (listed in figure 6). We tailor our design to the requirements of the application by using the tile dimensions m = 64, k = 64, n = 32. With these sizes, we only need to pad one input matrix of size 50304×256 to 50432×256. All other matrix sizes are evenly divisible by our tile size, and we maximize usage of the available compute core memory.

## A. Computation Core Kernel

Our design uses a 4×4 partition of the computation cores of the NPU. Each core runs the same code in parallel on different data; specifically, each core executes a vectorized kernel that multiplies two sub-matrices A′ (of size m × k) and B′ (of size k × n), accumulating results into an output tile C ′ of size m×n. To do so, the kernel initially zeroes C ′ , and then repeatedly acquires two input tiles A′ and B′ , multiplies them, and accumulates the result in-place into C ′ . After Kk such multiplications, the core's DMA sends the output tile back to the L2 memory cores. This process is repeated for every output tile.

We use double-buffering for all buffers; that is, there are two physical buffers reserved to hold the A′ , B′ and C ′ tiles, and the DMA and computation core alternate between them. While the DMA receives inputs and/or sends results from one set of buffers, the computation core simultaneously performs its computation using previously received tiles stored in the other set of buffers.

Our computation kernel uses an AI Engine fused-multiplyadd (FMA) instruction called VMAC. This instruction multiplies two matrices of size 4 × 8 and 8 × 4 and adds the result to an accumulator register holding an output tile of size

4×4. The result of this operation is available four cycles after the instruction was issued. If a subsequent VMAC instruction uses the same accumulator register, the compiler must delay it by inserting no-ops to avoid a read-after-write data hazard. To avoid such no-ops, we structure our kernel so that it simultaneously calculates four independent output tiles, held in four distinct accumulator registers. The inner-most loop of our kernel contains four independent VMAC instructions that execute back-to-back.

To use VMAC, sub-matrices of sizes 4 × 8 (for A′ ) and 8 × 4 (for B′ ) must be laid out contiguously in memory. We use both the computation core's DMA and the AI Engine's data swizzling instructions to ensure all data is tiled correctly. First, the DMA swizzles data at a 4-byte granularity (the finest granularity it supports), and then a so-called VSHUFFLE instruction swaps the remaining misplaced two bytes during execution. VMAC and VSHUFFLE can simultaneously execute because they are implemented in separate hardware units. As a result, these additional operations have no impact on runtime. We verified this empirically by turning off the transformations (breaking the correctness of results) and observing identical runtimes. Similarly, the VLOAD instructions required to load data into registers can execute in parallel, so we can ensure the inner loop of our kernel is compute-bound.

By checking the generated assembly for back-to-back vector instructions (no no-ops for stalling), we were able to verify that the innermost loop of our kernel fully utilizes the vector hardware. However, some pre- and postamble code ("filling the pipeline"), where utilization is not optimal, is necessary before entering such a loop. By maximizing the tile size, we can minimize the number of these less efficient pre- and postambles.

## B. Tiling and Data Movement

Figure 4 shows the data movement between the NPU cores and the host's main memory. Tiling the input matrix into submatrices enables parallelism between compute cores, as well as overlapped data movement and computation. The limited memory in the compute cores also makes tiling a necessity, as the entirety of the matrices does not fit into a compute core's L1 memory.

We are following an accumulate-in-place tiling recipe; our implementation iterates through the m× n-sized output tiles of the output matrix C in-order. We stream the required input tiles into the cores, and each compute core locally accumulates an entire output tile before streaming it back out.

More specifically, the L3 shim cores stream m× k-sized and k × n-sized sub-tiles of the input matrices A and B, respectively, into the L2 memory cores. Each of the four shim cores is responsible for one quarter of the input space. Rows of tiles of A are repeated N 4n times, and columns of tiles of B are repeated M 4m times. Transfers on each shim core are offset by a multiple of the tile size and its (zero-based) hardware column index.

That is, the shim core in the hardware column i ∈ {0, 1, 2, 3} will transfer input matrix A's rows im + 4jm through im+4(j+1)m-1, for j = 0, 1, 2, . . . , M 4m , tiled into k-column-wide blocks, and input matrix B's columns in+4jn through in+4(j +1)n-1, for j = 0, 1, 2, . . . , N 4n , tiled into k-rows-tall blocks.

<!-- p.6 -->

Fig. 4. Data movement and tiling of input and output matrices across the three memory levels. The data movement to the compute core (2, 3) is highlighted as an example: This core receives its sub-tile of the A matrix from the memory core in column 1, and its sub-tile of the B matrix from the memory core in column 2 (zero-indexed).

![Image](images/image_000003_25ae492d45ca397f064d9a61bb5cfac1c49ab857003ef6784d87d9d6a4d63ef4.png)

For example, the shim in the hardware column 3 streams in the following sub-matrices of A, in sequence:

<!-- formula-not-decoded -->

The L2 memory cores store blocks of four tiles of A and B locally (i.e. m×4k-sized and 4k×n-sized blocks). These are then distributed across compute cores as m×k- and k×n-sized tiles: A memory core in the hardware column i distributes input A's tiles across all the compute cores in the hardware row i+2, such that core (i+2, 0) receives the first tile, core (i+2, 1) receives the second tile, and so on. The input matrix B is distributed across compute cores in the same hardware column i as the memory core; that is, compute core (2, i) receives the first tile, core (3, i) receives the second tile, and so on. 2

Fig. 5. Data layout transformations performed on the hardware DMAs for matrix sub-tiles for input matrix A. Matrices B and C are analogously tiled with their respective dimensions.

![Image](images/image_000004_5870079ae0aa899339d20d5ff21e57e31d301f9efd45312c1d9e566df098e8af.png)

For every Kk input tiles of A and B streamed into a compute core, that core will produce one m×n-sized output tile of C. The L2 memory cores then perform a column-wise join into m× 4n-sized output tiles, which are then written back into their appropriate positions in the C output buffer by an L3 core.

The entirety of this data movement is parametrized by problem size M, K and N, as well as tile size m, k, n. This allows us to generate concrete design variants for different parameters. The intricacy of this design shows the versatility of the tool-flow and the specialization possibilities that baremetal hardware access enables.

## C. Data Layout Transformations

The DMAs in the XDNA architecture can be configured to access data in non-linear patterns at a granularity of 4 bytes. Figure 5 shows how we use this hardware feature to transform the layouts of sub-matrices of the input matrix A. Concretely, we first transform matrix A, which resides in rowmajor format in L3, such that sub-matrices of size m×k are laid out contiguously in L2. Going from L2 to L1, we then rearrange these m×k-sized tiles to satisfy the vector intrinsic size requirements (4 × 8-sized tiles) in L1. Analogously, the input matrix B is transformed from a column-major layout in L3 into k ×n-sized tiles in L2, then 8 × 4-sized tiles in L1. Output C morphs from 4×4-sized tiles in L1 into m×n-sized tiles in L2, and is then written out as a row-major matrix to L3.

2 Note that row 2 is the lowest row of compute cores.

<!-- p.7 -->

## D. Reconfiguration

By using the same tile size m, k, and n for all variations, we completely eliminate the need to reconfigure the compute (L1) and memory (L2) cores. Only the shim cores and two runtime parameters in each core require reconfiguration between different problem sizes. This minimal reconfiguration reduces the switching times between GEMM sizes.

The compute cores read two runtime parameters from memory: The number of tiles to accumulate Kk for each output tile, and the number of output tiles MN mn to produce before re-reading the parameters. This allows the cores to adapt to new sizes after each complete GEMM. When reconfiguring for different problem sizes, the command processor writes new values for these parameters to each core's memory.

## VII. EVALUATION

We evaluated our implementation on a Asus Vivobook Pro 15 with an AMD Ryzen 9 7940HS CPU, equipped with a Phoenix XDNA NPU. We ran Ubuntu 22.04 LTS (kernel 6.10), and XRT/XDNA driver version 2.18.0. We disabled dynamic frequency scaling on the CPU and ran all benchmarks in a non-GUI environment. We restarted the test machine between each run. We observed that an unloaded CPU is critical for a competitive CPU-only implementation. In real-world use cases, where there may be concurrently running applications, this gives an additional edge to the NPU implementation.

We timed 41 training epochs of GPT-2 individually (llm.c's default). Each epoch consists of 197 GFLOP (see figure 2). We measured power consumption by polling a power driver file (/sys/class/power supply/BAT0/power now) every 14 s.

For benchmarks with high variance, the figures include boxes-and-whiskers to show the spread of the measurements; the standard deviation for all other figures is below 5%.

## A. GEMM Performance

Figure 6 visualizes the performance of our matrix multiplication design in isolation. We observe that the NPU implementation is faster than the CPU for every problem size. On average, GEMMs for sizes of the forward and backward passes are 3.1× and 2.8× faster, respectively. GEMM size 256 × 50304 × 768 experienced the largest speedup of 4.2×, whereas the size 256 × 768 × 2304 improved the least with a 1.8× speedup. In general, the relative speedup is largest for the larger problem sizes, because larger sizes amortize the constant (problem-size-independent) overheads of each NPU invocation more effectively.

To demonstrate the benefit of our minimal-reconfiguration approach, which requires updating only the shim cores and two runtime parameters to switch between problem sizes, we also compared it to a design that reconfigures the whole NPU array (one xclbin configuration binary for each problem size). On the first iteration of a new GEMM size, our approach is, on average, 3.5× faster than reconfiguring the whole array.

CPU

NPU

256×768×2304

256×768×768

## GEMM Performance

## Forward Pass Problem Sizes

256×768×3072

256×3072×768

256×768×50304

0.0

0.2

0.4

Runtime per epoch [s]

## Backward Pass Problem Sizes

Fig. 6. Total runtime spent performing GEMM operations in each training epoch, split by problem size (mean of 41 training epochs, lower is better). Note that some GEMM sizes occur repeatedly in a single training epoch (in which case we show the sum of total runtime across all invocations), and the GEMM sizes issued in the forward pass also occur in the backward gradient calculations.

![Image](images/image_000005_9d6db70236b4c6fbcd9eee863a336e8cd6c21f478a2626d163962ab4cb14a54f.png)

On subsequent iterations of the same size, reconfiguration is no longer required, so the runtimes of both approaches are roughly identical.

Figure 7 shows the contributors to the overall runtime of all GEMM invocations. Most of the time is spent running the NPU kernel; however, CPU-side preparation work (copying, transposing, and synchronizing buffers) is also a significant contributor.

We also evaluated the numerical accuracy of our GEMM algorithm. Our NPU kernel consumes bfloat16 inputs and accumulates and outputs float32 values, whereas the original CPU implementation operates on float32 values for both inputs and outputs. Porting the CPU implementation to also use bfloat16 would slow it down significantly. The float32-based CPU-implementation lowers to highly efficient vector FMA instructions on the CPU, (e.g. vfmadd213ps), but such instructions do not exist for bfloat16. Therefore, our baseline is the unmodified CPU implementation that uses float32 inputs. This leads to small numerical divergences between the CPU and NPU implementations; however, despite using a lower-precision data type, the NPU implementation achieves a slightly better validation error after 41 epochs. Overall, the NPU output is numerically close to the CPU reference: the mean relative divergence is below 0.06% (standard deviation 0.03%). The maximum deviation from the reference occurs for the 50304×256×768 size and is 0.1%.

<!-- p.8 -->

Fig. 7. Total runtime of all GEMM invocations of one training epoch, broken up by constituent stages (mean of 41 training epochs, lower is better). Our implementation copies input and output buffers from the GEMM call sites into XRT buffers for use with the NPU. Only some input matrices require transposition; where needed, the transpose also includes input copying. "NPU kernel" measures the actual GEMM being performed on the NPU. "Input sync." and "output sync." are unavoidable dispatch overheads incurred by the XDNA driver synchronizing CPU buffers with the NPU.

![Image](images/image_000006_1f9086ddd6854b7e578c57a8a63e749fefb97f6a0fe26fc9abf4208f67f16f4c.png)

Fig. 8. Total runtime per training epoch of GPT-2 small (124M) fine-tuning with llm.c in both the vanilla version (CPU) and our version with offloaded GEMMs (CPU+NPU), split by major constituent operations (mean across 41 training epochs, lower is better).

![Image](images/image_000007_b775cd02f0b59c13b258bdc8cf1dad15b2d8cbd1629e5b3bfba47032a4db98ba.png)

## B. End-to-end Results

By plugging our optimized GEMM implementation into llm.c, we can improve end-to-end performance of this application. Figure 8 shows which individual operations in llm.c contribute to overall runtime.

Evidently, matrix multiplication operations dominate overall runtime, making them a worthwhile target for offloading to the NPU. Thanks to the unified L3 memory, runtimes of the other unaltered operations on the CPU remain the same.

Offloading yields appreciable speedups in end-to-end throughput and energy efficiency, as figure 9 shows. The reduced runtime and lower power consumption of the NPUbased implementation compound in the combined throughputper-Watt-second metric, giving the NPU-based implementation

## End-to-end Application Performance

Fig. 9. Throughputs (higher is better) of the vanilla llm.c implementation of GPT-2 (CPU) and our version with GEMMs offloaded (CPU+NPU). "(M)" refers to benchmarks run on mains power, "(B)" to benchmarks run on battery power. The improvements stem entirely from the offloaded GEMM operations; the remainder of computation still occurs on the CPU.

![Image](images/image_000008_01a94ca88ca8dd0fd7109069465e8e8fe9b4dd69b69a7e5f3a2455a73c10f37b.png)

an 1.4× edge over the CPU-based implementation. Raw throughput is improved as well, at a speedup of 1.7× and 1.2× for mains and battery power, respectively.

## VIII. DISCUSSION AND FUTURE WORK

Our implementation shows that performance and power efficiency gains can be achieved by swapping in an NPU-based implementation for the most compute-intensive operations. Our low-level approach enables us to tailor our NPU design to the application at hand (e.g., by choosing appropriate matrix tiling sizes and limiting ourselves to the minimal functionality required by the application) and avoid overheads that a "one-size-fits-all" framework may accumulate (such as matrix padding). Hardware-level NPU programming harmonizes well with the central motif of llm.c, which trades established ML frameworks for a bespoke pure-C implementation.

Although our individual matrix multiplication speedups and end-to-end gains are significant, a comparison with the advertised hardware capabilities shows that there is still headroom for future optimizations. The theoretical compute capabilities of the NPU are on the order of tera-FLOP/s, whereas the end-to-end throughput of our application is on the order of hundreds of giga-FLOP/s only. The reason for this disparity is twofold: First, since our implementation still executes all but the most intensive operations on the CPU, execution performance quickly becomes constrained by the CPU's capabilities. Second, our evaluation (figure 7) identifies the CPU-to-NPUto-CPU data round trip at each kernel invocation as a costly overhead.

Tantalizingly, our results indicate that providing a baremetal programming toolchain is the best path towards harnessing the full power of the available hardware, as it enables the straightforward investment of additional programming effort to implement the entire computation pipeline as an NPU design (thus eliminating CPU computation and the aforementioned CPU-to-NPU-to-CPU data movement bottlenecks). Other work has demonstrated the feasibility of such data-flow approaches.

<!-- p.9 -->

In the near term, keeping with our approach of offloading only individual compute-intensive operations, future work may address some of the shortcomings of our current implementation, for example by implementing zero-copy buffers for inputs and outputs and eliminating the need for transpose operations on the CPU through a more substantial rewrite of llm.c.

## IX. ACKNOWLEDGMENTS

Thank you to Joseph Melber, Kristof Denolf, and Phil James-Roxby from Advanced Micro Devices Inc., for their guidance and help in developing the NPU design and contributions to this research.

## REFERENCES

- [1] R. Raina, A. Madhavan, and A. Y. Ng, "Large-scale deep unsupervised learning using graphics processors," in Proceedings of the 26th annual international conference on machine learning, 2009, pp. 873-880.
- [2] Advanced Micro Devices Inc. mlir-aie: An mlir-based toolchain for amd ai engine-enabled devices. [Online]. Available: https: //github.com/Xilinx/mlir-aie
- [3] A. Karpathy. llm.c: LLM training in simple, raw C/CUDA. [Online]. Available: https://github.com/karpathy/llm.c
- [4] A. Krizhevsky, I. Sutskever, and G. E. Hinton, "Imagenet classification with deep convolutional neural networks," Advances in neural information processing systems, vol. 25, 2012.
- [5] Y. LeCun, Y. Bengio, and G. Hinton, "Deep learning," nature, vol. 521, no. 7553, pp. 436-444, 2015.
- [6] T. N. Theis and H.-S. P. Wong, "The end of moore's law: A new beginning for information technology," Computing in Science &amp; Engineering, vol. 19, no. 2, pp. 41-50, Mar 2017.
- [7] N. P. Jouppi, C. Young, N. Patil, and D. Patterson, "A domain-specific architecture for deep neural networks," Commun. ACM, vol. 61, no. 9, p. 50-59, Aug. 2018. [Online]. Available: https://doi.org/10.1145/3154484
- [8] T. Chen, Z. Du, N. Sun, J. Wang, C. Wu, Y. Chen, and O. Temam, "Diannao: a small-footprint high-throughput accelerator for ubiquitous machine-learning," in Proceedings of the 19th International Conference on Architectural Support for Programming Languages and Operating Systems, ser. ASPLOS '14. New York, NY, USA: Association for Computing Machinery, 2014, p. 269-284. [Online]. Available: https://doi.org/10.1145/2541940.2541967
- [9] J. Wang, J. Lin, and Z. Wang, "Efficient hardware architectures for deep convolutional neural network," IEEE Transactions on Circuits and Systems I: Regular Papers, vol. 65, no. 6, pp. 1941-1953, 2018.
- [10] M. Motamedi, P. Gysel, V. Akella, and S. Ghiasi, "Design space exploration of fpga-based deep convolutional neural networks," in 2016 21st Asia and South Pacific Design Automation Conference (ASP-DAC), 2016, pp. 575-580.
- [11] A. Putnam, A. M. Caulfield, E. S. Chung, D. Chiou, K. Constantinides, J. Demme, H. Esmaeilzadeh, J. Fowers, G. P. Gopal, J. Gray, M. Haselman, S. Hauck, S. Heil, A. Hormati, J.-Y. Kim, S. Lanka, J. Larus, E. Peterson, S. Pope, A. Smith, J. Thong, P. Y. Xiao, and D. Burger, "A reconfigurable fabric for accelerating large-scale datacenter services," SIGARCH Comput. Archit. News, vol. 42, no. 3, p. 13-24, Jun. 2014. [Online]. Available: https: //doi.org/10.1145/2678373.2665678
- [12] J. Fowers, K. Ovtcharov, M. Papamichael, T. Massengill, M. Liu, D. Lo, S. Alkalay, M. Haselman, L. Adams, M. Ghandi, S. Heil, P. Patel, A. Sapek, G. Weisz, L. Woods, S. Lanka, S. K. Reinhardt, A. M. Caulfield, E. S. Chung, and D. Burger, "A configurable cloud-scale dnn processor for real-time ai," in Proceedings of the 45th Annual International Symposium on Computer Architecture, ser. ISCA '18. IEEE Press, 2018, p. 1-14. [Online]. Available: https://doi.org/10.1109/ISCA.2018.00012
- [13] E. Qin, A. Samajdar, H. Kwon, V. Nadella, S. Srinivasan, D. Das, B. Kaul, and T. Krishna, "Sigma: A sparse and irregular gemm accelerator with flexible interconnects for dnn training," in 2020 IEEE International Symposium on High Performance Computer Architecture (HPCA), 2020, pp. 58-70.
- [14] Advanced Micro Devices Inc. (2024) AMD XDNA architecture. [Online]. Available: https://www.amd.com/en/technologies/xdna.html
- [15] --. AI engines and their applications. [Online]. Available: https: //docs.amd.com/v/u/en-US/wp506-ai-engine
- [16] A. Rico, S. Pareek, J. Cabezas, D. Clarke, B. Ozgul, F. Barat, Y. Fu, S. M¨unz, D. Stuart, P. Schlangen, P. Duarte, S. Date, I. Paul, J. Weng, S. Santan, V. Kathail, A. Sirasao, and J. Noguera, "AMD XDNA™ NPU in Ryzen™ AI processors," IEEE Micro, pp. 1-10, 2024.
- [17] Apple Inc. (2024, May) Apple introduces m4 chip. [Online]. Available: https://www.apple.com/newsroom/2024/05/apple-introduces-m4-chip/
- [18] Qualcomm Technologies Inc. (2024, Feb.) Unlocking on-device generative ai with an npu and heterogeneous computing. [Online]. Available: https://www. qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/ Unlocking-on-device-generative-AI-with-an-NPU-and-heterogeneous-computing. pdf
- [19] S. A. Chin, N. Sakamoto, A. Rui, J. Zhao, J. H. Kim, Y. HaraAzumi, and J. Anderson, "Cgra-me: A unified framework for cgra modelling and exploration," in 2017 IEEE 28th International Conference on Application-specific Systems, Architectures and Processors (ASAP), 2017, pp. 184-189.
- [20] M. Harris, "Mapping computational concepts to gpus," in ACM SIGGRAPH 2005 Courses, ser. SIGGRAPH '05. New York, NY, USA: Association for Computing Machinery, 2005, p. 50-es. [Online]. Available: https://doi.org/10.1145/1198555.1198768
- [21] A. Vilches, A. Navarro, R. Asenjo, F. Corbera, R. Gran, and M. J. Garzar´an, "Mapping streaming applications on commodity multi-cpu and gpu on-chip processors," IEEE Transactions on Parallel and Distributed Systems, vol. 27, no. 4, pp. 1099-1115, 2016.
- [22] A. Abdelfattah, D. Keyes, and H. Ltaief, "Kblas: An optimized library for dense matrix-vector multiplication on gpu accelerators," ACM Trans. Math. Softw., vol. 42, no. 3, May 2016. [Online]. Available: https://doi.org/10.1145/2818311
- [23] M. Osama, D. Merrill, C. Cecka, M. Garland, and J. D. Owens, "Stream-k: Work-centric parallel decomposition for dense matrixmatrix multiplication on the gpu," in Proceedings of the 28th ACM SIGPLAN Annual Symposium on Principles and Practice of Parallel Programming, ser. PPoPP '23. New York, NY, USA: Association for Computing Machinery, 2023, p. 429-431. [Online]. Available: https://doi.org/10.1145/3572848.3577479
- [24] J. Kurzak, S. Tomov, and J. Dongarra, "Autotuning gemm kernels for the fermi gpu," IEEE Transactions on Parallel and Distributed Systems, vol. 23, no. 11, pp. 2045-2057, 2012.
- [25] J. Zhuang, J. Lau, H. Ye, Z. Yang, Y. Du, J. Lo, K. Denolf, S. Neuendorffer, A. Jones, J. Hu, D. Chen, J. Cong, and P. Zhou, "Charm: Composing heterogeneous accelerators for matrix multiply on versal acap architecture," in Proceedings of the 2023 ACM/SIGDA International Symposium on Field Programmable Gate Arrays, ser. FPGA '23. New York, NY, USA: Association for Computing Machinery, 2023, p. 153-164. [Online]. Available: https://doi.org/10.1145/3543622.3573210
- [26] J. Zhuang, J. Lau, H. Ye, Z. Yang, S. Ji, J. Lo, K. Denolf, S. Neuendorffer, A. Jones, J. Hu, Y. Shi, D. Chen, J. Cong, and P. Zhou, "Charm 2.0: Composing heterogeneous accelerators for deep learning on versal acap architecture," ACM Trans. Reconfigurable Technol. Syst., vol. 17, no. 3, Sep. 2024. [Online]. Available: https://doi.org/10.1145/3686163
- [27] E. Taka, A. Arora, K.-C. Wu, and D. Marculescu, "Maxeva: Maximizing the efficiency of matrix multiplication on versal ai engine," in 2023 International Conference on Field Programmable Technology (ICFPT), 2023, pp. 96-105.
- [28] A. Radford, J. Wu, R. Child, D. Luan, D. Amodei, I. Sutskever et al., "Language models are unsupervised multitask learners," OpenAI blog, vol. 1, no. 8, p. 9, 2019.
- [29] T. Brown, B. Mann, N. Ryder, M. Subbiah, J. D. Kaplan, P. Dhariwal, A. Neelakantan, P. Shyam, G. Sastry, A. Askell et al., "Language models are few-shot learners," Advances in neural information processing systems, vol. 33, pp. 1877-1901, 2020.
- [30] H. Touvron, T. Lavril, G. Izacard, X. Martinet, M.-A. Lachaux, T. Lacroix, B. Rozi`ere, N. Goyal, E. Hambro, F. Azhar et al., "Llama: Open and efficient foundation language models," arXiv preprint arXiv:2302.13971, 2023.
- [31] A. Vaswani, N. Shazeer, N. Parmar, J. Uszkoreit, L. Jones, A. N. Gomez, L. u. Kaiser, and I. Polosukhin, "Attention is all you need," in Advances in Neural Information Processing Systems, I. Guyon, U. V. Luxburg, S. Bengio, H. Wallach, R. Fergus, S. Vishwanathan, and R. Garnett, Eds., vol. 30. Curran Associates, Inc., 2017. [Online]. Available: https://proceedings.neurips.cc/paper files/paper/2017/file/3f5ee243547dee91fbd053c1c4a845aa-Paper.pdf
- [32] G. Nguyen, S. Dlugolinsky, M. Bob´ak, V. Tran, ´A. L´opez Garc´ıa, I. Heredia, P. Mal´ık, and L. Hluch`y, "Machine learning and deep learning frameworks and libraries for large-scale data mining: a survey," Artificial Intelligence Review, vol. 52, pp. 77-124, 2019.
- [33] M. S. Murshed, C. Murphy, D. Hou, N. Khan, G. Ananthanarayanan, and F. Hussain, "Machine learning at the network edge: A survey," ACM Computing Surveys (CSUR), vol. 54, no. 8, pp. 1-37, 2021.
- [34] P. Warden. The unstoppable rise of disposable ml frameworks. [Online]. Available: https://petewarden.com/2023/10/15/ the-unstoppable-rise-of-disposable-ml-frameworks/
- [35] A. Karpathy. llama2.c: Inference llama 2 in one file of pure c. [Online]. Available: https://github.com/karpathy/llama2.c
- [36] G. Gerganov. llama.cpp: Llm inference in c/c++. [Online]. Available: https://github.com/ggerganov/llama.cpp
- [37] [Advanced Micro Devices Inc. (2024) llama.cpp in ryzen AI software. [Online]. Available: https://github.com/amd/RyzenAI-SW/blob/ 557e527b68c553df3ce719e9a05a2c78c8e56270/example/transformers/ ext/llama.cpp/ggml- ryzenai.cpp#L341](https://github.com/amd/RyzenAI-SW/blob/557e527b68c553df3ce719e9a05a2c78c8e56270/example/transformers/ext/llama.cpp/ggml-ryzenai.cpp#L341)
- [38] --. Versal adaptive soc aie-ml architecture manual (am020). [Online]. Available: https://docs.amd.com/r/en-US/am020-versal-aie-ml/ Functional-Overview
- [39] --. (2024) Ryzen AI software 1.3 documentation. [Online]. Available: https://ryzenai.docs.amd.com/en/latest/
- [40] --. Amd vitis™ ai software. [Online]. Available: https://www.amd. com/en/developer/resources/vitis-ai.html
- [41] [--. Ai engine api user guide. [Online]. Available: https://www.xilinx. com/htmldocs/xilinx2022 1/aiengine api/aie api/doc/index.html](https://www.xilinx.com/htmldocs/xilinx2022_1/aiengine_api/aie_api/doc/index.html)
- [42] C. Lattner, M. Amini, U. Bondhugula, A. Cohen, A. Davis, J. Pienaar, R. Riddle, T. Shpeisman, N. Vasilache, and O. Zinenko, "Mlir: Scaling compiler infrastructure for domain specific computation," in 2021 IEEE/ACM International Symposium on Code Generation and Optimization (CGO). IEEE, 2021, pp. 2-14.

<!-- p.10 -->


