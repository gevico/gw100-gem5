/**
 * @file ct_if.hh
 * @brief ComputeTile 与外界的接口
 *
 * ComputeTile 只依赖这些接口和外部通信，不应和 gem5 的 Packet 和端口
 * 发生关系，便于测试
 */

#ifndef __GW100__COMPUTE_COMPUTE_IF_HH__
#define __GW100__COMPUTE_COMPUTE_IF_HH__

#include<cstdint>

#include"base/types.hh"
#include"src/gw100/ml/ml_types.hh"

namespace gem5
{
namespace gw100{
    class CtWindowHandler 
    {
        public:
          virtual ~CtWindowHandler() = default;

          /**  compute tile 内部单元访问普遍接口 */
          virtual CtAccessResult access(CtAccess &a, Tick now) = 0;

          /** 如何去 reset 还没定 */
          virtual void reset() = 0;
    }

    using CtNocReadDone = std::function<void(CtStatus,const uint8_t *)>;
    using CtNocWriteDone = std::function<void(CtStatus)>;
    
    class CtNocIf
    {
        public:
          virtual ~CtNocIf() = default;

          /** noc 端口目前是否可以发送，不可以考虑实现队列 */
          virtual bool canSend() const = 0;

          /** 向 dpa 发起一次读 */
          virtual void sendRead(Addr dpa, unsigned len,CtNocReadDone done) = 0;

          /** 向 dpa 发起一次写 */
          virtual void sendWrite(Addr dpa, unsigned len,const uint8_t *data,CtNocWriteDone done) = 0;

    }

}
}

#endif