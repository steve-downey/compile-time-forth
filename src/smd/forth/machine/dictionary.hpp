// src/smd/forth/machine/dictionary.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_MACHINE_DICTIONARY_HPP
#define SRC_SMD_FORTH_MACHINE_DICTIONARY_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/static_vector.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/data_space.hpp>
#include <smd/forth/machine/forth_state.hpp>

#include <array>
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

/// A minimal stack-effect summary attached to a colon-word dictionary entry.
///
/// F12's stack-effect analysis is the real owner of this information; this
/// struct only records enough for F9/F11 to thread something through:
/// how many cells the word is known to consume/produce, and whether that
/// count has actually been computed yet. F12 may replace or extend this
/// shape once real stack-effect checking lands.
struct stack_effect {
    int inputs = 0;     ///< Cells consumed, meaningful only if @ref known.
    int outputs = 0;    ///< Cells produced, meaningful only if @ref known.
    bool known = false; ///< True once a real effect has been computed.

    friend constexpr auto operator==(stack_effect const &, stack_effect const &)
        -> bool = default;
};

/// A resolved colon-definition binding.
struct colon_word {
    /// Handle into the elaborated-core arena F11 will build. Deliberately a
    /// bare @c int (rather than a typed @c arena_box) -- F9 does not know
    /// F11's elaborated-core node type, so it cannot name a typed handle to
    /// it yet; F11 is expected to reinterpret this as an index into its own
    /// arena.
    int core_id = -1;
    stack_effect effect{}; ///< See @ref stack_effect.
};

/// A `VARIABLE`- or `CREATE`-defined word's binding: the data-space address
/// it names.
///
/// F11 also uses this binding for `CREATE` (there is no separate binding
/// kind for it): `CREATE NAME` installs @c NAME at the current data-space
/// top with no cells allotted, while `VARIABLE NAME` allots exactly one
/// cell first; both end up as "a name bound to an @ref addr" at the
/// dictionary level, and F16's `ALLOT` is what later actually extends
/// storage past a `CREATE`d address.
///
/// @note Was a placeholder plain @ref cell until DIV-0004
/// (`docs/divergences/DIV-0004-dictionary-addr-placeholder.md`) was
/// resolved in F11: D10 calls for `addr` to be its own distinct type,
/// explicitly convertible to/from @ref cell, and F10's @ref machine::addr
/// (`data_space.hpp`) is that type.
// d5b20e62-4731-4fbd-ac00-3c4b435207f4
struct variable_word {
    addr address{};
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

/// The set of things a dictionary entry can be bound to.
using dictionary_binding = std::variant<primitive, colon_word, variable_word,
                                        constant_word, foreign_word>;

/// One dictionary entry: a folded name plus what it is bound to.
template <int MaxName = 32>
struct dictionary_entry {
    word_name<MaxName> name{};
    dictionary_binding binding{};
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

    /// Defines @p name_text as a colon word.
    /// Diagnoses dictionary-full rather than overflowing.
    constexpr auto define_colon(std::string_view name_text, colon_word word)
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

    /// Looks up @p name_text, newest definition first (shadowing).
    /// Folds @p name_text to uppercase before comparing. Returns `nullptr`
    /// if no entry matches.
    [[nodiscard]] constexpr auto lookup(std::string_view name_text) const
        -> dictionary_entry<MaxName> const *;

    /// Looks up @p name_text like @ref lookup, but returns the matching
    /// entry's index (0-based, insertion order) rather than a pointer, or
    /// `-1` if no entry matches. F11's elaborator uses this to resolve a
    /// word reference to the @c word_index a @ref colon_word call
    /// (`core_call`/`core_push_xt` in the elaborated core) stores.
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
                          dictionary_binding binding) -> status;

    foundation::static_vector<dictionary_entry<MaxName>, MaxWords> entries_{};
};

template <int MaxWords, int MaxName>
constexpr auto dictionary<MaxWords, MaxName>::insert(std::string_view name_text,
                                                     dictionary_binding binding)
    -> status {
    if (entries_.size() >= MaxWords) {
        return foundation::parse_error{foundation::source_pos{},
                                       "dictionary full"};
    }
    entries_.push_back(
        dictionary_entry<MaxName>{make_word_name<MaxName>(name_text), binding});
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
dictionary<MaxWords, MaxName>::define_colon(std::string_view name_text,
                                            colon_word word) -> status {
    return insert(name_text, dictionary_binding{word});
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

/// Builds a dictionary with every F8/F13/F16 primitive installed under its
/// Forth name (`+ - * / MOD NEGATE ABS MIN MAX AND OR XOR INVERT LSHIFT
/// RSHIFT 1- 0= 0< = <> < > <= >= TRUE FALSE DUP DROP SWAP OVER ROT ?DUP NIP
/// TUCK DEPTH >R R> R@ . .S EMIT CR @ ! +! ALLOT`) -- 46 words, one per @ref
/// primitive enumerator (`.`, `.S`, `EMIT`, `CR` are step F13's output words,
/// D10; `1-` is also from that step, see DIV-0007; `@`, `!`, `+!`, `ALLOT`
/// are step F16's memory words, D10).
///
/// @tparam MaxWords Dictionary capacity; must be at least 46 plus whatever
///                  room the caller wants for later colon/variable/constant/
///                  foreign definitions.
/// @tparam MaxName  Maximum name length.
template <int MaxWords = 256, int MaxName = 32>
constexpr auto default_dictionary() -> dictionary<MaxWords, MaxName> {
    dictionary<MaxWords, MaxName> dict;
    constexpr std::array<std::pair<std::string_view, primitive>, 47> words{{
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
    }};
    for (auto const &[name_text, op] : words) {
        (void)dict.define_primitive(name_text, op);
    }
    return dict;
}

namespace detail {

// Every binding alternative, the closed binding variant, one entry, and the
// dictionary itself must all be trivially destructible: the dictionary is a
// fixed-capacity foundation::static_vector, never a destructor-owning heap
// container. Checked against one concrete instantiation (256 words, 32-char
// names) as a representative sample.
static_assert(std::is_trivially_destructible_v<stack_effect>);
static_assert(std::is_trivially_destructible_v<colon_word>);
static_assert(std::is_trivially_destructible_v<variable_word>);
static_assert(std::is_trivially_destructible_v<constant_word>);
static_assert(std::is_trivially_destructible_v<foreign_word>);
static_assert(std::is_trivially_destructible_v<dictionary_binding>);
static_assert(std::is_trivially_destructible_v<dictionary_entry<32>>);
static_assert(std::is_trivially_destructible_v<dictionary<256, 32>>);

} // namespace detail

} // namespace smd::forth::machine

#endif
