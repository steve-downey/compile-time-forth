// src/smd/forth/interpreter/session.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_SESSION_HPP
#define SRC_SMD_FORTH_INTERPRETER_SESSION_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/interpreter/compilebuf.hpp>
#include <smd/forth/interpreter/interp.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <string_view>
#include <type_traits>
#include <variant>

namespace smd::forth::interpreter {

// Step F25 (docs/forth-plan-2.md), D15: "the artifact is a session image."
// compiled_forth<Source> (forth.hpp) is NOT retargeted to build one of these
// yet -- that is F26's own job, once the R1 pipeline it currently builds is
// deleted. This header exists so F26 has something to retarget onto, and so
// this step's own merge criterion (a session round-trips: build at
// constexpr, run its top-level again at runtime from the same object) has
// something to build and run.

/// D15's session image: everything a session needs to run one of its own
/// top-level definitions again, once the constant-expression evaluation that
/// built it is over. Every field is a flat, capacity-parameterized,
/// trivially destructible type (D3); the struct itself is a plain
/// aggregate, so a @ref session built inside one constexpr evaluation is a
/// literal value that survives, unmodified, into ordinary runtime code
/// (D15's own "trivially copyable literal") -- a session cannot span
/// constant-expression evaluations (D15), but nothing stops the *result* of
/// one evaluation from being copied into another.
///
/// @tparam MaxCode  @ref code's own instruction-array capacity.
/// @tparam MaxWords @ref code's word-table capacity and @ref dictionary's
///                  entry capacity, shared (mirrors @ref machine::
///                  compiled_program's own existing convention).
/// @tparam MaxData  The data space capacity a live @ref machine::forth_state
///                  seeded from this session (@ref seed_from_session) needs
///                  to be constructed with.
/// @tparam MaxOut   @ref output's own capacity.
/// @tparam MaxName  Maximum word-name length, shared with @ref dictionary.
template <int MaxCode = 4096, int MaxWords = 256, int MaxData = 1024,
          int MaxOut = 256, int MaxName = 32>
struct session {
    /// The code space every `:` ... `;` pair the session compiled appended
    /// to (D15's "code space").
    compile_buffer<MaxCode, MaxWords> code{};

    /// Every word the session's own dictionary knows about by the time it
    /// finished building -- the seeded primitives plus every colon
    /// definition (D15's "the final dictionary").
    machine::dictionary<MaxWords, MaxName> dictionary{};

    /// Cells the session's own data space had allotted (`VARIABLE`/`ALLOT`)
    /// by the time it finished building (D15's "the data-space high-water
    /// mark", F16's seeding discipline generalized from a single batch
    /// compiled_program to a whole session).
    int data_space_high_water = 0;

    /// The output the session accumulated while it was built (D15's
    /// "captured output").
    foundation::static_vector<char, MaxOut> output{};
};

namespace detail {

static_assert(std::is_trivially_destructible_v<session<64, 32, 256, 128>>);

} // namespace detail

/// Seeds @p state's own data space from @p sess's own @ref session::
/// data_space_high_water (F16's seeding discipline, generalized from a
/// single batch @ref machine::compiled_program to a whole @ref session) --
/// so a freshly constructed @p state can safely fetch/store through any
/// address a `VARIABLE`/`CREATE` the session defined baked in as a literal,
/// exactly as @ref machine::run already does for a single compiled program.
template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxDepth, int MaxRDepth>
constexpr auto seed_from_session(
    session<MaxCode, MaxWords, MaxData, MaxOut, MaxName> const &sess,
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state)
    -> machine::status {
    auto r = state.data_space().allot(sess.data_space_high_water);
    if (!r.has_value()) {
        return r.error();
    }
    return std::monostate{};
}

// b4d8e2a6-7c1f-4e3a-9d5b-2a8f6c1e4d9b
/// Runs one of @p sess's own defined words again (D14/D15: "run its
/// top-level again"), against @p state, by name.
///
/// This is the operation a session image exists to make possible once the
/// constant evaluation that built @p sess is over: @p sess is a trivially
/// copyable literal (this header's own doc comment), so the exact object a
/// constexpr build produced can be handed to ordinary runtime code and one
/// of its own colon definitions invoked again from there, via the same VM
/// (@ref call_word) that ran it the first time (D14's "one semantics").
///
/// @param sess        The session to run a word from; @ref session::code's
///                     own @ref compile_buffer::program is mutated in place
///                     (its `program_entry` field, per @ref call_word) --
///                     copy @p sess first if the original must stay
///                     unmodified.
/// @param state       The live machine state to run against.
/// @param name        The word's name (case-insensitive, per @ref
///                     machine::dictionary::lookup).
/// @param fuel        The VM's own execution step budget.
template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxDepth, int MaxRDepth>
constexpr auto call_defined_word(
    session<MaxCode, MaxWords, MaxData, MaxOut, MaxName> &sess,
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state,
    std::string_view name, int fuel = 100000) -> machine::status {
    auto const *entry = sess.dictionary.lookup(name);
    if (entry == nullptr) {
        return foundation::parse_error{foundation::source_pos{},
                                       "word not found in session dictionary"};
    }
    auto const *cw = std::get_if<machine::compiled_colon_word>(&entry->binding);
    if (cw == nullptr) {
        return foundation::parse_error{
            foundation::source_pos{},
            "word is not a compiled colon word in this session"};
    }
    return call_word(sess.code, state, cw->entry_point, fuel);
}
// b4d8e2a6-7c1f-4e3a-9d5b-2a8f6c1e4d9b end

// c6e9f1b3-8a2d-4c7e-9f1a-3b6d8e2c5a7f
/// Builds a @ref session by interpreting @p text from a fresh @ref
/// forth_state seeded with @ref machine::default_dictionary (D15: "a session
/// cannot span constant-expression evaluations" -- this function is the one
/// boundary a session is ever built across, called once, wholesale, whether
/// at constexpr or ordinary runtime).
///
/// @tparam MaxDepth  The transient build-time @ref forth_state's data stack
///                    capacity; not part of the returned @ref session (only
///                    its final dictionary/code/data-space/output survive).
/// @tparam MaxRDepth Likewise, the build-time return stack capacity.
template <int MaxCode = 4096, int MaxWords = 256, int MaxData = 1024,
          int MaxOut = 256, int MaxName = 32, int MaxDepth = 64,
          int MaxRDepth = 64>
constexpr auto build_session(std::string_view text, int fuel = 100000)
    -> foundation::result<
        session<MaxCode, MaxWords, MaxData, MaxOut, MaxName>> {
    forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName> st{text};
    auto dict = machine::default_dictionary<MaxWords, MaxName>();
    compile_buffer<MaxCode, MaxWords> buf;

    auto r = interpret(st, dict, buf, fuel);
    if (!r.has_value()) {
        return r.error();
    }

    return session<MaxCode, MaxWords, MaxData, MaxOut, MaxName>{
        .code = buf,
        .dictionary = dict,
        .data_space_high_water = st.machine().data_space().size(),
        .output = st.machine().output(),
    };
}
// c6e9f1b3-8a2d-4c7e-9f1a-3b6d8e2c5a7f end

} // namespace smd::forth::interpreter

#endif
