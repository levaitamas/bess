// Copyright (c) 2017, The Regents of the University of California.
// Copyright (c) 2017, Vivian Fang.
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

#include "w_randomsplit.h"
#include <string>
#include <time.h>
#include <map>

static inline bool is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

const Commands WeightedRandomSplit::cmds = {
    {"set_droprate", "WeightedRandomSplitCommandSetDroprateArg",
     MODULE_CMD_FUNC(&WeightedRandomSplit::CommandSetDroprate), Command::THREAD_UNSAFE},
    {"set_gates_weights", "WeightedRandomSplitCommandSetGatesWeightsArg",
     MODULE_CMD_FUNC(&WeightedRandomSplit::CommandSetGatesWeights), Command::THREAD_UNSAFE}};

CommandResponse WeightedRandomSplit::Init(const bess::pb::WeightedRandomSplitArg &arg) {
  double drop_rate = arg.drop_rate();
  std::map<gate_idx_t,double> gates_weights;
  double weights_sum = 0;

  if (drop_rate < 0 || drop_rate > 1) {
    return CommandFailure(EINVAL, "drop rate needs to be between [0, 1]");
  }
  drop_rate_ = drop_rate;

  if (arg.gates_weights_size() > MAX_SPLIT_GATES) {
    return CommandFailure(EINVAL, "no more than %d gates", MAX_SPLIT_GATES);
  }

  for(const auto& it: arg.gates_weights()) {
    weights_sum += it.second;
  }

  for(auto& it: arg.gates_weights()) {
    gates_weights[it.first] = it.second / weights_sum;
  }


  for (const auto& it : gates_weights) {
    if (!is_valid_gate(it.first)) {
      return CommandFailure(EINVAL, "Invalid gate %d", it.first);
    }
  }

  ngates_ = gates_weights.size();
  size_t i = 0;
  for (const auto& it : gates_weights) {
    gates_[i] = it.first;
    weights_[i] = it.second;
    i++;
  }

  vosealias_ = VoseAlias<gate_idx_t>(gates_weights);

  return CommandSuccess();
}

CommandResponse WeightedRandomSplit::CommandSetDroprate(
    const bess::pb::WeightedRandomSplitCommandSetDroprateArg &arg) {
  double drop_rate = arg.drop_rate();
  if (drop_rate < 0 || drop_rate > 1) {
    return CommandFailure(EINVAL, "drop rate needs to be between [0, 1]");
  }
  drop_rate_ = drop_rate;

  return CommandSuccess();
}

CommandResponse WeightedRandomSplit::CommandSetGatesWeights(
    const bess::pb::WeightedRandomSplitCommandSetGatesWeightsArg &arg) {
  std::map<gate_idx_t,double> gates_weights;
  double weights_sum = 0;
  if (arg.gates_weights_size() > MAX_SPLIT_GATES) {
    return CommandFailure(EINVAL, "no more than %d gates", MAX_SPLIT_GATES);
  }

  for(const auto& it: arg.gates_weights()) {
    weights_sum += it.second;
  }

  for(auto& it: arg.gates_weights()) {
    gates_weights[it.first] = it.second / weights_sum;
  }

  for (const auto& it : gates_weights) {
    if (!is_valid_gate(it.first)) {
      return CommandFailure(EINVAL, "Invalid gate %d", it.first);
    }
  }

  ngates_ = gates_weights.size();
  size_t i = 0;
  for (const auto& it : gates_weights) {
    gates_[i] = it.first;
    weights_[i] = it.second;
    i++;
  }

  vosealias_ = VoseAlias<gate_idx_t>(gates_weights);

  return CommandSuccess();
}


void WeightedRandomSplit::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  if (ngates_ <= 0) {
    bess::Packet::Free(batch);
    return;
  }

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    if (rng_.GetReal() <= drop_rate_) {
      DropPacket(ctx, pkt);
    } else {
      gate_idx_t gate = vosealias_.generate_sample(rng_.GetReal(), rng_.GetReal());
      EmitPacket(ctx, pkt, gates_[gate]);
    }
  }
}

ADD_MODULE(WeightedRandomSplit, "weighted_random_split", "pseudo-randomly splits/drops packets")
