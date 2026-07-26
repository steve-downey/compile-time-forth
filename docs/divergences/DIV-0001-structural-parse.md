# DIV-0001: structural grammar, not an interpretive outer text interpreter

- **Status:** superseded by DIV-0011
- **Date:** 2026-07-18
- **Step:** plan-time (decision record D5 of docs/forth-plan.md)
- **Authority diverged from:** Forth-2012

## What diverged

Classical Forth parses with a stateful outer interpreter: immediate words run at compile time and lay down control-flow structure themselves.
This project instead parses source structurally with constexpr applicative parser combinators: colon definitions and control structures (`IF`/`ELSE`/`THEN`, `BEGIN`/`UNTIL`, `BEGIN`/`WHILE`/`REPEAT`, `DO`/`LOOP`/`+LOOP`) are grammar productions producing a nested tree.

## Why

The project's thesis is that Forth's control vocabulary — `EXIT`, `LEAVE`, `CATCH`/`THROW` — is exactly the one-shot, upward-only, dynamic-extent discipline that structured concurrency (sender/receiver) can express soundly.
A stateful outer interpreter with immediate words is orthogonal to that thesis and would dominate the reader and elaborator budget without illuminating anything about compile-time compilation.
Structural parsing also matches the project's other foundational choice (D3): every tree is a flat arena built by structural recursion, which an interpretive outer loop laying down control structure imperatively does not naturally produce.

## Consequences

- No `IMMEDIATE`, no `POSTPONE`, no `[` `]`, no `STATE`, no user-defined parsing words, anywhere in the project (all steps).
- The grammar (step F7) is a fixed, closed set of productions; there is no mechanism for a Forth program to extend its own syntax.
- The combinator library (step F4) is the production parser, not merely a tool for atoms (decision D6).
- The limitations doc (step F22) must list this divergence alongside D12's scope cuts.

## Revisit condition

None.
This is the project's foundational, permanent divergence from Forth-2012.
