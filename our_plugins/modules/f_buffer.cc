// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "f_buffer.h"

#include "../utils/format.h"

const Commands FractionalBuffer::cmds = {
  {"set_size", "FractionalBufferCommandSetSizeArg",
   MODULE_CMD_FUNC(&FractionalBuffer::CommandSetSize), Command::THREAD_SAFE},
  {"get_size", "FractionalBufferCommandGetSizeArg",
   MODULE_CMD_FUNC(&FractionalBuffer::CommandGetSize), Command::THREAD_SAFE}};

CommandResponse
FractionalBuffer::Init(const bess::pb::FractionalBufferArg &arg) {
  size_ = arg.size();
  if (size_ > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "'size' must be 0-%zu",
                          bess::PacketBatch::kMaxBurst);
  }
  return CommandSuccess();
}

void FractionalBuffer::DeInit() {
  bess::PacketBatch *buf = &buf_;
  bess::Packet::Free(buf);
}

std::string FractionalBuffer::GetDesc() const {
  return bess::utils::Format("%d/%lu", buf_.cnt(), size_);
}

void FractionalBuffer::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  // _size might change under us (via a SetSize() call) which may lead to a
  // segfault in certain cases, so first we copy current size parameter, we
  // make sure the copy actually took place by using a compiler barrier,
  // and then we use the copy throughout the call to make sure it doesn't
  // change behind our back
  size_t __size = size_;
  STORE_BARRIER();

  bess::PacketBatch *buf = &buf_;

  // after a set_size(0) call we need to take care of sending out the
  // packets currently kept in the buffer, otherwise these packets would
  // get stuck in the buffer and never get sent out
  if(__size == 0){
    if(unlikely(buf->cnt() > 0)){
      bess::PacketBatch *new_batch = ctx->task->AllocPacketBatch();
      new_batch->Copy(buf);
      buf->clear();
      RunNextModule(ctx, new_batch);
    }
    RunNextModule(ctx, batch);
    return;
  }

  // For extremely small "__size" arguments it can happen that the buffer
  // already contains more packets then the "__size", which may cause
  // memory corruption by "free_slots" becoming negative. So first we send
  // the buffer as is, and only after that we process the incoming
  // batch. The fact that we send an over-sized buffer once will not be a
  // problem as this is supposed to happen rarely (only at most once after
  // each set_size() API call).
  if(__size < size_t(buf->cnt())){
    bess::PacketBatch *initial_batch = ctx->task->AllocPacketBatch();
    initial_batch->Copy(buf);
    buf->clear();
    RunNextModule(ctx, initial_batch);
  }

  int free_slots = __size - buf->cnt();
  int left = batch->cnt();

  bess::Packet **p_buf = &buf->pkts()[buf->cnt()];
  bess::Packet **p_batch = &batch->pkts()[0];

  while (left >= free_slots) {
    buf->set_cnt(__size);
    bess::utils::CopyInlined(p_buf, p_batch,
                             free_slots * sizeof(bess::Packet *));

    p_buf = &buf->pkts()[0];
    p_batch += free_slots;
    left -= free_slots;

    bess::PacketBatch *new_batch = ctx->task->AllocPacketBatch();
    new_batch->Copy(buf);
    buf->clear();
    RunNextModule(ctx, new_batch);
    free_slots = __size;
  }

  buf->incr_cnt(left);
  bess::utils::CopyInlined(p_buf, p_batch, left * sizeof(bess::Packet *));
}

CommandResponse FractionalBuffer::SetSize(uint32_t size) {
  if (size_ > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "'size' must be 0-%zu",
                          bess::PacketBatch::kMaxBurst);
  }
  size_ = size;
  return CommandSuccess();
}

CommandResponse FractionalBuffer::CommandSetSize(
    const bess::pb::FractionalBufferCommandSetSizeArg &arg) {
  return SetSize(arg.size());
}

CommandResponse FractionalBuffer::CommandGetSize(
    const bess::pb::FractionalBufferCommandGetSizeArg &) {
  bess::pb::FractionalBufferCommandGetSizeResponse resp;
  resp.set_size(size_);
  return CommandSuccess(resp);
}
ADD_MODULE(FractionalBuffer, "fractional_buffer",
           "buffers packets into batches with configurable sizes")
