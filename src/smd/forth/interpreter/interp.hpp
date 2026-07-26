// src/smd/forth/interpreter/interp.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_INTERP_HPP
#define SRC_SMD_FORTH_INTERPRETER_INTERP_HPP

#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/result.hpp>
#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/foundation/source_span.hpp>
#include <smd/forth/interpreter/compilebuf.hpp>
#include <smd/forth/interpreter/input_source.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/dictionary.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>
#include <smd/forth/parser/cursor.hpp>
#include <smd/forth/parser/forth_chars.hpp>

#include <string_view>
#include <type_traits>
#include <variant>

namespace smd::forth::interpreter {

// Step F24 (docs/forth-plan-2.md): the Forth-2012 section 3.4 outer text
// interpreter, interpret state only (D13). F24 itself never left STATE 0 --
// there was no `:` yet -- but the field already existed, tested to exist and
// default correctly, because forth_state growing STATE/BASE/SOURCE/>IN
// together is the point of D13, not something to phase in word by word.
//
// D19 places "combinators below the word, owning scanning and
// classification" under parser<F>; F24's own number-per-BASE classification
// (is_number_token_in_base/token_to_cell_in_base below) follows that
// placement, generalizing parser::is_number_token/token_to_cell
// (parser/forth_chars.hpp, fixed to decimal per D8's original scope) rather
// than editing them. forth_chars.hpp itself relocated here from
// reader/forth_chars.hpp at step F26 ("the cut"): DIV-0012 deferred that move
// while reader/ was still alive for the R1 pipeline; F26 deletes reader/
// wholesale, so the deferral's own reason is gone and the file lands at the
// parser/ location D19 always recommended.
//
// Step F25 is what actually writes STATE: `interpret` below now compiles as
// well as interprets. `:`, `;`, `EXIT`, and `RECURSE` are recognized by
// direct name comparison before any dictionary lookup -- the same technique
// the (now-deleted) R1 elaborator used for `I`/`J`/`LEAVE`/`UNLOOP`; F26 adds
// `VARIABLE`/`CONSTANT`/`CREATE` to this same direct-name set (see below),
// since F26's own merge criterion requires the F16 memory-word programs to
// run through `compiled_forth` again and nothing else yet resolves them.
// Generalized immediate-word dispatch through the dictionary itself is
// F27's own job, not this one. Compiling a colon definition appends directly
// into a compile_buffer (interpreter/compilebuf.hpp, D16's retained
// compiled_program as the toolkit's own artifact shape) as each token is
// met, one instruction at a time; there is no elaborated tree anywhere in
// this path. Interpreting an already-compiled word (D14) calls into that
// same code space via compile_buffer::call_word, against the identical live
// @p st this loop itself is mutating -- one semantics, not a second
// evaluator.
//
// DIV-0012 also asked this step to consider folding SOURCE/BASE/STATE
// directly into machine::forth_state, collapsing this composed wrapper.
// F26 does not do that fold: see DIV-0012's own "F26 disposition" section
// for why (this step's own scope -- the cut, the retarget, and the F16
// memory-word additions below -- is already at the edge of one mergeable
// step; the fold touches every remaining consumer of machine::forth_state
// and is deferred, not abandoned).

/// The interpreter's own Forth machine state: @ref machine::forth_state (the
/// stacks, data space, and output buffer) plus the three fields D13 adds on
/// top of it -- an @ref input_source (`SOURCE`/`>IN`), `BASE` (default 10),
/// and `STATE` (0 = interpreting; F25 is what first writes a nonzero value).
/// Composition, not inheritance or an in-place edit of @ref machine::
/// forth_state: every other consumer of @ref machine::forth_state (@ref
/// machine::run, @ref machine::eval_program, `forth.hpp`'s
/// `forth_program`) keeps working unchanged against the narrower type, and
/// this wider one stays a literal, trivially destructible aggregate exactly
/// like its inner @ref machine::forth_state already is (D3).
///
/// @tparam MaxDepth  Data stack capacity, in cells (@ref machine::
///                   forth_state).
/// @tparam MaxRDepth Return stack capacity, in cells, likewise.
/// @tparam MaxData   Data space capacity, in cells, likewise.
/// @tparam MaxOut    Output buffer capacity, in characters, likewise.
/// @tparam MaxName   Maximum scanned-token length, in characters -- shared
///                   with the @ref machine::dictionary this state's own
///                   @ref interpret is run against, since a token longer
///                   than a dictionary name could never match one anyway.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut,
          int MaxName = 32>
class forth_state {
  public:
    /// The wrapped @ref machine::forth_state type. Qualified from the
    /// global namespace rather than written as the shorter `machine::...`:
    /// this class declares its own member function named @ref machine
    /// below, and resolving the alias unambiguously here (rather than
    /// relying on point-of-declaration lookup ordering) keeps the two
    /// `machine` names from ever being a source of confusion.
    using machine_state =
        ::smd::forth::machine::forth_state<MaxDepth, MaxRDepth, MaxData,
                                           MaxOut>;

    constexpr forth_state() = default;

    /// Constructs a forth_state whose @ref source is positioned at the start
    /// of @p text, with `BASE` 10 and `STATE` 0 (interpreting).
    constexpr explicit forth_state(std::string_view text) : source_{text} {}

    /// The wrapped machine state: stacks, data space, output buffer.
    [[nodiscard]] constexpr auto machine() -> machine_state &;
    /// The wrapped machine state: stacks, data space, output buffer.
    [[nodiscard]] constexpr auto machine() const -> machine_state const &;

    /// `SOURCE`/`>IN` (D13, D19): see @ref input_source.
    [[nodiscard]] constexpr auto source() -> input_source &;
    /// `SOURCE`/`>IN` (D13, D19): see @ref input_source.
    [[nodiscard]] constexpr auto source() const -> input_source const &;

    /// `BASE`: the radix number-per-BASE classification and formatting use.
    /// Defaults to 10.
    [[nodiscard]] constexpr auto base() const -> int;
    /// Sets `BASE`. No range check here (F24 has no `HEX`/`DECIMAL` word to
    /// route through one); a caller-supplied value outside 2..36 simply
    /// makes every subsequent number classification fail, which is a
    /// diagnosed "unknown word" at the interpreter loop, not UB.
    constexpr auto set_base(int value) -> void;

    /// `STATE`: 0 while interpreting, nonzero while compiling (`:` sets it,
    /// `;` clears it -- F25).
    [[nodiscard]] constexpr auto state() const -> int;
    /// Sets `STATE`. Written by @ref interpret itself as `:`/`;` are met
    /// (F25); present since F24 because D13 puts `STATE` in forth_state
    /// from the start, not as a later retrofit.
    constexpr auto set_state(int value) -> void;

  private:
    machine_state machine_{};
    input_source source_{};
    int base_{10};
    int state_{0};
};

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::machine()
    -> machine_state & {
    return machine_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::machine() const
    -> machine_state const & {
    return machine_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::source()
    -> input_source & {
    return source_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::source() const
    -> input_source const & {
    return source_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::base() const
    -> int {
    return base_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::set_base(int value)
    -> void {
    base_ = value;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::state() const
    -> int {
    return state_;
}

template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxName>
constexpr auto
forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName>::set_state(int value)
    -> void {
    state_ = value;
}

namespace detail {

static_assert(std::is_trivially_destructible_v<forth_state<64, 64, 1024, 256>>);

} // namespace detail

/// Returns the value of digit @p c in @p base, or -1 if @p c is not a valid
/// digit in that base. Letters above `9` are `A`..`Z` -- already uppercase,
/// since @ref parser::scan_word folds every token before this ever sees it
/// (D8) -- so base 16's `FF` and base 36's `Z` both work without a separate
/// lowercase branch.
[[nodiscard]] constexpr auto digit_value(char c, int base) -> int {
    int value = -1;
    if (c >= '0' && c <= '9') {
        value = c - '0';
    } else if (c >= 'A' && c <= 'Z') {
        value = c - 'A' + 10;
    }
    if (value < 0 || value >= base) {
        return -1;
    }
    return value;
}

/// True iff @p text is an optional leading `-` followed by one or more
/// valid digits in @p base, and nothing else -- @ref parser::is_number_token
/// generalized from decimal to `BASE` (D19): at `base == 10` the two agree
/// on every input (this header's own tests check that directly), since
/// letters are never valid decimal digits either way.
[[nodiscard]] constexpr auto is_number_token_in_base(std::string_view text,
                                                     int base) -> bool {
    std::size_t i = 0;
    if (!text.empty() && text[0] == '-') {
        i = 1;
    }
    if (i == text.size()) {
        return false;
    }
    for (; i < text.size(); ++i) {
        if (digit_value(text[i], base) < 0) {
            return false;
        }
    }
    return true;
}

/// Converts @p text into its signed value in @p base.
/// @pre is_number_token_in_base(text, base)
[[nodiscard]] constexpr auto token_to_cell_in_base(std::string_view text,
                                                   int base) -> machine::cell {
    bool const negative = !text.empty() && text[0] == '-';
    std::size_t i = negative ? 1 : 0;
    machine::cell value = 0;
    for (; i < text.size(); ++i) {
        value = value * base + digit_value(text[i], base);
    }
    return negative ? -value : value;
}

// Merge criteria (static_assert, immediately-invoked-lambda pattern):
// base-10 agreement with parser::is_number_token/token_to_cell, and a
// representative base-16 case.

static_assert(is_number_token_in_base("-1", 10) ==
              parser::is_number_token("-1"));
static_assert(is_number_token_in_base("1-", 10) ==
              parser::is_number_token("1-"));
static_assert(is_number_token_in_base("-", 10) == parser::is_number_token("-"));
static_assert(is_number_token_in_base("42", 10) ==
              parser::is_number_token("42"));
static_assert(is_number_token_in_base("DUP", 10) ==
              parser::is_number_token("DUP"));
static_assert(token_to_cell_in_base("42", 10) == parser::token_to_cell("42"));
static_assert(token_to_cell_in_base("-1", 10) == parser::token_to_cell("-1"));

static_assert(is_number_token_in_base("FF", 16));
static_assert(token_to_cell_in_base("FF", 16) == 255);
static_assert(!is_number_token_in_base("FG", 16));
static_assert(is_number_token_in_base("-FF", 16));
static_assert(token_to_cell_in_base("-FF", 16) == -255);

/// Decrements @p fuel, the text interpreter loop's own step budget (D22):
/// distinct from @ref machine::consume_vm_fuel (which only bounds a
/// *compiled* program's VM execution) and from @ref machine::consume_fuel
/// (which only bounds the direct evaluator) -- this one bounds @ref
/// interpret's own outer loop, so a pathological interpret-time source
/// (arbitrarily many tokens, none of which ever exhausts the data stack or
/// any other already-fueled subsystem) cannot run the constant evaluator
/// past its budget undiagnosed. Diagnoses exhaustion at @p pos rather than
/// looping forever. Called once per token @ref interpret actually
/// processes, so a budget of @p fuel bounds the total number of tokens any
/// one interpretation may consume.
[[nodiscard]] constexpr auto consume_interp_fuel(int &fuel,
                                                 foundation::source_pos pos)
    -> machine::status {
    if (fuel <= 0) {
        return foundation::parse_error{
            pos, "interpreter execution budget exhausted"};
    }
    --fuel;
    return std::monostate{};
}

/// The result of @ref scan_colon_header: a `:` definition's folded name,
/// plus its declared `( ... -- ... )` stack-effect comment span if one was
/// present (D20: captured, not verified until F30).
template <int MaxName>
struct colon_header {
    machine::word_name<MaxName> name{};
    foundation::source_span effect{};
    bool has_effect = false;
};

/// Scans the name following `:` and, if one is immediately present, a
/// declared `( ... -- ... )` stack-effect comment -- the header-opening half
/// of `:`, split out of @ref interpret only to keep that function's own loop
/// body readable.
///
/// @p name_cur must already be positioned at (or before, across only plain
/// whitespace/comments) the name itself -- @ref interpret passes its own
/// post-`:`-token cursor, which @ref parser::scan_word's trailing skip has
/// already advanced past any intertoken space before the name.
///
/// Unlike an ordinary @ref parser::scan_word call, this does not use @ref
/// parser::forth_lexeme's trailing skip to find the name's own end: a
/// `( ... )` comment immediately after the name must still be visible to
/// capture here, not already silently consumed as ordinary intertoken space
/// the way it would be for any other token.
template <int MaxName>
[[nodiscard]] constexpr auto scan_colon_header(parser::cursor name_cur)
    -> parser::parse_result<colon_header<MaxName>> {
    auto name_scanned = parser::scan_word<MaxName>(name_cur);
    if (!name_scanned.has_value()) {
        // Unreachable in practice, mirroring interpret's own identical
        // defensive comment: scan_word's some<> only ever fails on empty
        // input, and an empty name is diagnosed explicitly just below
        // instead.
        return name_scanned.error();
    }
    auto const &folded_name = name_scanned.value().value;
    if (folded_name.empty()) {
        return foundation::parse_error{name_cur.position(),
                                       "expected a name after :"};
    }

    // Recover the cursor immediately after the name's own characters, by
    // replaying bump() folded_name.size() times from name_cur -- the same
    // "replay to recover a position" technique input_source::cursor_at_in
    // already uses -- rather than trusting scan_word's own rest cursor,
    // which has already skipped past any trailing comment as ordinary
    // intertoken space (this function's own top comment).
    auto after_name = name_cur;
    for (int i = 0; i < folded_name.size() && !after_name.empty(); ++i) {
        after_name = after_name.bump();
    }

    auto after_ws = parser::skip_intertoken_space(after_name);
    if (!after_ws.empty() && after_ws.peek() == '(') {
        auto comment = parser::scan_paren_comment(after_ws);
        if (comment.has_value()) {
            return parser::parse_state<colon_header<MaxName>>{
                colon_header<MaxName>{.name = folded_name,
                                      .effect = comment.value().value,
                                      .has_effect = true},
                comment.value().rest};
        }
        // An unterminated '(' right after the name: fall through and leave
        // the cursor at the name's own end -- the very next token scan will
        // fail on the dangling '(' in context, exactly like
        // parser::skip_forth_space's own defensive choice for an
        // unterminated comment anywhere else.
    }
    return parser::parse_state<colon_header<MaxName>>{
        colon_header<MaxName>{.name = folded_name}, after_name};
}

// aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62
/// The Forth-2012 section 3.4 outer text interpreter (D13): scans one word
/// at a time from @p st's own @ref input_source and, per @p st's own
/// `STATE`, either interprets it or compiles it into @p buf, repeating
/// until @p st's source is exhausted or the first diagnosed error, whichever
/// comes first.
///
/// **Interpreting** (`STATE == 0`): a dictionary hit runs (a @ref
/// machine::primitive, via @ref machine::apply_primitive), calls (a @ref
/// machine::compiled_colon_word, via @ref call_word -- D14's "interpreting a
/// defined word runs its code on the VM against the live forth_state, one
/// semantics"), or pushes (a @ref machine::variable_word's address or a
/// @ref machine::constant_word's value); a miss is classified as a number
/// per @p st's own `BASE` and pushed, or diagnosed as an unknown word. `;`,
/// `EXIT`, and `RECURSE` are compile-only (D13, Forth-2012): diagnosed here
/// rather than acted on. `VARIABLE`, `CREATE`, and `CONSTANT` (F26) are
/// ordinary defining words, recognized here by the same direct-name
/// technique as `:`: they scan a following name and install a
/// @ref machine::variable_word (allotting one cell for `VARIABLE`, none for
/// `CREATE`) or a @ref machine::constant_word (popping the value `CONSTANT`
/// binds) into @p dict.
///
/// **Compiling** (`STATE == 1`, entered by `:`): a dictionary hit emits
/// (@ref machine::op::prim for a primitive, @ref machine::op::call for a
/// @ref machine::compiled_colon_word, or @ref machine::op::push for a
/// variable's address or a constant's value, baked in as a literal exactly
/// as the R1 elaborator's `core_var`/`core_const` once did) rather than
/// running; a miss is classified as a number and emitted as
/// @ref machine::op::push. `;` emits @ref machine::op::ret, installs the
/// finished @ref machine::compiled_colon_word into @p dict, and clears
/// `STATE`. `EXIT` emits @ref machine::op::ret without closing the
/// definition. `RECURSE` emits a self-@ref machine::op::call to the entry
/// point @ref scan_colon_header's own caller (`:` itself, below) recorded
/// *before* the body was compiled -- F14's discipline, carried forward
/// unchanged, is what makes this resolve without any back-patching.
/// `VARIABLE`/`CREATE`/`CONSTANT` are not recognized while compiling (F26
/// does not give defining-inside-a-colon-definition its own semantics): a
/// dictionary miss on one of those names inside `:` ... `;` falls through to
/// the same "unknown word" diagnosis any other undefined name would get.
///
/// `\` line comments and `( ... )` comments are ordinary intertoken space to
/// this loop (@ref parser::skip_forth_space, invoked here via @ref
/// parser::scan_word's own leading skip) in every position except
/// immediately after a `:` definition's own name, where @ref
/// scan_colon_header instead captures a `( ... )` comment as the declared
/// effect (D20: stored, unverified until F30).
///
/// Every stack/data-space/code-space misuse a primitive's own @ref
/// machine::apply_primitive, or @ref compile_buffer::emit, can diagnose is
/// returned here unchanged -- the same @ref foundation::parse_error value
/// either would have produced directly, not a re-diagnosed or repositioned
/// one.
///
/// @tparam MaxDepth    @p st's data stack capacity, in cells.
/// @tparam MaxRDepth   @p st's return stack capacity, in cells.
/// @tparam MaxData     @p st's data space capacity, in cells.
/// @tparam MaxOut      @p st's output buffer capacity, in characters.
/// @tparam MaxWords    @p dict's capacity, in entries -- independent of
///                     @p buf's own @c MaxBufWords (@ref compile_buffer's
///                     word-table capacity is unused by this step: every
///                     @ref machine::compiled_colon_word carries its own
///                     entry point directly, see compilebuf.hpp), so a small
///                     custom @p dict may pair with a generously sized
///                     @p buf, or vice versa, without their capacities
///                     having to match.
/// @tparam MaxCode     @p buf's instruction-array capacity.
/// @tparam MaxBufWords @p buf's own (unused by this step) word-table
///                     capacity.
/// @tparam MaxName     Maximum word-name/token length, in characters; shared
///                     between @p st and @p dict (see @ref forth_state).
/// @param  st      The interpreter state to run against; mutated in place --
///                 its stacks, output buffer, `STATE`, and `>IN` all advance
///                 as the loop runs, even on the run that ends in a
///                 diagnosed error.
/// @param  dict    The dictionary to resolve words against, and to install
///                 every finished colon definition into.
/// @param  buf     The code space every `:` ... `;` pair compiles into.
/// @param  fuel    The interpreter loop's own step budget (@ref
///                 consume_interp_fuel): decremented once per token
///                 processed; exhaustion is a diagnosed error, never a hang.
/// @param  vm_fuel The VM's own step budget (D22, a distinct budget from
///                 @p fuel), passed to @ref call_word each time interpreting
///                 a defined word actually runs one.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxWords,
          int MaxCode, int MaxBufWords, int MaxName = 32>
constexpr auto
interpret(forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName> &st,
          machine::dictionary<MaxWords, MaxName> &dict,
          compile_buffer<MaxCode, MaxBufWords> &buf, int fuel = 100000,
          int vm_fuel = 100000) -> machine::status {
    // Bookkeeping for the definition currently being compiled, if any --
    // local to this call frame, not part of forth_state itself (DIV-0012:
    // keep the seam thin, add nothing that makes F26's later fold of
    // SOURCE/BASE/STATE into machine::forth_state harder). Meaningful only
    // while st.state() != 0.
    machine::word_name<MaxName> compiling_name{};
    int compiling_entry = -1;
    foundation::source_span compiling_effect{};
    bool compiling_has_effect = false;

    for (;;) {
        auto pre = st.source().cursor_at_in();
        auto token_start = parser::skip_forth_space(pre);
        if (token_start.empty()) {
            if (st.state() != 0) {
                return foundation::parse_error{token_start.position(),
                                               "unterminated colon definition"};
            }
            // Nothing left but whitespace/comments: a clean end of source,
            // not an error.
            return std::monostate{};
        }

        auto budget = consume_interp_fuel(fuel, token_start.position());
        if (!budget.has_value()) {
            return budget;
        }

        auto scanned = parser::scan_word<MaxName>(pre);
        if (!scanned.has_value()) {
            // Unreachable in practice: token_start non-empty means at least
            // one is_word_char run exists at that position, so scan_word's
            // own some<> cannot fail here. Diagnosed defensively, per D7,
            // rather than assumed away.
            return scanned.error();
        }

        auto const &token = scanned.value().value;
        st.source().set_in(scanned.value().rest.position().offset);

        std::string_view text{token.begin(),
                              static_cast<std::size_t>(token.size())};

        if (st.state() == 0) {
            // -- Interpreting -----------------------------------------

            // 5d9a3f7c-2e6b-4a81-9c4d-7f1b8e3a6d52
            if (text == ":") {
                auto header =
                    scan_colon_header<MaxName>(st.source().cursor_at_in());
                if (!header.has_value()) {
                    return header.error();
                }
                compiling_name = header.value().value.name;
                compiling_effect = header.value().value.effect;
                compiling_has_effect = header.value().value.has_effect;
                st.source().set_in(header.value().rest.position().offset);
                compiling_entry = buf.here();
                st.set_state(1);
                continue;
            }
            // 5d9a3f7c-2e6b-4a81-9c4d-7f1b8e3a6d52 end
            if (text == ";") {
                return foundation::parse_error{
                    token_start.position(),
                    "\";\" is compile-only, used while interpreting"};
            }
            if (text == "EXIT") {
                return foundation::parse_error{
                    token_start.position(),
                    "EXIT is compile-only, used while interpreting"};
            }
            if (text == "RECURSE") {
                return foundation::parse_error{
                    token_start.position(),
                    "RECURSE is compile-only, used while interpreting"};
            }
            if (text == "VARIABLE" || text == "CREATE") {
                // F26 ("the cut"): VARIABLE/CREATE were R1-elaborator syntax
                // productions (reader::syn_variable/syn_create); D13 has no
                // reader phase, so they are ordinary defining words the
                // interpreter recognizes by direct name comparison, exactly
                // like `:` above -- generalized immediate-word dispatch
                // through the dictionary is still F27's own job. VARIABLE
                // allots one cell; CREATE allots none, matching the R1
                // elaborator's own documented reading of "CREATE reserves an
                // address with nothing behind it yet" (machine/dictionary.hpp
                // variable_word's own doc comment) -- both end up as a name
                // bound to a data-space address, the same variable_word
                // binding machine::dictionary already carries.
                auto name_scanned =
                    parser::scan_word<MaxName>(st.source().cursor_at_in());
                if (!name_scanned.has_value()) {
                    return name_scanned.error();
                }
                auto const &folded_name = name_scanned.value().value;
                if (folded_name.empty()) {
                    return foundation::parse_error{
                        token_start.position(),
                        "expected a name after VARIABLE/CREATE"};
                }
                st.source().set_in(name_scanned.value().rest.position().offset);
                int const cells_to_allot = (text == "VARIABLE") ? 1 : 0;
                auto a = st.machine().data_space().allot(cells_to_allot);
                if (!a.has_value()) {
                    return a.error();
                }
                std::string_view name_text{
                    folded_name.begin(),
                    static_cast<std::size_t>(folded_name.size())};
                auto def_r = dict.define_variable(
                    name_text, machine::variable_word{a.value()});
                if (!def_r.has_value()) {
                    return def_r;
                }
                continue;
            }
            if (text == "CONSTANT") {
                // Forth-2012 CONSTANT semantics directly, not R1's
                // elaborate_constant (which required a syntactically
                // preceding literal token and constant-folded it): pop
                // whatever the data stack holds at this point, whatever
                // pushed it there -- `7 CONSTANT LUCKY` and
                // `3 4 + CONSTANT SEVEN` are equally valid under an
                // interpreter that has already executed the "7" (or "3 4 +")
                // before meeting CONSTANT.
                auto value = st.machine().data().pop();
                if (!value.has_value()) {
                    return value.error();
                }
                auto name_scanned =
                    parser::scan_word<MaxName>(st.source().cursor_at_in());
                if (!name_scanned.has_value()) {
                    return name_scanned.error();
                }
                auto const &folded_name = name_scanned.value().value;
                if (folded_name.empty()) {
                    return foundation::parse_error{
                        token_start.position(),
                        "expected a name after CONSTANT"};
                }
                st.source().set_in(name_scanned.value().rest.position().offset);
                std::string_view name_text{
                    folded_name.begin(),
                    static_cast<std::size_t>(folded_name.size())};
                auto def_r = dict.define_constant(
                    name_text, machine::constant_word{value.value()});
                if (!def_r.has_value()) {
                    return def_r;
                }
                continue;
            }

            auto const *entry = dict.lookup(text);
            if (entry != nullptr) {
                if (auto const *op =
                        std::get_if<machine::primitive>(&entry->binding)) {
                    auto r = machine::apply_primitive(*op, st.machine());
                    if (!r.has_value()) {
                        return r;
                    }
                    continue;
                }
                if (auto const *cw = std::get_if<machine::compiled_colon_word>(
                        &entry->binding)) {
                    auto r =
                        call_word(buf, st.machine(), cw->entry_point, vm_fuel);
                    if (!r.has_value()) {
                        return r;
                    }
                    continue;
                }
                if (auto const *vw =
                        std::get_if<machine::variable_word>(&entry->binding)) {
                    auto r = st.machine().data().push(
                        static_cast<machine::cell>(vw->address));
                    if (!r.has_value()) {
                        return r;
                    }
                    continue;
                }
                if (auto const *cnw =
                        std::get_if<machine::constant_word>(&entry->binding)) {
                    auto r = st.machine().data().push(cnw->value);
                    if (!r.has_value()) {
                        return r;
                    }
                    continue;
                }
                return foundation::parse_error{
                    token_start.position(),
                    "word is not executable yet (F25: primitives and colon "
                    "words only)"};
            }

            if (is_number_token_in_base(text, st.base())) {
                auto r = st.machine().data().push(
                    token_to_cell_in_base(text, st.base()));
                if (!r.has_value()) {
                    return r;
                }
                continue;
            }

            return foundation::parse_error{token_start.position(),
                                           "unknown word"};
        }

        // -- Compiling -------------------------------------------------

        if (text == ":") {
            return foundation::parse_error{token_start.position(),
                                           "nested : is not allowed"};
        }
        // 8b4e1c7a-3f9d-4a62-8e5b-1d7c4a9f6e38
        if (text == ";") {
            auto ret_r = buf.emit(machine::op::ret, machine::cell{0},
                                  token_start.position());
            if (!ret_r.has_value()) {
                return ret_r.error();
            }
            std::string_view name_text{
                compiling_name.begin(),
                static_cast<std::size_t>(compiling_name.size())};
            auto def_r = dict.define_compiled_colon(
                name_text, machine::compiled_colon_word{
                               .entry_point = compiling_entry,
                               .effect_span = compiling_effect,
                               .has_effect = compiling_has_effect});
            if (!def_r.has_value()) {
                return def_r;
            }
            compiling_entry = -1;
            st.set_state(0);
            continue;
        }
        if (text == "EXIT") {
            auto r = buf.emit(machine::op::ret, machine::cell{0},
                              token_start.position());
            if (!r.has_value()) {
                return r.error();
            }
            continue;
        }
        if (text == "RECURSE") {
            auto r = buf.emit(machine::op::call,
                              static_cast<machine::cell>(compiling_entry),
                              token_start.position());
            if (!r.has_value()) {
                return r.error();
            }
            continue;
        }
        // 8b4e1c7a-3f9d-4a62-8e5b-1d7c4a9f6e38 end

        auto const *entry = dict.lookup(text);
        if (entry != nullptr) {
            if (auto const *op =
                    std::get_if<machine::primitive>(&entry->binding)) {
                auto r =
                    buf.emit(machine::op::prim, static_cast<machine::cell>(*op),
                             token_start.position());
                if (!r.has_value()) {
                    return r.error();
                }
                continue;
            }
            if (auto const *cw = std::get_if<machine::compiled_colon_word>(
                    &entry->binding)) {
                auto r = buf.emit(machine::op::call,
                                  static_cast<machine::cell>(cw->entry_point),
                                  token_start.position());
                if (!r.has_value()) {
                    return r.error();
                }
                continue;
            }
            if (auto const *vw =
                    std::get_if<machine::variable_word>(&entry->binding)) {
                // A variable's address is baked in as a literal, exactly the
                // way the R1 elaborator's core_var did (docs/
                // compiler_architecture.org's Phase 4 section, now history).
                auto r = buf.emit(machine::op::push,
                                  static_cast<machine::cell>(vw->address),
                                  token_start.position());
                if (!r.has_value()) {
                    return r.error();
                }
                continue;
            }
            if (auto const *cnw =
                    std::get_if<machine::constant_word>(&entry->binding)) {
                auto r = buf.emit(machine::op::push, cnw->value,
                                  token_start.position());
                if (!r.has_value()) {
                    return r.error();
                }
                continue;
            }
            return foundation::parse_error{
                token_start.position(),
                "word is not compilable yet (F25: primitives and colon "
                "words only)"};
        }

        if (is_number_token_in_base(text, st.base())) {
            auto r = buf.emit(machine::op::push,
                              token_to_cell_in_base(text, st.base()),
                              token_start.position());
            if (!r.has_value()) {
                return r.error();
            }
            continue;
        }

        return foundation::parse_error{token_start.position(), "unknown word"};
    }
}
// aa1d6f83-9b3c-4e2a-8d5f-3c7b1e9a4f62 end

} // namespace smd::forth::interpreter

#endif
