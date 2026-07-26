# DIV-0020: Hayes ttester adapted, not copied verbatim

- **Status:** accepted-permanent
- **Date:** 2026-07-26
- **Step:** F32 (conformance), docs/forth-plan-2.md
- **Authority diverged from:** the upstream text itself (John Hayes' `TESTER.FR`, 1995, as revised
  into `ttester.fs` by Anton Ertl, 2007 -- both found locally at
  `/usr/share/gforth/0.7.3/test/ttester.fs`)

## What diverged

`src/smd/forth/conformance/ttester_corpus.hpp`'s `SMD_FORTH_TTESTER_SOURCE` is an adaptation of
upstream's own `ttester.fs`, not a verbatim transcription. Four things are removed, one thing is
kept unchanged that a reader unfamiliar with upstream might expect this step to have had to add
itself.

**1. The whole floating-point apparatus is omitted, not conditionally compiled out.** Upstream
guards its own floating-point words (`F{`, `F->`, `F}`, `FTESTER`, `FEXACTLY=`, `FNEARLY=`, ...)
behind `HAS-FLOATING`/`HAS-FLOATING-STACK`, computed via `ENVIRONMENT?` and selected with
`[IF]`/`[ELSE]`/`[THEN]`. This project has neither floating point (D12: out of scope) nor
`ENVIRONMENT?`/`[IF]`/`[ELSE]`/`[THEN]` (D12: environmental queries out of scope; conditional
compilation was never built at all). Upstream's own false branch for `HAS-FLOATING-STACK` already
defines every floating word as a no-op (`: F{ ; : F-> ; : F} ; : EMPTY-FSTACK ; : F...}T ;`) and
`FTESTER`'s own non-floating-stack branch is unreachable without floats sharing the integer stack
at all -- so the adaptation simply never emits any call to these words, which is observably
identical to upstream always taking that same false branch, without needing the selection
machinery upstream uses to get there.

**2. The `X.../R.../...}T` combinator family is dropped entirely** (`X}T`, `R}T`, `XX}T`, `XR}T`,
..., up to `RRRR}T`, plus the `XTESTER`/`FTESTER`/`...}T` primitives they are built from). Upstream
needs these so one test can specify how many of the results after `->` are floats versus cells,
sharing one stack. With no floating point at all, that distinction cannot arise; the plain,
unsuffixed `}T` (which upstream itself defines as `XTESTER`-per-cell repeated `...}T` amount of
times, in the no-shared-float-stack case) already does everything a test here needs.

**3. `ERROR1` drops upstream's own `SOURCE TYPE` call** (upstream: `TYPE SOURCE TYPE CR
EMPTY-STACK`, echoing the whole source line the failing test came from after the message).
This project has no `SOURCE` word (not yet built; see the exclusion list, `docs/
conformance-exclusions.md`) and F32's own merge criterion does not need one: `ERROR1`'s job is to
make a wrong `T{ ... -> ... }T` observable in a built session's own captured output
(`session::output`), and the message text alone (`"INCORRECT RESULT: "` / `"WRONG NUMBER OF
RESULTS: "`) already does that, verified directly by `ttester_corpus.test.cpp`'s own runtime
tests. Echoing the source line is upstream's own convenience for a human reading interactive
output, not part of what makes the diagnostic fire.

**4. The `TESTING` word (a verbose-mode "talking comment" echoer built on `SOURCE`/`>IN`) is
omitted outright**, for the same reason as (3) -- no `SOURCE`, and no consumer needs it: Catch2
test names (`TtesterCorpusTest - ...`, `Core*SuiteTest - ...`) serve the same "what is this batch
of assertions testing" purpose upstream's own `TESTING` comment does, without needing `SOURCE`/
`>IN` at all.

**5. Kept unchanged, deliberately: every zero-trip `DO` guard.** Upstream never writes a bare
`count 0 DO ... LOOP` where `count` could be zero -- every one is guarded (`DEPTH START-DEPTH @ >
IF ... DO ... LOOP THEN`, `DEPTH START-DEPTH @ < IF ... DO ... LOOP THEN`), because Forth-2012's
own `DO` (unlike `?DO`) does not skip its body when the initial index equals the limit; it loops
until the index wraps all the way back around, which for a 64-bit cell is not something any fuel
budget this project uses will ever complete. A probe run against this project's own
`build_session` confirmed the point directly: `0 0 DO 1 . LOOP` (bare, unguarded) burns through a
1000-step interpreter fuel budget without completing, and real `gforth 0.7.3` does the same thing
(confirmed by direct invocation, `: T 0 0 DO 1 . LOOP ; T`, which does not return). This is not a
gap this adaptation introduces or works around -- it is upstream's own long-standing correctness
requirement, kept verbatim because the great majority of `T{ ... -> ... }T` assertions leave zero
results after `->` (`T{ 1 DROP -> }T`, for instance), which is exactly the shape a missing guard
would break on.

## Why

**Adaptation, not reimplementation.** Every word this header keeps is upstream's own logic,
character-for-character apart from whitespace; nothing here re-derives `T{`/`->`/`}T`'s own
semantics independently. The four omissions above are all "this project has no way to reach
upstream's own false/no-op branch, so take it directly" rather than a behavioral change -- the
observable result (`T{ ... -> ... }T` compares stack contents, upstream's own diagnostics fire on
a mismatch, floating point never enters into it) is identical to running unmodified `ttester.fs`
on a system that reports no floating-point support and has no separate float stack.

**Scope discipline.** Building `ENVIRONMENT?`, `[IF]`/`[ELSE]`/`[THEN]`, or `SOURCE` solely to run
upstream's own text unmodified would mean implementing three separate out-of-scope features (D12)
for a single consumer of each, none of which this step's own merge criterion (docs/forth-plan-2.md
§6, F32) asks for.

## Consequences

- `docs/conformance-exclusions.md` lists `ENVIRONMENT?`, floating point, and `SOURCE` as excluded
  from this project's own core-suite battery for the reasons above (D12, and "not yet built"
  respectively) -- this DIV is that list's own citation for why the ttester specifically needed
  none of them.
- Any future step that adds `SOURCE` (there is no such step currently planned) could restore
  upstream's own `ERROR1`/`TESTING` bodies verbatim without otherwise touching this header.
- The zero-trip `DO` guard convention this DIV calls out is the same one any future core-suite
  shard (`core_suite_*.test.cpp`) must keep using for its own `T{ ... -> ... }T` groups that leave
  zero results after `->` -- not a ttester-only concern.

## Revisit condition

`none` for items 1-4 (accepted-permanent: a system without floating point, `ENVIRONMENT?`, or
`SOURCE` has no verbatim upstream branch to fall back to instead of this adaptation). Item 5 is
not a divergence to revisit at all -- it is upstream's own required behavior, recorded here only
so a future reader does not mistake the guard for an artifact of this project's own `DO` and try
to "simplify" it away.
