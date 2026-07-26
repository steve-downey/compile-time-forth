# Forth-2012 core word set: F32 conformance exclusion list

Step F32 (docs/forth-plan-2.md), D23: "core word set first, core-ext
opportunistically, in gforth's direction." This is the exclusion list D23
and the step's own merge criterion ask for: every Forth-2012 core (or
core-ext) word this project's own conformance battery
(`src/smd/forth/conformance/`) does not test, with a citation for why —
either this project's own declared scope cut (D12), a filed divergence
(DIV-NNNN), or "not yet implemented" (a gap, not a design decision, and not
this step's job to close).

**Silent skipping is the failure mode this list exists to prevent.** If a
word is not covered by `ttester_corpus.hpp` plus a `core_suite_*.test.cpp`
shard, it must appear below with a reason. A word covered by a shard is not
repeated here.

## Covered (for cross-reference — see `core_suite_*.test.cpp`)

Arithmetic/logic: `+ - * / MOD NEGATE ABS MIN MAX AND OR XOR INVERT LSHIFT
RSHIFT 1- 1+ 0= 0< = <> < >` (`<= >=` are this project's own additions, not
Forth-2012 core words — tested alongside for regression coverage, not
conformance).
Stack manipulation: `DUP DROP SWAP OVER ROT ?DUP DEPTH >R R> R@` (`NIP
TUCK` are core-ext, not core; tested alongside).
Memory and the D21 address-unit words: `@ ! +! ALLOT , VARIABLE CONSTANT
CREATE CELLS CELL+ CHARS CHAR+ C@ C!`.
Control flow: `IF ELSE THEN BEGIN UNTIL WHILE REPEAT DO LOOP +LOOP LEAVE
UNLOOP I J EXIT RECURSE`.
Colon compiler and execution tokens: `: ; IMMEDIATE POSTPONE LITERAL '
EXECUTE ['] [ ] DEFER IS TO VALUE DOES>`. `COMPILE,` is installed and
dispatchable but not stress-tested here — DIV-0023 (filed this step)
records that it currently has no reachable valid usage (its own textbook
idiom needs it non-immediate; this project's own copy is immediate).
Exception handling: `CATCH THROW ABORT ABORT"`.
Strings and parsing: `S" ." [CHAR] WORD PARSE CHAR COUNT TYPE`.
Output: `. .S EMIT CR` (`.S` is a Programming-Tools word, not core; tested
alongside since it already exists).

## Excluded: out of scope by design (D12)

These are declared scope cuts, not gaps — this project has no floating
point, no double-cell arithmetic, no `PICK`/`ROLL`, no blocks/files, no
locals, no `MARKER`/`FORGET`, no environmental queries, and no `>NUMBER`.
No test is written against any of these; a test that named one would not
be testing this project, it would be testing a feature the project
explicitly does not have.

- **Floating point, all of it**: `F! F@ FCONSTANT FVARIABLE FLOAT+ FLOATS
  FDROP FDUP FSWAP FOVER FROT F+ F- F* F/ FNEGATE FABS FMIN FMAX FROUND
  FTRUNC F0= F0< F< F~ F>D D>F REPRESENT >FLOAT FLITERAL FALIGN FALIGNED`,
  and the entire Floating-Point word set. (D12.)
- **Double-cell arithmetic and storage**: `2! 2@ 2DROP 2DUP 2OVER 2SWAP
  D+ D- DNEGATE DABS DMIN DMAX D0= D0< D2* D2/ D< D= S>D M* M+ M*/ UM* UM/MOD
  FM/MOD SM/REM */ */MOD D.` and friends. (D12: "no double-cell.") Plain
  `2*`/`2/` (arithmetic shift by one, *not* double-cell despite the name)
  are listed separately below — they are a gap, not this cut.
- **`PICK`/`ROLL`**: explicitly named in D12; "add only when a criterion
  demands, per the 1+ policy" — no criterion in this project has yet.
- **Blocks and files**: `BLOCK BUFFER FLUSH LOAD SAVE-BUFFERS UPDATE
  BLK \` (block-comment `\` is implemented, D19 — this is the separate
  block-source-id word) and the whole File-Access word set. (D12.)
- **Locals**: `(LOCAL) {: ... :}` and the Locals word set. (D12.)
- **`MARKER`/`FORGET`**: no dictionary un-definition mechanism exists.
  (D12.)
- **Environmental queries**: `ENVIRONMENT?` itself, and by extension every
  query string a program would ask it about. (D12.) DIV-0020 records the
  concrete consequence for the ttester adaptation: upstream's own
  `ENVIRONMENT?`-gated floating-point selection has no way to run here at
  all, not merely "queries false."
- **`>NUMBER`**: explicitly named in D12.

## Excluded: deferred per the orchestrator's own 1+ policy

- **`EVALUATE`**: F32's own orchestrator note says build it only if the
  ttester or the core-suite battery demands it, else defer with a DIV.
  Neither does — DIV-0021 records the deferral.

## Excluded: not yet implemented (a gap, not a scope cut — not this step's job to close)

These are ordinary Forth-2012 core words this project simply has not built
yet. None is barred by D12; each is a legitimate future addition. Listing
them here (rather than silently skipping) is the point: a reader auditing
this project's own Forth-2012 coverage should be able to tell "excluded on
purpose" from "not built yet" at a glance.

- **Number-radix and formatting**: `BASE DECIMAL` (`HEX` is core-ext).
  No Forth-visible word yet exposes `machine::forth_state::base()`/
  `set_base` (both exist internally, D13's own composed register set) —
  every literal in this project's own source is parsed decimal only.
  Also `# #S #> <# HOLD SIGN` (numeric-output formatting, meaningless
  without `BASE`/`HOLD`'s own pictured-numeric buffer).
- **Introspection**: `STATE SOURCE >IN HERE FIND >BODY WORDS`. `STATE`/
  `SOURCE`/`>IN`/`HERE` all have a live internal register or accessor
  (`forth_state`/`data_space`) with no Forth-visible word wrapping it yet;
  `FIND`/`>BODY`/`WORDS` have no internal counterpart to wrap either.
- **Console I/O**: `ACCEPT KEY QUIT SPACE SPACES BL`. This project's own
  output model (D10, `forth_state`'s captured output buffer) has no
  matching *input* device (`ACCEPT`/`KEY` read from one), and `QUIT`
  presumes a top-level REPL loop to restart, which D13's one-shot
  `compiled_forth<Source>` model does not have; `BL` (the space-character
  constant, `32`) and `SPACE`/`SPACES` built from it are trivial but simply
  not yet wired as dictionary entries — `core_suite_strings.test.cpp` uses
  the literal `32` where upstream Forth-2012 source would write `BL`.
- **Remaining single-cell arithmetic**: `/MOD */ */MOD M* UM* UM/MOD
  FM/MOD SM/REM 2* 2/`. `/MOD` (combined quotient/remainder) and `*/`/
  `*/MOD` (scaled multiply-divide) are ordinary core words with no
  existing primitive; `2*`/`2/` are single-cell arithmetic shifts (not
  the double-cell cut above) with no existing primitive either.
- **Memory block operations**: `FILL MOVE ALIGN ALIGNED`. `ALIGN`/
  `ALIGNED` would be trivial no-ops under D21 (the same family as
  `CELLS`/`CHARS` this step adds), but this step's own brief names only
  `CELLS CELL+ CHARS CHAR+ C@ C!` — `ALIGN`/`ALIGNED` are left for
  whichever future step needs them. `FILL`/`MOVE` (block fill/copy over
  `data_space`) have no existing primitive.
- **`0<>`**: a core-ext convenience (`<> 0` in effect); not yet a
  dictionary entry.

## Known divergences affecting what a covered word's own test can assert

- **DIV-0009 / D21 — one address unit is one cell.** Every `core_suite_
  memory.test.cpp` assertion involving `CELLS`/`CELL+`/`CHARS`/`CHAR+` is
  written against this project's own cell-granular convention (`1 CELLS`
  is `1`, not a byte count); a test written against byte-addressed
  Forth-2012 semantics would fail here by design, not by defect.
- **DIV-0017 — `S"`/`."`/`ABORT"` consume all whitespace after their own
  token, not exactly one delimiter space.** No covered test in
  `core_suite_strings.test.cpp` probes a string literal with deliberate
  extra leading spaces; if one ever is added, this is the citation for why
  it is expected to diverge from Forth-2012's exactly-one-space rule.
- **DIV-0023 — `COMPILE,` has no currently-reachable valid usage** (its own
  textbook idiom needs it non-immediate; this project's own copy is
  immediate). `core_suite_defining.test.cpp` does not stress-test it.
- **DIV-0024 — `CATCH` cannot restore `i*x` if the caught word itself
  popped below it before throwing.** A test shaped `"1 ['] CHK CATCH"`
  where `CHK`'s own `ABORT"` consumes that same `1` as its own condition
  fails to build (`"stack truncate: depth out of range"`); the working
  shape (`interp.test.cpp`'s own `AbortQuoteCaughtByCatch`, mirrored in
  `core_suite_strings.test.cpp`) has the caught word push its own argument
  internally instead, never dipping below its own call boundary.
- **DIV-0022 — `/`/`MOD` are symmetric (truncating), gforth's are floored,
  for a negative operand with an inexact result.** `core_suite_
  arithmetic.test.cpp`'s own `/`/`MOD` assertions stick to non-negative
  operands (where both conventions already agree); the gforth differential
  harness excludes negative-operand division from its own battery for the
  same reason (see below).

## gforth differential harness scope (D23)

`src/smd/forth/conformance/gforth_diff.test.cpp` runs a battery of
programs through both this project's own `interpreter::build_session` (at
ordinary runtime, not `constexpr`) and real `gforth` (invoked as a
subprocess, `gforth -e '<program> .s bye'`), diffing final stacks. It is
gated on `gforth` being found on `PATH` at CMake configure time
(`find_program`) — if not found, the test is not registered at all, and
CMake emits a `STATUS` (not silent) message saying so, per this project's
own "silent skipping is the failure mode to avoid" rule. Programs in the
battery are drawn from the same covered-word list above (Section
"Covered"), not the excluded words — the harness's job is differential
agreement on what both systems actually implement, not a second attempt at
words this project has already excluded above. One further, narrower
exclusion applies only within this harness: negative-operand `/`/`MOD`
(DIV-0022 — this project's own symmetric division legitimately disagrees
with gforth's own floored division there, so the harness's own battery
sticks to operands where both conventions already agree).
