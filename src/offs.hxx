#pragma once

#include <cstdint>

namespace offs {

constexpr std::uintptr_t cam_inst = 0x2a10198;    // qword_142A10198
constexpr std::uintptr_t cam_node = 0x2a101a0;    // qword_142A101A0
constexpr std::uintptr_t cls_tab = 0x2a149c0;     // qword_142A149C0
constexpr std::uintptr_t gs_cfg = 0x2a177b0;      // qword_142A177B0
constexpr std::uint32_t gs_players = 352;
constexpr std::uint32_t pl_base = 296;
constexpr std::uint32_t pl_cur = 1592;
constexpr std::uint32_t pl_stride = 336;
constexpr std::uint32_t pl_go = 288;

constexpr std::uint32_t cls_char = 4006;    // fish included

constexpr std::uint32_t cls_lock = 504;
constexpr std::uint32_t cls_depth = 508;
constexpr std::uint32_t cls_buckets = 512;
constexpr std::uint32_t cls_nbuckets = 520;

constexpr std::uint32_t go_flags = 0;
constexpr std::uint32_t go_cls = 4;
constexpr std::uint32_t go_next = 24;
constexpr std::uint32_t go_name = 32;
constexpr std::uint32_t go_inst = 48;
constexpr std::uint32_t go_loaded = 0x200000;
constexpr std::uint32_t go_live = 0x20000000;

constexpr std::uint32_t ch_body = 1480;
constexpr std::uint32_t ch_head = 2264;

constexpr std::uint32_t en_node = 328;

constexpr std::uint32_t nd_world = 0x80;
constexpr std::uint32_t nd_pos = 0xb0;
constexpr std::uint32_t nd_pose = 344;

constexpr std::uint32_t ps_nbones = 160;
constexpr std::uint32_t ps_bones = 216;
constexpr std::uint32_t ps_parents = 248;
constexpr std::uint32_t ps_desc = 272;
constexpr std::uint32_t bone_pos = 48;

constexpr std::uint32_t cm_vfov = 0x19c;
constexpr std::uint32_t cm_aspect = 0x1a4;

constexpr std::uintptr_t run_file = 0x55fca0;     // sub_14055FCA0
constexpr std::uintptr_t run_buf = 0x55f8d0;      // sub_14055F8D0
constexpr std::uintptr_t idle_slot = 0x1c74138;   // CWinApp vtable, OnIdle
constexpr std::uintptr_t idle_fn = 0x1352b0;      // sub_1401352B0
constexpr std::uint32_t vm_ctx = 1752;
constexpr std::uint32_t ctx_state = 88;
constexpr std::uintptr_t vm = 0x2a14978;          // qword_142A14978
constexpr std::uint32_t vm_nstates = 84;
constexpr std::uint32_t vm_state = 88;

constexpr std::uint32_t ls_gt = 120;
constexpr std::uint32_t tb_lsizenode = 11;
constexpr std::uint32_t tb_node = 32;
constexpr std::uint32_t ts_len = 16;
constexpr std::uint32_t ts_chars = 24;

}
