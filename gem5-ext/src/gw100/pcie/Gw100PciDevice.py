"""
@file  Gw100PciDevice.py
@brief GW100 PCIe endpoint 的 SimObject 声明

寄存器层按 spec-pcie 实现：配置空间与 capability、BAR0 全局寄存器、
BAR2 窗口（经 mem_port 访问设备内存）、FLR。中断投递、DMA 和命令协议
在后续阶段接入。参数的取值由 gem5-ext/configs/gw100/devices/ 的设备
预设给出，这里只给默认值。

@see spec-pcie
"""

from m5.objects.PciDevice import PciEndpoint, PciMemBar, PciMemUpperBar
from m5.params import *
from m5.proxy import *


class Gw100PrefetchMemBar(PciMemBar):
    """可预取的内存 BAR：gem5 的 PciMemBar 不支持 prefetchable 位。"""

    type = "Gw100PrefetchMemBar"
    cxx_header = "src/gw100/pcie/bar2/prefetch_mem_bar.hh"
    cxx_class = "gem5::gw100::Gw100PrefetchMemBar"


class Gw100PciDevice(PciEndpoint):
    """GW100 的 PCIe endpoint：寄存器层。"""

    type = "Gw100PciDevice"
    cxx_header = "src/gw100/pcie/gw100_pci_device.hh"
    cxx_class = "gem5::gw100::Gw100PciDevice"

    # 配置空间头部（spec-pcie §4.1）。Vendor/Device ID 为仿真用占位值。
    VendorID = 0x1234
    DeviceID = 0x1000
    Revision = 0x01
    ClassCode = 0x12
    SubClassCode = 0x00
    ProgIF = 0x00
    InterruptPin = 0x01
    # Status.Capabilities List；capability 链从 0x40 开始，由 C++ 提供
    Status = 0x0010
    CapabilityPtr = 0x40

    # BAR0：寄存器，1 MiB，32 位不可预取；BAR2/3：64 MiB 64 位可预取窗口
    BAR0 = PciMemBar(size="1MiB")
    BAR2 = Gw100PrefetchMemBar(size="64MiB")
    BAR3 = PciMemUpperBar()

    # 真实的 endpoint 从配置写请求中捕获自己的总线号；gem5 的配置访问
    # 不携带总线号，由板卡按挂载位置给出（VExpress 的设备都在根总线 0）
    pci_bus = Param.UInt8(0, "设备所在的 PCI 总线号，用于 Requester ID")
    version = Param.UInt32(0x00000100, "DEV_VERSION：[23:16] 主、[15:8] 次、[7:0] 步进")
    hdi_max_contexts = Param.UInt8(16, "DEV_CAPS 中报告的上下文个数")
    hdma_channels = Param.UInt8(8, "DEV_CAPS 中报告的 DMA 通道个数")
    msix_table_size = Param.UInt8(32, "DEV_CAPS 中报告的 MSI-X 表项个数")

    bar2_window_count = Param.Unsigned(16, "BAR2 窗口个数")
    bar2_window_size = Param.MemorySize("4MiB", "每个 BAR2 窗口的大小")
    bar2_latency = Param.Latency(
        "200ns", "经 BAR2 访问设备内存的固定延迟（功能访问，理想化）"
    )

    devmem_base = Param.Addr(0x0, "设备内存的起始设备物理地址")
    devmem_size = Param.MemorySize("32GiB", "设备内存大小")
    device_pa_bits = Param.Unsigned(40, "设备物理地址宽度")

    timestamp_hz = Param.UInt32(100000000, "设备时间戳频率，Hz")
    link_speed = Param.UInt8(4, "当前链路速率：1 = Gen1 … 4 = Gen4")
    link_width = Param.UInt8(4, "当前链路宽度")

    mem_port = RequestPort("接设备内存，BAR2 窗口和 HDMA 经它访问")
    bar2_mem_port = RequestPort("精确配置的独立 BAR2 timing 内存端口")
    async_memory = Param.Bool(False, "采用有界异步 PIO 与独立 BAR2 入口")
    memory_ingresses = VectorParam.Gw100MemoryIngress(
        [], "FLR 与电源变化时作废的两个 PCIe 内存入口"
    )
    devmem_max_outstanding = Param.Unsigned(
        32,
        "HDMA 对设备内存的在途请求上限：NoC 端口的缓冲，待 spec-noc 确定"
        "（推算）",
    )

    # Host DMA（spec-host-dma §12）
    hdma_max_ring_log2 = Param.Unsigned(8, "log2(hdma.max_ring_entries)")
    hdma_max_desc_bytes = Param.MemorySize("256MiB", "hdma.max_desc_bytes")
    hdma_sgl_segment_max = Param.Unsigned(
        4096, "hdma.sgl_segment_max_bytes"
    )
    hdma_sgl_max_segments = Param.Unsigned(512, "hdma.sgl_max_segments")
    hdma_max_read_request = Param.Unsigned(
        512, "hdma.max_read_request（pcie.dma_max_read_request）"
    )
    hdma_max_outstanding_reads = Param.Unsigned(
        64, "hdma.max_outstanding_reads"
    )
    hdma_completion_buffer = Param.MemorySize(
        "32KiB", "hdma.completion_buffer"
    )
    hdma_d2h_buffer = Param.MemorySize("16KiB", "hdma.d2h_buffer")
    pcie_cpl_timeout = Param.Latency(
        "50ms", "host 读请求的完成超时：PCIe 默认范围 50 us – 50 ms 的上限"
    )
    pcie_stale_tag_quarantine = Param.Latency(
        "250ms", "pcie.stale_tag_quarantine"
    )
    pcie_tx_credits = Param.Unsigned(
        16,
        "在途写请求（TLP）的上限：replay buffer 4 KiB ÷ 256 B（推算，"
        "spec-pcie §10.4）",
    )

    fw_load_max = Param.MemorySize(
        "1MiB", "启动窗口与装载区的大小（mgmt.fw_load_max）"
    )
    flr_proc_timeout = Param.Latency(
        "10ms", "一个 FLR 处于处理中的上限（pcie.flr_proc_timeout）"
    )

    wdt_fatal = IntSinkPin(
        "看门狗第二级超时的专用信号，接管理 Tile 的 wdt_fatal"
        "（spec-mgmt-tile §7.4）"
    )

    dev_reset_req = IntSourcePin(
        "设备完全复位的请求，接管理 Tile（CRC）的 dev_reset_req"
    )
    func_reset = ResetResponsePort(
        "CRC 送来的功能逻辑复位信号，接管理 Tile 的 pcie_reset"
    )

    mgmt_link = Param.Gw100MgmtLink(
        NULL, "管理链路 ML；为空时设备不接管理 Tile（只用于寄存器层的测试）"
    )
