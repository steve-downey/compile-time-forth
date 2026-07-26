<div class="abstract" id="org50e40d8">
<p>
Part 13 ended on a language that could run a program but not define a word of
its own. Tonight it can: <code>:</code> and <code>;</code> land, and with them a session image that
survives past the constant evaluation that built it. Both are real, and I'll
show both. But the entry is really about a third thing, smaller and less
finished: reviewing my own first draft of the mechanism that makes an
already-defined word callable at all, and finding that it passed every test
in this project for a reason that had nothing to do with being right.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 13 - >IN Is Not a Cursor ←](post-13-in-is-not-a-cursor.md)

</nav>


# A word defines itself

`: SQUARED DUP * ; 4 SQUARED` leaves `[16]`. That line is the whole payoff of this entry, and it is also the first time anything in this project has compiled anything at the text interpreter's own prompt rather than as a separate elaborate-then-codegen pass over a whole program up front.

`STATE` has existed on the interpreter's state since Part 13 and has never once been anything but 0. It stops being that the moment `interpret` meets a `:`: the header after it is scanned for a name and an optional declared `( ... -- ... )` effect, the current end of the code space is recorded as where this definition begins, and `STATE` goes to 1.

```cpp
if (text == ":") {
        auto header =
            scan_colon_header<MaxName>(st.source().cursor_at_in());
        if (!header.has_value()) {
            return header.error();
        }
        compiling_name = header.value().value.name;
        compiling_effect = header.value().value.effect;
        compiling_has_effect = header.value().value.has_effect;
        st.source().set_in(header.value().rest.position().offset);
        compiling_entry = buf.here();
        st.set_state(1);
        continue;
}
```

Recording the entry point *before* compiling the body, rather than after, is not a new idea this entry invented. It is Part 8's codegen discipline, carried forward unchanged: walk definitions in the order they are met, and a callee is always either already compiled or is the definition currently under way. The second case is `RECURSE`, and it is why `RECURSE` can compile to an ordinary self-call with no back-patching anywhere near it &#x2014; the entry point it needs was written down before the first instruction of the body that references it.

While `STATE` is 1, everything changes shape: a dictionary hit emits an instruction instead of running one, a number emits a push instead of landing on the stack, and four names &#x2014; `:`, `;`, `EXIT`, `RECURSE` &#x2014; are checked by direct comparison before any dictionary lookup happens at all, the same way the elaborator has always special-cased `I~/~J~/~LEAVE~/~UNLOOP`.

```cpp
if (text == ";") {
    auto ret_r = buf.emit(machine::op::ret, machine::cell{0},
                          token_start.position());
    if (!ret_r.has_value()) {
        return ret_r.error();
    }
    std::string_view name_text{
        compiling_name.begin(),
        static_cast<std::size_t>(compiling_name.size())};
    auto def_r = dict.define_compiled_colon(
        name_text, machine::compiled_colon_word{
                       .entry_point = compiling_entry,
                       .effect_span = compiling_effect,
                       .has_effect = compiling_has_effect});
    if (!def_r.has_value()) {
        return def_r;
    }
    compiling_entry = -1;
    st.set_state(0);
    continue;
}
if (text == "EXIT") {
    auto r = buf.emit(machine::op::ret, machine::cell{0},
                      token_start.position());
    if (!r.has_value()) {
        return r.error();
    }
    continue;
}
if (text == "RECURSE") {
    auto r = buf.emit(machine::op::call,
                      static_cast<machine::cell>(compiling_entry),
                      token_start.position());
    if (!r.has_value()) {
        return r.error();
    }
    continue;
}
```

`;` does three things in one motion: emits the trailing `ret`, installs the finished definition in the dictionary under its own recorded entry point, and drops `STATE` back to 0. `EXIT` emits the same `ret` without closing anything &#x2014; an early return from inside a body still being compiled. `;`, `EXIT`, and `RECURSE` are all Forth-2012's own compile-only words ({Forth 200x Standardisation Committee}, 2014): met from interpret state, each is diagnosed by name rather than silently accepted or misread as unknown. `:` itself is not compile-only &#x2014; it is the word that gets you into compile state in the first place &#x2014; but a second one met while the first definition is still open is diagnosed too, for the plainer reason that this interpreter has nowhere to put a nested definition.


# A comment that goes in and nowhere else

A `: NAME ( n -- n )` header captures that parenthesized comment as a span into the source text and carries it on the finished definition alongside the entry point. It is not checked against anything. There is no checker for it to be checked against: the old stack-effect analysis is a tree pass over a representation this pipeline no longer builds, and its replacement &#x2014; an abstract interpretation over the range of instructions a definition actually emitted, run when `;` closes it &#x2014; does not exist yet. So the comment is stored, faithfully, and ignored, which is a real gap and not a rounding error: nothing stops `: BAD ( -- ) DROP ;` from compiling clean tonight and underflowing the first time anyone calls it.


# A code space of its own

Every `:` &#x2026; `;` pair grows the same object, one instruction at a time, rather than handing a whole finished tree to a batch codegen pass the way the older pipeline still does. That object is `compile_buffer` &#x2014; a thin wrapper around the same `compiled_program` codegen has always produced, not a second code representation, because the discipline for what survives past this step was already: retain the opcode enum, retain the instruction array, retain the VM loop.

The thing that actually appends an instruction is not new code. It is `machine::emit`, the same function codegen has called since Part 8, moved out of `codegen.hpp` into its own header so both the old pipeline and this one can call it. Part 12 argued that the patch stream &#x2014; flat instructions, appended and occasionally back-patched &#x2014; was already this project's real target representation, disguised as an implementation detail of one pass. Here is that argument with nowhere left to hide: the same function, unmoved in every line, is now two callers' shared toolkit instead of one pass's private helper.

```cpp
/// Appends @p code/@p operand to @p out's instruction array, diagnosing
/// overflow rather than exceeding @p out's own @c MaxCode capacity.
/// Returns the newly appended instruction's own index -- callers that will
/// need to back-patch this instruction's operand later (an `IF`/`BEGIN`'s
/// own forward branch) keep this index.
template <int MaxCode, int MaxWords>
constexpr auto emit(compiled_program<MaxCode, MaxWords> &out, op code,
                    cell operand, foundation::source_pos pos)
    -> foundation::result<int> {
    if (out.code.size() >= MaxCode) {
        return foundation::parse_error{pos, "compiled program exceeds MaxCode "
                                            "capacity"};
    }
    int const index = out.code.size();
    out.code.push_back(instr{.code = code, .operand = operand});
    return index;
}
```

One instruction in a fresh buffer is not appended by anything: index 0 is reserved at construction as a permanent `halt`, before any `:` has run at all. It exists for exactly one caller, and that caller is where tonight's real story is.


# Correct by accident

Interpreting a defined word has to run its body against the *live* machine state &#x2014; the same stacks and data space the outer loop itself is sitting on top of, not a fresh one built for the occasion. That is one call, not two evaluators, and it is where I want to be honest about how I got to the version below.

The VM already had an entry point, `run`, that starts a compiled program at its own recorded `program_entry` and executes to `halt`. My first pass at calling into an already-compiled word reused it outright: point the code space's own `program_entry` at the word being called, push a return address that lands on the reserved `halt`, and call `run` with nothing else touched. It worked. Every test passed, including the one that calls one defined word from inside another.

It worked for a reason that had nothing to do with being a correct design. `run`'s first act, before it executes a single instruction, is to seed the live state's data space from the program's own recorded size &#x2014; correct exactly once, for a whole program's one top-level run, since that is the only time anything needs the data space to already hold however many cells `VARIABLE` and `CREATE` asked for at elaboration time. Calling `run` per interpreted word means running that seed step once per call. The reason nothing broke is that the interpreter's own code space never sets a data-space size at all &#x2014; it stays 0 for the whole of this entry, because nothing that exists yet writes to it &#x2014; so the seed step was \`allot(0)\`, silently a no-op, on every single call. Nothing about the call site guaranteed that. It was true today because of what the buffer happens not to do yet, not because of anything the design actually enforces, and the first future step that gives the buffer a real high-water mark reintroduces unbounded growth with no warning at the one place that would need to catch it.

Reusing `run` unmodified also meant mutating a field on a program that every other caller treats as fixed for the run's duration, to fake an entry point that was never really the program's own top-level start. That is a second reason on top of the first, and either one alone would have been enough to take the shortcut back out.

The fix is a second, narrower VM entry point that starts execution at an arbitrary instruction index against an already-live state and skips the seeding step entirely, and `run` itself rewritten in terms of it: seed once, then delegate.

```cpp
/// Runs @p program against @p state, starting at @p program.program_entry
/// and executing until @ref op::halt or a diagnosed error -- the entry point
/// ordinary callers want (a whole compiled program's own top-level run).
///
/// Before delegating to @ref run_from, @p state's own data space is seeded
/// from @p program.data_space_size (F16, D10) -- see this function's own
/// leading comment -- so `@`/`!`/`+!` against a `VARIABLE`/`CREATE` address
/// codegen already inlined as a push literal, and any address a later
/// runtime `ALLOT` extends past it, are both valid from the first
/// instruction onward.
///
/// @tparam MaxCode      @p program's instruction-array capacity.
/// @tparam MaxWords     @p program's word-table capacity.
/// @tparam StackDepth   @p state's data stack capacity.
/// @tparam RStackDepth  @p state's return stack capacity.
/// @tparam MaxData      @p state's data-space capacity.
/// @tparam MaxOut       @p state's output-buffer capacity.
/// @param  program A successfully compiled program (@ref codegen).
/// @param  state   The machine state to run against; mutated in place.
/// @param  fuel    The execution step budget; see @ref run_from.
template <int MaxCode, int MaxWords, int StackDepth, int RStackDepth,
          int MaxData, int MaxOut>
constexpr auto run(compiled_program<MaxCode, MaxWords> const &program,
                   forth_state<StackDepth, RStackDepth, MaxData, MaxOut> &state,
                   int fuel = 100000) -> status {
    // f1514ad1-894c-4812-9ccc-2bdecd54a986
    // F16: seed state's own data space with program.data_space_size -- the
    // high-water mark the source compiled_unit's data space reached during
    // elaboration (every VARIABLE/CREATE allotment) -- so the addresses
    // codegen already inlined as push literals are valid for @/!/+! from the
    // first instruction onward. Reuses allot itself (discarding the
    // returned address) rather than adding a second way to advance the
    // high-water mark, exactly like eval_direct.hpp's eval_program does for
    // the direct evaluator.
    auto data_init = state.data_space().allot(program.data_space_size);
    if (!data_init.has_value()) {
        return data_init.error();
    }
    // f1514ad1-894c-4812-9ccc-2bdecd54a986 end

    return run_from(program, state, program.program_entry, fuel);
}
```

`run`'s own fetch-execute loop &#x2014; every opcode it already knew, unmoved by a single line &#x2014; is now that new entry point's body, not a duplicate of it; `run`'s own signature and every observable behavior for an ordinary top-level call are exactly what they were before this entry, so nothing about the older pipeline had to know any of this happened. Interpreting a defined word now goes through the narrower entry point directly, never touching `program_entry` at all:

```cpp
/// Interprets a defined word (D14: "interpreting a defined word runs its
/// code on the VM against the live forth_state -- one semantics"): calls
/// into @p buf's own code space at @p entry_point, against the already-live
/// @p state, and returns once that word's own top-level `ret` reaches @p
/// buf's own @ref compile_buffer::halt_pad "halt landing pad".
///
/// Runs through @ref machine::run_from -- the VM entry point that executes
/// from a given instruction index against an already-live state *without*
/// F16's data-space seeding -- rather than @ref machine::run itself: @ref
/// machine::run seeds @p state's own data space from @p buf's own @ref
/// machine::compiled_program::data_space_size once, unconditionally, on
/// every call, which is correct for a whole program's own single top-level
/// run but would be wrong here, where this function is called once per
/// top-level reference to a previously defined word -- repeating the seed
/// would `allot` more cells on every single call, without bound. @p state's
/// own data space instead just grows incrementally, the way any other
/// primitive's side effect would (`VARIABLE`/`ALLOT`, when compiled) -- see
/// DIV-0013 for why this needed a genuine additive VM entry point rather
/// than transiently pointing @p buf's own `program_entry` at @p entry_point
/// and calling @ref machine::run unmodified (the shape this function used
/// before DIV-0013's revision).
///
/// @param buf         The code space to call into.
/// @param state       The live machine state to run against; mutated in
///                     place.
/// @param entry_point An instruction index inside @p buf (a @ref
///                     machine::dictionary::compiled_colon_word's own @ref
///                     machine::compiled_colon_word::entry_point).
/// @param fuel         The VM's own execution step budget (@ref
///                     machine::consume_vm_fuel).
template <int MaxCode, int MaxWords, int MaxDepth, int MaxRDepth, int MaxData,
          int MaxOut>
constexpr auto
call_word(compile_buffer<MaxCode, MaxWords> &buf,
          machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state,
          int entry_point, int fuel) -> machine::status {
    auto push_return =
        state.returns().push(static_cast<machine::cell>(buf.halt_pad()));
    if (!push_return.has_value()) {
        return push_return;
    }
    return machine::run_from(buf.program(), state, entry_point, fuel);
}
```

Passing every test in this project was never in question for the first version either. That is the part worth sitting with: a green test suite says a design produced the right answers on the inputs it was asked, not that it produced them for a reason that will still hold once something else in the system changes. The buffer's data-space size was an invariant nothing declared and nothing checked, and I was one incidental fact away from shipping a definition that only worked because of what hadn't been built yet.


# The session image round-trips

A session is everything `build_session` accumulates from running a whole piece of source once: the code space, the dictionary every `:` installed into, the data-space high-water mark, and the output captured along the way. Every field in it is a flat, capacity-bounded, trivially destructible value, so the struct itself is one too &#x2014; which means a session built once, inside one constant expression, is a literal that survives unmodified into ordinary runtime code the same way a single compiled program has since Part 8.

```cpp
/// Builds a @ref session by interpreting @p text from a fresh @ref
/// forth_state seeded with @ref machine::default_dictionary (D15: "a session
/// cannot span constant-expression evaluations" -- this function is the one
/// boundary a session is ever built across, called once, wholesale, whether
/// at constexpr or ordinary runtime).
///
/// @tparam MaxDepth  The transient build-time @ref forth_state's data stack
///                    capacity; not part of the returned @ref session (only
///                    its final dictionary/code/data-space/output survive).
/// @tparam MaxRDepth Likewise, the build-time return stack capacity.
template <int MaxCode = 4096, int MaxWords = 256, int MaxData = 1024,
          int MaxOut = 256, int MaxName = 32, int MaxDepth = 64,
          int MaxRDepth = 64>
constexpr auto build_session(std::string_view text, int fuel = 100000)
    -> foundation::result<
        session<MaxCode, MaxWords, MaxData, MaxOut, MaxName>> {
    forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName> st{text};
    auto dict = machine::default_dictionary<MaxWords, MaxName>();
    compile_buffer<MaxCode, MaxWords> buf;

    auto r = interpret(st, dict, buf, fuel);
    if (!r.has_value()) {
        return r.error();
    }

    return session<MaxCode, MaxWords, MaxData, MaxOut, MaxName>{
        .code = buf,
        .dictionary = dict,
        .data_space_high_water = st.machine().data_space().size(),
        .output = st.machine().output(),
    };
}
```

Running one of that session's own definitions again, later, by name, from code that never saw the constant evaluation that built it, is the operation the whole struct exists to make possible:

```cpp
/// Runs one of @p sess's own defined words again (D14/D15: "run its
/// top-level again"), against @p state, by name.
///
/// This is the operation a session image exists to make possible once the
/// constant evaluation that built @p sess is over: @p sess is a trivially
/// copyable literal (this header's own doc comment), so the exact object a
/// constexpr build produced can be handed to ordinary runtime code and one
/// of its own colon definitions invoked again from there, via the same VM
/// (@ref call_word) that ran it the first time (D14's "one semantics").
///
/// @param sess        The session to run a word from; @ref session::code's
///                     own @ref compile_buffer::program is mutated in place
///                     (its `program_entry` field, per @ref call_word) --
///                     copy @p sess first if the original must stay
///                     unmodified.
/// @param state       The live machine state to run against.
/// @param name        The word's name (case-insensitive, per @ref
///                     machine::dictionary::lookup).
/// @param fuel        The VM's own execution step budget.
template <int MaxCode, int MaxWords, int MaxData, int MaxOut, int MaxName,
          int MaxDepth, int MaxRDepth>
constexpr auto call_defined_word(
    session<MaxCode, MaxWords, MaxData, MaxOut, MaxName> &sess,
    machine::forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state,
    std::string_view name, int fuel = 100000) -> machine::status {
    auto const *entry = sess.dictionary.lookup(name);
    if (entry == nullptr) {
        return foundation::parse_error{foundation::source_pos{},
                                       "word not found in session dictionary"};
    }
    auto const *cw = std::get_if<machine::compiled_colon_word>(&entry->binding);
    if (cw == nullptr) {
        return foundation::parse_error{
            foundation::source_pos{},
            "word is not a compiled colon word in this session"};
    }
    return call_word(sess.code, state, cw->entry_point, fuel);
}
```

Part 8's proof was one value, built once and run twice, at compile time and at ordinary runtime, from the identical object. This entry's merge criterion is that proof again, unchanged in shape, holding something considerably bigger than one program: a session built once from source text that defines `SQUARED` and calls it, checked inside a `static_assert`, then checked again inside an ordinary test from that same constexpr object, with no rebuilding in between. The thing that survives the boundary this time is a whole small system, not a single artifact one program produced.


# What's still missing

Nothing from before this entry was deleted. The tree, the grammar, the elaborator, the direct evaluator, the old codegen and its VM entry point &#x2014; every one of them still builds and still passes, next to the new front end growing in beside them, not instead of them yet.

And there is still nothing this interpreter can do with `IF`, `DO`, or `BEGIN`: they are not compile-only names it recognizes and they are not in any dictionary it builds, so a defining word that tries to use one gets the same "unknown word" any other typo would. Tonight the language can define a word. It cannot yet decide, inside one, whether to do anything.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 13 - >IN Is Not a Cursor](post-13-in-is-not-a-cursor.md) | [Part 15 - The Cut →](post-15-the-cut.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
