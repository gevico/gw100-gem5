/**
 * @file  fc.hh
 * @brief 把功能核心库的名字引入 gem5::gw100
 *
 * gem5 侧代码都在 gem5::gw100 里，非限定查找不会进入顶层的 ::gw100，
 * 所以这里逐个用 using 声明引入，而不用 using namespace，避免与 gem5 自身的名字
 * （例如 decode）冲突。gem5 侧代码只包含本文件，不直接包含 gw100/*.hh。
 */

#ifndef __GW100_COMMON_FC_HH__
#define __GW100_COMMON_FC_HH__

#include "gw100/inst.hh"
#include "gw100/isa_defs.hh"
#include "gw100/numerics.hh"
#include "gw100/vector_ops.hh"

namespace gem5
{
namespace gw100
{

namespace isa = ::gw100::isa;
namespace fc = ::gw100::fc;

using ::gw100::AddrSpan;
using ::gw100::ByteRun;
using ::gw100::LocalWindow;
using ::gw100::TensorDesc;
using ::gw100::TensorInst;

using ::gw100::addrSpan;
using ::gw100::byteRuns;
using ::gw100::decode;
using ::gw100::encode;
using ::gw100::opcodeName;
using ::gw100::resolveLocal;

} // namespace gw100
} // namespace gem5

#endif // __GW100_COMMON_FC_HH__
