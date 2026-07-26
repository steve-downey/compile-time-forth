<div class="abstract" id="org9da69fd">
<p>
The direct evaluator walks the tree. The stack machine flattens the tree into a
linear instruction array first, then runs the array with an instruction pointer.
The compiled artifact is a trivially copyable literal &#x2014; one value that is built
during compilation and runs, unchanged, both then and at ordinary runtime. That
is the whole point of the project made concrete. Eight of the seventeen opcodes
do real work; the other nine are reserved on purpose, and I want to say why.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 7 - The Oracle ←](post-7-the-oracle.md)

</nav>


# Flattening the tree

Codegen turns the elaborated core into a flat array of instructions, each one an opcode and a single immediate operand. The tree's structure becomes control flow between array indices. A literal is a `push`. A primitive reference is a `prim` carrying the primitive's enumerator. A call to a colon word is a `call` to that word's entry-point index; `ret` comes back.

The one part with any craft in it is branching, because a forward branch has to name a target that doesn't exist yet. When codegen emits the `branch0` for an `IF`, it does not yet know where `THEN` will land &#x2014; that code hasn't been generated. So it emits the branch with a placeholder operand, remembers the slot, and back-patches the real target in once it gets there. Backward branches, like `BEGIN/UNTIL` looping to the top, need no patching: the target is already behind you.

```c++
// src/smd/forth/machine/codegen.hpp  (shape, not verbatim)
int fixup = emit(op::branch0, /* placeholder */ 0);  // target unknown here
// ... generate the arm ...
code[fixup].operand = current_index();               // patch THEN's landing
```

`branch0` is worth a sentence on its own. It pops a flag and branches when the flag is *zero* &#x2014; Forth false. That polarity is not arbitrary. "Branch when false" is exactly what `IF` needs (skip the arm when the condition is false) and what `BEGIN/UNTIL` needs (loop back while the flag is still false). One opcode, both jobs, because the two constructs want the same test.


# One value, both times

Here is the artifact. One instruction:

```cpp
struct instr {
    op code{};      ///< Which operation to perform.
    cell operand{}; ///< The operation's single immediate operand.
};
```

and the compiled program it lives in:

```cpp
template <int MaxCode = 4096, int MaxWords = 256>
struct compiled_program {
    foundation::static_vector<instr, MaxCode> code{}; ///< The flat program.

    /// Entry-point instruction index for dictionary word `i`, or `-1` if
    /// word `i` is not a colon word.
    foundation::static_vector<int, MaxWords> entry_points{};

    /// Instruction index where the top-level program body begins -- the
    /// VM's own "main," run after every colon definition's own code (which
    /// only ever runs when called).
    int program_entry = 0;

    /// Cells the source `compiled_unit`'s data space had actually allotted
    /// (`VARIABLE`/`CREATE`) by the time codegen ran. F16's own @ref run
    /// consumes this at the start of every run to seed a fresh
    /// @ref forth_state's own data space before executing any instruction.
    int data_space_size = 0;

    /// See the class doc comment: not computed by this step.
    int required_stack_depth = -1;
    /// See the class doc comment: not computed by this step.
    int required_return_depth = -1;
};
```

That it is trivially copyable is the crux of the entire project, and the header `static_assert`-s exactly that. `compiled_program` is a literal type &#x2014; there is nothing in it for the compiler to have to prove trivial, just arrays of plain aggregates. Which means it can be built by a `constexpr` codegen call and then *be a value*: a namespace-scope constant, baked into the translation unit, indistinguishable from a table a human typed out.

And because it is an ordinary value, the same one runs in both worlds:

```c++
constexpr auto prog  = compile("...");        // codegen runs during compilation
static_assert(run(prog, compile_time_state)); // the VM runs during compilation too

// prog is just a constexpr value; nothing stops an ordinary call from running it
auto result = run(prog, runtime_state);       // same bytes, ordinary runtime
```

Same instruction array, same VM, once inside the constant evaluator and once as plain executable code. The program does not get recompiled for runtime; it survived compilation as data and runs from there. That is what I meant in Part 0 by "the same compiled program runs at compile time and at runtime," and it is the first entry where I can point at the object and say: this one, here, both times.


# Eight opcodes, and nine reserved

The opcode enum has seventeen entries. This step gives real codegen and VM behavior to eight: `push`, `push_xt`, `prim`, `call`, `ret`, `branch`, `branch0`, `halt`. The other nine exist in the enum and do nothing yet.

Six of them &#x2014; `do_setup`, `loop_step`, `plus_loop_step`, `push_index`, `leave`, `unloop` &#x2014; are the `DO` &#x2026; `LOOP` machinery. Codegen diagnoses a counted loop as not implemented rather than emitting any of them, the same stop the direct evaluator already makes. The other three &#x2014; `execute`, `catch_mark`, `throw_op` &#x2014; are for execution tokens and exceptions, which is where the bet from Part 0 eventually gets cashed. None of them is emitted. The VM diagnoses any of the nine if it ever meets one, so the path is defensive, not reachable.

I reserved them without building them for a specific reason, and it is not laziness. Putting the enumerators in now means the enum never has to change when the later steps land &#x2014; no renumbering, no churn in the one type every backend shares. But implementing placeholder *semantics* for them would be worse than leaving them empty: it would mean guessing at a loop-parameter frame shape and a handler-frame shape that the real steps should design deliberately, and then very likely tearing that guess out. Reserve the space, diagnose the gap, design the behavior when it is actually the job. A reserved opcode that faults loudly is honest; a placeholder opcode that half-works is a trap.


# The bound I don't have

Two fields default to `-1`: `required_stack_depth` and `required_return_depth`. The plan asks a `compiled_program` to carry the stack capacities a caller needs to run it safely. I am carrying the fields and not the numbers.

The reason is that no analysis in the project computes the right number yet. The effect checker from Part 6 computes each definition's *net* effect and its minimum entry depth &#x2014; not the running *peak* depth a body reaches partway through. `DUP DUP DROP DROP` has a net effect of zero and a peak two cells deeper than it started. A real capacity bound needs that peak, tracked across every path including loops, and nothing here tracks it. I would rather ship `-1`, meaning "not computed," than ship a number I made up. A wrong bound is worse than no bound, because a wrong one reads as a guarantee.

So callers size their own `forth_state` generously by hand, just as the tests already do &#x2014; a stack depth of 64 chosen by looking at the program, not by reading a field. It is not a runtime hazard: every stack operation stays bounds-checked, so an under-sized state faults cleanly instead of corrupting anything. It only means the artifact cannot yet answer "how big does my state need to be." Whichever later step first wants to auto-size a state from a program is the step that gets to extend the analysis. Today it says `-1` and means it.

The machine compiles and runs, and it agrees with the oracle on everything the oracle can do. One thing is left before any of this is usable from outside: a single call to reach it, instead of four pipeline stages wired together by hand.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 7 - The Oracle](post-7-the-oracle.md) | [Part 9 - The One-Shot API, and the Summit Not Yet Reached →](post-9-one-shot-api.md)

</nav>


# References
