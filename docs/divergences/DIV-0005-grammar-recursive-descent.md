# DIV-0005: hand-written recursive descent for nested control structures

- **Status:** accepted-permanent
- **Date:** 2026-07-18
- **Step:** F7 (grammar)
- **Authority diverged from:** docs/forth-plan.md (D6)

## What diverged

D6 (`docs/forth-plan.md`) says the F4 combinator library is the production
parser, not merely a tool for atoms.
`read_program.hpp`'s token-level scanning (names, numbers, keywords,
comment capture) is built entirely from the F4/F5 free-function combinators
(`scan_word`, `skip_forth_space`, `scan_paren_comment`, `is_number_token`,
`token_to_cell`, `parser::some`, `parser::satisfy`), matching D6 exactly.
The grammar's *nested* productions -- `if`, `begin-until`, `begin-while`,
`do-loop`, each of which recursively contains `body-item*` -- are instead a
small set of mutually recursive plain `constexpr` functions
(`parse_body_until`, `parse_body_item_from_token`, `parse_if`, `parse_begin`,
`parse_do`), not a chain of combinator composition (`map`/`lift2`/`operator|`
built up the way `parser_ops.test.cpp`'s number parser or F5's `scan_word`
are).

## Why

A combinator object composed via `map`/`lift2`/`operator|` has a concrete,
finite C++ type -- `parser<F>` for some closed-over `F`. A body-item
production that recursively contains itself (an `IF` inside a `BEGIN`
inside a colon definition's body) would need a combinator whose own type
mentions itself: there is no finite `parser<F>` instantiation for "the
parser that parses `body-item*`," because `body-item` is defined partly in
terms of parsers built from body-item-parsing again.
The usual C++ answer to this shape -- type-erase the recursive parser
behind a `std::function`-like wrapper -- is not available here: it needs
heap allocation for arbitrary captured state, and D3 bars heap-backed
types (`fix`/`Box`) from the compiled pipeline; there is also no
`constexpr`-friendly type-erased callable in the C++26 standard library to
reach for instead.
Separately, assembling syntax-tree nodes requires threading a mutable
`foundation::tree_arena` alongside the cursor so each production can
allocate its own node via `foundation::make_arena_box`; the F4 `parser<F>`
signature (`cursor -> parse_result<T>`) has no slot for that shared,
mutated-in-place argument, so even a non-recursive production could not be
composed purely from `parser<F>` combinators without either widening that
signature project-wide or passing the arena through some other channel.

Plain function templates do not have this problem: `parse_body_until`
declares `parse_body_item_from_token` (and vice versa) via ordinary forward
declaration, and each calls into `parse_if`/`parse_begin`/`parse_do`, which
call back into `parse_body_until` -- C++ has always supported mutually
recursive function templates this way, with no self-referential type
involved, only self-referential *calls*.

## Consequences

- Every hand-written recursive function still calls down into the F4/F5
  free-function combinators for its own token-level work (`scan_token`,
  built from `scan_word`, is the single scanning primitive all of them
  share); only the tree-shaped recursion itself, and the arena-threading,
  is hand-written.
- Recursion is bounded by a `MaxDepth` template parameter on
  `read_program`, checked (`depth >= max_depth`) before `parse_if`/
  `parse_begin`/`parse_do` ever recurse one level deeper, diagnosed as
  `"max nesting depth exceeded"` rather than either compiling forever or
  silently accepting unbounded nesting -- this is what keeps the *compile
  time* cost of the recursion bounded too, independent of any input
  program's own nesting depth, since `MaxDepth` is fixed at the call site
  regardless of what depth a given program actually reaches.
- F11 (elaboration) and any later step walking or rebuilding a tree-shaped
  structure with genuine self-recursion (an elaborated-core node kind that
  can contain itself) should expect to reach for the same
  plain-mutually-recursive-function-template shape rather than trying to
  compose it from `functor`/`applicative`/`alternative`-style combinators,
  for the identical reason given here.
- The limitations doc (step F22) should note this alongside D6 when it
  explains where "the combinator library is the parser" needed a
  documented exception rather than a silent hand-rolled fallback.

## Revisit condition

None known. A `constexpr`-compatible type-erased callable in a future C++
standard, or a deliberate decision to widen the F4 `parser<F>` calling
convention project-wide to thread an arena parameter, would be the two
ways this could be revisited; neither exists today.
