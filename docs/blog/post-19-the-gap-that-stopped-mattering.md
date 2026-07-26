<div class="abstract" id="org7fb335d">
<p>
<code>CATCH</code>, <code>THROW</code>, and <code>ABORT</code> land tonight, and <code>ABORT"</code> stops being the hard
stop it's been since it was written: it's a real <code>THROW -2</code> now, exactly as
Forth-2012 says. The interesting part isn't the new words, though. It's what
<code>THROW</code> had to do to unwind a return stack that can hold, at any moment, an
arbitrary mix of call frames, loop frames, and whatever a program's own <code>&gt;R</code>
put there and hasn't taken back. Eight entries ago I admitted I'd never
checked that a loop's own teardown finds the right cells. Tonight's <code>THROW</code>
doesn't check either &#x2014; and that turns out to be why it's correct.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 18 - What >IN Was For ←](post-18-what-in-was-for.md)

</nav>


# Two things left open

Part 11 ended on a confession I've been carrying since: `LEAVE`, `UNLOOP`, and a loop's own ordinary fall-through all tear down a two-cell frame off the top of the return stack, and none of them checks that the frame actually sitting there is theirs. It's true for every program I'd written, because the whole-body balance check the effect checker already ran guarantees it. It wasn't true by construction, and I said so rather than pretend the two came to the same thing.

Part 18 ended on a smaller, more recent debt: `ABORT"` printed its message and stopped the whole interpreter cold, standing in for the unwind Forth-2012 actually specifies, because there was no `THROW` yet and no handler for one to reach. I called it a placeholder with a name, not a corner I'd forgotten. Both debts come due tonight, and they come due against the same mechanism.


# What THROW has to get through

By the time a `THROW` actually runs, one return stack can be carrying, interleaved, in whatever order the program built them: a return address for every call still in progress, a two-cell `limit~/~index` frame for every `DO` loop still open, any number of loose cells a program pushed with `>R` and hasn't popped back yet, and now a handler frame for every `CATCH` still active. `THROW` has to reach the innermost handler through all of it, in one motion, from inside a definition that has no idea how deep it's nested or what any of its callers left lying around.

There are two ways I could see to do that. Tag every cell with what kind of frame it belongs to, and have `THROW` walk down popping by kind until it finds a handler tag. Or leave everything untagged, as it's been since `>R` was added, and prove &#x2014; by exhaustive testing, since there's still no stack-effect checker to prove it any other way &#x2014; that the layout is always what the code assumes. Tagging means widening every one of `call`'s, `DO`'s, and `>R`'s own pushes to carry a discriminant nobody currently needs. Exhaustive testing means trusting a battery of cases to stand in for a proof that doesn't exist.

I did neither.


# A depth, not a scan

`forth_state` gains one more scalar register alongside `BASE` and `STATE`: `handler_depth`, the return-stack depth at which the innermost active handler's own frame begins, or `-1` if nothing is catching anything right now. `CATCH` sets it when a handler frame goes up. `THROW` reads it directly and truncates the return stack to that recorded depth in a single call &#x2014; discarding everything above it without ever asking what any of those cells were.

```cpp
/// Unwinds to the innermost active `CATCH` handler and pushes @p n, or
/// diagnoses an uncaught `THROW` carrying @p n if `state.handler_depth()` is
/// `-1` (no handler active) -- the one place both @ref op::throw_op and the
/// `ABORT"`/machine-fault-mapping paths in @ref run_from's own `op::prim`
/// case perform the actual unwind, so both stay in exact agreement.
///
/// @p n must be nonzero: @ref op::throw_op's own `n == 0` no-op case (per
/// Forth-2012) is handled by its caller, before this function is ever
/// called.
///
/// On success, mutates @p ip to the resumed instruction index; @p state's
/// data and return stacks, and its own handler-depth register, are restored
/// to exactly what they were when the matching `CATCH` began running its own
/// execution token.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
constexpr auto
perform_throw(forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &state, cell n,
              int &ip) -> status {
    int const handler = state.handler_depth();
    if (handler < 0) {
        // Uncaught: foundation::parse_error::message must stay a static
        // string literal (it does not own or copy what it points to, per
        // its own doc comment -- the exact wall DIV-0017's own F29 finding
        // named), so the thrown code cannot ride along in the message text.
        // where.offset carries it instead: a deliberate, documented reuse of
        // a field that already means nothing for a condition with no real
        // source position of its own.
        return foundation::parse_error{
            foundation::source_pos{.offset = static_cast<int>(n)},
            "uncaught THROW (code in foundation::parse_error::where.offset)"};
    }
    auto tr = state.returns().truncate(handler + 3);
    if (!tr.has_value()) {
        return tr;
    }
    auto resume_ip = state.returns().pop();
    if (!resume_ip.has_value()) {
        return resume_ip.error();
    }
    auto saved_data_depth = state.returns().pop();
    if (!saved_data_depth.has_value()) {
        return saved_data_depth.error();
    }
    auto prev_handler = state.returns().pop();
    if (!prev_handler.has_value()) {
        return prev_handler.error();
    }
    auto td = state.data().truncate(static_cast<int>(saved_data_depth.value()));
    if (!td.has_value()) {
        return td;
    }
    auto push_r = state.data().push(n);
    if (!push_r.has_value()) {
        return push_r;
    }
    state.set_handler_depth(static_cast<int>(prev_handler.value()));
    ip = static_cast<int>(resume_ip.value());
    return std::monostate{};
}
```

That's the whole answer to Part 11's gap, and I want to sit with it for a moment, because it isn't the answer I expected to write. I didn't verify that a loop's own teardown always finds its own frame at the top of the stack. I made it irrelevant to this code path instead. Whatever mix of call frames, loop frames, and `>R` leftovers a caught execution built between `CATCH` and the `THROW` that unwinds past it, they are all sitting above the handler's own recorded depth, and after one `truncate` call they are simply gone &#x2014; not identified, not walked, not even looked at. `THROW` doesn't need to know a loop frame is two cells instead of one, because it never asks the return stack what shape anything on it is. It only asks how deep the handler wants it to be.

Restoring the previous state is the other half of the same frame. A handler's three cells &#x2014; the previous handler depth, the data-stack depth recorded when `CATCH` began, and where execution resumes &#x2014; get popped after the truncate, in that order, and the data stack gets truncated to its own recorded depth too, so a caught `THROW` leaves the stack precisely where it was before the guarded call started, plus the thrown code on top. Nesting falls out of that same first cell: each handler frame remembers the depth of the handler outside it, so restoring it after a catch always relinks to the next one out, however many are stacked up.

If nothing is catching &#x2014; `handler_depth` still `-1` &#x2014; there's nowhere to truncate to, and the `THROW` is diagnosed instead: an uncaught exception, same as any other error this project reports. The thrown code itself has nowhere obvious to live in that diagnosis, since the message has to stay a plain string literal; it rides in the source-position field instead, which otherwise means nothing for a condition that never had a real position of its own. A borrowed field, not a new one &#x2014; there's already too much machinery in this project's error type to be adding to it for one case.


# Two instructions, no patch list

`CATCH` itself compiles to exactly two instructions: one that pops the execution token and pushes the handler frame, and one that runs only if the call inside completes normally.

```cpp
case op::catch_mark: {
    // `CATCH` (step F31, D11/D18): xt CATCH ( xt -- 0 | n ). Pops
    // the execution token, pushes a 3-cell handler frame [prev
    // handler-depth, saved data-stack depth, resume ip] onto the
    // return stack (`in.operand` is the resume ip -- either
    // interp.hpp's own compiled or interpreting CATCH case computes
    // it, see each one's own comment), points handler_depth() at
    // this frame's own base, then pushes this instruction's own
    // return address (`ip + 1`, where `prim primitive::catch_ok` --
    // the normal-completion epilogue -- always sits, emitted right
    // after this instruction by both of interp.hpp's own CATCH
    // cases) and jumps to the token, exactly like @ref op::execute.
    // See this header's own top comment (by perform_throw) for how
    // this composes with @ref op::throw_op to unwind correctly
    // through an arbitrary mix of call/DO-loop/`>R`/handler frames
    // sharing this same return stack.
    auto xt = state.data().pop();
    if (!xt.has_value()) {
        return xt.error();
    }
    int const frame_base = state.returns().depth();
    int const saved_data_depth = state.data().depth();
    int const prev_handler = state.handler_depth();
    if (auto r = state.returns().push(static_cast<cell>(prev_handler));
        !r.has_value()) {
        return r;
    }
    if (auto r =
            state.returns().push(static_cast<cell>(saved_data_depth));
        !r.has_value()) {
        return r;
    }
    if (auto r = state.returns().push(in.operand); !r.has_value()) {
        return r;
    }
    state.set_handler_depth(frame_base);
    if (auto r = state.returns().push(static_cast<cell>(ip + 1));
        !r.has_value()) {
        return r;
    }
    ip = static_cast<int>(xt.value());
    break;
}
case op::throw_op: {
    // `THROW` (step F31, D11): n THROW ( n -- ). Zero is a no-op
    // per Forth-2012; otherwise unwind to the innermost active
    // handler (@ref perform_throw), or diagnose an uncaught THROW
    // carrying n.
    auto n = state.data().pop();
    if (!n.has_value()) {
        return n.error();
    }
    if (n.value() == 0) {
        ++ip;
        break;
    }
    auto r = perform_throw(state, n.value(), ip);
    if (!r.has_value()) {
        return r;
    }
    break;
}
```

That second instruction is a primitive, not a third opcode alongside the two above &#x2014; it doesn't move the instruction pointer anywhere, it just pops the frame, restores the register, and pushes `0`, so the existing dispatch for an ordinary primitive already does the whole job:

```cpp
// Step F31 (docs/forth-plan-2.md), D11/D18: CATCH's own "normal
// completion" epilogue. See vm.hpp's own op::catch_mark for the other
// half of CATCH's compiled shape (it cannot itself be a primitive: it
// must jump to the popped execution token, which apply_primitive has no
// way to do).
catch_ok, ///< ( -- 0 ) Pops CATCH's own 3-cell handler frame off the
              ///< return stack (prev-handler, saved data-stack depth,
              ///< resume ip -- the last two unused on this, the *normal*
              ///< completion path; see vm.hpp's own perform_throw for the
              ///< path that does use them), restores the handler-depth
              ///< register from the frame's own prev-handler field, and
              ///< pushes 0 -- CATCH's own "no exception" result. Reached
              ///< only as the return address CATCH's own op::catch_mark
              ///< pushed before jumping to the caught execution token, once
              ///< that token's own body returns normally (never reached at
              ///< all if a THROW inside it unwound past this frame instead).
```

What I like about this shape, and didn't expect going in, is that neither instruction needs a patch. `IF` has to emit a branch before it knows where the else-branch or the `THEN` actually land, and comes back later to fix the operand once codegen has walked far enough to know. `CATCH` never has that problem, because both of its own instructions have fixed length and the resume point &#x2014; where execution should be, either on normal completion or on a caught throw &#x2014; is exactly one past the second instruction, known the instant the first one is emitted. There's nothing to come back and fix.


# Faults join the mapping, and ABORT" stops pretending

A handful of machine faults &#x2014; stack overflow, stack underflow, division by zero, an out-of-bounds data-space address &#x2014; now get mapped to their standard Forth-2012 codes and become catchable, but only once a handler is actually active. With nothing catching, every one of those diagnoses keeps the message it always had, unchanged; nothing about an uncaught fault looks any different tonight than it did last entry. The mapping is a small table matched against each fault's own static message text, consulted only after confirming a handler exists, which is a cheaper and more contained change than giving every diagnosis in this project a fault-code field of its own for the sake of a handful of catchable cases.

`ABORT` is the plainest word in the whole entry: it compiles to a literal `-1` followed by `THROW`, because that is what Forth-2012 says `ABORT` is, no dedicated opcode required. `ABORT"` is where Part 18's placeholder actually closes. Its runtime primitive still prints the message just as before; what changes is what happens once it's printed. Instead of a hard stop, it fails with a distinguished condition that the machine always routes through `THROW -2` &#x2014; caught by an enclosing `CATCH` if one is open, or an uncaught diagnosed throw carrying `-2` if not. Unlike the general fault mapping above, this one isn't conditional on a handler being active, because `ABORT"` doesn't have an "uncaught but still generic" reading the way a stack underflow does. Forth-2012 defines it as `THROW -2`, full stop, whether or not anything is listening ({Forth 200x Standardisation Committee}, 2014).


# Everything at once

The test I actually cared about is the one that stops treating `>R`, `DO`, and `CATCH` as if they lived on separate stacks:

```cpp
TEST_CASE("InterpTest - CatchUnwindsThroughToRDoLoopAndCallFrames") {
    forth_state<64, 64, 1024, 256> st{
        ": DEEP 42 >R 10 0 DO 5 THROW LOOP R> DROP ; "
        ": GUARD ['] DEEP CATCH DUP IF .\" CAUGHT \" . ELSE DROP .\" OK \" "
        "THEN ; "
        ": FIND5B 10 0 DO I 5 = IF I LEAVE THEN LOOP ; "
        "GUARD FIND5B"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    CHECK(output_of(st) == "CAUGHT 5 ");
    REQUIRE(st.data().depth() == 1);
    CHECK(st.data().peek().value() == 5);
    // The return stack is exactly back to where it started: no leftover
    // cells from the >R, the DO-loop frame, or CATCH's own handler frame.
    CHECK(st.returns().depth() == 0);
}
```

`DEEP` pushes a loose cell with `>R`, opens a counted loop, and throws from inside it. `GUARD` catches whatever comes back. By the time `THROW` fires, the return stack underneath it holds `GUARD`'s own call frame, the loop's two-cell frame, and the `>R`'d `42`, all above the handler frame `CATCH` set up &#x2014; three different shapes of cell, stacked in the order the program happened to build them. One `truncate` call throws all three away at once, and `FIND5B` running an entirely ordinary `DO~/~LEAVE` loop right afterward is there to show that doing so didn't leave anything shifted, only gone. The return stack comes back to depth zero.


# What's still assumed

Here's the part I don't want to undersell by going quiet about it. Nothing in this entry touched `LOOP`, `+LOOP`, or `UNLOOP`'s own teardown. All three still assume, exactly as they did in Part 11, that their own two-cell frame is sitting at the very top of the return stack the moment they go to discard it. `THROW` doesn't need that assumption anymore, because it never looks at what it's discarding. A loop's own ordinary close still does look, in the sense that it still just pops two cells and trusts they're the right two.

So a definition like this one (shape, not verbatim code I've run):

```forth
: BAD 10 0 DO 5 >R LOOP ;
```

&#x2014; an unbalanced `>R` inside a loop, never taken back before `LOOP` closes &#x2014; still tears down the wrong two cells, silently, rather than complaining about anything. Forth-2012 already requires the return stack to be balanced before `LOOP` runs, so this is a broken program, not a broken system; the gap isn't that misuse is legal, it's that nothing in this project says so when it happens. That's a narrower gap than the one Part 11 recorded &#x2014; the one path that actually had to reach through an arbitrary mix of frames now does, correctly, by construction &#x2014; but it is not the same thing as closed, and I'm not writing it as if it were.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 18 - What >IN Was For](post-18-what-in-was-for.md) | [Part 20 - Just Edges →](post-20-just-edges.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
