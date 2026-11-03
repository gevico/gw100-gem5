/**
 * @file  boot_ctrl.hh
 * @brief HIF 的启动与 FLR 控制：STAGE、启动窗口、提交计数、FLR 门控
 *
 * host 看到的 BOOT_STATUS、FW_SIZE（BAR0，BootRegs）和固件写入的
 * FW_BOOT_STATUS 等（HIF 寄存器区，HifRegs）都是本对象状态的视图：
 * - 启动窗口在 STAGE = 2、不在 FLR 进行中、未锁存故障时打开；合法写入
 *   按 64 B 边界拆成 BOOT_WR 经 ML 写入装载区，并计入提交计数
 *   （spec-pcie §7.2）；
 * - FW_START 在提交计数归零后才以 EVENT 发出；
 * - BOOT_WR 失败或启动期间 ML 故障时锁存启动通路故障；
 * - FLR：门控、排空、以 BARRIER 发出 FLR 事件、TAKE/DONE 确认、处理
 *   超时（spec-pcie §13.2）。
 *
 * @see spec-pcie, spec-mgmt-link, spec-mgmt-tile
 */

#ifndef __GW100_PCIE_BOOT_BOOT_CTRL_HH__
#define __GW100_PCIE_BOOT_BOOT_CTRL_HH__

#include <cstdint>
#include <string>

#include "sim/eventq.hh"
#include "src/gw100/ml/ml_endpoint.hh"
#include "src/gw100/pcie/status/status_unit.hh"

namespace gem5
{
namespace gw100
{

/** HIF 的启动与 FLR 控制 */
class BootCtrl
{
  public:
    /** ERROR 编码：启动通路故障（spec-pcie §6.4） */
    static constexpr uint32_t ErrLink = 5;

    /**
     * 构造：status 记录 BAR2 错误，fw_load_max 为装载区大小，
     * flr_timeout 为 pcie.flr_proc_timeout，em 用于调度处理超时
     */
    BootCtrl(const std::string &name, StatusUnit &status,
             uint32_t fw_load_max, Tick flr_timeout, EventManager &em);

    /** 接入 ML 的 HIF 侧适配器，登记启动写入和事件两个客户端 */
    void bindMl(MlEndpoint *ep);

    /** ML 的链路状态或错误变化时调用 */
    void mlChanged();

    /** host 读 BOOT_STATUS：FLR 进行中时按门控显示 */
    uint32_t hostBootStatus() const;

    /** host 写 BOOT_CTRL（W1S）：[0] FW_START，[1] DEV_RESET */
    void hostBootCtrl(uint32_t v);

    /** host 读 FW_SIZE */
    uint32_t fwSize() const { return size; }

    /** host 写 FW_SIZE：只在启动窗口打开时有效 */
    void hostWriteFwSize(uint32_t v);

    /** 启动窗口是否打开 */
    bool windowOpen() const;

    /**
     * BAR2 在启动窗口模式下的写：off 为窗口内偏移。返回 false 表示
     * 写入非法（已记录 BOOT_WIN 错误与 WIN_WRITE）
     */
    bool windowWrite(Addr off, unsigned len, const uint8_t *data);

    /** 固件读 FW_BOOT_STATUS：HIF 中的实际值 */
    uint32_t fwBootStatus() const;

    /** 固件写 FW_BOOT_STATUS */
    void fwWriteBootStatus(uint32_t v);

    /** 固件写 FW_SIZE_FW：只能写 0 */
    void fwWriteFwSize(uint32_t v);

    /** 固件读 BOOT_LOAD_STATUS：[0] WIN_WRITE */
    uint32_t bootLoadStatus() const { return winWrite ? 1 : 0; }

    /** 固件读 FLR_STATUS：[0] IN_PROGRESS，[1] DELIVERED，[2] FAIL */
    uint32_t flrStatus() const;

    /** 固件写 FLR_CTRL（W1S）：[0] TAKE，[1] DONE */
    void flrCtrl(uint32_t v);

    /** 发生 FLR（含 D3hot→D0）：在 host 可见状态复位之后调用 */
    void flr();

    /** DEV_STATUS.DEBUG_WIN：固件经 DEV_CTRL_FW 开启 */
    bool debugWin = false;

    /** DEV_STATUS.DEV_FATAL：只能由完全复位、热复位、冷复位清除 */
    bool devFatal = false;

    /** 调试输出用的名字（DPRINTF 需要） */
    const std::string &name() const { return _name; }

  private:
    /** 调试输出用的名字 */
    const std::string _name;

    /** 窗口状态可能变化后调用：窗口由关闭变为打开时清零 WIN_WRITE */
    void refreshWindow();

    /** 尝试发出待发的 FW_START 事件和 FLR 屏障 */
    void trySend();

    /** 锁存启动通路故障（spec-pcie §7.2） */
    void latchLinkFault();

    /** FLR 处理失败：置位 FAIL 与 DEV_FATAL */
    void flrFail();

    /** 是否有 FLR 处于"处理中" */
    bool flrProcessing() const { return flrNeedSend || barriersOut > 0; }

    /** 按 ML 是否为 UP 启动或停止处理超时的计时 */
    void updateFlrTimer();

    /** 处理超时到达 */
    void flrTimeout();

    /** BAR2 错误的记录 */
    StatusUnit &status;
    /** 装载区大小 */
    const uint32_t fwLoadMax;
    /** pcie.flr_proc_timeout */
    const Tick flrTimeoutTicks;
    /** 调度处理超时的事件管理者 */
    EventManager &em;
    /** ML 的 HIF 侧适配器 */
    MlEndpoint *ml = nullptr;
    /** 启动写入的 ML 客户端 */
    unsigned bootClient = 0;
    /** 事件（FW_START、FLR 屏障）的 ML 客户端 */
    unsigned evtClient = 0;

    /** 固件写入的 STAGE */
    uint32_t stage = 0;
    /** 固件写入的 FW_READY */
    bool fwReady = false;
    /** ERROR */
    uint32_t error = 0;
    /** FW_SIZE */
    uint32_t size = 0;
    /** BOOT_LOAD_STATUS.WIN_WRITE */
    bool winWrite = false;
    /** 上一次检查时窗口是否打开 */
    bool lastWindowOpen = false;
    /** 是否已锁存启动通路故障 */
    bool linkFault = false;

    /** 已接受、尚未确认的 BOOT_WR 个数（提交计数） */
    unsigned commitCount = 0;
    /** FW_START 已被接受、事件尚未被 ML 接受 */
    bool fwStartPending = false;

    /** FLR 进行中 */
    bool flrInProgress = false;
    /** FLR 的屏障尚未被 ML 接受 */
    bool flrNeedSend = false;
    /** 已被 ML 接受、OK 尚未返回的 FLR 屏障个数 */
    unsigned barriersOut = 0;
    /** FLR_STATUS.DELIVERED */
    bool delivered = false;
    /** FLR_STATUS.FAIL */
    bool flrFailed = false;
    /** 处理超时的事件 */
    EventFunctionWrapper flrTimer;
    /** 处理超时剩余的时间（ML 不为 UP 时暂停） */
    Tick flrRemaining = 0;
    /** 计时从何时开始（计时中有效） */
    Tick flrTimerStart = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BOOT_BOOT_CTRL_HH__
