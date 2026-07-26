// src/smd/forth/machine/emit.hpp                                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_EMIT_HPP
#define SRC_SMD_FORTH_MACHINE_EMIT_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/instruction.hpp>

namespace smd::forth::machine {

// Step F25 (docs/forth-plan-2.md): a relocation, not a rewrite (D16 -- "the
// emission utilities ... survive as the compiling words' toolkit"). @ref emit
// itself is unchanged from step F14's own codegen.hpp, byte for byte; moving
// it here only gives it a home neither R1's batch codegen nor F25's own
// one-`:`-at-a-time colon compiler (interpreter::compile_buffer,
// interpreter/compilebuf.hpp) has to own exclusively. codegen.hpp now
// includes this header instead of defining @ref emit itself, so the R1
// pipeline's own behavior is unchanged (still builds and passes until F26's
// cut deletes it).

// c2f6a5d1-3b9e-4a7c-8d2f-6e1b9c4a7f3d
/// Appends @p code/@p operand to @p out's instruction array, diagnosing
/// overflow rather than exceeding @p out's own @c MaxCode capacity.
/// Returns the newly appended instruction's own index -- callers that will
/// need to back-patch this instruction's operand later (an `IF`/`BEGIN`'s
/// own forward branch) keep this index.
template <int MaxCode, int MaxWords>
constexpr auto emit(compiled_program<MaxCode, MaxWords> &out, op code,
                    cell operand, foundation::source_pos pos)
    -> foundation::result<int> {
    if (out.code.size() >= MaxCode) {
        return foundation::parse_error{pos, "compiled program exceeds MaxCode "
                                            "capacity"};
    }
    int const index = out.code.size();
    out.code.push_back(instr{.code = code, .operand = operand});
    return index;
}
// c2f6a5d1-3b9e-4a7c-8d2f-6e1b9c4a7f3d end

namespace detail {

// Merge criterion (static_assert, immediately-invoked-lambda pattern): emit
// appends in order, returns the appended index, and diagnoses MaxCode
// overflow rather than exceeding capacity.

static_assert([] {
    compiled_program<4, 4> out{};
    auto r0 = emit(out, op::push, cell{1}, foundation::source_pos{});
    auto r1 = emit(out, op::push, cell{2}, foundation::source_pos{});
    return r0.has_value() && r0.value() == 0 && r1.has_value() &&
           r1.value() == 1 && out.code.size() == 2 &&
           out.code[0].code == op::push && out.code[0].operand == 1 &&
           out.code[1].code == op::push && out.code[1].operand == 2;
}());

static_assert([] {
    compiled_program<1, 4> out{};
    auto r0 = emit(out, op::halt, cell{0}, foundation::source_pos{});
    auto r1 = emit(out, op::halt, cell{0}, foundation::source_pos{});
    return r0.has_value() && !r1.has_value();
}());

} // namespace detail

} // namespace smd::forth::machine

#endif
