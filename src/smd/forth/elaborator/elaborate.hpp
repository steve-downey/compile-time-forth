// src/smd/forth/elaborator/elaborate.hpp                             -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_ELABORATOR_ELABORATE_HPP
#define SRC_SMD_FORTH_ELABORATOR_ELABORATE_HPP

#include <smd/forth/elaborator/elaborated_core.hpp>
#include <smd/forth/foundation/arena_box.hpp>
#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/data_space.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/reader/syntax_tree.hpp>

#include <string_view>
#include <type_traits>
#include <variant>

namespace smd::forth::elaborator {

// Elaboration walks the syntax tree in program order, threading a
// machine::dictionary forward as it goes (D9): a colon definition's body
// resolves against whatever the dictionary looks like at the point that
// definition is elaborated, so a later redefinition never retroactively
// changes an earlier definition's already-elaborated calls (static binding
// falls out of machine::dictionary::lookup's newest-first, never-overwrite
// design -- see handoff.md's Step F9 section).
//
// Building the elaborated core is exactly the same "self-recursive tree
// shape plus a mutable arena" situation F7's grammar hit walking into the
// syntax tree (DIV-0005): elaborate_body and elaborate_body_item below are
// mutually recursive plain constexpr function templates, not a chain of
// functor/applicative/alternative combinator composition.

/// Views a @ref reader::syn_name as a @c std::string_view for dictionary
/// lookups and comparisons against `EXIT`/`RECURSE`.
template <int MaxName>
[[nodiscard]] constexpr auto name_view(reader::syn_name<MaxName> const &name)
    -> std::string_view {
    return std::string_view{name.begin(),
                            static_cast<std::size_t>(name.size())};
}

/// Returns the source position carried by whichever alternative @p node
/// currently holds -- every @ref reader::syn_node alternative has a `pos`
/// member, so this works uniformly across all eleven syntax-tree node kinds.
template <int MaxNodes, int MaxBody, int MaxName>
[[nodiscard]] constexpr auto
syn_node_pos(reader::syn_node<MaxNodes, MaxBody, MaxName> const &node)
    -> foundation::source_pos {
    return std::visit([](auto const &alt) { return alt.pos; }, node.value);
}

/// Appends @p message at @p pos to @p unit's warning log, silently dropping
/// it if the log is already at capacity (a warning is never itself a reason
/// to fail elaboration -- see @ref compiled_unit::warnings).
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto push_warning(compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                          MaxData, MaxWarnings> &unit,
                            foundation::source_pos pos, char const *message)
    -> void {
    if (unit.warnings.size() < MaxWarnings) {
        unit.warnings.push_back(foundation::parse_error{pos, message});
    }
}

// Forward declarations: elaborate_body and elaborate_body_item are mutually
// recursive (a body may contain IF/BEGIN/DO, each of which elaborates its own
// nested body-item* sequences). See DIV-0005.

template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto elaborate_body(
    reader::syntax_tree<MaxNodes, MaxBody, MaxName> const &tree,
    reader::syn_body<MaxNodes, MaxBody, MaxName> const &forms,
    compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData, MaxWarnings>
        &unit,
    int self_index) -> foundation::result<core_body<MaxNodes, MaxBody>>;

template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto elaborate_body_item(
    reader::syntax_tree<MaxNodes, MaxBody, MaxName> const &tree,
    reader::syn_node<MaxNodes, MaxBody, MaxName> const &node,
    compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData, MaxWarnings>
        &unit,
    int self_index) -> foundation::result<core_box<MaxNodes, MaxBody>>;

/// Resolves a bare word reference (a @ref reader::syn_word's name) to a
/// @ref core_box, per D9 and D11.
///
/// `EXIT` and `RECURSE` are handled before any dictionary lookup -- neither
/// is ever installed as a dictionary entry; `EXIT` becomes @ref core_exit,
/// `RECURSE` becomes a @ref core_call back to @p self_index, the definition
/// currently being compiled. Both are diagnosed as errors when
/// @p self_index is `-1` (not currently inside a colon definition) --
/// this project's documented choice for "EXIT/RECURSE at top level" (F11).
/// Any other name is resolved against @p unit's dictionary at its *current*
/// state (static binding); an unresolved name is "unknown word".
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto elaborate_word_ref(
    compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData, MaxWarnings>
        &unit,
    std::string_view name_text, foundation::source_pos pos, int self_index)
    -> foundation::result<core_box<MaxNodes, MaxBody>> {
    if (name_text == "EXIT") {
        if (self_index < 0) {
            return foundation::parse_error{pos, "EXIT outside a definition"};
        }
        return foundation::make_arena_box(
            unit.arena,
            core_node<MaxNodes, MaxBody>{.value = core_exit{.pos = pos}});
    }
    if (name_text == "RECURSE") {
        if (self_index < 0) {
            return foundation::parse_error{pos, "RECURSE outside a definition"};
        }
        return foundation::make_arena_box(
            unit.arena,
            core_node<MaxNodes, MaxBody>{
                .value = core_call{.word_index = self_index, .pos = pos}});
    }
    auto const *entry = unit.dictionary.lookup(name_text);
    if (entry == nullptr) {
        return foundation::parse_error{pos, "unknown word"};
    }
    int const index = unit.dictionary.lookup_index(name_text);
    return std::visit(
        [&](auto const &binding)
            -> foundation::result<core_box<MaxNodes, MaxBody>> {
            using B = std::decay_t<decltype(binding)>;
            if constexpr (std::is_same_v<B, machine::primitive>) {
                return foundation::make_arena_box(
                    unit.arena,
                    core_node<MaxNodes, MaxBody>{
                        .value = core_prim{.op = binding, .pos = pos}});
            } else if constexpr (std::is_same_v<B, machine::colon_word>) {
                return foundation::make_arena_box(
                    unit.arena,
                    core_node<MaxNodes, MaxBody>{
                        .value = core_call{.word_index = index, .pos = pos}});
            } else if constexpr (std::is_same_v<B, machine::variable_word>) {
                return foundation::make_arena_box(
                    unit.arena,
                    core_node<MaxNodes, MaxBody>{
                        .value =
                            core_var{.address = binding.address, .pos = pos}});
            } else if constexpr (std::is_same_v<B, machine::constant_word>) {
                return foundation::make_arena_box(
                    unit.arena, core_node<MaxNodes, MaxBody>{
                                    .value = core_const{.value = binding.value,
                                                        .pos = pos}});
            } else {
                // foreign_word: F19 is what makes these callable; nothing
                // installs one yet, but the visit must stay exhaustive.
                return foundation::parse_error{
                    pos, "foreign words not yet callable"};
            }
        },
        entry->binding);
}

/// Resolves `' NAME` (a @ref reader::syn_tick) to a @ref core_push_xt.
///
/// Unlike @ref elaborate_word_ref, this never special-cases `EXIT`/
/// `RECURSE` (ticking either is simply "unknown word", since neither is a
/// dictionary entry) and never branches on binding kind -- an execution
/// token is just a dictionary index, whatever it refers to.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto
elaborate_tick(compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                             MaxWarnings> &unit,
               std::string_view name_text, foundation::source_pos pos)
    -> foundation::result<core_box<MaxNodes, MaxBody>> {
    int const index = unit.dictionary.lookup_index(name_text);
    if (index < 0) {
        return foundation::parse_error{pos, "unknown word"};
    }
    return foundation::make_arena_box(
        unit.arena,
        core_node<MaxNodes, MaxBody>{
            .value = core_push_xt{.word_index = index, .pos = pos}});
}

template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto elaborate_body_item(
    reader::syntax_tree<MaxNodes, MaxBody, MaxName> const &tree,
    reader::syn_node<MaxNodes, MaxBody, MaxName> const &node,
    compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData, MaxWarnings>
        &unit,
    int self_index) -> foundation::result<core_box<MaxNodes, MaxBody>> {
    return std::visit(
        [&](auto const &alt)
            -> foundation::result<core_box<MaxNodes, MaxBody>> {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, reader::syn_literal>) {
                return foundation::make_arena_box(
                    unit.arena,
                    core_node<MaxNodes, MaxBody>{
                        .value = core_push{.value = alt.cell, .pos = alt.pos}});
            } else if constexpr (std::is_same_v<T, reader::syn_word<MaxName>>) {
                return elaborate_word_ref(unit, name_view(alt.name), alt.pos,
                                          self_index);
            } else if constexpr (std::is_same_v<T, reader::syn_tick<MaxName>>) {
                return elaborate_tick(unit, name_view(alt.name), alt.pos);
            } else if constexpr (std::is_same_v<
                                     T, reader::syn_if<MaxNodes, MaxBody,
                                                       MaxName>>) {
                auto then_r =
                    elaborate_body(tree, alt.then_body, unit, self_index);
                if (!then_r.has_value()) {
                    return then_r.error();
                }
                auto else_r =
                    elaborate_body(tree, alt.else_body, unit, self_index);
                if (!else_r.has_value()) {
                    return else_r.error();
                }
                return foundation::make_arena_box(
                    unit.arena, core_node<MaxNodes, MaxBody>{
                                    .value = core_if<MaxNodes, MaxBody>{
                                        .then_body = then_r.value(),
                                        .else_body = else_r.value(),
                                        .pos = alt.pos}});
            } else if constexpr (std::is_same_v<
                                     T, reader::syn_begin_until<
                                            MaxNodes, MaxBody, MaxName>>) {
                auto body_r = elaborate_body(tree, alt.body, unit, self_index);
                if (!body_r.has_value()) {
                    return body_r.error();
                }
                return foundation::make_arena_box(
                    unit.arena,
                    core_node<MaxNodes, MaxBody>{
                        .value = core_begin_until<MaxNodes, MaxBody>{
                            .body = body_r.value(), .pos = alt.pos}});
            } else if constexpr (std::is_same_v<
                                     T, reader::syn_begin_while<
                                            MaxNodes, MaxBody, MaxName>>) {
                auto cond_r =
                    elaborate_body(tree, alt.condition, unit, self_index);
                if (!cond_r.has_value()) {
                    return cond_r.error();
                }
                auto body_r = elaborate_body(tree, alt.body, unit, self_index);
                if (!body_r.has_value()) {
                    return body_r.error();
                }
                return foundation::make_arena_box(
                    unit.arena,
                    core_node<MaxNodes, MaxBody>{
                        .value = core_begin_while<MaxNodes, MaxBody>{
                            .condition = cond_r.value(),
                            .body = body_r.value(),
                            .pos = alt.pos}});
            } else if constexpr (std::is_same_v<
                                     T, reader::syn_do_loop<MaxNodes, MaxBody,
                                                            MaxName>>) {
                auto body_r = elaborate_body(tree, alt.body, unit, self_index);
                if (!body_r.has_value()) {
                    return body_r.error();
                }
                return foundation::make_arena_box(
                    unit.arena, core_node<MaxNodes, MaxBody>{
                                    .value = core_do_loop<MaxNodes, MaxBody>{
                                        .body = body_r.value(),
                                        .is_plus_loop = alt.is_plus_loop,
                                        .pos = alt.pos}});
            } else {
                // syn_colon_def / syn_variable / syn_constant / syn_create:
                // unreachable in practice -- F7's grammar never puts these
                // inside a body-item sequence -- but the visit must stay
                // exhaustive, so this is a diagnosed error, not UB.
                return foundation::parse_error{
                    alt.pos, "declaration not allowed inside a body"};
            }
        },
        node.value);
}

template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto elaborate_body(
    reader::syntax_tree<MaxNodes, MaxBody, MaxName> const &tree,
    reader::syn_body<MaxNodes, MaxBody, MaxName> const &forms,
    compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData, MaxWarnings>
        &unit,
    int self_index) -> foundation::result<core_body<MaxNodes, MaxBody>> {
    core_body<MaxNodes, MaxBody> items{};
    for (int i = 0; i < forms.size(); ++i) {
        auto const &node = tree.arena.get(forms[i]);
        auto item = elaborate_body_item(tree, node, unit, self_index);
        if (!item.has_value()) {
            return item.error();
        }
        items.push_back(item.value());
    }
    return items;
}

/// Elaborates a `: NAME ... ;` colon definition (@p cd) into @p unit: warns
/// (does not error) on redefinition, reserves @p cd's own future dictionary
/// index before elaborating its body (so `RECURSE` inside the body resolves
/// to that index), builds a @ref core_seq node for the body, and finally
/// installs the new @ref machine::colon_word.
///
/// No new dictionary entry can be created while a colon definition's own
/// body is being elaborated (F7's grammar disallows nested `:` and
/// `VARIABLE`/`CONSTANT`/`CREATE` inside a body-item sequence), so
/// `unit.dictionary.size()` observed just before @ref elaborate_body runs is
/// exactly the index @p cd will occupy once @ref machine::dictionary::
/// define_colon appends it below.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto
elaborate_colon_def(reader::syntax_tree<MaxNodes, MaxBody, MaxName> const &tree,
                    reader::syn_colon_def<MaxNodes, MaxBody, MaxName> const &cd,
                    compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                                  MaxWarnings> &unit) -> machine::status {
    auto const name_text = name_view(cd.name);
    if (unit.dictionary.lookup(name_text) != nullptr) {
        push_warning(unit, cd.pos, "redefined word");
    }
    int const word_index = unit.dictionary.size();
    auto body = elaborate_body(tree, cd.body, unit, word_index);
    if (!body.has_value()) {
        return body.error();
    }
    auto body_box = foundation::make_arena_box(
        unit.arena, core_node<MaxNodes, MaxBody>{
                        .value = core_seq<MaxNodes, MaxBody>{
                            .items = body.value(), .pos = cd.pos}});
    return unit.dictionary.define_colon(
        name_text, machine::colon_word{.core_id = body_box.id_,
                                       .effect = machine::stack_effect{}});
}

/// Elaborates a `VARIABLE NAME` declaration: warns on redefinition, allots
/// one data-space cell (`machine::data_space::allot`), and installs @p v's
/// name bound to the resulting address.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto
elaborate_variable(reader::syn_variable<MaxName> const &v,
                   compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                                 MaxWarnings> &unit) -> machine::status {
    auto const name_text = name_view(v.name);
    if (unit.dictionary.lookup(name_text) != nullptr) {
        push_warning(unit, v.pos, "redefined word");
    }
    auto address = unit.data_space.allot(1);
    if (!address.has_value()) {
        return address.error();
    }
    return unit.dictionary.define_variable(
        name_text, machine::variable_word{.address = address.value()});
}

/// Elaborates a `CREATE NAME` declaration: warns on redefinition and
/// installs @p c's name bound to the *current* data-space top, allotting
/// nothing (F16's `ALLOT` is what later actually extends storage past a
/// `CREATE`d address -- this is F11's documented, literal reading of "CREATE
/// just records the address").
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto
elaborate_create(reader::syn_create<MaxName> const &c,
                 compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                               MaxWarnings> &unit) -> machine::status {
    auto const name_text = name_view(c.name);
    if (unit.dictionary.lookup(name_text) != nullptr) {
        push_warning(unit, c.pos, "redefined word");
    }
    return unit.dictionary.define_variable(
        name_text, machine::variable_word{.address = unit.data_space.here()});
}

/// Elaborates `<literal> CONSTANT NAME`: the elaborator constant-folds the
/// immediately preceding literal push (@p value, already extracted by @ref
/// elaborate's top-level loop) into @p cn's dictionary entry. Warns (does
/// not error) on redefinition.
template <int MaxNodes, int MaxBody, int MaxName, int MaxWords, int MaxData,
          int MaxWarnings>
constexpr auto
elaborate_constant(reader::syn_constant<MaxName> const &cn, machine::cell value,
                   compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData,
                                 MaxWarnings> &unit) -> machine::status {
    auto const name_text = name_view(cn.name);
    if (unit.dictionary.lookup(name_text) != nullptr) {
        push_warning(unit, cn.pos, "redefined word");
    }
    return unit.dictionary.define_constant(name_text,
                                           machine::constant_word{value});
}

// 5dfe0ab6-e4f5-481b-ad89-926e077d406f
/// Elaborates a complete @ref reader::syntax_tree (F7's output) into a
/// @ref compiled_unit, per D9.
///
/// Walks `tree.program` in order. Colon definitions extend the dictionary as
/// encountered (@ref elaborate_colon_def); `VARIABLE`/`CREATE` allocate from
/// the data space (@ref elaborate_variable / @ref elaborate_create);
/// `CONSTANT` consumes the immediately preceding top-level literal (@ref
/// elaborate_constant) -- a `CONSTANT` with no immediately preceding literal
/// is a diagnosed error, not silently ignored. Every other top-level form is
/// an ordinary body-item, elaborated exactly as it would be inside a colon
/// definition's body, except with `self_index = -1` (so a bare `EXIT` or
/// `RECURSE` at the top level is diagnosed rather than accepted).
///
/// The dictionary starts pre-populated with every F8 primitive (@ref
/// machine::default_dictionary), so ordinary arithmetic/stack words resolve
/// to @ref core_prim from the first definition onward.
///
/// @tparam MaxNodes    Elaborated-core arena capacity.
/// @tparam MaxBody     Maximum number of forms in any one node's body, and of
///                     top-level items in the program.
/// @tparam MaxName     Maximum dictionary-entry name length; reused from
///                     @p tree's own @c MaxName.
/// @tparam MaxWords    Maximum number of dictionary entries.
/// @tparam MaxData     Maximum number of data-space cells.
/// @tparam MaxWarnings Maximum number of collected redefinition warnings.
template <int MaxNodes = 1024, int MaxBody = 64, int MaxName = 32,
          int MaxWords = 256, int MaxData = 1024, int MaxWarnings = 64>
constexpr auto
elaborate(reader::syntax_tree<MaxNodes, MaxBody, MaxName> const &tree)
    -> foundation::result<compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords,
                                        MaxData, MaxWarnings>> {
    compiled_unit<MaxNodes, MaxBody, MaxName, MaxWords, MaxData, MaxWarnings>
        unit{};
    unit.dictionary = machine::default_dictionary<MaxWords, MaxName>();

    int i = 0;
    while (i < tree.program.size()) {
        auto const &node = tree.arena.get(tree.program[i]);

        if (auto const *cd =
                std::get_if<reader::syn_colon_def<MaxNodes, MaxBody, MaxName>>(
                    &node.value)) {
            auto status = elaborate_colon_def(tree, *cd, unit);
            if (!status.has_value()) {
                return status.error();
            }
            ++i;
            continue;
        }
        if (auto const *v =
                std::get_if<reader::syn_variable<MaxName>>(&node.value)) {
            auto status = elaborate_variable(*v, unit);
            if (!status.has_value()) {
                return status.error();
            }
            ++i;
            continue;
        }
        if (auto const *c =
                std::get_if<reader::syn_create<MaxName>>(&node.value)) {
            auto status = elaborate_create(*c, unit);
            if (!status.has_value()) {
                return status.error();
            }
            ++i;
            continue;
        }
        if (auto const *cn =
                std::get_if<reader::syn_constant<MaxName>>(&node.value)) {
            return foundation::parse_error{
                cn->pos, "CONSTANT requires a preceding integer literal"};
        }
        if (auto const *lit = std::get_if<reader::syn_literal>(&node.value)) {
            if (i + 1 < tree.program.size()) {
                auto const &next_node = tree.arena.get(tree.program[i + 1]);
                if (auto const *cn = std::get_if<reader::syn_constant<MaxName>>(
                        &next_node.value)) {
                    auto status = elaborate_constant(*cn, lit->cell, unit);
                    if (!status.has_value()) {
                        return status.error();
                    }
                    i += 2;
                    continue;
                }
            }
            // Falls through: an ordinary top-level literal, not consumed by
            // a following CONSTANT.
        }

        auto item = elaborate_body_item(tree, node, unit, /*self_index=*/-1);
        if (!item.has_value()) {
            return item.error();
        }
        if (unit.program.size() >= MaxBody) {
            return foundation::parse_error{
                syn_node_pos(node), "top-level body exceeds MaxBody capacity"};
        }
        unit.program.push_back(item.value());
        ++i;
    }
    return unit;
}
// 5dfe0ab6-e4f5-481b-ad89-926e077d406f end

} // namespace smd::forth::elaborator

#endif
