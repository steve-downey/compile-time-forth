// src/smd/forth/elaborator/elaborated_core.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_ELABORATOR_ELABORATED_CORE_HPP
#define SRC_SMD_FORTH_ELABORATOR_ELABORATED_CORE_HPP

#include <smd/forth/foundation/arena_box.hpp>
#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/data_space.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <type_traits>
#include <variant>

namespace smd::forth::elaborator {

// Forward declaration only, exactly like reader::syn_node (F6): the composite
// node kinds below (core_if, core_begin_until, core_begin_while, core_do_loop,
// core_seq) need to name a handle to a child node before the closed node type
// is fully defined. This works because foundation::arena_box<T, MaxNodes>
// never requires T to be complete -- it stores only an integer index -- so no
// fix/Box open recursion is needed here either (D3): the elaborated core is
// one closed node type (core_node) stored flat in a single
// foundation::tree_arena and addressed by these handles.
template <int MaxNodes = 1024, int MaxBody = 64>
struct core_node;

/// Handle to a @ref core_node stored in a core-tree arena.
template <int MaxNodes = 1024, int MaxBody = 64>
using core_box = foundation::arena_box<core_node<MaxNodes, MaxBody>, MaxNodes>;

/// A sequence of child-node handles -- the shape every node "body" uses.
template <int MaxNodes = 1024, int MaxBody = 64>
using core_body =
    foundation::static_vector<core_box<MaxNodes, MaxBody>, MaxBody>;

// 8a9a3c73-7150-4019-97f5-0485af9c56bb
/// Pushes a literal integer, e.g. the elaborated form of a `syn_literal`.
struct core_push {
    machine::cell value{};        ///< The literal value.
    foundation::source_pos pos{}; ///< Where the literal appeared in source.
};

/// Invokes a primitive opcode directly -- no dictionary indirection needed at
/// run time, since the 46 primitives never change identity once elaborated.
struct core_prim {
    machine::primitive op{};      ///< Which primitive to run.
    foundation::source_pos pos{}; ///< Where the reference appeared in source.
};

/// Calls a colon-defined word by dictionary index.
///
/// @ref word_index is resolved once, at elaboration time, against whatever
/// the dictionary looked like at the calling word's own position (static
/// binding); a later redefinition of the same name does not retroactively
/// change an already-elaborated `core_call`. `RECURSE` resolves to the
/// definition currently being compiled, using this same node kind.
struct core_call {
    int word_index = -1;          ///< Index into the dictionary.
    foundation::source_pos pos{}; ///< Where the reference appeared in source.
};

/// Pushes the data-space address of a `VARIABLE`- or `CREATE`-defined word.
struct core_var {
    machine::addr address{};      ///< The variable's data-space address.
    foundation::source_pos pos{}; ///< Where the reference appeared in source.
};

/// Pushes a `CONSTANT`'s fixed value, resolved once at elaboration time.
struct core_const {
    machine::cell value{};        ///< The constant's value.
    foundation::source_pos pos{}; ///< Where the reference appeared in source.
};

/// Pushes the execution token (dictionary index) of a word, e.g. the
/// elaborated form of `' NAME`. Unlike @ref core_call, this resolves to any
/// binding kind -- primitive, colon word, variable, constant, or foreign --
/// since `EXECUTE` (F18a) is what later decides what to do with the token.
struct core_push_xt {
    int word_index = -1;          ///< Index into the dictionary.
    foundation::source_pos pos{}; ///< Where the tick appeared in source.
};

/// Returns from the definition currently executing -- the elaborated form of
/// a bare `EXIT` word inside a colon-definition body.
struct core_exit {
    foundation::source_pos pos{}; ///< Where the `EXIT` appeared in source.
};
// 8a9a3c73-7150-4019-97f5-0485af9c56bb end

// 36591b76-b56e-4425-a5c6-a6f32dda00ae
/// An elaborated `IF ... [ELSE ...] THEN` conditional.
///
/// `else_body` is empty when there was no `ELSE` clause.
template <int MaxNodes = 1024, int MaxBody = 64>
struct core_if {
    core_body<MaxNodes, MaxBody> then_body{}; ///< `IF` ... `[ELSE]`.
    core_body<MaxNodes, MaxBody> else_body{}; ///< `ELSE` ... `THEN`.
    foundation::source_pos pos{}; ///< Where the `IF` token appeared.
};

/// An elaborated `BEGIN ... UNTIL` post-tested loop.
template <int MaxNodes = 1024, int MaxBody = 64>
struct core_begin_until {
    core_body<MaxNodes, MaxBody> body{}; ///< `BEGIN` ... `UNTIL`.
    foundation::source_pos pos{};        ///< Where the `BEGIN` token appeared.
};

/// An elaborated `BEGIN ... WHILE ... REPEAT` pre-tested loop.
template <int MaxNodes = 1024, int MaxBody = 64>
struct core_begin_while {
    core_body<MaxNodes, MaxBody> condition{}; ///< `BEGIN` ... `WHILE`.
    core_body<MaxNodes, MaxBody> body{};      ///< `WHILE` ... `REPEAT`.
    foundation::source_pos pos{}; ///< Where the `BEGIN` token appeared.
};

/// An elaborated `DO ... LOOP` or `DO ... +LOOP` counted loop.
template <int MaxNodes = 1024, int MaxBody = 64>
struct core_do_loop {
    core_body<MaxNodes, MaxBody> body{}; ///< `DO` ... `LOOP`/`+LOOP`.
    bool is_plus_loop{};          ///< True for `+LOOP`, false for plain `LOOP`.
    foundation::source_pos pos{}; ///< Where the `DO` token appeared.
};

/// A sequence of elaborated items -- what a colon definition's body becomes.
///
/// `machine::colon_word::core_id` is the arena index of one of these: the
/// only way to give a bare `int` handle (F9 could not name a typed handle to
/// a type it did not know yet) somewhere to point at.
template <int MaxNodes = 1024, int MaxBody = 64>
struct core_seq {
    core_body<MaxNodes, MaxBody> items{}; ///< The sequence's elaborated items.
    foundation::source_pos pos{}; ///< Where the sequence's first form began.
};
// 36591b76-b56e-4425-a5c6-a6f32dda00ae end

// 6100f8d6-3f92-47ce-b4e2-d4137a801740
/// The closed node type for the elaborated core.
///
/// One variant over every node kind, stored flat in a
/// @ref foundation::tree_arena and addressed by @ref core_box handles (D3) --
/// there is no heap-backed `fix`/`Box` recursion anywhere in this type,
/// exactly like @ref smd::forth::reader::syn_node.
///
/// @tparam MaxNodes Arena capacity shared by every handle in the tree.
/// @tparam MaxBody  Maximum number of forms in any one node's body.
template <int MaxNodes, int MaxBody>
struct core_node {
    std::variant<core_push, core_prim, core_call, core_var, core_const,
                 core_push_xt, core_exit, core_if<MaxNodes, MaxBody>,
                 core_begin_until<MaxNodes, MaxBody>,
                 core_begin_while<MaxNodes, MaxBody>,
                 core_do_loop<MaxNodes, MaxBody>, core_seq<MaxNodes, MaxBody>>
        value{};
};
// 6100f8d6-3f92-47ce-b4e2-d4137a801740 end

/// A collected, non-fatal diagnostic -- currently only "word redefined" --
/// produced alongside a successful @ref compiled_unit.
///
/// Reuses @ref foundation::parse_error's shape (a source position plus a
/// static message) rather than inventing a second identical struct: the only
/// difference between a warning and an error is which channel it travels
/// through, not its shape.
template <int MaxWarnings = 64>
using warning_log =
    foundation::static_vector<foundation::parse_error, MaxWarnings>;

// 41bb0434-f952-4ad7-b913-cc70dd06d0c5
/// The result of elaborating a whole program (F11): the elaborated-core
/// arena, the dictionary as it stands after every definition has been
/// processed, the data space consumed by `VARIABLE`/`CREATE` declarations,
/// the top-level executable body (in source order), and any collected
/// warnings (currently only redefinition notices).
///
/// @tparam MaxNodes    Elaborated-core arena capacity.
/// @tparam MaxBody     Maximum number of forms in any one node's body, and of
///                     top-level items in the program.
/// @tparam MaxName     Maximum dictionary-entry name length.
/// @tparam MaxWords    Maximum number of dictionary entries.
/// @tparam MaxData     Maximum number of data-space cells.
/// @tparam MaxWarnings Maximum number of collected warnings.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32,
          int MaxWords = 256, int MaxData = 1024, int MaxWarnings = 64>
struct compiled_unit {
    using node = core_node<MaxNodes, MaxBody>;
    using box = core_box<MaxNodes, MaxBody>;

    foundation::tree_arena<node, MaxNodes> arena{};      ///< Backing storage.
    machine::dictionary<MaxWords, MaxName> dictionary{}; ///< Resolved words.
    machine::data_space<MaxData> data_space{}; ///< `VARIABLE`/`CREATE` cells.
    core_body<MaxNodes, MaxBody> program{};    ///< Top-level executable body.
    warning_log<MaxWarnings> warnings{};       ///< Collected non-fatal notices.
};
// 41bb0434-f952-4ad7-b913-cc70dd06d0c5 end

namespace detail {

// Every node kind, and the closed node type itself, must be trivially
// destructible (D3): the tree lives in a flat tree_arena, never behind a
// destructor-owning heap type. Checked against one concrete instantiation
// (the header's own defaults) as a representative sample.
static_assert(std::is_trivially_destructible_v<core_push>);
static_assert(std::is_trivially_destructible_v<core_prim>);
static_assert(std::is_trivially_destructible_v<core_call>);
static_assert(std::is_trivially_destructible_v<core_var>);
static_assert(std::is_trivially_destructible_v<core_const>);
static_assert(std::is_trivially_destructible_v<core_push_xt>);
static_assert(std::is_trivially_destructible_v<core_exit>);
static_assert(std::is_trivially_destructible_v<core_if<>>);
static_assert(std::is_trivially_destructible_v<core_begin_until<>>);
static_assert(std::is_trivially_destructible_v<core_begin_while<>>);
static_assert(std::is_trivially_destructible_v<core_do_loop<>>);
static_assert(std::is_trivially_destructible_v<core_seq<>>);
static_assert(std::is_trivially_destructible_v<core_node<1024, 64>>);
static_assert(std::is_trivially_destructible_v<compiled_unit<>>);

} // namespace detail

} // namespace smd::forth::elaborator

#endif
