// src/smd/forth/machine/dictionary.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_DICTIONARY_HPP
#define SRC_SMD_FORTH_MACHINE_DICTIONARY_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/data_space.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace smd::forth::machine {

/// Storage for a dictionary entry's name.
///
/// Entries always store their name already folded to uppercase (@ref
/// make_word_name folds on construction); @ref dictionary::lookup also folds
/// its query, so callers may look up either case.
template <int MaxName = 32>
using word_name = foundation::static_vector<char, MaxName>;

/// Folds a single ASCII letter to uppercase; every other byte is unchanged.
constexpr auto fold_upper(char c) -> char {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

/// Builds a @ref word_name from @p text, folding every character to
/// uppercase.
///
/// @pre text.size() <= MaxName
template <int MaxName = 32>
constexpr auto make_word_name(std::string_view text) -> word_name<MaxName> {
    word_name<MaxName> built{};
    for (char c : text) {
        built.push_back(fold_upper(c));
    }
    return built;
}

/// Returns true if the already-folded @p name spells the same word as
/// @p text once @p text is itself folded to uppercase.
template <int MaxName>
constexpr auto word_name_equals_folded(word_name<MaxName> const &name,
                                       std::string_view text) -> bool {
    if (name.size() != static_cast<int>(text.size())) {
        return false;
    }
    for (int i = 0; i < name.size(); ++i) {
        if (name[i] != fold_upper(text[static_cast<std::size_t>(i)])) {
            return false;
        }
    }
    return true;
}

/// A colon definition compiled directly by the F25 colon compiler
/// (`interpreter::interpret`, D13), one instruction at a time, as the text
/// interpreter meets `:` ... `;`.
///
/// Named `compiled_colon_word` rather than the shorter `colon_word` for
/// historical reasons DIV-0013 records in full: F25 needed a binding kind
/// distinct from the R1 elaborator's own `colon_word` (whose `core_id` was
/// an index into an elaborator arena this compiler never built), so it added
/// this one alongside it rather than repurposing it. Step F26 ("the cut")
/// deletes the elaborator and, with it, `colon_word` and its `stack_effect`
/// payload type -- this is now the *only* colon-definition binding kind, but
/// keeps its `compiled_` prefix (DIV-0013 leaves the rename/fold choice to
/// this step's own discretion; renaming was judged not worth touching every
/// consumer of this name for a cosmetic gain, see DIV-0014).
///
/// @ref entry_point is recorded *before* the body is compiled (F14's own
/// discipline, carried forward unchanged): a `RECURSE` inside the body
/// resolves to this same value.
struct compiled_colon_word {
    /// Instruction index, in the interpreter's own code space
    /// (`interpreter::compile_buffer`), where this word's body begins.
    int entry_point = -1;

    /// The declared `( ... -- ... )` stack-effect comment immediately after
    /// the definition's name, if the source had one -- captured verbatim as
    /// a span into the interpreter's own source text (D20: stored, not
    /// verified; the checker lands at F30). Meaningless unless @ref
    /// has_effect is true.
    foundation::source_span effect_span{};

    /// True iff a `( ... )` comment was actually present immediately after
    /// the name (@ref effect_span is only meaningful when this is true) --
    /// Forth-2012 does not require one.
    bool has_effect = false;

    friend constexpr auto operator==(compiled_colon_word const &,
                                     compiled_colon_word const &)
        -> bool = default;
};

/// A `VARIABLE`- or `CREATE`-defined word's binding: the data-space address
/// it names.
///
/// The same binding kind serves both words (there is no separate binding
/// kind for `CREATE`): `CREATE NAME` installs @c NAME at the current
/// data-space top with no cells allotted, while `VARIABLE NAME` allots
/// exactly one cell first (`interpreter::interpret`, F26); both end up as "a
/// name bound to an @ref addr" at the dictionary level, and `ALLOT` (F16) is
/// what later actually extends storage past a `CREATE`d address.
///
/// @note Was a placeholder plain @ref cell until DIV-0004
/// (`docs/divergences/DIV-0004-dictionary-addr-placeholder.md`) was
/// resolved in F11: D10 calls for `addr` to be its own distinct type,
/// explicitly convertible to/from @ref cell, and F10's @ref machine::addr
/// (`data_space.hpp`) is that type.
///
/// @ref does_entry is step F28's own addition (D18's "does-field"): `-1`
/// (the default) means an ordinary `VARIABLE`/`CREATE`, whose only execution
/// semantics is "push @ref address"; a nonnegative value is an instruction
/// index in the interpreter's own code space that `DOES>` installed
/// (`interpreter::apply_control_word`'s own `does_` case) -- executing such a
/// word pushes @ref address *and then* calls into @ref does_entry, exactly
/// as if `address` had been pushed by hand immediately before an ordinary
/// call. `interpreter::execute_entry`/`compile_entry` are what actually give
/// this field runtime meaning; `machine/` only carries it.
// d5b20e62-4731-4fbd-ac00-3c4b435207f4
struct variable_word {
    addr address{};
    int does_entry = -1;
};
// d5b20e62-4731-4fbd-ac00-3c4b435207f4 end

/// A `CONSTANT`-defined word's binding: its fixed value.
struct constant_word {
    cell value = 0;
};

/// A foreign-function binding (F19); @c index is an opaque handle into
/// whatever registry F19 builds. F9 only reserves the slot.
struct foreign_word {
    int index = -1;
};

// 9b4e6a1c-2f8d-4c3a-8e7b-5d1a9c4f6b3e
/// A `VALUE`-defined word's binding (step F28): the data-space cell holding
/// its current value. Unlike @ref variable_word, executing a @ref value_word
/// pushes the *contents* of @ref address (`interpreter::execute_entry`'s own
/// case for it), not the address itself -- `TO NAME` is what later changes
/// that content (`interpreter::apply_control_word`'s own `to_` case).
struct value_word {
    addr address{};
};

/// A `DEFER`-defined word's binding (step F28): the data-space cell holding
/// its current target, as an execution token (D18) -- an instruction index
/// in the interpreter's own code space, or `-1` if `IS` has never been used
/// on this word yet. Storing the target in data space rather than on the
/// dictionary entry itself (contrast @ref variable_word::does_entry, which
/// `DOES>` does mutate on the entry directly) means an ordinary compiled
/// reference to a deferred word (`interpreter::compile_entry`'s own case for
/// it: push @ref address, `@`, `EXECUTE`) needs no dictionary access at
/// runtime at all -- executing the unset `-1` sentinel through `EXECUTE`
/// diagnoses cleanly as an out-of-range instruction pointer (D7), and
/// `interpreter::execute_entry`'s own case gives the friendlier "deferred
/// word has no action" diagnosis for the common case of invoking a deferred
/// word directly by name.
struct defer_word {
    addr address{};
};
// 9b4e6a1c-2f8d-4c3a-8e7b-5d1a9c4f6b3e end

// 0d3b390a-1051-4f76-867c-b4f161264827
/// Step F27 (docs/forth-plan-2.md), D17: the C++-installed control words
/// (`IF ELSE THEN`, `BEGIN UNTIL`, `BEGIN WHILE REPEAT`, `DO LOOP +LOOP
/// LEAVE UNLOOP I J`, `LITERAL`, `POSTPONE`, `IMMEDIATE`, `[`, `]`,
/// `COMPILE,`) carry no VM entry point of their own: unlike a primitive
/// (inlined as @ref op::prim at every reference site) or a @ref
/// compiled_colon_word (real instructions in the interpreter's own code
/// space), a control word's whole job is to mutate that code space directly
/// while a definition is being compiled -- @ref op::branch0 sentinels,
/// back-patched operands, the return-stack loop machinery's own setup/step
/// instructions -- and to use the data stack as the Forth-2012 control-flow
/// stack (the orig/dest discipline) meanwhile. That mutation needs the
/// interpreter's own @c compile_buffer, a type `machine/` has no reason to
/// know about, so this enum and @ref control_word are only the
/// dictionary-level tag identifying *which* action a name is bound to;
/// `interpreter::apply_control_word` (`interp.hpp`) is what the tag actually
/// dispatches to.
enum class control_builtin : std::uint8_t {
    if_,
    else_,
    then_,
    begin_,
    until_,
    while_,
    repeat_,
    do_,
    loop_,
    plus_loop_,
    leave_,
    unloop_,
    i_,
    j_,
    literal_,
    postpone_,
    immediate_,
    bracket_open_,  ///< `[`
    bracket_close_, ///< `]`
    compile_comma_, ///< `COMPILE,`

    // Step F28 (docs/forth-plan-2.md), D18: execution tokens and defining
    // words. See interp.hpp's own apply_control_word/compile_entry for what
    // each one actually does; machine/ only carries the tag.
    tick_,         ///< `'`
    bracket_tick_, ///< `[']`
    execute_,      ///< `EXECUTE`
    create_,       ///< `CREATE`
    does_,         ///< `DOES>`
    value_,        ///< `VALUE`
    to_,           ///< `TO`
    defer_,        ///< `DEFER`
    is_,           ///< `IS`

    // Step F29 (docs/forth-plan-2.md), D19/D21: parsing words and strings.
    // Every one of these five must consume its own following source text
    // (a comment body, or a `"`-delimited string) at the exact moment it is
    // met, regardless of `STATE` -- interp.hpp's own `apply_control_word`
    // has the full rationale for each; `machine/` only carries the tag,
    // exactly as it does for every other control word.
    paren_,        ///< `(`  -- reexpressed from scanner-level comment
                   ///< skipping (D19): parses to the next `)`, discarding it.
    backslash_,    ///< `\`  -- likewise, reexpressed: parses to end of line.
    s_quote_,      ///< `S"` -- parses a `"`-delimited string into data space
                   ///< (D21) and leaves its address/length.
    dot_quote_,    ///< `."` -- like `s_quote_`, but prints the string instead
                   ///< of leaving it on the stack.
    char_bracket_, ///< `[CHAR]` -- compile-only `CHAR`: compiles a literal
                   ///< push of the named character's own code.
    abort_quote_,  ///< `ABORT"` -- compile-only: parses a `"`-delimited
                   ///< message and compiles a call to the `abort_quote`
                   ///< primitive (`machine/forth_state.hpp`) that prints it
                   ///< and diagnoses if the runtime flag is nonzero
                   ///< (DIV-0017: a hard stop, not yet Forth-2012's
                   ///< `THROW -2` -- `THROW`/`CATCH` land at F31).
};

/// A control-builtin word's binding: which action (@ref control_builtin) it
/// performs. See that enum's own doc comment for why this carries no VM
/// entry point the way @ref compiled_colon_word does.
struct control_word {
    control_builtin which{};

    friend constexpr auto operator==(control_word const &, control_word const &)
        -> bool = default;
};
// 0d3b390a-1051-4f76-867c-b4f161264827 end

/// The set of things a dictionary entry can be bound to.
using dictionary_binding =
    std::variant<primitive, variable_word, constant_word, foreign_word,
                 compiled_colon_word, control_word, value_word, defer_word>;

/// One dictionary entry: a folded name, what it is bound to, and whether it
/// is `IMMEDIATE` (step F27, D17/D18).
///
/// @ref immediate defaults to false for every @c define_* method except
/// @ref dictionary::define_control (whose caller -- @ref default_dictionary
/// -- passes it explicitly per built-in control word, since most of them
/// must run the moment the text interpreter meets them, even while
/// compiling); a later definition may still gain it via @ref
/// dictionary::mark_last_immediate (`IMMEDIATE`, the word, D13's
/// "execute when interpreting or when immediate" rule is what the
/// interpreter's own dispatch reads this flag to implement).
template <int MaxName = 32>
struct dictionary_entry {
    word_name<MaxName> name{};
    dictionary_binding binding{};
    bool immediate = false;
};

/// An arena-backed, linear, newest-first word list.
///
/// @ref lookup scans from the most recently defined entry backward, so
/// redefining a name shadows the earlier definition without erasing it --
/// traditional Forth behavior. Redefinition is legal, not an error; earlier
/// resolutions (already-elaborated colon-word bodies that reference the old
/// binding) keep resolving to the old entry, since F11's elaborator resolves
/// names in program order against whatever the dictionary looks like at that
/// point (static binding). @ref lookup folds its query to uppercase before
/// comparing, so callers may pass either case (`dup` finds `DUP`).
///
/// @tparam MaxWords Maximum number of entries.
/// @tparam MaxName  Maximum name length, in characters.
template <int MaxWords, int MaxName = 32>
class dictionary {
  public:
    constexpr dictionary() = default;

    /// Defines @p name_text as the primitive opcode @p op.
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_primitive(std::string_view name_text, primitive op)
        -> status;

    /// Defines @p name_text as a variable.
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_variable(std::string_view name_text,
                                   variable_word word) -> status;

    /// Defines @p name_text as a constant.
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_constant(std::string_view name_text,
                                   constant_word word) -> status;

    /// Defines @p name_text as a foreign word.
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_foreign(std::string_view name_text, foreign_word word)
        -> status;

    /// Defines @p name_text as a colon word compiled by the F25 colon
    /// compiler (@ref compiled_colon_word).
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_compiled_colon(std::string_view name_text,
                                         compiled_colon_word word) -> status;

    /// Defines @p name_text as a C++-installed control word (step F27, D17).
    /// @p immediate defaults to false (`]` and `IMMEDIATE` itself install as
    /// ordinary, non-immediate words -- see @ref dictionary_entry's own doc
    /// comment); every other control word @ref default_dictionary installs
    /// passes @c true explicitly.
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_control(std::string_view name_text, control_word word,
                                  bool immediate = false) -> status;

    /// Defines @p name_text as a `VALUE` (step F28).
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_value(std::string_view name_text, value_word word)
        -> status;

    /// Defines @p name_text as a `DEFER`red word (step F28), initially with
    /// no target (`IS` is what sets one, via @ref value_word/@ref
    /// defer_word's own data-space-backed target rather than a dictionary
    /// mutation -- see @ref defer_word's own doc comment).
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_defer(std::string_view name_text, defer_word word)
        -> status;

    /// `IMMEDIATE` (step F27): flags the most recently defined entry (by
    /// insertion order, regardless of binding kind) as immediate. Diagnoses
    /// an empty dictionary rather than indexing before the first entry.
    constexpr auto mark_last_immediate() -> status;

    /// `DOES>` (step F28): installs @p does_entry_point (an instruction
    /// index in the interpreter's own code space) onto the most recently
    /// defined entry's own @ref variable_word::does_entry. Diagnoses an
    /// empty dictionary, or a most-recent entry that is not a @ref
    /// variable_word (i.e. not `CREATE`d), rather than misapplying `DOES>`
    /// to the wrong kind of word.
    constexpr auto attach_does(int does_entry_point) -> status;

    /// Looks up @p name_text, newest definition first (shadowing).
    /// Folds @p name_text to uppercase before comparing. Returns `nullptr`
    /// if no entry matches.
    [[nodiscard]] constexpr auto lookup(std::string_view name_text) const
        -> dictionary_entry<MaxName> const *;

    /// Looks up @p name_text like @ref lookup, but returns the matching
    /// entry's index (0-based, insertion order) rather than a pointer, or
    /// `-1` if no entry matches.
    [[nodiscard]] constexpr auto lookup_index(std::string_view name_text) const
        -> int;

    /// Returns the entry at @p index (0-based, insertion order).
    /// @pre 0 <= index < size()
    [[nodiscard]] constexpr auto entry_at(int index) const
        -> dictionary_entry<MaxName> const &;

    /// The number of entries currently defined (including shadowed ones).
    [[nodiscard]] constexpr auto size() const -> int;

  private:
    constexpr auto insert(std::string_view name_text,
                          dictionary_binding binding, bool immediate = false)
        -> status;

    foundation::static_vector<dictionary_entry<MaxName>, MaxWords> entries_{};
};

template <int MaxWords, int MaxName>
constexpr auto dictionary<MaxWords, MaxName>::insert(std::string_view name_text,
                                                     dictionary_binding binding,
                                                     bool immediate) -> status {
    if (entries_.size() >= MaxWords) {
        return foundation::parse_error{foundation::source_pos{},
                                       "dictionary full"};
    }
    entries_.push_back(dictionary_entry<MaxName>{
        make_word_name<MaxName>(name_text), binding, immediate});
    return std::monostate{};
}

template <int MaxWords, int MaxName>
constexpr auto
dictionary<MaxWords, MaxName>::define_primitive(std::string_view name_text,
                                                primitive op) -> status {
    return insert(name_text, dictionary_binding{op});
}

template <int MaxWords, int MaxName>
constexpr auto
dictionary<MaxWords, MaxName>::define_variable(std::string_view name_text,
                                               variable_word word) -> status {
    return insert(name_text, dictionary_binding{word});
}

template <int MaxWords, int MaxName>
constexpr auto
dictionary<MaxWords, MaxName>::define_constant(std::string_view name_text,
                                               constant_word word) -> status {
    return insert(name_text, dictionary_binding{word});
}

template <int MaxWords, int MaxName>
constexpr auto
dictionary<MaxWords, MaxName>::define_foreign(std::string_view name_text,
                                              foreign_word word) -> status {
    return insert(name_text, dictionary_binding{word});
}

template <int MaxWords, int MaxName>
constexpr auto dictionary<MaxWords, MaxName>::define_compiled_colon(
    std::string_view name_text, compiled_colon_word word) -> status {
    return insert(name_text, dictionary_binding{word});
}

template <int MaxWords, int MaxName>
constexpr auto dictionary<MaxWords, MaxName>::define_control(
    std::string_view name_text, control_word word, bool immediate) -> status {
    return insert(name_text, dictionary_binding{word}, immediate);
}

template <int MaxWords, int MaxName>
constexpr auto
dictionary<MaxWords, MaxName>::define_value(std::string_view name_text,
                                            value_word word) -> status {
    return insert(name_text, dictionary_binding{word});
}

template <int MaxWords, int MaxName>
constexpr auto
dictionary<MaxWords, MaxName>::define_defer(std::string_view name_text,
                                            defer_word word) -> status {
    return insert(name_text, dictionary_binding{word});
}

template <int MaxWords, int MaxName>
constexpr auto dictionary<MaxWords, MaxName>::mark_last_immediate() -> status {
    if (entries_.size() == 0) {
        return foundation::parse_error{foundation::source_pos{},
                                       "IMMEDIATE: no definition to mark"};
    }
    entries_[entries_.size() - 1].immediate = true;
    return std::monostate{};
}

template <int MaxWords, int MaxName>
constexpr auto dictionary<MaxWords, MaxName>::attach_does(int does_entry_point)
    -> status {
    if (entries_.size() == 0) {
        return foundation::parse_error{foundation::source_pos{},
                                       "DOES>: no preceding CREATE"};
    }
    auto *vw =
        std::get_if<variable_word>(&entries_[entries_.size() - 1].binding);
    if (vw == nullptr) {
        return foundation::parse_error{
            foundation::source_pos{},
            "DOES>: most recent definition was not CREATEd"};
    }
    vw->does_entry = does_entry_point;
    return std::monostate{};
}

template <int MaxWords, int MaxName>
constexpr auto
dictionary<MaxWords, MaxName>::lookup(std::string_view name_text) const
    -> dictionary_entry<MaxName> const * {
    for (int i = entries_.size() - 1; i >= 0; --i) {
        if (word_name_equals_folded(entries_[i].name, name_text)) {
            return &entries_[i];
        }
    }
    return nullptr;
}

template <int MaxWords, int MaxName>
constexpr auto
dictionary<MaxWords, MaxName>::lookup_index(std::string_view name_text) const
    -> int {
    for (int i = entries_.size() - 1; i >= 0; --i) {
        if (word_name_equals_folded(entries_[i].name, name_text)) {
            return i;
        }
    }
    return -1;
}

template <int MaxWords, int MaxName>
constexpr auto dictionary<MaxWords, MaxName>::entry_at(int index) const
    -> dictionary_entry<MaxName> const & {
    return entries_[index];
}

template <int MaxWords, int MaxName>
constexpr auto dictionary<MaxWords, MaxName>::size() const -> int {
    return entries_.size();
}

/// Builds a dictionary with every F8/F13/F16/F28/F29 primitive installed
/// under its Forth name (`+ - * / MOD NEGATE ABS MIN MAX AND OR XOR INVERT
/// LSHIFT RSHIFT 1- 1+ 0= 0< = <> < > <= >= TRUE FALSE DUP DROP SWAP OVER
/// ROT ?DUP NIP TUCK DEPTH >R R> R@ . .S EMIT CR @ ! +! ALLOT , PARSE WORD
/// CHAR COUNT TYPE`, plus the runtime half of `ABORT"`) -- 54 words, one per
/// @ref primitive enumerator (`.`, `.S`, `EMIT`, `CR` are step F13's output
/// words, D10; `1-` is also from that step, see DIV-0007; `1+` is step F17,
/// see DIV-0010; `@`, `!`, `+!`, `ALLOT` are step F16's memory words, D10;
/// `,` is step F28's, D10/D21; `PARSE WORD CHAR COUNT TYPE` and the runtime
/// primitive behind `ABORT"` are step F29's, D19/D21 -- see
/// `machine/forth_state.hpp`'s own `primitive` enum for why these five are
/// ordinary, non-immediate words rather than control words) -- plus every
/// step F27 control word (`IF ELSE THEN BEGIN UNTIL WHILE REPEAT DO LOOP
/// +LOOP LEAVE UNLOOP I J LITERAL POSTPONE IMMEDIATE [ ] COMPILE,`, D17), 20,
/// every step F28 execution-token/defining-word control word (`' [' EXECUTE
/// CREATE DOES> VALUE TO DEFER IS`, D18), 9 more, and every step F29 parsing
/// control word (`( \ S" ." [CHAR] ABORT"`, D19/D21 -- each must consume its
/// own following source text at the moment it is met, so each is immediate;
/// see @ref control_builtin's own doc comment), 6 more, for 89 entries
/// total.
///
/// @tparam MaxWords Dictionary capacity; must be at least 89 plus whatever
///                  room the caller wants for later colon/variable/constant/
///                  foreign definitions.
/// @tparam MaxName  Maximum name length.
template <int MaxWords = 256, int MaxName = 32>
constexpr auto default_dictionary() -> dictionary<MaxWords, MaxName> {
    dictionary<MaxWords, MaxName> dict;
    constexpr std::array<std::pair<std::string_view, primitive>, 54> words{{
        {"+", primitive::plus},
        {"-", primitive::minus},
        {"*", primitive::star},
        {"/", primitive::slash},
        {"MOD", primitive::mod_},
        {"NEGATE", primitive::negate},
        {"ABS", primitive::abs_},
        {"MIN", primitive::min_},
        {"MAX", primitive::max_},
        {"AND", primitive::and_},
        {"OR", primitive::or_},
        {"XOR", primitive::xor_},
        {"INVERT", primitive::invert},
        {"LSHIFT", primitive::lshift},
        {"RSHIFT", primitive::rshift},
        {"1-", primitive::one_minus},
        {"1+", primitive::one_plus},
        {"0=", primitive::zero_equal},
        {"0<", primitive::zero_less},
        {"=", primitive::equal},
        {"<>", primitive::not_equal},
        {"<", primitive::less},
        {">", primitive::greater},
        {"<=", primitive::less_equal},
        {">=", primitive::greater_equal},
        {"TRUE", primitive::true_},
        {"FALSE", primitive::false_},
        {"DUP", primitive::dup},
        {"DROP", primitive::drop},
        {"SWAP", primitive::swap},
        {"OVER", primitive::over},
        {"ROT", primitive::rot},
        {"?DUP", primitive::question_dup},
        {"NIP", primitive::nip},
        {"TUCK", primitive::tuck},
        {"DEPTH", primitive::depth},
        {">R", primitive::to_r},
        {"R>", primitive::r_from},
        {"R@", primitive::r_fetch},
        {".", primitive::dot},
        {".S", primitive::dot_s},
        {"EMIT", primitive::emit},
        {"CR", primitive::cr},
        {"@", primitive::fetch},
        {"!", primitive::store},
        {"+!", primitive::plus_store},
        {"ALLOT", primitive::allot},
        {",", primitive::comma},
        {"PARSE", primitive::parse},
        {"WORD", primitive::word},
        {"CHAR", primitive::char_},
        {"COUNT", primitive::count},
        {"TYPE", primitive::type_},
        {"(ABORT\")", primitive::abort_quote},
    }};
    for (auto const &[name_text, op] : words) {
        (void)dict.define_primitive(name_text, op);
    }

    // Step F27, D17: the C++-installed control words. Eighteen are
    // immediate -- they must run the moment the text interpreter meets
    // them, even while compiling (D13's "execute ... when immediate"),
    // since their whole job is mutating the code space being compiled right
    // then. `IMMEDIATE` and `]` are the two exceptions: both have ordinary
    // execution semantics only ever meant to fire while interpreting
    // (`STATE == 0`), where every found word already executes regardless of
    // its own immediate flag, so neither needs one.
    //
    // Step F28, D18 adds two more immediate control words: `[']` must run at
    // compile time (it compiles a `push <xt>` literal into the definition
    // being built) and `TO` must resolve its target's address at the moment
    // it is met either way (interpreting: store directly; compiling: emit
    // the equivalent store sequence) -- see interp.hpp's own
    // apply_control_word for both.
    //
    // Step F29, D19/D21 adds six more, all immediate for the same reason:
    // each must consume its own following source text (a comment body or a
    // `"`-delimited string) at the moment it is met, not later. `(` and `\`
    // are the reexpressed comment forms (previously scanner-level, D19);
    // `S"`/`."` work both interpreting and compiling; `[CHAR]`/`ABORT"` are
    // additionally compile-only (see interp.hpp's own apply_control_word).
    constexpr std::array<std::pair<std::string_view, control_builtin>, 26>
        immediate_control_words{{
            {"IF", control_builtin::if_},
            {"ELSE", control_builtin::else_},
            {"THEN", control_builtin::then_},
            {"BEGIN", control_builtin::begin_},
            {"UNTIL", control_builtin::until_},
            {"WHILE", control_builtin::while_},
            {"REPEAT", control_builtin::repeat_},
            {"DO", control_builtin::do_},
            {"LOOP", control_builtin::loop_},
            {"+LOOP", control_builtin::plus_loop_},
            {"LEAVE", control_builtin::leave_},
            {"UNLOOP", control_builtin::unloop_},
            {"I", control_builtin::i_},
            {"J", control_builtin::j_},
            {"LITERAL", control_builtin::literal_},
            {"POSTPONE", control_builtin::postpone_},
            {"[", control_builtin::bracket_open_},
            {"COMPILE,", control_builtin::compile_comma_},
            {"[']", control_builtin::bracket_tick_},
            {"TO", control_builtin::to_},
            {"(", control_builtin::paren_},
            {"\\", control_builtin::backslash_},
            {"S\"", control_builtin::s_quote_},
            {".\"", control_builtin::dot_quote_},
            {"[CHAR]", control_builtin::char_bracket_},
            {"ABORT\"", control_builtin::abort_quote_},
        }};
    for (auto const &[name_text, which] : immediate_control_words) {
        (void)dict.define_control(name_text, control_word{which},
                                  /* immediate */ true);
    }

    // Step F28, D18: the remaining ordinary (non-immediate) control words.
    // `'`, `EXECUTE`, `CREATE`, `VALUE`, `DEFER`, and `IS` all have ordinary
    // execution semantics that only ever need to fire while interpreting
    // (D13's own rule already covers them); `DOES>` and `CREATE` also gain
    // real *compiled* forms via interp.hpp's own compile_entry (`op::
    // does_enter`/`op::create_word`, machine/instruction.hpp), so meeting
    // them while compiling appends that opcode instead of running them, per
    // the same D13 rule.
    constexpr std::array<std::pair<std::string_view, control_builtin>, 9>
        ordinary_control_words{{
            {"IMMEDIATE", control_builtin::immediate_},
            {"]", control_builtin::bracket_close_},
            {"'", control_builtin::tick_},
            {"EXECUTE", control_builtin::execute_},
            {"CREATE", control_builtin::create_},
            {"DOES>", control_builtin::does_},
            {"VALUE", control_builtin::value_},
            {"DEFER", control_builtin::defer_},
            {"IS", control_builtin::is_},
        }};
    for (auto const &[name_text, which] : ordinary_control_words) {
        (void)dict.define_control(name_text, control_word{which},
                                  /* immediate */ false);
    }

    return dict;
}

namespace detail {

// Every binding alternative, the closed binding variant, one entry, and the
// dictionary itself must all be trivially destructible: the dictionary is a
// fixed-capacity foundation::static_vector, never a destructor-owning heap
// container. Checked against one concrete instantiation (256 words, 32-char
// names) as a representative sample.
static_assert(std::is_trivially_destructible_v<variable_word>);
static_assert(std::is_trivially_destructible_v<constant_word>);
static_assert(std::is_trivially_destructible_v<foreign_word>);
static_assert(std::is_trivially_destructible_v<compiled_colon_word>);
static_assert(std::is_trivially_destructible_v<control_word>);
static_assert(std::is_trivially_destructible_v<value_word>);
static_assert(std::is_trivially_destructible_v<defer_word>);
static_assert(std::is_trivially_destructible_v<dictionary_binding>);
static_assert(std::is_trivially_destructible_v<dictionary_entry<32>>);
static_assert(std::is_trivially_destructible_v<dictionary<256, 32>>);

} // namespace detail

} // namespace smd::forth::machine

#endif
