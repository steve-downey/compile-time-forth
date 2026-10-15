<!-- markdownlint-disable MD013 -->

# Forth-2012 limitations and divergences

Step F36 (consolidation, `docs/forth-plan-2.md`) rolls this document up from
three sources that, until now, each held one piece of "what this project
does not do, and why": the rescoped D12 scope cut, D21's cell-granularity
system characteristic, and the thirty divergence records filed one per
step under `docs/divergences/`. This is the single place a reader asks
"does `smd::forth` do X" and gets a citable answer, rather than searching
`git log` or a growing handoff file.

This document is a roll-up, not a duplicate. Each item below is one line and
a link; the full record — rationale, consequences, the step that filed it —
stays in the cited file. Where `docs/conformance-exclusions.md` already
lists a Forth-2012 core word with its own citation, this document does not
re-list the word; it links the section instead.

## Scope: what this project deliberately does not build (D12, rescoped)

`docs/forth-plan-2.md`'s revision table rescopes D12. Moving *into* scope
(all now implemented, steps F27–F35): the compilation-semantics word set
(`IMMEDIATE POSTPONE [ ] LITERAL STATE`), `WORD`/`PARSE`, `S" ." ABORT"`,
`DEFER`/`IS`, `VALUE`/`TO`, and `EVALUATE` deferred rather than built (see
[DIV-0021](divergences/DIV-0021-f32-evaluate-deferred.md) below).

Remaining out of scope, permanently, by design — not gaps:

- Floating point, in its entirety.
- Double-cell arithmetic and storage.
- `PICK`/`ROLL` (the project's own "1+ policy": added only when a criterion
  demands one; none has yet).
- Blocks and files, and the File-Access word set.
- Locals (`(LOCAL)`, `{: ... :}`).
- `MARKER`/`FORGET` (no dictionary un-definition mechanism exists).
- Environmental queries (`ENVIRONMENT?` and everything gated on it — see
  [DIV-0020](divergences/DIV-0020-f32-ttester-adaptation.md) for the
  concrete consequence this has for the ported ttester).
- `>NUMBER`.

`docs/conformance-exclusions.md` is the word-by-word ledger this section
summarizes: every Forth-2012 core/core-ext word this project's own
conformance battery does not test, each cited as a scope cut (above), a
filed divergence, or "not yet implemented" (a gap, explicitly distinguished
from a cut). That document's own exclusion sections are not repeated here.

## System characteristic: one address unit is one cell (D21)

The F16 data space is cell-granular, not byte-addressed: `CELLS` is
identity, `CELL+` is `1+`, `CHARS` is identity, `CHAR+` is `1+`; `C@`/`C!`
alias `@`/`!`; strings store one character per cell. This is a legal
Forth-2012 system characteristic (implementations are permitted to choose
their own address-unit granularity), declared once here rather than
annotated on every memory word that touches it.
[DIV-0009](divergences/DIV-0009-f16-cell-granular-memory-words.md) is the
filed record; `docs/conformance-exclusions.md`'s "Known divergences
affecting what a covered word's own test can assert" section names the
concrete test-writing consequence.

## Divergence log

Every entry filed under `docs/divergences/DIV-NNNN-*.md`, in order. Status
is the file's own, as of this roll-up; a superseded entry is kept (never
deleted — the project's own "graduates in place" convention for anything a
second document names by number) and marked as such here too, so this table
is complete rather than curated down to only the currently-live ones.

| DIV | Status | What it records |
|---|---|---|
| [DIV-0001](divergences/DIV-0001-structural-parse.md) | superseded by DIV-0011 | Structural grammar (parser combinators over a syntax tree), not a stateful outer text interpreter — the project's original, later-overturned architecture choice. |
| [DIV-0002](divergences/DIV-0002-parse-error-equality-constexpr.md) | accepted-permanent | `parse_error::operator==`'s fast path is not constexpr-portable across compilers; rewritten to a `string_view` comparison. |
| [DIV-0003](divergences/DIV-0003-parser-foundation-typeclass.md) | accepted-permanent | Parser combinators register directly against `foundation`'s shared typeclass machinery rather than a project-local adapter. |
| [DIV-0004](divergences/DIV-0004-dictionary-addr-placeholder.md) | resolved (F11) | The dictionary's `variable_word` used `cell`, not a distinct `addr` type, as an interim placeholder. |
| [DIV-0005](divergences/DIV-0005-grammar-recursive-descent.md) | superseded by DIV-0011 | Hand-written recursive descent for nested control structures, retired with the R1 grammar pipeline at step F26. |
| [DIV-0006](divergences/DIV-0006-exit-not-flow-sensitive.md) | superseded by DIV-0011 | The R1 stack-effect checker was not flow-sensitive across `EXIT`; the gap itself is inherited by F30's effect lint, not closed by the pivot. |
| [DIV-0007](divergences/DIV-0007-f13-output-words-and-one-minus.md) | accepted-permanent | F13 adds output-word and `1-` primitives the plan's own word table omitted. |
| [DIV-0008](divergences/DIV-0008-f14-deferred-opcodes-and-capacity-fields.md) | accepted-permanent | F14 reserves nine opcodes without behavior yet and leaves "required stack capacity" uncomputed at that step. |
| [DIV-0009](divergences/DIV-0009-f16-cell-granular-memory-words.md) | accepted-permanent | Memory words address data space in cells, not Forth-2012 address units — D21's own filing (see above). |
| [DIV-0010](divergences/DIV-0010-f17-one-plus-and-loop-analysis.md) | accepted-permanent | F17 adds the `1+` primitive and refines `DO`-loop stack-effect analysis beyond the plan's literal text. |
| [DIV-0011](divergences/DIV-0011-true-forth-pivot.md) | accepted-permanent | The true-Forth pivot: D5 overturned, the Forth-2012 §3.4 text interpreter (not a syntax tree) is the architecture from F24 onward. |
| [DIV-0012](divergences/DIV-0012-f24-composed-forth-state-and-deferred-token-move.md) | closed at F28 | F24's interpreter state is composed, not an in-place edit of `machine::forth_state`; the D19 token-layer move (deferred here) closed at F26. |
| [DIV-0013](divergences/DIV-0013-colon-compiler-binding-and-invocation.md) | closed at F28 | The colon compiler's binding shape and word-invocation mechanism, left open by the plan's own step text; binding-shape half closed at F26. |
| [DIV-0014](divergences/DIV-0014-the-cut.md) | accepted-permanent | "The cut": the public-API reshape, the effect-gate suspension window, and F16's memory words moved into the interpreter. |
| [DIV-0015](divergences/DIV-0015-f27-control-word-design.md) | accepted-permanent (POSTPONE cut resolved at F28) | Control-word binding shape, the `POSTPONE`-of-a-control-word whole-body-alias design, and a reverted balance check. |
| [DIV-0016](divergences/DIV-0016-f28-execution-tokens-and-defining-words.md) | accepted-permanent (one scope cut named in the file) | Execution-token encoding, `CREATE`/`DOES>` dictionary access from inside a compiled body, and two data-space-backed binding kinds. |
| [DIV-0017](divergences/DIV-0017-f29-parsing-words-and-strings.md) | accepted-permanent | Parsing-word data placement, comment reexpression as ordinary immediate words, and `ABORT"`'s interim runtime behavior. |
| [DIV-0018](divergences/DIV-0018-f31-catch-throw-design.md) | accepted-permanent | `CATCH`/`THROW` frame design, machine-fault mapping scope, and `ABORT"` closure. |
| [DIV-0019](divergences/DIV-0019-f30-effect-lint-design.md) | accepted-permanent | Effect-lint CFG design: instruction-level worklist, exit-agreement relaxation, `LEAVE` poisoning, capacity/position choices. |
| [DIV-0020](divergences/DIV-0020-f32-ttester-adaptation.md) | accepted-permanent | The Hayes ttester is adapted, not copied verbatim — four removals, one addition, from upstream `ttester.fs`. |
| [DIV-0021](divergences/DIV-0021-f32-evaluate-deferred.md) | accepted-permanent (until a future criterion demands it) | `EVALUATE` deferred, not built — neither the ttester nor the core-suite battery demanded it. |
| [DIV-0022](divergences/DIV-0022-f32-symmetric-vs-floored-division.md) | accepted-permanent | `/`/`MOD` are symmetric (truncating); gforth's are floored, for a negative operand with an inexact result. |
| [DIV-0023](divergences/DIV-0023-f32-compile-comma-unreachable.md) | open | `COMPILE,` has no currently-reachable valid usage under this project's own immediate-`POSTPONE` design. |
| [DIV-0024](divergences/DIV-0024-f32-catch-cannot-regrow-below-saved-depth.md) | open | `CATCH` cannot restore `i*x` if the caught word itself popped below the saved depth before throwing. |
| [DIV-0025](divergences/DIV-0025-f30-basic-block-leader-defect.md) | accepted-permanent (fixed in place) | `recover_basic_blocks` degenerated every block to one instruction; found and fixed during F30. |
| [DIV-0026](divergences/DIV-0026-f33-sender-receiver-type-erasure.md) | accepted-permanent | `word_sender::run` must not be templated on its own connecting receiver — a general lesson for recursive sender composition. |
| [DIV-0027](divergences/DIV-0027-f35-bootstrap-prelude.md) | open | The bootstrap prelude's injection point (`compiled_forth<Source>` only), scope, and control-word reach. |
| [DIV-0028](divergences/DIV-0028-f33-fallback-handler-depth-isolation.md) | accepted-permanent | The VM-fallback path must hide an enclosing sender-level `CATCH` from a word it runs natively. |
| [DIV-0029](divergences/DIV-0029-f34-foreign-vocabulary-design.md) | accepted-permanent | The foreign vocabulary is threaded beside the dictionary, not stored inside it. |
| [DIV-0030](divergences/DIV-0030-f34-sender-merge-criterion-reading.md) | accepted-permanent | F34's "static_assert via VM and sender backends" merge criterion is read as static_assert-via-VM plus a runtime sender equivalence check. |

DIV-0031 through DIV-0034 are allocated to step F36 and are unused: this
step's own error-quality and consolidation work (the positioned-diagnostic
battery, the two new negative-compile tests, this document, the rewritten
presentation, and the `session.hpp` `MaxStack` diagnostic fix —
`interpreter/session.test.cpp`'s own
`BuildSessionDiagnosesFinalDepthExceedingMaxStack`) is ordinary
implementation and bug-fixing within the plan's own step section, not a
deviation from it or from Forth-2012, so none of the four needed filing.

## Where this fits

`docs/compiler_architecture.org` is the living architecture document; this
file is its limitations companion, not a duplicate of it. `docs/
conformance-exclusions.md` is the word-level ledger this document's scope
section summarizes. `docs/divergences/TEMPLATE.md` is the template new DIVs
are filed from — it is not itself a divergence and is not listed above.
