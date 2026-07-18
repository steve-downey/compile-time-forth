# DIV-0003: Parser combinators register against `foundation`'s typeclass machinery directly

- **Status:** accepted-permanent
- **Date:** 2026-07-18
- **Step:** F4 (import parser combinators)
- **Authority diverged from:** docs/forth-plan.md D2 (imported infrastructure: renamespace and debt-fix only, semantic changes need a divergence doc)

## What diverged

The imported `parser_ops.hpp` (from
`~/src/compile-time-scheme/main/src/smd/smdscheme/parser/parser_ops.hpp`)
defined its own local `ParserApplicative<Impl>`/`ParserAlternative<Impl>`
CRTP layers and its own local `parser_typeclass<T>` lookup variable,
entirely independent of `smd::smdscheme::foundation`'s
`functor`/`applicative`/`alternative` CRTP bases and typeclass variables —
the Scheme reference's parser combinators never actually plugged into its
own foundation typeclass machinery, despite that machinery existing
alongside it.

In `smd::forth::parser::parser_ops`, the combined typeclass object instead
derives directly from `smd::forth::foundation::functor<parser_functor_impl>`,
`foundation::applicative<parser_applicative_impl>`, and
`foundation::alternative<parser_alternative_impl>`, and `parser<F>` is
registered against foundation's own `functor_typeclass`,
`applicative_typeclass`, and `alternative_typeclass` variable templates (in
`parser_ops.hpp`, reopening `namespace smd::forth::foundation` to add the
three partial specializations). `foundation::fmap`/`foundation::invoke`/
`foundation::alt` (the free CPOs) now dispatch to `parser<F>` exactly as
they would to any other registered type; there is no separate,
parser-only typeclass lookup mechanism.

One consequence: `foundation::alternative<Impl>` requires both an `alt` and
an `empty` primitive (the identity element for `alt`), where the Scheme
reference's local parser typeclass provided only `alt`. `empty` is added as
`parser_alternative_impl::empty<T>()`, a parser of value type `T` that
always fails without consuming — but because `parser<F>` is a family of
types parameterized by the wrapped callable rather than one concrete
container type, and `foundation::empty_fn`'s calling convention
(`tc_type{}.empty()`, zero arguments) has nothing to deduce `T` from, the
generic zero-argument free function `foundation::empty<T>()` cannot reach
this member for `parser<F>`. Callers who need an always-failing parser call
`parser_v.empty<T>()` directly (the typeclass instance's own template
member, with `T` supplied explicitly), which works and is exercised in
`parser_ops.test.cpp`.

## Why

The plan's Step F4 section explicitly calls for "the functor/applicative/
alternative typeclass-object layering (`parser_v` CPO)" where "the parser
types register against `smd::forth::foundation`'s typeclass machinery" —
this is a deliberate improvement over the reference implementation, not an
accidental behavior change: it makes the parser the first production,
exported type to register against foundation's CRTP bases and CPOs (F3's
own tests for `functor.hpp`/`applicative.hpp`/`alternative.hpp` only used
small test-local instance types, per `handoff.md`), so a typeclass-law
regression in `foundation` itself would now be caught through a real
consumer, not just synthetic test doubles.

## Consequences

- `parser_ops.hpp` includes `foundation/{functor,applicative,alternative}.hpp`
  and depends on their CRTP-base shape; a change to those headers' `Impl`
  contract (the required primitive method set) is a breaking change for
  `parser_ops` too, not just for `foundation`'s own tests.
- `foundation::empty<T>()` (the generic zero-argument CPO) is unusable for
  `parser<F>` specifically, for the structural reason given above. Any
  later step (F5 onward) that wants an always-failing parser must call
  `parser_v.empty<T>()` directly; this is documented on
  `parser_alternative_impl::empty`'s doc comment in `parser_ops.hpp` and
  exercised in `parser_ops.test.cpp`'s `AlternativeEmptyIsIdentity` test.
- The Scheme reference's `sequence_left`/`sequence_right` names on the
  typeclass object are not carried over; `foundation::applicative<Impl>`
  already derives the same two operations under the names `discard_second`
  (keeps the first argument) and `discard_first` (keeps the second
  argument) — reusing foundation's vocabulary rather than maintaining two
  names for the same operation. The free functions `sequence_left`/
  `sequence_right` in `parser.hpp` (Layer 0, not the typeclass object) are
  unaffected and still exist under their original names.

## Revisit condition

None. This is the intended, plan-directed integration shape and is kept
permanently.
