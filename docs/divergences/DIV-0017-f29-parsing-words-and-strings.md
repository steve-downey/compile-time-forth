# DIV-0017: Parsing-word data placement, comment reexpression, and ABORT"'s interim runtime behavior

- **Status:** accepted-permanent, except `ABORT"`'s own interim hard-stop behavior (see Revisit condition)
- **Date:** 2026-07-26
- **Step:** F29 (parsing words and strings), docs/forth-plan-2.md
- **Authority diverged from:** Forth-2012 (`ABORT"`'s own runtime semantics, narrowed scope);
  docs/forth-plan-2.md (F29's own step text leaves the PARSE/WORD buffer strategy,
  the mechanics of reexpressing `(`/`\`, and S"'s own leading-whitespace handling open)

## What diverged

D19 says parsing words consume the same input stream as everything else, and names
`PARSE`/`WORD`/`CHAR`/`[CHAR]`/`S"`/`."`/`ABORT"` plus the `(`/`\` reexpression as this
step's own deliverables. Neither D19 nor F29's own step text says how `PARSE`/`WORD`
should store their result, whether `(`/`\` becoming "ordinary words" changes anything
about the token scanner itself, or what `ABORT"` should do about `THROW`, which does
not exist until F31. This step had to make four concrete choices.

**1. `PARSE`/`WORD`/`S"`/`."`/`ABORT"` all copy their text into freshly allotted
data-space cells (D21: one cell per character), never a reusable scratch buffer.**
Every other allotting word already in this project (`CREATE`, `VALUE`, `DEFER`, `,`)
grows data space monotonically; `PARSE`/`WORD` do the same, rather than writing into
some fixed-size "word buffer" area the way many historical Forth implementations do.
A program that calls `WORD` in a tight loop consumes data space proportionally,
bounded by the caller's own `MaxData`, exactly like a loop calling `CREATE` would.

**2. `(`/`\` are deleted from the scanner (`parser::skip_forth_space`, `forth_lexeme`)
and reinstalled as ordinary, immediate, non-compile-only `control_builtin` dictionary
entries.** `parser::scan_word`'s own trailing skip changes from `forth_lexeme`
(whitespace + both comment forms) to plain `lexeme` (whitespace only), so a source
`(` or `\` character survives as an ordinary one-character token whenever the next
character is whitespace, exactly like any other Forth-2012 word. Two new tags
(`paren_`, `backslash_`) give them dictionary entries; each scans forward from
`state.source()` via a new shared combinator, `parser::scan_delimited`, and advances
`>IN` past its own terminator. A consequence: a comment can no longer appear between
`:` and its own name (`: ( oops ) NAME` now names the word `(`) — the same behavior a
raw name-parse gives in any Forth-2012 system, and `interpreter::scan_colon_header`
(Phase 8) is simplified accordingly (it no longer needs to protect a `( ... )`
stack-effect comment from `scan_word`'s own trailing skip, since that skip no longer
risks consuming one).

**3. `S"`/`."`/`ABORT"` consume whatever whitespace `scan_word`'s own trailing skip
leaves behind before their own string, not exactly one mandatory delimiter space.**
Forth-2012 requires exactly one space between `S"` and its string, treating any
further leading spaces as significant content; this project's ordinary token
scanning already strips *all* trailing whitespace after any token (the same
convention every other word in this project already relies on), so a string literal
with more than one leading space before its first non-space character loses those
extra leading spaces. Ordinary usage (`S" hello"`, one space) is unaffected.

**4. `ABORT"` is a hard stop, not `THROW -2`.** Its compiled form's own runtime
primitive (`machine::primitive::abort_quote`) prints the message (if the flag is
nonzero) and returns a diagnosed `foundation::parse_error`, which propagates all the
way out of `interpret()` — there is no `CATCH` to unwind to, because `CATCH`/`THROW`
do not exist until F31.

## Why

**Monotonic data-space growth for parsing words.** A dedicated scratch buffer would
need its own capacity, which would either be hardcoded (barred by this project's own
"capacities are template parameters" rule) or a new template parameter threaded onto
`machine::forth_state` — a much larger, more invasive change (every function already
templated over `forth_state`'s existing four capacity parameters would need a fifth)
for a benefit ("reuse the same cells across calls") this project's own existing
convention (every other allotting word already grows monotonically) does not ask for.

**Deleting scanner-level comment handling, not merely adding an alternative.**
Keeping `skip_forth_space` alongside the new dictionary words would mean two
mechanisms for the same thing, with `skip_forth_space`'s own callers silently
shadowing the new words (a `(` or `\` character would never survive to be tokenized
at all, since the scanner would already have consumed it). The whole point of "`(`
and `\` re-expressed as ordinary immediate/parsing words over the same stream" is
that they are no longer scanner-level special cases; keeping the old function around,
unused, would be exactly the kind of dead alternative path this project's own house
style disallows.

**Accepting the leading-whitespace imprecision for `S"`/`."`/`ABORT"`.** Correctly
distinguishing "the one mandatory delimiter space" from "significant leading content"
would require `S"`'s own action to run *before* `scan_word`'s own trailing skip
consumes anything past the `S"`/`."`/`ABORT"` token itself — a special case in the
main interpret loop's own token-scan trailing behavior, applying only to these three
tokens. This project has been actively removing exactly this kind of direct-name
special-casing (D13's own uniform dispatch, Phase 10); the imprecision is narrow
(affects only strings with more than one deliberate leading space) and is documented
here rather than silently accepted.

**`ABORT"` as a hard stop.** `foundation::parse_error`'s own contract requires a
static-lifetime message pointer (it does not own or copy the string it points to), so
`abort_quote` cannot attach the actual dynamic message text to the diagnosed error
value itself. Printing the message to the output buffer before returning a generic
diagnosed error is the closest approximation available without `THROW`/`CATCH`: the
message is observably printed (a real, testable side effect), and the interpretation
still stops rather than silently continuing past a met `ABORT"` condition, which
D7 rules out. This is a deliberate, documented interim, not a permanent design.

## Consequences

- `machine::primitive` gained six enumerators (`parse`, `word`, `char_`, `count`,
  `type_`, `abort_quote`); `machine::control_builtin` gained six more (`paren_`,
  `backslash_`, `s_quote_`, `dot_quote_`, `char_bracket_`, `abort_quote_`).
  `interpreter::effect_lint.hpp`'s own `primitive_data_effect` switch gained cases
  for all six new primitives.
- `default_dictionary`'s own entry count grew from 77 to 89 (54 primitives + 35
  control words); every pre-existing test sized close to that (the `MaxWords = 96`
  instances in `session.test.cpp`/`vm.test.cpp`) still clears it comfortably, but a
  future step growing `default_dictionary` further should expect to repeat this
  audit (DIV-0015 and DIV-0016 both name the same pattern).
- `parser::skip_forth_space` and `parser::forth_lexeme` are deleted; `parser::
  scan_word` now uses plain `lexeme` (`alt.hpp`, already existing) for its own
  trailing skip. Every caller of `scan_word` (POSTPONE's own name scan, `'`/`[']`,
  `VALUE`/`TO`/`DEFER`/`IS`) is unaffected in ordinary usage — none of those scans a
  raw string immediately afterward the way `S"`/`."`/`ABORT"` do.
- `interpreter::scan_colon_header` (Phase 8) no longer needs the "replay bump()
  from the un-skipped starting cursor" workaround its own pre-F29 shape used: it can
  use `parser::scan_word`'s own rest cursor directly, since that cursor no longer
  risks having silently consumed a `( ... )` stack-effect comment as ordinary
  intertoken space.
- A comment between `:` and its own name is no longer tolerated (see item 2 above);
  no existing test relied on this, and no merge criterion needs it.
- `ABORT"`'s own message text never reaches the diagnosed `foundation::parse_error`
  value itself, only the output buffer; a caller inspecting `r.error().message`
  after an `ABORT"`-triggered failure sees a generic, static string, not the
  program's own message.

## Revisit condition

Items 1-3 (monotonic data-space growth, `(`/`\` reexpression, and the
leading-whitespace imprecision) are accepted-permanent: none is a scope cut made to
save time, and revisiting any of them would trade a real simplicity for a benefit no
merge criterion currently needs.

`ABORT"`'s own hard-stop behavior is the one open item, by construction (F31 is
already scheduled to build `CATCH`/`THROW`). When F31 lands, `abort_quote` should
`THROW -2` (the exact value not yet used anywhere in this project) after printing its
message, unwinding to the nearest `CATCH` rather than propagating all the way out of
`interpret()` unconditionally the way it does now. Revisit at F31.
