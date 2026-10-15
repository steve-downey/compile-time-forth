# DIV-0029: The foreign vocabulary is threaded beside the dictionary, not stored in it

- **Status:** accepted-permanent
- **Date:** 2026-07-29
- **Step:** F34 (foreign function interface), docs/forth-plan-2.md
- **Authority diverged from:** docs/forth-plan-2.md §6 F34, and the R1 F19 text it
  carries (`default_dictionary().with_foreign("GCD", gcd_word)`)

## What diverged

The plan (F34's own "R1 F19 carried" paragraph) specifies `foreign_word` as a plain
function pointer `result<void>(*)(forth_state&)` "registered into the dictionary by a
builder API before compilation," spelled
`default_dictionary().with_foreign("GCD", gcd_word)`.

The signature is delivered verbatim: `machine::foreign_fn<MaxDepth, MaxRDepth,
MaxData, MaxOut>` is `status (*)(forth_state<...> &)`, with direct access to both
stacks, the data space, and the output buffer, and the same `foundation::result`
failure channel `apply_primitive` uses.

Three things around it differ.

**1. The function pointers live in a separate registry, not on the dictionary
entry.** `machine::foreign_vocabulary<MaxForeign, MaxDepth, MaxRDepth, MaxData,
MaxOut>` (`machine/foreign.hpp`) is a flat, fixed-capacity array of those pointers;
`machine::foreign_word` (the dictionary binding) carries an `index` into it, exactly
the "opaque handle into whatever registry F19 builds" the F9-era slot already
reserved. `machine::foreign_dictionary` bundles a `dictionary` and a
`foreign_vocabulary` so one `with_foreign` call appends to both and their indices can
never disagree, and `machine::default_foreign_dictionary()` is the bundle's own
`default_dictionary()` starting point.

**2. `with_foreign` returns a `foundation::result`, not the bundle directly.** A full
dictionary or a full registry is a diagnosed failure (D7); at the only site this type
is meant for -- a namespace-scope `constexpr` initializer -- `.value()` on a failed
result is not a core constant expression, so the failure is a hard compile error,
exactly the contract `forth.hpp`'s own `compiled_forth<Source>` already has. The
chained spelling is therefore
`default_foreign_dictionary<>().with_foreign("GCD", &gcd_word, declared_effect(2, 1)).value()`.

**3. The vocabulary is a fifth threaded parameter, defaulted to `nullptr`, exactly
like F28's own `dict`.** `machine::run_from`/`run`, `interpreter::call_word`/
`execute_entry`/`apply_control_word`/`interpret`/`build_session`/
`build_session_with_prelude`/`call_defined_word`, and `sender::run_word_via_vm`/
`word_sender`/`run_from_via_senders`/`run_via_senders` each gained one nullable,
non-owning `foreign_vocabulary const *` parameter and one defaulted `MaxForeign`
template parameter. Every call site that existed before this step compiles and behaves
identically. `forth.hpp` gains `compiled_forth_with(text, vocabulary, fuel)`, a
function rather than a variable template, because the vocabulary is a *value*
carrying function pointers rather than an NTTP.

A fourth, smaller divergence, forced by the toolchain rather than by design:
**a null function pointer is diagnosed at `foreign_vocabulary::call`, not at
`foreign_vocabulary::add`.** Under GCC's own UndefinedBehaviorSanitizer
(`-fsanitize=undefined`, part of this project's default `Asan` config) the address of a
function is not usable in a constant expression *as an operand of a comparison*:
`fn == nullptr` alone makes any enclosing `static_assert` fail to evaluate with "is not
a constant expression", even though calling through the same pointer is perfectly
constant-evaluable. A registration is exactly where such a comparison *would* be
evaluated during constant evaluation -- a `constexpr` vocabulary is built by calling
`add` -- so a check there would make every compile-time FFI session uncompilable in the
configuration this project builds in.

The obstruction only fires when the comparison is actually *evaluated* during constant
evaluation, and it need not be. `call` therefore spells its check

```cpp
if (!std::is_constant_evaluated() && functions_[index] == nullptr) { ... }
```

and the order matters: short-circuiting means the comparison is never evaluated in a
constant expression, so the obstruction never fires, while at ordinary runtime it is an
ordinary null check reported through the same `foundation::result` channel an
out-of-range index already uses. Both forms were compiled side by side under
`g++-16 -std=c++26 -fsanitize=undefined` to confirm: the bare comparison fails with
"non-constant condition for static assertion", the guarded form compiles and
constant-evaluates.

Nothing is left undiagnosed and nothing is lost at compile time. A genuinely null
pointer reached during constant evaluation falls through the guard to the call itself,
and calling through a null function pointer is not a constant expression -- a hard
compile error, a strictly better diagnosis than a returned result. So the durable
invariant holds unqualified: all misuse is a diagnosed error, never UB.

## Why

**Why the registry cannot live in the dictionary.** `machine::forth_state` is
capacity-parameterized on four template parameters; `machine::dictionary` is
parameterized on two, neither of them a state capacity. A `dictionary_entry` therefore
has no way to *name* the type `status (*)(forth_state<A,B,C,D> &)` at all. Two
alternatives were considered and rejected:

- **Fold the registry into `forth_state` itself** (legal: a class may hold a pointer
  to a function taking a reference to that same class). Rejected because it makes the
  set of foreign words part of *machine state* rather than part of the vocabulary --
  D7/D10/D13 name exactly what `forth_state` carries, and a registry of C++ functions
  is not that -- and because registration would then be two-sided (a dictionary header
  here, a state slot there) with nothing keeping the two in agreement.
- **Type-erase the state behind a virtual interface**, so `foreign_word` could hold a
  capacity-independent function pointer directly and nothing would need threading.
  Rejected because it gives up precisely what the plan asks for ("direct access to the
  underlying stacks, data space, and output buffer, as requested"): every foreign word
  would reach the machine through a narrow, invented abstraction instead of through the
  same `forth_state` a primitive gets.

Threading a nullable pointer is the minimal addition that keeps the plan's own
signature intact, and it is not a new mechanism: it is the *identical* shape DIV-0016
already established for `dict`, for the identical reason (a compiled body has no other
channel to something the VM does not otherwise know about).

**Why the declared effect lives on the dictionary entry, not on the registry.**
`interpreter::effect_lint`'s own `instruction_effect` sees an `op::foreign` instruction
and a `dictionary`; it is never given the vocabulary. Putting `effect_known`/
`effect_inputs`/`effect_outputs` on `machine::foreign_word` (mirroring
`compiled_colon_word`'s own three fields) lets the checker resolve a declared effect by
the same linear dictionary scan `op::call` already uses, with no new plumbing at all.

## Consequences

- `machine::op` gains one enumerator, `foreign`, appended after `halt`. Its operand is
  a registry index, not a code-space address. `interpreter::compile_entry` emits it for
  a `foreign_word` met while compiling, and `interpreter::resolve_execution_token`
  emits it inside the same guarded, `ret`-terminated stub primitives already get --
  which is what makes "callable via `'`/`EXECUTE` like any other word" true by
  construction and closes half of DIV-0016's own remaining `'`/`[']` scope cut (see
  that record's F34 addendum).
- `machine::dictionary_binding`'s `foreign_word` alternative gains three effect fields
  and a defaulted `operator==`; it stays trivially destructible, as does the new
  registry and bundle (`machine/foreign.hpp`'s own `detail` static_asserts).
- Both executors treat a foreign word's own failure exactly as they treat a
  primitive's: `ABORT"`'s distinguished condition always routes through `THROW -2`, and
  every other diagnosis is mapped to its standard Forth-2012 `THROW` code when a
  handler is active (DIV-0018's rule, reused verbatim in `vm.hpp`'s and
  `sender/lower.hpp`'s own `op::foreign` cases). `CATCH` over a foreign word therefore
  works with no FFI-specific code anywhere.
- **A foreign word must not use the return stack as data.** D24's own fallback trigger,
  `sender::word_uses_return_stack_data`, scans *instructions* for `>R`/`R>`/`R@`; a
  foreign call is opaque to it, so a foreign word that manipulated `state.returns()`
  from C++ would be lowered natively by the sender backend and could silently disagree
  with the VM. This is a documented constraint on foreign words, not a defect in the
  trigger: nothing in F34's own merge criteria needs a return-stack-manipulating
  foreign word, and a later step that does would have to extend the trigger (a
  per-registration flag on `foreign_word` is the obvious shape).
- A session image carries foreign *headers* but never function pointers, so re-running
  a session at ordinary runtime requires the embedding program to supply the same
  vocabulary again, alongside the `forth_state` (`interpreter::call_defined_word`'s own
  `foreign` parameter). The `forth_state` it supplies must use the same four capacities
  the vocabulary's function pointers are typed on.
- `build_session` gained a `vm_fuel` parameter, defaulted to the same value
  `interpret`'s own default already used, so that the new `vocabulary` parameter could
  precede it without changing any existing behavior.
- The `!std::is_constant_evaluated() && p == nullptr` shape is reusable, and worth
  knowing generally: **any** null check on a function pointer in a `constexpr` path in
  this project must be written that way, or it breaks compile-time evaluation under the
  default build configuration. `machine/foreign.test.cpp`'s own
  `VocabularyDiagnosesANullImplementation` covers the runtime side; the compile-time
  side is a hard compile error by construction and has no `static_assert` counterpart.

## Revisit condition

`accepted-permanent` for the registry/threading design and for the effect-on-the-header
placement: none is a scope cut, and both fit the shapes DIV-0016 already established.

Two narrower items could reopen:

- The `is_constant_evaluated` guard, and `add`'s own inability to reject null at
  registration time, both close the moment GCC's UBSan instrumentation stops making
  function-address comparisons non-constant, or if this project ever gains a build
  configuration in which the comparison can be written unconditionally. That is a
  toolchain accommodation, not a design position -- but it costs nothing while it
  lasts, since the guarded check diagnoses exactly what an unguarded one would.
- The "no return stack as data" constraint on foreign words closes if a later step's
  own merge criterion needs one; the fix is a flag on `foreign_word` that
  `word_uses_return_stack_data` consults, not a redesign.
