# DIV-0002: `parse_error::operator==` fast path is not constexpr-portable

- **Status:** accepted-permanent
- **Date:** 2026-07-18
- **Step:** F3 (import foundation)
- **Authority diverged from:** docs/forth-plan.md D2 (imported infrastructure: renamespace and debt-fix only, semantic changes need a divergence doc)

## What diverged

The imported `parse_error::operator==` (from
`~/src/compile-time-scheme/main/src/smd/smdscheme/foundation/parse_error.hpp`)
opened with a fast path: `if (lhs.message == rhs.message) return true;`,
comparing the two `char const *` message pointers directly before falling
back to a null check and a character-by-character scan.
In `smd::forth::foundation::parse_error`, that fast path is replaced with an
explicit `if (lhs.message == nullptr && rhs.message == nullptr) return true;`
ahead of the existing `if (lhs.message == nullptr || rhs.message == nullptr)
return false;` check.
The observable truth table is unchanged: two null messages are still equal,
one null and one non-null are still unequal, and two non-null messages fall
through to the same character scan as before (which trivially returns true
when the two pointers happen to be equal, since a string trivially equals
itself character-by-character).

## Why

`gcc-16` accepts the original fast path in a `static_assert` context: it
treats string-literal addresses in the same translation unit as pooled
consistently, so `lhs.message == rhs.message` for two `"oops"` literals is a
usable constant expression that evaluates to `true`.
`clang-21` rejects it: per `[expr.eq]`, whether two pointers to
potentially-overlapping string-literal objects compare equal is
unspecified, and a comparison whose result is unspecified is not usable in a
constant expression, so `parse_error.test.cpp`'s
`static_assert(parse_error{source_pos{1, 1, 2}, "oops"} == parse_error{source_pos{1, 1, 2}, "oops"})`
failed to compile under `clang-21` with "comparison of addresses of
potentially overlapping literals has unspecified value".
This project's baseline requires both `gcc-16` and `clang-21` green (F1); the
reference Scheme repo's own test suite apparently never exercised this
`static_assert` shape against `clang-21` with an identical literal on both
sides, so the defect went undetected there.
Comparing a pointer to `nullptr` (a null pointer constant) is always usable
in a constant expression on both compilers, so restricting the pointer
comparisons to null checks removes the non-portable step without changing
behavior for non-null messages.

## Consequences

- No test behavior changed; `parse_error.test.cpp`'s existing assertions
  (imported unmodified) still hold under both toolchains.
- Any later step that copies more `foundation`/`parser`/`reader` files by the
  same D2 process should check imported `operator==`/`operator<=>` bodies for
  the same pattern (an equality fast path comparing two potentially-distinct
  pointers, rather than one of them being a null-pointer-constant) before
  trusting a `gcc-16`-only compile as portable.

## Revisit condition

None. This is a compile-time-portability fix with no semantic effect, kept
permanently as the only conforming implementation of `parse_error` equality
across the project's two required toolchains.
