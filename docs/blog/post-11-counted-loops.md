<div class="abstract" id="org7b4527a">
<p>
<code>DO LOOP +LOOP I J LEAVE UNLOOP</code> are real now, through both backends, with a
counted loop's parameters living in a two-cell frame on the return stack. The
evaluator and the compiled machine came together the way the return-stack
discipline said they would. What did not come together on the first try was
the static stack-effect checker, which turns out to have been quietly wrong
about <code>DO...LOOP</code> for two entries running, in a way no single top-level loop
could ever expose. Nesting one counted loop inside another is what finally
asked the question the checker couldn't answer.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 10 - The Address Was Always a Cell ←](post-10-memory-words.md)

</nav>


# A loop that finally counts

`DO` &#x2026; `LOOP` has sat in the grammar and the elaborated core for a while now, diagnosed rather than run: the parser accepted it, the tree held a real `core_do_loop` node for it, and both backends refused to touch it. This entry is where that stops being true. `DO`, `LOOP`, `+LOOP`, `I`, `J`, `LEAVE`, and `UNLOOP` all work now, in the direct evaluator and in the compiled stack machine, and they agree with each other on every program I threw at them.

The mechanism is the return stack, which has been sitting there since the machine itself was built, mostly used for call frames and for whatever `>R` and `R>` put on it by hand. A counted loop's parameters are just another kind of frame on the same stack: `DO` pops `limit` and `start` off the data stack and pushes them onto the return stack as `(limit index)`, index on top. `I` reads that top cell without disturbing it; `J`, one loop out, reads two cells further down. The frame sits directly above whatever call frame is already there, and it has to be gone &#x2014; popped back off &#x2014; before that call's own `ret` runs, or the return address underneath it is not what comes back.

```cpp
} else if constexpr (std::is_same_v<T,
                                        elaborator::core_loop_index>) {
        // The loop frame DO established on the return stack is (limit
        // index) per loop, index on top; a level-L index sits at
        // return-stack offset 2*L (I=0, J=2). Reading it does not
        // disturb the return stack.
        auto v = state.returns().peek(2 * alt.level);
        if (!v.has_value()) {
            return v.error();
        }
        auto r = state.data().push(v.value());
        if (!r.has_value()) {
            return r.error();
        }
        return eval_signal::normal;
} else if constexpr (std::is_same_v<T, elaborator::core_leave>) {
        return eval_signal::left;
} else if constexpr (std::is_same_v<T, elaborator::core_unloop>) {
        // Discard the innermost loop frame (index then limit) without
        // branching, leaving the return stack as it was before DO.
        auto index = state.returns().pop();
        if (!index.has_value()) {
            return index.error();
        }
        auto limit = state.returns().pop();
        if (!limit.has_value()) {
            return limit.error();
        }
        return eval_signal::normal;
```

`I` and `J` and `LEAVE` and `UNLOOP` are not primitives. There was a temptation to make them entries in `machine::primitive` the way `+` or `DUP` are, since that machinery already exists and works. I didn't, for the same reason `EXIT` and `RECURSE` never became primitives back when they were added: none of the four read an operand off the data stack the way an ordinary word does, and none of them names something a program could ever look up by a different spelling. They're recognized directly in word resolution, before any dictionary lookup happens at all, right where `EXIT` and `RECURSE` already were. A `core_loop_index` node carries which loop it means as a plain field (0 for `I`, 1 for `J`), which is a cheaper and more honest way to say "the innermost enclosing loop, or the one outside that" than pretending it's a word with an address.

The loop body itself runs the ordinary way &#x2014; `eval_body` over whatever's inside `DO ... LOOP` &#x2014; and then the frame's index gets advanced and checked:

```cpp
} else if constexpr (std::is_same_v<T, elaborator::core_do_loop<
                                               MaxNodes, MaxBody>>) {
        // `limit start DO ... LOOP/+LOOP` (F17). The loop parameters
        // live on the return stack as the pair (limit index), index
        // on top, so I/J read them by offset and they nest correctly
        // with an outer loop's own frame. `start` is the top data
        // cell, `limit` the one below it.
        auto start = state.data().pop();
        if (!start.has_value()) {
            return start.error();
        }
        auto limit = state.data().pop();
        if (!limit.has_value()) {
            return limit.error();
        }
        if (auto r = state.returns().push(limit.value());
            !r.has_value()) {
            return r.error();
        }
        if (auto r = state.returns().push(start.value());
            !r.has_value()) {
            return r.error();
        }
        for (;;) {
            auto step = consume_fuel(fuel, alt.pos);
            if (!step.has_value()) {
                return step.error();
            }
            auto body_r = eval_body(unit, alt.body, state, fuel);
            if (!body_r.has_value()) {
                return body_r.error();
            }
            if (body_r.value() == eval_signal::exited) {
                // A well-formed early EXIT out of a DO loop is preceded
                // by UNLOOP (diagnosis 4), which already tore down the
                // frame; just keep unwinding to the enclosing call.
                return eval_signal::exited;
            }
            if (body_r.value() == eval_signal::left) {
                // LEAVE: discard this loop's frame and fall out past
                // the loop.
                if (auto r = state.returns().pop(); !r.has_value()) {
                    return r.error();
                }
                if (auto r = state.returns().pop(); !r.has_value()) {
                    return r.error();
                }
                return eval_signal::normal;
            }
            // Advance the loop index (top of the return stack).
            auto index = state.returns().pop();
            if (!index.has_value()) {
                return index.error();
            }
            auto lim = state.returns().peek(0);
            if (!lim.has_value()) {
                return lim.error();
            }
            bool terminate = false;
            cell next{};
            if (alt.is_plus_loop) {
                // `+LOOP`: add the increment the body left on the data
                // stack; terminate when the index crosses the boundary
                // between limit-1 and limit (Forth-2012), i.e. when the
                // sign of (index - limit) flips.
                auto incr = state.data().pop();
                if (!incr.has_value()) {
                    return incr.error();
                }
                cell const before = index.value() - lim.value();
                next = index.value() + incr.value();
                cell const after = next - lim.value();
                terminate = (before ^ after) < 0;
            } else {
                // `LOOP`: add one; terminate when the index reaches the
                // limit (Forth-2012 equality test).
                next = index.value() + 1;
                terminate = next == lim.value();
            }
            if (terminate) {
                if (auto r = state.returns().pop(); !r.has_value()) {
                    return r.error();
                }
                return eval_signal::normal;
            }
            if (auto r = state.returns().push(next); !r.has_value()) {
                return r.error();
            }
        }
```


# Two termination rules, not one

I assumed `+LOOP` would be `LOOP` with the constant 1 replaced by whatever the body left on the stack, and for a page or two of writing I coded it that way: advance the index by the increment, compare to the limit, stop on equality. It's wrong, and it's wrong in a way that only shows up once the increment is something other than exactly 1 &#x2014; which is the entire reason `+LOOP` exists. Step by 2 toward a limit that isn't reachable by steps of 2 from wherever the loop happens to start, or count down with a negative increment, and an index-equals-limit test can walk right past the limit without the two values ever landing on the same cell in the same pass. The loop that was supposed to run five times runs forever, or wraps.

Forth-2012 doesn't test equality for `+LOOP`; it tests whether the index crossed the boundary between one cell below the limit and the limit itself ({Forth 200x Standardisation Committee}, 2014). Concretely: take `index - limit` before adding the increment and after, and check whether the sign flipped.

```cpp
case op::plus_loop_step: {
    // `+LOOP`: pop the increment; index += n; terminate when the index
    // crosses the boundary between limit-1 and limit (Forth-2012),
    // i.e. when the sign of (index - limit) flips.
    auto incr = state.data().pop();
    if (!incr.has_value()) {
        return incr.error();
    }
    auto index = state.returns().pop();
    if (!index.has_value()) {
        return index.error();
    }
    auto limit = state.returns().peek(0);
    if (!limit.has_value()) {
        return limit.error();
    }
    cell const before = index.value() - limit.value();
    cell const next = index.value() + incr.value();
    cell const after = next - limit.value();
    if (((before ^ after) < 0)) {
        if (auto r = state.returns().pop(); !r.has_value()) {
            return r.error();
        }
        ++ip;
    } else {
        if (auto r = state.returns().push(next); !r.has_value()) {
            return r;
        }
        ip = static_cast<int>(in.operand);
    }
    break;
}
```

Plain `LOOP` keeps the simple equality test, which is correct for it because its increment is always exactly 1 and an index counting up by ones cannot skip past an integer limit. `+LOOP` needed the general rule the moment it needed a variable increment at all, and there was no way to discover that except by reading the standard closely enough to notice the two words don't actually share a termination condition, only a family resemblance.


# LEAVE, and who catches it

`LEAVE` is a one-shot, forward-only exit out of the innermost enclosing counted loop &#x2014; the same dynamic-extent discipline `EXIT` already uses to leave a definition early. The direct evaluator gets a new signal for it, `eval_signal::left`, which propagates up through nested `IF` and `BEGIN` bodies the same way `eval_signal::exited` already does. The difference is what absorbs it: an `exited` signal keeps climbing until it reaches the call boundary of the definition it's leaving; a `left` signal stops at the nearest `core_do_loop`, which discards that loop's return-stack frame and falls through to whatever comes after the loop, as if the loop had simply finished.

The VM version of the same idea has to solve a harder problem, because there is no call stack of signal values to unwind through &#x2014; there's just an instruction pointer. `LEAVE` compiles to a branch, but the branch target, the instruction just past the loop, doesn't exist yet at the point the `LEAVE` itself is emitted; codegen is still walking the loop body forward. So it emits the branch with a sentinel operand, `-1`, which can never be a real instruction index, and comes back to patch it once the loop's own `loop_step` or `plus_loop_step` has been emitted and the real exit address is known:

```cpp
} else if constexpr (std::is_same_v<T, elaborator::core_do_loop<
                                               MaxNodes, MaxBody>>) {
        // `limit start DO <body> LOOP/+LOOP` (F17):
        //
        //     do_setup                  ; frame <- (limit start)
        //   start: <body>
        //     loop_step | plus_loop_step  operand=start
        //   exit: ...                   ; LEAVE branches here
        //
        // do_setup pops (limit start) into a return-stack frame;
        // loop_step/plus_loop_step advance the index and either branch
        // back to `start` or fall through (tearing the frame down) to
        // `exit`. Every LEAVE emitted inside <body> is back-patched to
        // `exit` here.
        auto setup_r = emit(out, op::do_setup, cell{0}, alt.pos);
        if (!setup_r.has_value()) {
            return setup_r.error();
        }
        int const loop_start = out.code.size();
        auto body_r = codegen_emit_body(unit, alt.body, out);
        if (!body_r.has_value()) {
            return body_r;
        }
        auto step_r = emit(
            out, alt.is_plus_loop ? op::plus_loop_step : op::loop_step,
            static_cast<cell>(loop_start), alt.pos);
        if (!step_r.has_value()) {
            return step_r.error();
        }
        int const loop_exit = out.code.size();
        // Back-patch this loop's own LEAVE branches (still at the -1
        // sentinel; any inner loop's leaves were already patched).
        for (int k = loop_start; k < loop_exit; ++k) {
            if (out.code[k].code == op::leave &&
                out.code[k].operand == cell{-1}) {
                out.code[k].operand = static_cast<cell>(loop_exit);
            }
        }
        return std::monostate{};
```

The back-patch scans the instructions belonging to one loop and only patches the `-1` sentinels still sitting there. An inner loop's own `LEAVE` branches are already patched to its own exit by the time its `codegen_emit_body` call returns, so an outer loop's scan, over the range starting where its own body began, only ever finds sentinels that are actually its.

`LEAVE` also does something to the static checker that `EXIT` never had to: it poisons the whole containing definition's computed stack effect to unknown, the same treatment `?DUP` already gets for being genuinely input-dependent. What the data stack looks like at the instant a `LEAVE` fires and what it looks like on the loop's ordinary fall-through path have no reason to match, and the checker isn't trying to prove they do. That's also, incidentally, what makes `IF I LEAVE THEN` &#x2014; an `IF` with a `LEAVE` in the then-branch and nothing in the else-branch &#x2014; type-check at all, despite the two branches visibly disagreeing about what they leave on the stack. The checker isn't reconciling them; it's declined to look.


# UNLOOP finishes a sentence I left hanging

Some entries back, adding the effect checker's exit-path diagnosis, I wrote a rule that any `EXIT` found lexically inside a `DO` loop's body was an error, full stop, no exceptions. At the time that was the honest rule, because leaving a definition from inside a counted loop pops the return address with a plain `ret`, and a counted loop's own parameters are sitting on that same return stack, directly above the address `ret` needs. An early `EXIT` would pop a loop-parameter cell and call it a return address. There was no word that let a program clear its own loop frame first, so there was no way to make the shape safe, and I diagnosed it unconditionally rather than pretend otherwise.

`UNLOOP` is that word. It discards the innermost loop's frame &#x2014; index, then limit &#x2014; without branching anywhere, so a following `EXIT`'s `ret` finds the real return address where the frame used to be. The diagnosis now only fires when there's no `UNLOOP` earlier on the path to an `EXIT`: `UNLOOP EXIT` and `IF UNLOOP EXIT THEN` are both accepted and both run correctly; a bare `EXIT` inside `DO` with nothing clearing it first is still rejected, just as before. This isn't a new leniency bolted onto an old rule so much as the rule getting to say the thing it always meant, once the word it was written in anticipation of actually existed.


# The checker was already wrong

Here's the part that cost the most time, and it isn't LEAVE, or +LOOP's termination rule, or anything I set out to build this entry. It's a bug that was sitting in the stack-effect checker's `DO...LOOP` model since the entry that first wrote it, invisible the entire time because nothing had ever exercised the one shape that would expose it.

The checker's job for a loop body is the same as for any control structure: figure out what the body itself consumes and produces, then figure out what the whole construct contributes to whatever surrounds it. For `DO...LOOP`, the earlier model said the construct's own contribution was the same as the body's own effect &#x2014; if the body needs two cells and leaves two behind, so does the loop as a whole. That is wrong, but it is wrong in a way a single loop at the top of a definition can never reveal, because `DO` itself pops two cells, `limit` and `start`, before the body ever runs, and a lone top-level loop's `limit` and `start` are just two more cells among whatever else the definition pushes &#x2014; nothing downstream cares whether they were consumed by `DO` or by an ordinary primitive.

Nest one counted loop inside another, and the inner loop's own `limit` and `start` are pushed from **inside** the outer loop's body. If the outer loop's own bookkeeping doesn't know that `DO` eats two cells nobody else gave back, the outer body looks like it grows by two cells every time through &#x2014; a program that runs perfectly correctly gets rejected as "unbalanced," for a reason that has nothing to do with anything actually wrong with it. I found this by writing the merge criterion for `J`, which needs two loops, one inside the other, to mean anything at all &#x2014; and watching a completely ordinary loop-summing-a-loop program fail to compile.

```cpp
} else if constexpr (std::is_same_v<
                         T, core_do_loop<MaxNodes, MaxBody>>) {
    auto body_r =
        analyze_body(unit, alt.body, self_index, self_effect);
    if (!body_r.has_value()) {
        return body_r.error();
    }
    if (body_r.value().r_delta != 0) {
        return foundation::parse_error{
            alt.pos, "return stack is unbalanced across a "
                     "control-structure body"};
    }
    auto const &body_eff = body_r.value().eff;
    if (body_eff.known) {
        // A plain `LOOP` body must be net-zero; a `+LOOP` body
        // must leave exactly the increment `+LOOP` then
        // consumes (net +1).
        if (alt.is_plus_loop) {
            if (body_eff.net() != 1) {
                return foundation::parse_error{
                    alt.pos,
                    "+LOOP body must leave exactly the "
                    "increment (net +1)"};
            }
        } else if (body_eff.net() != 0) {
            return foundation::parse_error{
                alt.pos,
                "DO loop body must have a net-zero stack "
                "effect"};
        }
        // `DO` itself consumes the two cells (limit start)
        // sitting above whatever the body needs; for `+LOOP`
        // the body's extra output is consumed by `+LOOP`
        // itself. Either way the whole `DO ... LOOP`/`+LOOP`
        // needs body.inputs + 2 at entry and leaves
        // body.inputs behind. Modelling those two consumed
        // cells (F12 did not) is what lets a nested counted
        // loop -- whose inner `limit start` are pushed inside
        // the outer loop's own body -- still satisfy the outer
        // body's net-zero requirement.
        item_eff = known(body_eff.inputs + 2, body_eff.inputs);
    } else {
        item_eff = unknown_effect;
    }
```

The fix is to model what `DO` actually consumes: the whole `DO...LOOP` needs the body's own inputs plus two, and gives back the body's own outputs, full stop. That is not a new check or a loosened one &#x2014; it is the same net-zero requirement on a plain `LOOP` body the checker always enforced, now attached to an entry cost the checker had simply never counted. The bug was there the whole time a single loop could not ask the question that would have found it.

The same block picked up a second, smaller correction while I was in there: a `+LOOP` body is required to leave exactly one cell net, not zero, because that cell is the increment `+LOOP` itself is about to consume. I had been about to leave the `+LOOP` arm sharing the plain `LOOP` arm's net-zero requirement on the theory that they're close enough. They are not. A perfectly good "sum the even numbers under ten" definition, written the obvious way with the running total on the stack and a literal 2 pushed right before `+LOOP`, would have been rejected by the exact rule meant to let programs like it through.


# A small hole that had nothing to do with loops

The first program I wrote to prove any of this worked was summing 1 through N with a counted loop:

```forth
: SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO   \ leaves [15]
```

It needs `1+` to turn `N` into an exclusive upper bound for `DO`, and the dictionary didn't have one. It had `1-`, added a while back for an equally small reason, and nothing symmetric on the other side. Adding `1+` is not interesting on its own &#x2014; it's an ordinary arithmetic primitive, one line in four different files, with the same fixed effect every other primitive its size has. What's worth remarking is only that a loop needed it to say something as plain as "count from zero, not including N" without writing `N 1 +` by hand every time, and Forth apparently agreed with me: this is exactly why `1+` exists as a standard word instead of everyone spelling it out.

The rest of the merge criteria came together once the checker stopped lying about `DO`'s own cost. A linear scan that stops early:

```forth
: FIND5 10 0 DO I 5 = IF I LEAVE THEN LOOP ;  FIND5   \ leaves [5]
```

Nested loops summing an outer index across a 3-by-3 pass:

```forth
: TENS 0 3 0 DO 3 0 DO J + LOOP LOOP ;  TENS   \ leaves [9]
```

A `+LOOP` counting by twos:

```forth
: SUMEVEN 0 10 0 DO I + 2 +LOOP ;  SUMEVEN   \ leaves [20]
```

And an early return from inside a loop, by way of `UNLOOP`:

```forth
: FIRST 10 0 DO I DUP DUP * 8 > IF UNLOOP EXIT THEN DROP LOOP -1 ;  FIRST
\ leaves [3] -- the first index whose square exceeds 8
```

Every one of these runs identically through the direct evaluator and the compiled VM, which is the only claim this whole project is actually trying to make good on, one word at a time.


# What I haven't checked

One thing I noticed while writing the frame teardown and didn't chase down: none of it &#x2014; not `LEAVE`'s discard, not `UNLOOP`'s, not the ordinary termination path's &#x2014; verifies that the top of the return stack is actually the loop's own frame at the moment it goes to tear it down. It assumes that, because for any program that passes the existing return-stack-balance check it has to be true. But that check is a whole-body property, not a step-by-step one, and I have no merge criterion that mixes `>R` and `R>` with `LEAVE` or `I` inside the same loop to actually stress the assumption. It's the same shape of gap the early-`EXIT` diagnosis already has &#x2014; correct for every program I've written, unverified for programs I haven't. I'm leaving it that unproven, rather than pretend a case I haven't tried is a case I've covered.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 10 - The Address Was Always a Cell](post-10-memory-words.md) | [Next: Part 12 - The Patch Stream Was Already There →](post-12-the-patch-stream-was-already-there.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
