"""
@file  memory.py
@brief 独立 timing System 中的四 Tile、八通道通用设备内存

@see spec-dram, spec-address-map, spec-clock-reset-power
"""

from m5.objects import (
    AddrRange,
    Gw100MemoryIngress,
    Gw100MemoryTile,
    LPDDR5_8433_1x16_BG_BL32,
    NoncoherentXBar,
    SrcClockDomain,
    System,
    VoltageDomain,
)

from ..generated import memory_defs as d


def make_memory_system(
    dev,
    mgmt,
    *,
    fail_mask=0,
    timeout_mask=0,
    fault_phase=3,
    corrupt_selftest=False,
    corrupt_mask=0,
):
    """连接三个硬件来源和八个物理通道，配置注入仅改变反馈。"""
    s = System(mem_mode="timing", cache_line_size=d.BURST_BYTES)
    s.voltage_domain = VoltageDomain()
    s.clk_domain = SrcClockDomain(
        clock=["1GHz", "25MHz"],
        init_perf_level=1,
        voltage_domain=s.voltage_domain,
    )
    s.ddr_domain = SrcClockDomain(
        clock=[f"{d.CK_PS}ps", "25MHz"],
        init_perf_level=1,
        voltage_domain=s.voltage_domain,
    )
    s.mem_ranges = [AddrRange(d.TOTAL_BYTES)]
    s.mmap_using_noreserve = True
    s.membus = NoncoherentXBar(
        width=d.FIFO_BYTES,
        frontend_latency=2,
        forward_latency=2,
        response_latency=2,
    )
    s.system_port = s.membus.cpu_side_ports
    controllers = []
    for ch in range(d.CHANNELS):
        dram = LPDDR5_8433_1x16_BG_BL32(
            device_size=f"{d.DEVICE_BYTES}B",
            ranks_per_channel=d.RANKS_PER_CHANNEL,
            read_buffer_size=d.READ_SLOTS,
            write_buffer_size=d.WRITE_SLOTS,
            tRFC=f"{d.TRFC_PS}ps",
            tXS=f"{d.TXS_PS}ps",
            tREFI=f"{d.TREFI_PS}ps",
            defer_startup=True,
            range=AddrRange(
                start=0,
                size=d.TOTAL_BYTES,
                intlvHighBit=10,
                intlvBits=3,
                intlvMatch=ch,
            ),
        )
        ctrl = dram.controller()
        ctrl.clk_domain = s.ddr_domain
        ctrl.static_frontend_latency = f"{d.PIPELINE_CYCLES * d.CK_PS}ps"
        ctrl.static_backend_latency = f"{d.PIPELINE_CYCLES * d.CK_PS}ps"
        controllers.append(ctrl)
    s.controllers = controllers
    s.tiles = [
        Gw100MemoryTile(
            tile_id=k,
            controllers=controllers[k * 2 : k * 2 + 2],
            ddr_domain=s.ddr_domain,
            calibration_latency=f"{d.CALIBRATION_PS}ps",
            training_latency=f"{d.TRAIN_PS}ps",
            fail_mask=fail_mask,
            timeout_mask=timeout_mask,
            fault_phase=fault_phase,
            corrupt_selftest=corrupt_selftest,
            selftest_fail_mask=corrupt_mask,
        )
        for k in range(d.TILES)
    ]
    for k, tile in enumerate(s.tiles):
        tile.cpu_side = s.membus.mem_side_ports
        for ch in range(d.CHANNELS_PER_TILE):
            tile.mem_side = controllers[k * 2 + ch].port
    domains = [mgmt.clk_domain, dev.clk_domain, dev.clk_domain]
    s.ingresses = [
        Gw100MemoryIngress(
            source_id=i, source_domain=domains[i], tiles=s.tiles
        )
        for i in range(3)
    ]
    for ingress in s.ingresses:
        ingress.mem_side = s.membus.cpu_side_ports
    mgmt.tile.ni = s.ingresses[d.SOURCE_MGMT].cpu_side
    dev.mem_port = s.ingresses[d.SOURCE_HDMA].cpu_side
    dev.bar2_mem_port = s.ingresses[d.SOURCE_BAR2].cpu_side
    dev.memory_ingresses = s.ingresses[1:]
    mgmt.tile.memory_tiles = s.tiles
    mgmt.tile.memory_ingresses = s.ingresses
    mgmt.tile.memory_core_domain = s.clk_domain
    mgmt.tile.memory_ddr_domain = s.ddr_domain
    return s
