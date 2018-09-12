// Copyright (c) 2016-2018, Nefeli Networks, Inc.
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

#include "dpdkacl.h"

#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/udp.h"

static const struct rte_acl_field_def ipv4_defs[5] = {
  {
    .type = RTE_ACL_FIELD_TYPE_BITMASK,
    .size = sizeof(uint8_t),
    .field_index = 0,
    .input_index = 0,
    .offset = offsetof(struct match_headers, proto),
  },
  {
    .type = RTE_ACL_FIELD_TYPE_MASK,
    .size = sizeof(uint32_t),
    .field_index = 1,
    .input_index = 1,
    .offset = offsetof(struct match_headers, ipv4_src),
  },
  {
    .type = RTE_ACL_FIELD_TYPE_MASK,
    .size = sizeof(uint32_t),
    .field_index = 2,
    .input_index = 2,
    .offset = offsetof(struct match_headers, ipv4_dst),
  },
  {
    .type = RTE_ACL_FIELD_TYPE_RANGE,
    .size = sizeof(uint16_t),
    .field_index = 3,
    .input_index = 3,
    .offset = offsetof(struct match_headers, src_port),
  },
  {
    .type = RTE_ACL_FIELD_TYPE_RANGE,
    .size = sizeof(uint16_t),
    .field_index = 4,
    .input_index = 3,
    .offset = offsetof(struct match_headers, dst_port),
  },
};

RTE_ACL_RULE_DEF(ipv4_rule, RTE_DIM(ipv4_defs));

const Commands DPDKACL::cmds = {
    {"add", "DPDKACLArg", MODULE_CMD_FUNC(&DPDKACL::CommandAdd),
     Command::THREAD_UNSAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&DPDKACL::CommandClear),
     Command::THREAD_UNSAFE}};

DPDKACL::DPDKACL() : Module() {
  max_allowed_workers_ = Worker::kMaxWorkers;
  curr_prio = RTE_ACL_MAX_PRIORITY;
  buffer = new struct match_headers[bess::PacketBatch::kMaxBurst * sizeof(struct match_headers)];
  results = new uint32_t[bess::PacketBatch::kMaxBurst];
  ptrs = new struct match_headers*[bess::PacketBatch::kMaxBurst];
  for(size_t i = 0; i < bess::PacketBatch::kMaxBurst; ++i) {
    ptrs[i] = &(buffer[i]);
  }

  struct rte_acl_param prm = {
    .name = "DPDK ACL",
    .socket_id = SOCKET_ID_ANY,
    .rule_size = RTE_ACL_RULE_SZ(RTE_DIM(ipv4_defs)),
    .max_rule_num = MAX_RULES, /* maximum number of rules in the AC context. */
  };
  aclctx = rte_acl_create(&prm);
  if(aclctx == nullptr) {
    throw CommandFailure(rte_errno, "Failed to create ACL context");
  }
}

DPDKACL::~DPDKACL() {
  rte_acl_free(aclctx);
  delete[] ptrs;
  delete[] results;
  delete[] buffer;
}

CommandResponse DPDKACL::Init(const dpdkacl::pb::DPDKACLArg &arg) {
  return CommandAdd(arg);
}

//CommandResponse __attribute__((optimize("O0"))) DPDKACL::CommandAdd(const dpdkacl::pb::DPDKACLArg &arg) {
CommandResponse DPDKACL::CommandAdd(const dpdkacl::pb::DPDKACLArg &arg) {
  int ret;
  for (const auto &rule : arg.rules()) {
    --curr_prio;
    if(curr_prio < RTE_ACL_MIN_PRIORITY) {
      return CommandFailure(EINVAL, "Too much firewall rules added");
    }
    struct ipv4_rule acl_rule;
    memset(&acl_rule, 0, sizeof(struct ipv4_rule));
    acl_rule.data = {
      .category_mask = 1,
      .priority = curr_prio,
      .userdata = (uint32_t)(rule.drop() == false ? 1 : 0)
    };

    Ipv4Prefix sip = Ipv4Prefix(rule.src_ip());
    Ipv4Prefix dip = Ipv4Prefix(rule.dst_ip());
    uint16_t src_port = rule.src_port();
    uint16_t dst_port = rule.dst_port();
    // IP proto
    acl_rule.field[0].value.u8 = rule.ipproto();
    // IP proto "mask"
    acl_rule.field[0].mask_range.u8 = rule.ipproto() == 0 ? 0x00 : 0xff;
    // source IP
    acl_rule.field[1].value.u32 = sip.addr.value();
    // source IP mask
    acl_rule.field[1].mask_range.u32 = sip.prefix_length();
    // destination IP
    acl_rule.field[2].value.u32 = dip.addr.value();
    // destination IP mask
    acl_rule.field[2].mask_range.u32 = dip.prefix_length();
    // source port
    acl_rule.field[3].value.u16 = src_port;
    // source port "mask"
    acl_rule.field[3].mask_range.u16 = src_port == 0 ? 0xffff : 0x0000;
    // destination port
    acl_rule.field[4].value.u16 = dst_port;
    // destination port "mask"
    acl_rule.field[4].mask_range.u16 = dst_port == 0 ? 0xffff : 0x0000;

    ret = rte_acl_add_rules(aclctx, (const struct rte_acl_rule*) &acl_rule, 1);
    if(ret != 0) {
      return CommandFailure(-1 * ret, "Failed to add rules");
    }
  }
  struct rte_acl_config cfg;
  cfg.num_categories = 1;
  cfg.num_fields = RTE_DIM(ipv4_defs);
  cfg.max_size = UINT32_MAX;
  memcpy(cfg.defs, ipv4_defs, sizeof(ipv4_defs));
  ret = rte_acl_build(aclctx, &cfg);
  if(ret != 0) {
    return CommandFailure(-1 * ret, "Failed to build internal ACL structures");
  }
  return CommandSuccess();
}

CommandResponse DPDKACL::CommandClear(const bess::pb::EmptyArg &) {
  rte_acl_reset_rules(aclctx);
  return CommandSuccess();
}

void DPDKACL::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;

  gate_idx_t incoming_gate = ctx->current_igate;

  int cnt = batch->cnt();
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
    size_t ip_bytes = ip->header_length << 2;
    Udp *udp =
        reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ip_bytes);
    buffer[i].proto = ip->protocol;
    buffer[i].ipv4_src = ip->src.raw_value();
    buffer[i].ipv4_dst = ip->dst.raw_value();
    buffer[i].src_port = udp->src_port.raw_value();
    buffer[i].dst_port = udp->dst_port.raw_value();
  }

  if(rte_acl_classify(aclctx, (const uint8_t**)ptrs, results, cnt, 1) != 0) {
    throw CommandFailure(EINVAL, "Failed to process packets");
  }

  for(int i = 0; i < cnt; ++i) {
    if(results[i] != 0) {
      EmitPacket(ctx, batch->pkts()[i], incoming_gate);
    } else {
      DropPacket(ctx, batch->pkts()[i]);
    }
  }
}

ADD_MODULE(DPDKACL, "dpdkacl", "ACL module using DPDK library")
