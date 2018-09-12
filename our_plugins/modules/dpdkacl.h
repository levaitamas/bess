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

#ifndef BESS_MODULES_DPDKACL_H_
#define BESS_MODULES_DPDKACL_H_

#include <rte_config.h>
#include <rte_acl.h>

#include "../module.h"
#include "../utils/ip.h"
#include "../pb/dpdkacl_msg.pb.h"
#include "../pb/module_msg.pb.h"

#define MAX_RULES 10239

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::Ipv4Prefix;

struct match_headers {
  uint8_t  proto;
  uint32_t ipv4_src;
  uint32_t ipv4_dst;
  uint16_t src_port;
  uint16_t dst_port;
} __attribute__((packed));

class DPDKACL final : public Module {
 public:
  static const Commands cmds;
  
  DPDKACL();
  ~DPDKACL();

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  CommandResponse Init(const dpdkacl::pb::DPDKACLArg &arg);
  CommandResponse CommandAdd(const dpdkacl::pb::DPDKACLArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  struct rte_acl_ctx* aclctx;
  int32_t curr_prio;
  struct match_headers* buffer;
  struct match_headers** ptrs;
  uint32_t* results;
};

#endif  // BESS_MODULES_DPDKACL_H_
