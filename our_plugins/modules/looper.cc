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

#include "looper.h"

#include "../utils/format.h"

const Commands Looper::cmds = {
    {"set_burst", "LooperCommandSetBurstArg",
     MODULE_CMD_FUNC(&Looper::CommandSetBurst), Command::THREAD_SAFE},
    {"get_status", "LooperCommandGetStatusArg",
     MODULE_CMD_FUNC(&Looper::CommandGetStatus), Command::THREAD_SAFE}};

CommandResponse Looper::Init(const bess::pb::LooperArg &arg) {
  task_id_t tid;
  CommandResponse err;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return CommandFailure(ENOMEM, "Task creation failed");
  }

  if (arg.burst() > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "burst size must be [0,%zu]",
                          bess::PacketBatch::kMaxBurst);
  }

  if(arg.burst()) {
    burst_ = arg.burst();
  }
  else {
    burst_ = bess::PacketBatch::kMaxBurst;
  }


  return CommandSuccess();
}

void Looper::DeInit() {
  if (!pkts_.empty()) {
    for(auto& pkt: pkts_) {
      bess::Packet::Free(pkt);
    }
    pkts_.clear();
    pkts_.shrink_to_fit();
  }
}

std::string Looper::GetDesc() const {
  uint64_t num_pkts = pkts_.size();
  return bess::utils::Format("packets: %lu, burst size: %u", num_pkts, burst_);
}

/* from upstream */
void Looper::ProcessBatch(Context *, bess::PacketBatch *batch) {
  bess::Packet **pkts = batch->pkts();
  for (int i = 0; i < batch->cnt(); i++) {
    pkts_.push_back(bess::Packet::copy(pkts[i]));
  }
}

/* to downstream */
struct task_result Looper::RunTask(Context *ctx, bess::PacketBatch *batch,
                                  void *) {
  if (children_overload_ > 0) {
    return {
        .block = true, .packets = 0, .bits = 0,
    };
  }

  if (pkts_.size() == 0) {
    return {.block = true, .packets = 0, .bits = 0};
  }

  const uint32_t burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;
  const int pkt_size = batch->pkts()[0]->total_len();

  for(size_t i=batch->cnt(); i < burst; i++) {
    batch->add(bess::Packet::copy(pkts_[(pkts_position + i) % pkts_.size()]));
  }

  pkts_position = (pkts_position + batch->cnt()) % pkts_.size();

  RunNextModule(ctx, batch);

  return {.block = false,
          .packets = burst,
          .bits = (pkt_size + pkt_overhead) * burst * 8};
}

CommandResponse Looper::CommandSetBurst(
    const bess::pb::LooperCommandSetBurstArg &arg) {
  if (arg.burst() > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "burst size must be [0,%zu]",
                          bess::PacketBatch::kMaxBurst);
  }
  burst_ = arg.burst();
  return CommandSuccess();
}


CommandResponse Looper::CommandGetStatus(
    const bess::pb::LooperCommandGetStatusArg &) {
  bess::pb::LooperCommandGetStatusResponse resp;
  uint64_t num_pkts = pkts_.size();
  resp.set_burst(burst_);
  resp.set_num_packets(num_pkts);
  return CommandSuccess(resp);
}


CheckConstraintResult Looper::CheckModuleConstraints() const {
  CheckConstraintResult status = CHECK_OK;
  if (num_active_tasks() - tasks().size() < 1) {  // Assume multi-producer.
    LOG(ERROR) << "Queue has no producers";
    status = CHECK_NONFATAL_ERROR;
  }

  if (tasks().size() > 1) {  // Assume single consumer.
    LOG(ERROR) << "More than one consumer for the queue" << name();
    return CHECK_FATAL_ERROR;
  }

  return status;
}

ADD_MODULE(Looper, "looper", "loops received packets")
