// src/smd/forth/reader/syntax_tree.hpp                              -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_READER_SYNTAX_TREE_HPP
#define SRC_SMD_FORTH_READER_SYNTAX_TREE_HPP

#include <smd/forth/foundation/arena_box.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/foundation/static_vector.hpp>

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <variant>

namespace smd::forth::reader {

// Forward declaration only: the composite node kinds below need to name a
// handle to a sibling/child node before the closed node type is fully
// defined. This works because foundation::arena_box<T, MaxNodes> never
// requires T to be complete -- it stores only an integer index -- so the
// forward declaration is all any of them need. There is no fix/Box open
// recursion here (D3): the tree is one closed node type (syn_node) stored
// flat in a single foundation::tree_arena and addressed by these handles.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
struct syn_node;

/// Handle to a @ref syn_node stored in a syntax-tree arena.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
using syn_box =
    foundation::arena_box<syn_node<MaxNodes, MaxBody, MaxName>, MaxNodes>;

/// A sequence of child-node handles -- the shape every node "body" uses.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
using syn_body =
    foundation::static_vector<syn_box<MaxNodes, MaxBody, MaxName>, MaxBody>;

/// Storage for a word or definition name.
///
/// Names are stored already case-folded to uppercase: F5's lexer folds case
/// at scan time (see `handoff.md`), so nothing downstream of the syntax tree
/// ever needs to fold again.
template <int MaxName = 32>
using syn_name = foundation::static_vector<char, MaxName>;

// e74ee0de-460c-41da-aeb4-23235b52df77
/// A literal integer pushed on the stack, e.g. `42`.
struct syn_literal {
    std::int64_t cell{};          ///< The literal value.
    foundation::source_pos pos{}; ///< Where the literal appears in source.
};

/// A bare word reference, e.g. `DUP` or `+`.
///
/// Resolution against the dictionary happens in elaboration (F11), not here;
/// at this stage a word is just its (already case-folded) spelling.
template <int MaxName = 32>
struct syn_word {
    syn_name<MaxName> name{};     ///< Case-folded word name.
    foundation::source_pos pos{}; ///< Where the word appears in source.
};
// e74ee0de-460c-41da-aeb4-23235b52df77 end

// e701a3a8-aeff-4b78-9dd5-df3b16fcb6b8
/// A colon definition: `: NAME ... ;`, optionally preceded by a `( ... )`
/// stack-effect comment immediately after the name.
///
/// @tparam MaxNodes Arena capacity shared by every handle in the tree.
/// @tparam MaxBody  Maximum number of body forms.
/// @tparam MaxName  Maximum name length.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
struct syn_colon_def {
    syn_name<MaxName> name{}; ///< The defined word's name.
    /// The raw `( ... )` stack-effect comment span, or an empty span
    /// (`first == last`, the default) when none was written.
    /// F7's grammar captures the span; F12's stack-effect checker re-slices
    /// the original source text through it when it needs the declared
    /// effect text (e.g. `( a b -- c )`).
    foundation::source_span declared_effect{};
    syn_body<MaxNodes, MaxBody, MaxName> body{}; ///< The definition's body.
    foundation::source_pos pos{}; ///< Where the `:` token appears.
};

/// An `IF ... [ELSE ...] THEN` conditional.
///
/// `else_body` is empty when there is no `ELSE` clause.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
struct syn_if {
    syn_body<MaxNodes, MaxBody, MaxName> then_body{}; ///< `IF` ... `[ELSE]`.
    syn_body<MaxNodes, MaxBody, MaxName> else_body{}; ///< `ELSE` ... `THEN`.
    foundation::source_pos pos{}; ///< Where the `IF` token appears.
};

/// A `BEGIN ... UNTIL` post-tested loop.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
struct syn_begin_until {
    syn_body<MaxNodes, MaxBody, MaxName> body{}; ///< `BEGIN` ... `UNTIL`.
    foundation::source_pos pos{}; ///< Where the `BEGIN` token appears.
};

/// A `BEGIN ... WHILE ... REPEAT` pre-tested loop.
///
/// `condition` runs first on every iteration; `body` runs only while
/// `condition` left a true flag, then control returns to `condition`.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
struct syn_begin_while {
    syn_body<MaxNodes, MaxBody, MaxName> condition{}; ///< `BEGIN` ... `WHILE`.
    syn_body<MaxNodes, MaxBody, MaxName> body{};      ///< `WHILE` ... `REPEAT`.
    foundation::source_pos pos{}; ///< Where the `BEGIN` token appears.
};

/// A `DO ... LOOP` or `DO ... +LOOP` counted loop.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
struct syn_do_loop {
    syn_body<MaxNodes, MaxBody, MaxName> body{}; ///< `DO` ... `LOOP`/`+LOOP`.
    bool is_plus_loop{};          ///< True for `+LOOP`, false for plain `LOOP`.
    foundation::source_pos pos{}; ///< Where the `DO` token appears.
};
// e701a3a8-aeff-4b78-9dd5-df3b16fcb6b8 end

// 5b6b1441-8050-442d-ac28-bdd32acb8615
/// A `VARIABLE NAME` declaration.
template <int MaxName = 32>
struct syn_variable {
    syn_name<MaxName> name{};     ///< The declared variable's name.
    foundation::source_pos pos{}; ///< Where the `VARIABLE` token appears.
};

/// A `CONSTANT NAME` declaration.
///
/// The constant's value is whatever the preceding body form(s) leave on the
/// stack, per Forth-2012; it is not duplicated into this node.
template <int MaxName = 32>
struct syn_constant {
    syn_name<MaxName> name{};     ///< The declared constant's name.
    foundation::source_pos pos{}; ///< Where the `CONSTANT` token appears.
};

/// A `CREATE NAME` declaration.
template <int MaxName = 32>
struct syn_create {
    syn_name<MaxName> name{};     ///< The declared name.
    foundation::source_pos pos{}; ///< Where the `CREATE` token appears.
};

/// A `' NAME` tick, taking the execution token of `NAME`.
template <int MaxName = 32>
struct syn_tick {
    syn_name<MaxName> name{};     ///< The ticked word's name.
    foundation::source_pos pos{}; ///< Where the `'` token appears.
};
// 5b6b1441-8050-442d-ac28-bdd32acb8615 end

// 0c3a46c6-f7a0-4c65-9798-bf6c73ae5967
/// The closed node type for the syntax tree.
///
/// One variant over every node kind, stored flat in a
/// @ref foundation::tree_arena and addressed by @ref syn_box handles (D3) --
/// there is no heap-backed `fix`/`Box` recursion anywhere in this type.
///
/// @tparam MaxNodes Arena capacity shared by every handle in the tree.
/// @tparam MaxBody  Maximum number of forms in any one node's body.
/// @tparam MaxName  Maximum length of any name stored in the tree.
template <int MaxNodes, int MaxBody, int MaxName>
struct syn_node {
    std::variant<syn_literal, syn_word<MaxName>,
                 syn_colon_def<MaxNodes, MaxBody, MaxName>,
                 syn_if<MaxNodes, MaxBody, MaxName>,
                 syn_begin_until<MaxNodes, MaxBody, MaxName>,
                 syn_begin_while<MaxNodes, MaxBody, MaxName>,
                 syn_do_loop<MaxNodes, MaxBody, MaxName>, syn_variable<MaxName>,
                 syn_constant<MaxName>, syn_create<MaxName>, syn_tick<MaxName>>
        value{};
};
// 0c3a46c6-f7a0-4c65-9798-bf6c73ae5967 end

/// An arena-backed syntax tree.
///
/// A single arena of @ref syn_node, plus the sequence of top-level forms (in
/// source order) that make up the program: colon definitions, `VARIABLE`,
/// `CONSTANT`, and `CREATE` declarations, and any top-level executable words.
///
/// @tparam MaxNodes Arena capacity.
/// @tparam MaxBody  Maximum body length for any one node.
/// @tparam MaxName  Maximum name length.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32>
struct syntax_tree {
    using node = syn_node<MaxNodes, MaxBody, MaxName>;
    using box = syn_box<MaxNodes, MaxBody, MaxName>;

    foundation::tree_arena<node, MaxNodes> arena{}; ///< Backing storage.
    syn_body<MaxNodes, MaxBody, MaxName> program{}; ///< Top-level forms.
};

/// Builds a @ref syn_name from an already case-folded name.
///
/// @pre text.size() <= MaxName
template <int MaxName = 32>
constexpr auto make_syn_name(std::string_view text) -> syn_name<MaxName> {
    syn_name<MaxName> result{};
    for (char c : text) {
        result.push_back(c);
    }
    return result;
}

/// Returns true if @p name spells exactly @p text.
template <int MaxName>
constexpr auto syn_name_equals(syn_name<MaxName> const &name,
                               std::string_view text) -> bool {
    if (name.size() != static_cast<int>(text.size())) {
        return false;
    }
    for (int i = 0; i < name.size(); ++i) {
        if (name[i] != text[static_cast<std::size_t>(i)]) {
            return false;
        }
    }
    return true;
}

namespace detail {

// Every node kind, and the closed node type itself, must be trivially
// destructible (D3): the tree lives in a flat tree_arena, never behind a
// destructor-owning heap type. Checked against one concrete instantiation
// (the header's own defaults) as a representative sample.
static_assert(std::is_trivially_destructible_v<syn_literal>);
static_assert(std::is_trivially_destructible_v<syn_word<>>);
static_assert(std::is_trivially_destructible_v<syn_colon_def<>>);
static_assert(std::is_trivially_destructible_v<syn_if<>>);
static_assert(std::is_trivially_destructible_v<syn_begin_until<>>);
static_assert(std::is_trivially_destructible_v<syn_begin_while<>>);
static_assert(std::is_trivially_destructible_v<syn_do_loop<>>);
static_assert(std::is_trivially_destructible_v<syn_variable<>>);
static_assert(std::is_trivially_destructible_v<syn_constant<>>);
static_assert(std::is_trivially_destructible_v<syn_create<>>);
static_assert(std::is_trivially_destructible_v<syn_tick<>>);
static_assert(std::is_trivially_destructible_v<syn_node<1024, 64, 32>>);

} // namespace detail

} // namespace smd::forth::reader

#endif
