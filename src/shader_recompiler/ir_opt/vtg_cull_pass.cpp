// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <unordered_map>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/value.h"

#include "shader_recompiler/ir_opt/passes.h"

namespace Shader::Optimization {

// NVIDIA's driver appends a primitive-cull epilogue to vertex programs (VOTE.VTG + EXIT FCSM_TR)
// that discards primitives whose vertices fail the frustum/w tests, including vertices with
// NaN/Inf coordinates. The flow test cannot be evaluated on the host, so such primitives would be
// rasterized as garbage (giant streaks) and can hang mobile GPUs. Emulate the cull at the position
// stores: a vertex with w <= 0 (or NaN) or NaN in x/y/z gets a position that clipping discards.
void VtgCullPass(Environment& env, IR::Program& program) {
    if (!env.vtg_cull_epilogue) {
        return;
    }
    LOG_WARNING(Shader, "VtgCullPass: stage={} blocks={}", static_cast<int>(program.stage), program.post_order_blocks.size());
    if (program.stage != Stage::VertexA && program.stage != Stage::VertexB) {
        return;
    }
    for (IR::Block* const block : program.post_order_blocks) {
        std::array<IR::Inst*, 4> stores{};
        std::unordered_map<const IR::Inst*, size_t> order;
        size_t index{0};
        IR::Inst* first_store{nullptr};
        for (IR::Inst& inst : block->Instructions()) {
            order.emplace(&inst, index++);
            if (inst.GetOpcode() != IR::Opcode::SetAttribute) {
                continue;
            }
            const IR::Attribute attr{inst.Arg(0).Attribute()};
            if (attr < IR::Attribute::PositionX || attr > IR::Attribute::PositionW) {
                continue;
            }
            const size_t component{static_cast<size_t>(attr) -
                                   static_cast<size_t>(IR::Attribute::PositionX)};
            if (stores[component] == nullptr && first_store == nullptr) {
                first_store = &inst;
            }
            stores[component] = &inst;
        }
        if (first_store == nullptr || stores[3] == nullptr) {
            if (first_store != nullptr) {
                LOG_WARNING(Shader, "VtgCullPass: block has position stores but no W store");
            }
            continue;
        }
        const size_t first_index{order.at(first_store)};
        const auto defined_before_first{[&](const IR::Value& value) {
            if (value.IsImmediate()) {
                return true;
            }
            // Values defined in other blocks dominate the stores that already use them.
            const auto it{order.find(value.Inst())};
            return it == order.end() || it->second < first_index;
        }};
        const IR::Value w_value{stores[3]->Arg(1).Resolve()};
        if (!defined_before_first(w_value)) {
            LOG_WARNING(Shader, "VtgCullPass: W value not defined before first position store (imm={})", w_value.IsImmediate());
            continue;
        }
        LOG_WARNING(Shader, "VtgCullPass: applying cull to position stores");
        IR::IREmitter ir{*block, IR::Block::InstructionList::s_iterator_to(*first_store)};
        IR::U1 cull{ir.LogicalNot(ir.FPGreaterThan(IR::F32{w_value}, ir.Imm32(0.0f)))};
        for (size_t component = 0; component < 3; ++component) {
            if (stores[component] == nullptr) {
                continue;
            }
            const IR::Value value{stores[component]->Arg(1).Resolve()};
            if (!defined_before_first(value)) {
                continue;
            }
            cull = ir.LogicalOr(cull, ir.FPIsNan(IR::F32{value}));
        }
        for (size_t component = 0; component < 4; ++component) {
            IR::Inst* const store{stores[component]};
            if (store == nullptr) {
                continue;
            }
            const IR::Value value{store->Arg(1).Resolve()};
            if (!defined_before_first(value)) {
                continue;
            }
            const f32 culled_value{component == 3 ? -1.0f : 0.0f};
            store->SetArg(1, ir.Select(cull, ir.Imm32(culled_value), IR::F32{value}));
        }
    }
}

} // namespace Shader::Optimization
