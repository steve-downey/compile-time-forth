# DIV-0004: dictionary `variable_word` uses `cell`, not a distinct `addr` type, as a placeholder

- **Status:** resolved (F11)
- **Date:** 2026-07-18
- **Step:** F9 (dictionary), resolved in F11 (elaborated core and resolution)
- **Authority diverged from:** docs/forth-plan.md

## What diverged

`docs/forth-plan.md`'s Step F9 spec writes the dictionary entry variant as
`{primitive_opcode, colon_word{core_id, effect}, variable_word{addr},
constant_word{cell}, foreign_word{index}}` and separately (Step F10, D10)
states that `addr` is a distinct typed index, explicitly convertible to/from
`cell`, so addresses can live safely on the data stack.
`src/smd/forth/machine/dictionary.hpp`'s `variable_word` struct instead
stores a plain `cell addr` field:

```cpp
struct variable_word {
    cell addr = 0;
};
```

## Why

F9 (dictionary) and F10 (data space) both depend only on F8 (machine
substrate) and run in parallel, in separate worktrees, per the plan's
parallelism summary; neither is a dependency of the other.
The distinct `addr` type is F10's deliverable (`src/smd/forth/machine/data_space.hpp`,
per the plan's own Step F10 text), and does not exist anywhere in the F8
baseline this worktree was cut from.
F9 therefore has no typed `addr` to name.
The only representation of a data-space location that exists as of F8 is a
plain `cell` (F8's placeholder `forth_state::data_space()` is a bare
`foundation::static_vector<cell, MaxData>`, addressed by plain indices), so
`variable_word::addr` uses that placeholder rather than inventing a second,
possibly-conflicting `addr` type in a header F10 does not own.

## Consequences

- F10 lands a real, distinct `addr` type (explicitly convertible to/from
  `cell`, per D10) in its own new file.
  When F9 and F10 are merged together, whoever does that merge (or a small
  follow-up step) should change `variable_word::addr`'s type from `cell` to
  F10's `addr`, and add whatever include `dictionary.hpp` needs to name it.
- Until that follow-up lands, any code that builds a `variable_word` is
  passing a raw data-space index as a `cell`, with no compile-time
  protection against mixing an address with an ordinary numeric value on the
  stack -- exactly the hazard D10's distinct `addr` type exists to prevent.
  This is fine as an F9-local implementation detail (nothing in F9 puts a
  `variable_word::addr` value on the data stack), but F11 (elaboration,
  which builds `variable_word` entries when it processes `VARIABLE`
  declarations) and F16 (memory words end-to-end) must not treat this
  placeholder `cell` as load-bearing type safety -- it is not the D10
  guarantee yet.
- No other part of F9 depends on the shape of `addr`; `dictionary_entry`,
  `lookup`, and `default_dictionary` are unaffected by which type
  `variable_word::addr` ends up being.

## Revisit condition

Closed once F10 merges and a follow-up change retypes
`variable_word::addr` from `cell` to F10's distinct `addr` type.

## Resolution (F11)

F10 (`src/smd/forth/machine/data_space.hpp`) merged first and defines
`machine::addr` exactly as D10 specifies: a private `cell` index, an
`explicit addr(cell)` constructor, and an `explicit operator cell() const`.
F11 (elaborated core and resolution) is the first step that actually
constructs a `variable_word` (elaborating a `VARIABLE`/`CREATE`
declaration), so it retypes the field as part of its own work:

```cpp
struct variable_word {
    addr address{};
};
```

The field is also renamed from `addr` to `address` -- naming a data member
identically to its own type (`addr addr{};`) does not compile in C++
(`-Wchanges-meaning`: the member declaration's own name hides the type name
mid-declaration), so the field could not keep its original spelling once its
type became `addr` itself. `dictionary.hpp` gained an `#include
<smd/forth/machine/data_space.hpp>` for the type. `dictionary.test.cpp`
was updated to construct `variable_word{addr{3}}` (explicit conversion,
matching `addr`'s explicit constructor) and to read back `.address` instead
of `.addr`.

No other part of F9 or F10 depended on `variable_word::addr`'s type or
name, so this is the only touched call site outside `dictionary.hpp`/
`dictionary.test.cpp` themselves.
