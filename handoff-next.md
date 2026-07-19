# Next step: Step F11 — elaborated core and resolution

Step F7 (grammar) is done in worktree `wt-f7` / branch `step/f7`. Together
with F6 (syntax tree, already merged), F9 (dictionary, already merged), and
F10 (data space, already merged), every dependency Step F11 needs is now
satisfied. This file is a full rewrite for F11 — see `handoff.md`'s "Step
F7 — Grammar" section (and the F2–F10 sections above it) for the complete
historical record; this file only summarizes what F11 needs to start.

## What F11 is

Read `docs/forth-plan.md` section "Step F11 — elaborated core and
resolution" for the authoritative spec. In short: F11 walks the syntax tree
(`src/smd/forth/reader/syntax_tree.hpp`, produced by F7's
`read_program`) in program order, threading a `machine::dictionary` forward
as it goes, resolving every `syn_word`/`syn_tick` reference against that
dictionary, and building a new tree — the elaborated core — in the same
flat-arena, `arena_box`-handle style as the syntax tree, per D3. Program-
order threading is what makes static binding "just work": a colon
definition's body resolves against whatever the dictionary looks like *at
the point that definition is elaborated*, so a later redefinition of a word
does not retroactively change an earlier definition's already-elaborated
calls (`dictionary::lookup` is newest-first, and `dictionary`'s `define_*`
methods never remove or overwrite a shadowed entry — see `handoff.md`'s
"Step F9 — Dictionary" section for the exact mechanics).

## The grammar/syntax-tree API F11 consumes (Step F7)

- **Entry point:** `template <int MaxNodes = 1024, int MaxBody = 64, int
  MaxName = 32, int MaxDepth = 32> constexpr auto smd::forth::reader::
  read_program(std::string_view source) -> foundation::result<
  reader::syntax_tree<MaxNodes, MaxBody, MaxName>>`
  (`src/smd/forth/reader/read_program.hpp`). Note the extra `MaxDepth`
  parameter relative to `syntax_tree`'s own three capacities — it only
  bounds the *parser's* recursion, not anything F11 needs to thread through;
  F11's elaborator should pick its own capacities (likely reusing
  `MaxNodes`/`MaxBody`/`MaxName` from whatever `syntax_tree` it was handed,
  plus a `MaxDepth`-shaped bound of its own if its own core-building
  recursion turns out to need one — see DIV-0005 below).
- The syntax tree's eleven node kinds (`syn_literal`, `syn_word<MaxName>`,
  `syn_colon_def<...>`, `syn_if<...>`, `syn_begin_until<...>`,
  `syn_begin_while<...>`, `syn_do_loop<...>`, `syn_variable<MaxName>`,
  `syn_constant<MaxName>`, `syn_create<MaxName>`, `syn_tick<MaxName>`) are
  fully populated by F7, including every `foundation::source_pos pos`
  field — F11 has real source positions to attach to its own diagnostics
  (e.g. "word X not found" at the position of the `syn_word` that names it),
  not placeholders.
- `syntax_tree.program` (a `syn_body<...>`, i.e. `static_vector` of
  `syn_box` handles) holds every top-level form in source order: colon
  defs, `VARIABLE`/`CONSTANT`/`CREATE` declarations, and any top-level
  executable words/literals/control-structures (the grammar's `item`
  production is a strict superset of `body-item`, so bare executable code
  at the top level parses too, even though the plan doesn't dwell on what
  it means — that semantic question is F11's/F12's, not F7's).
  **F11 should walk `program` in order**, dispatching on each item's
  `std::variant` alternative via `std::get`/`std::visit` against
  `tree.arena.get(handle).value`.
- `syn_colon_def.declared_effect` is a `foundation::source_span` into the
  *original source text* passed to `read_program` — empty (`first ==
  last`) if no qualifying comment was present, otherwise the exact span of
  a `( ... )` comment containing `--`, captured immediately after the
  definition's name. **F11 does not need to parse this span's text itself**
  (that's F12's job, stack-effect analysis) — F11 just needs to keep the
  original source text available (or the span itself) if it wants to
  thread a `colon_word.effect` guess through early, otherwise it can install
  every new colon word with `stack_effect{.known = false}` and leave it to
  F12, exactly as F9's `handoff.md` section already anticipated.
- `syn_constant` carries only a name (no value field) — per Forth-2012, a
  `CONSTANT`'s value comes from whatever the immediately preceding body
  form(s) left on the stack. **F7 does not resolve or attach that value**;
  it is still just a `syn_literal` (or other body form) followed by a
  `syn_constant` node as two separate top-level items in `program`, in
  source order. F11 is the step that has to notice this adjacency and
  compute/attach the actual constant value when it builds a
  `machine::constant_word`.
- Reserved words are enforced only at parse time (a colon-def may not name
  itself `IF`/`BEGIN`/etc.) — F11 does not need to re-check this; by the
  time a `syntax_tree` exists, every `syn_colon_def.name` is guaranteed not
  to collide with a reserved word. Ordinary word *resolution* failures
  (`dictionary::lookup` returning `nullptr` for a `syn_word`/`syn_tick`
  that names something never defined) are **not** caught by F7 at all —
  that diagnosis is entirely F11's to produce, with a real source position
  now available on every `syn_word`/`syn_tick` node to attach it to.

## DIV-0005 — read this before writing F11's own recursive walk

`docs/divergences/DIV-0005-grammar-recursive-descent.md`: F7's nested
control-structure productions (`IF`/`BEGIN...UNTIL`/`BEGIN...WHILE...
REPEAT`/`DO...LOOP`, each recursively containing more body-items) are
**not** built from chained F4 combinators (`map`/`lift2`/`operator|`) — a
self-recursive grammar production has no finite `parser<F>` type to
compose it from, and building tree nodes needs a mutable arena threaded
alongside the parser state, which the combinator calling convention
(`cursor -> parse_result<T>`) has no slot for. F7 instead used a small set
of mutually recursive plain `constexpr` function templates
(`parse_body_until`, `parse_body_item_from_token`, `parse_if`,
`parse_begin`, `parse_do`), each still built from F4/F5 free-function
combinators for its own token-level work, and bounded by an explicit
`MaxDepth` template parameter checked before ever recursing one level
deeper.

**F11 will almost certainly hit the identical wall**, since walking a
`syn_if`/`syn_begin_until`/etc. to build a corresponding elaborated-core
node is exactly this same "self-recursive tree shape plus a mutable arena"
situation, just walking instead of parsing. Reach for the same
plain-mutually-recursive-function-template shape (with its own bounded
recursion parameter if the elaborated core can nest independently of the
syntax tree's own nesting) rather than trying to force it through
`functor`/`applicative`/`alternative`-style combinators — there is nothing
in `foundation`'s typeclass machinery designed for tree-shaped recursion
with shared mutable state, and inventing one is out of scope for F11.

## Standing action item: DIV-0004 follow-up (retype `variable_word::addr`)

F9 (dictionary) filed DIV-0004 because `machine::variable_word::addr` had
to be a plain `cell` — F10 (the step that defines `machine::addr`, the
distinct typed address D10 calls for) ran in parallel off the same F8
baseline, so F9 had no dependency on it at the time. **Both F9 and F10 are
now merged.** Before or as part of F11's own work (F11 is the first step
that actually constructs `variable_word` values, when elaborating a
`VARIABLE` declaration), retype `machine::dictionary.hpp`'s
`variable_word::addr` field from `cell` to `machine::addr`
(`src/smd/forth/machine/data_space.hpp`'s type) — update the field's type
and `dictionary.hpp`'s include list (`#include
<smd/forth/machine/data_space.hpp>`); nothing else in `dictionary.hpp`
needs to change, since `addr`'s only current uses are construction and
storage, not arithmetic. Close DIV-0004 (mark it superseded/resolved) once
this lands, and record the change in `handoff.md` under whichever step
does it (most naturally F11, since F11 is the first real caller of
`define_variable`).

## Dictionary and data-space APIs F11 threads through elaboration

See `handoff.md`'s "Step F9 — Dictionary" and "Step F10 — Data space"
sections for full detail; summary:

- `machine::dictionary<MaxWords, MaxName>` (or
  `machine::default_dictionary<MaxWords, MaxName>()` to start with the 37
  F8 primitives pre-installed) is what F11 threads through the program-
  order walk. `lookup(std::string_view) const -> dictionary_entry<MaxName>
  const *` resolves a name (case-insensitively); `nullptr` means
  undefined — F11 must turn that into a real `parse_error`/diagnostic using
  the `syn_word`/`syn_tick`'s own `pos`, since `dictionary` itself has no
  notion of source position.
- `define_colon`/`define_variable`/`define_constant`/`define_foreign` each
  return `status`, diagnosing "dictionary full" rather than overflowing.
  Redefinition is always legal (never rejected) — F11 is expected to
  *notice* a pre-existing entry (via `lookup` before defining) and route
  that fact into whatever collected-diagnostics/warning channel it
  introduces, per the plan's "warn, don't error, on redefinition" note.
- `machine::data_space<MaxData>::allot(int) -> result<addr>` is what F11's
  `VARIABLE` elaboration should call (allot exactly one cell) to get the
  `addr` a `variable_word` should store (see the DIV-0004 follow-up above).

## Standing constraints (unchanged)

- The Makefile is the single build interface (`TOOLCHAIN`/`CONFIG` only).
- Baseline is `gnu++26` on `gcc-16` primary / `clang-21` secondary.
- Tests use Catch2; every public constexpr API needs a `static_assert`
  (immediately-invoked-lambda pattern where a parser/function call is
  needed); add matching `TEST_CASE`s for Catch2 visibility.
- Every tree is a flat `tree_arena` of trivially destructible nodes behind
  `arena_box` handles (D3) — no `fix`/`Box` anywhere in F11's elaborated
  core either; forward-declare the closed node type before its composite
  alternatives, exactly as F6's `syntax_tree.hpp` and F7's
  `read_program.hpp` both do.
- All capacities are template parameters with defaults; no hardcoded
  capacity constants.
- Before handoff: `make compile`, `make test`, `make lint` green on
  `gcc-16` (and `clang-21` if available); both `smoke.sh` runs end
  `SMOKE OK`; `checklist.md` ticked; `handoff.md` appended (not rewritten);
  `handoff-next.md` rewritten for whatever comes next (F12, per the plan's
  parallelism summary, unless it says otherwise); divergence docs filed for
  anything done differently than `docs/forth-plan.md` or Forth-2012
  semantics (next free number: check `docs/divergences/` — DIV-0005 is the
  latest as of this merge).

## Known open items going into F11

- The DIV-0004 follow-up above (retype `variable_word::addr`) — not yet
  done by anyone; F11 is the natural place.
- The recurring `make lint` `clang-format`/`gersemi` tooling-version drift
  on a shifting subset of pre-existing files (currently
  `machine/{CMakeLists.txt,data_space.hpp,dictionary.hpp,
  dictionary.test.cpp}` and `reader/forth_chars.test.cpp` in this worker's
  sandbox; different files were flagged at F6/F8/F9/F10) — still
  unresolved as a standing environment issue, not specific to any one
  step. Whoever eventually does a dedicated formatting-hygiene pass should
  treat it as one item, not something to chase file-by-file each step.
