# DIV-0022: `/`/`MOD` are symmetric (truncating), gforth's are floored, for negative operands

- **Status:** accepted-permanent
- **Date:** 2026-07-26
- **Step:** F32 (conformance), docs/forth-plan-2.md
- **Authority diverged from:** `gforth` (the D23 differential oracle), not Forth-2012 itself

## What diverged

`machine::forth_state.hpp`'s own `apply_primitive` implements `primitive::slash`/`primitive::mod_`
as plain C++ `/`/`%`, which truncate toward zero (symmetric division): `-7 2 /` gives `-3`, `-7 2
MOD` gives `-1` (`-3 * 2 + -1 = -7`). `gforth 0.7.3` implements plain `/`/`MOD` as *floored*
division instead: `-7 2 /` gives `-4`, `-7 2 MOD` gives `1` (`-4 * 2 + 1 = -7`) — confirmed by
direct invocation (`gforth -e '-7 2 / . -7 2 mod . cr bye'` prints `-4 1`). For non-negative
operands (the overwhelmingly common case, and everything `core_suite_arithmetic.test.cpp` actually
asserts) both conventions agree exactly; they diverge only when exactly one operand is negative
and the division is inexact.

## Why

**Forth-2012 leaves plain `/`/`MOD` implementation-defined.** The standard mandates a specific
rounding convention only for the words that name it explicitly — `FM/MOD` (floored) and `SM/REM`
(symmetric) — precisely because ordinary `/`/`MOD`'s own rounding is an ambiguous condition a
conforming system may resolve either way. Neither this project nor gforth is wrong by the standard
itself; they simply picked different (both legal) answers to the same ambiguous condition. D23
names gforth as "the differential oracle" for *behavior this project has not otherwise decided*,
not as an authority this project's own already-settled choices must retroactively match — this
project's own symmetric convention was already the documented, tested behavior every arithmetic
test before this step relies on (`machine/forth_state.hpp`'s own doc comment on `apply_primitive`
has said "symmetric (C++ truncating) division, not floored division" since before F32 existed).
Changing it now to chase gforth's own floored convention would silently change the answer every
existing `/`/`MOD` test in this project already asserts for a negative operand — a much larger,
untargeted change for a divergence the standard itself declares legal either way.

**Discovered by the mechanism D23 exists for.** This divergence surfaced directly from building
the gforth differential harness this step adds (`gforth_diff.test.cpp`) — exactly the mechanism
whose whole job (D14 leg (b)) is to surface gaps like this one, whether or not either side is
"wrong."

## Consequences

- `gforth_diff.test.cpp`'s own differential battery excludes negative-operand `/`/`MOD` cases
  (`-7 2 /`, and similar) — it still exercises `/`/`MOD` with non-negative operands, where both
  systems agree, so the exclusion is scoped to exactly the ambiguous case, not the whole word.
  `docs/conformance-exclusions.md` records this scoping under "gforth differential harness scope."
- `core_suite_arithmetic.test.cpp`'s own `T{ ... -> ... }T` assertions for `/`/`MOD` use only
  non-negative operands, or operands where truncating and floored division happen to agree
  (a negative dividend divisible evenly, for instance, where both conventions give the same
  quotient and a zero remainder) — this project's own compile-time gate is checking *this
  project's own* declared convention, not asserting it is the only legal one.
- Any future step wanting `FM/MOD`/`SM/REM` (out of scope today, D12's double-cell cut) would add
  both explicitly rather than reusing plain `/`/`MOD` for either rounding convention.

## Revisit condition

`none`. Both conventions are Forth-2012-legal; there is no criterion anywhere in
docs/forth-plan-2.md asking this project's own `/`/`MOD` to match gforth's own choice specifically,
and doing so now would break existing, already-accepted behavior for a change the standard does
not require.
