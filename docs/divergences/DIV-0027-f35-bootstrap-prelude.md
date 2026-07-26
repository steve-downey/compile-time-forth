# DIV-0027: The bootstrap prelude's scope, injection point, and control-word reach

- **Status:** open
- **Date:** 2026-07-26
- **Step:** F35 (bootstrap prelude, stretch), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md (implementation choices the plan leaves open, not a
  Forth-2012 semantic divergence)

## What diverged

1. **Injection point:** `interpreter::build_session` (`interpreter/session.hpp`) is untouched.
   A new function, `interpreter::build_session_with_prelude` (`interpreter/prelude.hpp`), compiles
   `interpreter::prelude_source` and a caller's own text as one concatenated `SOURCE`, then delegates
   to `build_session`. Only `forth.hpp`'s own `compiled_forth<Source>` (the public one-shot API) was
   switched to call it. Every direct `build_session` caller in this project's own test suite
   (`session.test.cpp`, `vm.test.cpp`, `conformance/gforth_diff.test.cpp`,
   `conformance/ttester_corpus.test.cpp`'s own runtime-side calls) keeps building a prelude-free
   session at whatever capacities it already used — several of those (`MaxCode=64`) are sized tightly
   enough that always injecting the prelude there would have required raising them for no benefit
   those tests need.
2. **Which words moved:** `?DUP`/`NIP`/`TUCK` were removed from `machine::default_dictionary`'s own
   C++-installed primitive list and redefined in `interpreter::prelude_source` as ordinary colon words
   (`: NIP SWAP DROP ;`, `: TUCK SWAP OVER ;`, `: ?DUP DUP IF DUP THEN ;`). Their own
   `machine::primitive` enumerators (`nip`/`tuck`/`question_dup`) and `machine::apply_primitive` cases
   are *not* deleted — nothing else in this project reaches them once no dictionary entry points at
   them (a fully unreachable-but-harmless leftover), and removing the enumerators themselves would
   touch `machine::forth_state.hpp`'s own switch and `interpreter::effect_lint.hpp`'s own primitive-
   effect table for no benefit this step's own criterion asks for.
3. **Which words stayed:** `1+`/`1-`, despite being equally derivable (`: 1+ 1 + ;`, `: 1- 1 - ;`),
   stayed C++-installed primitives, unmoved. `interpreter::corpus::sumto_program`/`countdown_program`
   (`interpreter/control_flow_corpus.hpp`, the named acceptance battery) are exercised directly through
   `machine::default_dictionary` and `interpreter::interpret` in `interp.test.cpp`, with no prelude in
   the path at all (that is exactly what "still passes verbatim" means for a raw-`interpret`-plus-
   `default_dictionary` test) — moving `1+`/`1-` out of `default_dictionary` would break that battery,
   not merely shrink a count. `MIN`/`MAX` were left alone too: a from-scratch Forth definition needs
   `2DUP`, not currently in this project's vocabulary at all, and D12's own "1+" policy is explicit —
   do not add a word solely to enable moving another one.
4. **Control-word reach:** the prelude defines `WHEN`/`OTHERWISE`/`ENDIF` as whole-body `POSTPONE`
   aliases of `IF`/`ELSE`/`THEN` respectively — a full `IF`/`ELSE`/`THEN` replacement, under new names,
   not only a `THEN`-only `ENDIF`-class synonym (see "Why" below for what "full" does and does not
   mean here).

## Why

**Injection-point risk.** `interpreter::build_session`'s own signature and behavior are relied on
directly by a dozen-plus test call sites across `session.test.cpp`, `machine/vm.test.cpp`, and two
`conformance/` files, several at capacities (`MaxCode=64`, `MaxWords=160`) sized for their own narrow
scenario, not for "a real session." Making every session — including those — pay the prelude's own
code-space and dictionary-entry cost by changing `build_session` itself would have forced raising
capacities across files this step has no reason to touch, for a policy question (does *this* test's
session want the prelude) those files never asked. Scoping the change to `compiled_forth<Source>`
alone — the actual "session" a user of the public API gets, and the one D15 itself names — reads
"compiled by every session" as "every session a user of this library builds," not "every internal call
to the session-building primitive this project's own tests happen to use for narrower unit coverage."

**Word-selection risk.** The instructions warned explicitly that `control_flow_corpus.hpp` "is the
acceptance battery and must keep passing verbatim." Auditing every consumer of
`machine::default_dictionary` found two shapes: (a) tests that go through `compiled_forth`/
`build_session_with_prelude`, where a word moving to the prelude is invisible (the name still resolves,
now to a compiled colon word instead of a primitive, and Forth-2012 promises no observable difference
between the two binding kinds, D14); (b) tests that call `default_dictionary` and `interpreter::
interpret` directly, with no prelude in the path at all, where a moved word simply stops existing.
`1+`/`1-` are in the second category (`interp.test.cpp`'s own corpus-driven tests); `?DUP`/`NIP`/`TUCK`
are not — nothing outside the `compiled_forth`-based conformance suites (`core_suite_stack.test.cpp`,
`core_suite_strings.test.cpp`) or the primitive-level `forth_state.test.cpp` (which calls
`apply_primitive` directly, bypassing dictionary lookup entirely) references them by name.

**Control-word reach — resolving the criterion's own hedge.** F35's own merge criterion says "one
control word (`ENDIF`-class at minimum, a full `IF` replacement if it holds)," and the step brief's own
framing suggested the second half might not be reachable, pointing at DIV-0015's boundary. Re-reading
`interpreter::apply_control_word`'s own `if_`/`else_`/`then_` cases (`interp.hpp`) shows each is exactly
as self-contained as `then_` alone (D17's own stated `ENDIF` example): `if_` emits one `op::branch0`
sentinel and pushes its own index; `else_` pops that index, emits one `op::branch`, and pushes its own
new index; `then_` pops and patches. None of the three reads or depends on anything the *other* two
left behind beyond what the data stack already carries by design (the orig/dest discipline itself).
Nothing in `apply_control_word`'s own `postpone_` case singles out `then_` — the whole-body-alias path
it takes for "a bare `machine::control_word` with no compiled form" applies identically to any of the
eighteen words in that category. So `: WHEN POSTPONE IF ; IMMEDIATE`, `: OTHERWISE POSTPONE ELSE ;
IMMEDIATE`, and `: ENDIF POSTPONE THEN ; IMMEDIATE`, defined and used together
(`prelude.test.cpp`'s own `WhenOtherwiseEndifReplaceIfElseThen`), genuinely reproduce `IF ... ELSE ...
THEN` end to end, under new names — "a full `IF` replacement" **holds**, in exactly the sense DIV-0015
already staked out: a whole-body alias, not a from-scratch reimplementation. It is not, and cannot be
(per DIV-0015's own "no VM-representable form"), a definition that *mixes* `POSTPONE IF` with other
code, or that builds `IF`-like behavior out of anything other than `POSTPONE IF` itself — there remains
no way to reach `op::branch0`/the orig-dest discipline except through the same C++ `apply_control_word`
case the original word already used. Nothing about this finding moves the DIV-0015 boundary; it only
establishes that the boundary's positive side (whole-body alias) reaches every structural control word
individually, not only `THEN`.

## Consequences

- `machine::default_dictionary()` now installs 95 entries (was 98); `dictionary.test.cpp`'s own
  `dict.size() == 95` and its doc comment record the new count and cite this DIV.
- Every session built through `compiled_forth<Source>` (all `conformance/core_suite_*.test.cpp`,
  `conformance/ttester_corpus.test.cpp`'s own compile-time calls, `forth.test.cpp`,
  `test_neg_syntax_error.cpp`) now pays a constant, measured 35-token interpreter-loop step-count tax
  (`interpreter::prelude_source`'s own doc comment) before its own text is scanned at all — D22's own
  "budgets are architecture" made concrete and measured, not merely asserted.
  Every capacity those existing call sites use (`MaxCode` down to 512 explicitly, 4096 by default;
  `MaxWords` 256 by default) already had ample headroom for the prelude's own ~10 compiled instructions
  and 6 dictionary entries; none needed raising.
- Any future step that wants to move `1+`/`1-` (or any other word `interp.test.cpp`'s own raw-
  `interpret`-plus-`default_dictionary` tests reference directly) into the prelude must first either
  route those specific tests through `build_session_with_prelude` instead, or accept that doing so
  changes what "the acceptance battery passes verbatim" is checked against.
- A future step wanting the same alias treatment for the remaining structural control words
  (`BEGIN`/`UNTIL`/`WHILE`/`REPEAT`/`DO`/`LOOP`/`+LOOP`/`LEAVE`/`UNLOOP`/`I`/`J`/`LITERAL`) can follow
  the identical `: NAME POSTPONE TARGET ; IMMEDIATE` shape this step already proved out for all three
  conditional words; this step deliberately did not do so itself (D12's own "1+" policy — the merge
  criterion asked for "one control word ... a full `IF` replacement if it holds," not exhaustive
  coverage of every structural word).

## Revisit condition

None required to close this DIV as written — it is a design record, not an open gap. Would need
revisiting only if a later step wants to move `1+`/`1-` (see "Consequences" above) or wants
`build_session` itself, rather than only `compiled_forth`, to carry the prelude by default.
