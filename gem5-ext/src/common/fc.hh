/**
 * 把功能核心库（命名空间 ::acis，不依赖 gem5）的名字引入 gem5::acis。
 *
 * gem5 侧代码都在 gem5::acis 里，非限定查找不会进入顶层的 ::acis，
 * 所以这里逐个用 using 声明引入，而不用 using namespace，避免与 gem5 自身的名字
 * （例如 decode）冲突。gem5 侧代码只包含本文件，不直接包含 acis/*.hh。
 */

#ifndef __ACIS_COMMON_FC_HH__
#define __ACIS_COMMON_FC_HH__

#include "acis/inst.hh"
#include "acis/isa_defs.hh"
#include "acis/numerics.hh"
#include "acis/vector_ops.hh"

namespace gem5
{
namespace acis
{

namespace isa = ::acis::isa;
namespace fc = ::acis::fc;

using ::acis::AddrSpan;
using ::acis::ByteRun;
using ::acis::LocalWindow;
using ::acis::TensorDesc;
using ::acis::TensorInst;

using ::acis::addrSpan;
using ::acis::byteRuns;
using ::acis::decode;
using ::acis::encode;
using ::acis::opcodeName;
using ::acis::resolveLocal;

} // namespace acis
} // namespace gem5

#endif // __ACIS_COMMON_FC_HH__
