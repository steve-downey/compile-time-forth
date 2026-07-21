# DIV-0009: F15 public-API default capacities, new negative-compile infrastructure, and org-anchor handling

- **Status:** accepted-permanent
- **Date:** 2026-07-21
- **Step:** F15 (public one-shot API)
- **Authority diverged from:** docs/forth-plan.md

## What diverged

`docs/forth-plan.md`'s Step F15 section gives `compiled_forth<Source>`'s
literal call syntax (`compiled_forth<"...">`) and its two convenience
accessors (`.stack()`, `.output()`) but does not specify concrete capacity
numbers, whether those capacities should be further template parameters or
hardcoded, or how the merge criterion's "a negative-compile test proves a
syntax error fails compilation" should actually be implemented and enforced.
This step makes three concrete choices the plan left open:

1. **`compiled_forth<Source, MaxCode = 4096, MaxNodes = 1024, MaxBody = 64,
   MaxName = 32, MaxDepth = 32, MaxWords = 256, MaxData = 1024,
   MaxWarnings = 64, StackDepth = 1024, RStackDepth = 1024, MaxOut =
   4096>`** exposes every pipeline capacity as a further template
   parameter with a default, rather than hardcoding them, per this
   project's own standing D2 rule ("all capacities are template parameters
   with defaults"). `compiled_forth<"...">` (one argument) still matches
   the plan's own literal call-site wording exactly, because every
   parameter after `Source` has a default. The first eight defaults
   (`MaxCode` through `MaxWarnings`) are copied verbatim from the
   production defaults `read_program.hpp`/`elaborate.hpp`/`codegen.hpp`
   already use on their own template parameters, so they are not new
   numbers, only reused ones. `StackDepth`, `RStackDepth`, and `MaxOut`
   have no equivalent existing production default to inherit --
   F13's/F14's own test files only ever chose small test-scoped values
   (16-64 cells, matching each test's own known-small programs) -- so this
   step picks `1024`/`1024`/`4096` respectively: generous headroom for a
   public one-shot API whose caller does not know in advance how deep a
   given program's data/return stack will run or how much text it will
   print, in the same order of magnitude as this project's other
   already-established "generous default" capacities (`MaxNodes = 1024`,
   `MaxData = 1024`).
2. **A new, narrowly scoped negative-compile-test mechanism**: this project
   had no negative-compile-test harness before this step (see F4's own
   handoff.md section: `test_neg_parser_concept.cpp` was deliberately not
   imported from `compile-time-scheme`, with F21, "error-quality and
   negative-compile pass," named as where general infrastructure would
   arrive). F15's own merge criterion needs one real proof for one specific
   claim now, not a general harness, so this step adds a single
   `try_compile()` call in `src/smd/forth/CMakeLists.txt` (after this
   directory's own `add_subdirectory()` calls, so every header this
   library owns is already registered on `compile-time-forth.forth`'s
   `FILE_SET`) that compiles `neg_compile_syntax_error.cpp` -- a program
   containing `compiled_forth<": SQUARED DUP *">` (missing its closing
   `;`) -- via `LINK_LIBRARIES compile-time-forth.forth` (so the probe
   compile automatically inherits the real target's own include
   directories and C++ standard requirement) and fails the whole configure
   step with `message(FATAL_ERROR ...)` if that probe compile ever
   *succeeds*. This runs at CMake configure time, which `make compile`
   re-triggers automatically whenever `CMakeLists.txt` or
   `neg_compile_syntax_error.cpp` changes (CMake's own generated
   rerun-cmake build rule) -- so the check is enforced by the ordinary
   build, not a separate opt-in step. It does not attempt to match a
   specific compiler diagnostic substring (AGENTS.md's "match specific
   diagnostics in negative compile tests" guidance): GCC's and Clang's own
   constexpr-failure diagnostics for this particular failure shape (`std::
   get` on the wrong `std::variant` alternative, reached transitively
   through several inlined constexpr calls) are verbose, implementation-
   specific, and not a stable string to pin a test to; the check instead
   asserts the one fact both compilers agree on and that actually matters
   for the contract: the translation unit does not compile.
3. **`compile-time-forth.org`'s four placeholder transclusion anchors are
   updated to real anchors**, not deferred to F22 with a TODO note (the
   plan explicitly left this choice to the worker). Since `forth.cpp` no
   longer exists (this component became header-only once its only
   function moved into `compiled_forth`'s own header-only pipeline), the
   four-anchor shape ("declaration" / "definition" / "test" / "hello")
   became three anchors instead: `source_literal`'s own declaration
   (`forth.hpp`, anchor `a5ec0c45-...`), the merge-criterion `static_assert`
   block from `forth.test.cpp` (anchor `19adbd89-...`), and `hello.cpp`'s
   `main` (anchor `e8803c30-...`) -- there is no longer a separate
   "definition" `.cpp` file to transclude a second anchor from, since
   `compiled_forth`'s own definition lives in the same header as its
   declaration.

## Why

- Choice 1 keeps every capacity consistent with D2 without inventing
  numbers where a project-standard one already exists (the first eight),
  and documents the reasoning for the three genuinely new ones (the last
  three) rather than picking them silently.
- Choice 2 satisfies the plan's own literal merge criterion ("a negative-
  compile test proves a syntax error fails compilation") with real,
  automatically enforced infrastructure, without building the general F21
  harness ahead of its own step -- narrowly scoped to the one file/one
  claim F15 itself needs proven.
- Choice 3 was cheap once `forth.cpp` was deleted (the anchors were already
  broken by that deletion regardless of which choice was made) and keeps
  `compile-time-forth.org` demonstrating real, current transclusion targets
  rather than a stale reference plus a deferred TODO.

## Consequences

- F16/F17/F18a and later steps that add new public-facing capabilities
  should follow `compiled_forth`'s own pattern (defaults matching an
  existing production default where one exists, a documented new choice
  where none does) rather than inventing another set of capacity defaults
  independently.
- F21 ("error-quality and negative-compile pass") inherits one working
  example of a `try_compile()`-based negative-compile check
  (`src/smd/forth/CMakeLists.txt`) to generalize from, but is not
  obligated to reuse this exact mechanism -- F21's own scope is a general
  harness, which may reasonably look different (e.g. one CMake function
  wrapping this pattern, applied to a table of `(source, expected-
  diagnostic-substring)` pairs) once diagnostic-quality work gives Forth
  syntax errors stable, matchable messages worth asserting against.
- `compile-time-forth.org` now transcludes from `forth.hpp` (twice, for
  `source_literal`'s declaration) and `forth.test.cpp`/`hello.cpp`; a
  future step editing those anchors' surrounding code must keep the named
  UUID comment pairs intact (docs/CODING_RULES.md's own transclusion
  rules).

## Revisit condition

Choice 1's `StackDepth`/`RStackDepth`/`MaxOut` defaults should be revisited
if a future step computes a real whole-program peak-stack-depth bound
(DIV-0008's own revisit condition) and wants `compiled_forth` to size a
`forth_state` from that bound instead of a fixed generous constant.
Choice 2 is closed once F21 lands a general negative-compile-test harness
that supersedes or subsumes this one-off `try_compile()` call.
Choice 3 has no revisit condition; it is accepted-permanent.
