// Copyright (c) 2017, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// Copyright (c) 2019, Felicián Németh
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

#include "last.h"

#include "../utils/format.h"

using bess::utils::be32_t;

const Commands Last::cmds = {
    {"get_last", "EmptyArg", MODULE_CMD_FUNC(&Last::GetLast),
     Command::THREAD_UNSAFE},
    {"reset", "EmptyArg", MODULE_CMD_FUNC(&Last::Reset),
     Command::THREAD_SAFE},
};

CommandResponse
Last::Init(const bess::pb::EmptyArg &) {
  return CommandSuccess();
}

std::string Last::GetDesc() const {

  uint64_t diff = last_ - first_;
  uint64_t ms = (diff / 1000) % 1000;
  uint64_t s = diff / 1000000;
  return bess::utils::Format("%" PRIu64 ".%" PRIu64, s, ms);
}

CommandResponse Last::GetLast(const bess::pb::EmptyArg &) {
  hsnlab::last::pb::LastCommandGetLastResponse ret;
  ret.set_first(first_);
  ret.set_last(last_);
  return CommandSuccess(ret);
}

CommandResponse Last::Reset(const bess::pb::EmptyArg &) {
  first_ = last_ = 0;
  return CommandSuccess();
}

void Last::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  last_ = tsc_to_ns(rdtsc());
  if ((unlikely(first_ == 0))) {
    first_ = last_;
  }

  RunChooseModule(ctx, ctx->current_igate, batch);
}

ADD_MODULE(Last, "last", "Save the arrival time of the last incoming packet")
