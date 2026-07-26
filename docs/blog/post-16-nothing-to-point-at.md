<div class="abstract" id="orgcec6f76">
<p>
Part 15 ended with the tree gone and nothing to show for it yet: <code>IF</code>,
<code>BEGIN</code>, and <code>DO</code> were unknown words, and every program this project has
been carrying since Part 7 to prove control flow works was stuck behind
that gap. Tonight closes it. <code>IF ELSE THEN</code>, <code>BEGIN UNTIL</code>, <code>BEGIN WHILE
REPEAT</code>, and the whole <code>DO LOOP</code> family are real dictionary entries now,
immediate on the orig/dest discipline Forth-2012 names, patching their own
branches the moment the outer interpreter reaches them. Part 12 argued this
would work without changing anything about the patch stream underneath it.
That argument gets tested tonight, and it holds. What doesn't survive
untouched is my first idea for catching an unclosed <code>IF</code> at <code>;</code> &#x2014; it was
wrong, one of my own tests said so, and what replaced it is smaller than I
wanted.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 15 - The Cut ←](post-15-the-cut.md)

</nav>


# The words, as the standard actually describes them

Forth-2012 doesn't define `IF` as syntax. It defines it as an ordinary dictionary entry with one property most words don't have: it runs the instant the outer interpreter meets it, whether or not that interpreter is currently compiling. That property has a name, `IMMEDIATE`, and it's what lets `IF`'s own action be "emit a branch and remember where," instead of something a parser has to know about in advance.

```cpp
/// Step F27 (docs/forth-plan-2.md), D17: the C++-installed control words
/// (`IF ELSE THEN`, `BEGIN UNTIL`, `BEGIN WHILE REPEAT`, `DO LOOP +LOOP
/// LEAVE UNLOOP I J`, `LITERAL`, `POSTPONE`, `IMMEDIATE`, `[`, `]`,
/// `COMPILE,`) carry no VM entry point of their own: unlike a primitive
/// (inlined as @ref op::prim at every reference site) or a @ref
/// compiled_colon_word (real instructions in the interpreter's own code
/// space), a control word's whole job is to mutate that code space directly
/// while a definition is being compiled -- @ref op::branch0 sentinels,
/// back-patched operands, the return-stack loop machinery's own setup/step
/// instructions -- and to use the data stack as the Forth-2012 control-flow
/// stack (the orig/dest discipline) meanwhile. That mutation needs the
/// interpreter's own @c compile_buffer, a type `machine/` has no reason to
/// know about, so this enum and @ref control_word are only the
/// dictionary-level tag identifying *which* action a name is bound to;
/// `interpreter::apply_control_word` (`interp.hpp`) is what the tag actually
/// dispatches to.
enum class control_builtin : std::uint8_t {
    if_,
    else_,
    then_,
    begin_,
    until_,
    while_,
    repeat_,
    do_,
    loop_,
    plus_loop_,
    leave_,
    unloop_,
    i_,
    j_,
    literal_,
    postpone_,
    immediate_,
    bracket_open_,  ///< `[`
    bracket_close_, ///< `]`
    compile_comma_, ///< `COMPILE,`
};

/// A control-builtin word's binding: which action (@ref control_builtin) it
/// performs. See that enum's own doc comment for why this carries no VM
/// entry point the way @ref compiled_colon_word does.
struct control_word {
    control_builtin which{};

    friend constexpr auto operator==(control_word const &, control_word const &)
        -> bool = default;
};
```

Twenty of those tags, one dictionary entry each, all immediate from the moment they're installed. The dispatch on the tag &#x2014; what `IF` or `DO` actually **does** &#x2014; lives one layer up, in the interpreter, because that's where the thing being mutated lives: a control word's job is to reach into the code space currently under construction and change it, and the VM never sees that code space, only finished instructions.

Here is `IF~/~ELSE~/~THEN` doing exactly that:

```cpp
case control_builtin::if_:
case control_builtin::while_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto r = buf.emit(op::branch0, cell{-1}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        return st.machine().data().push(static_cast<cell>(r.value()));
}
case control_builtin::else_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto orig = st.machine().data().pop();
        if (!orig.has_value()) {
            return orig.error();
        }
        auto r = buf.emit(op::branch, cell{-1}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        auto p = patch(static_cast<int>(orig.value()),
                       static_cast<cell>(buf.here()));
        if (!p.has_value()) {
            return p;
        }
        return st.machine().data().push(static_cast<cell>(r.value()));
}
case control_builtin::then_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto orig = st.machine().data().pop();
        if (!orig.has_value()) {
            return orig.error();
        }
        return patch(static_cast<int>(orig.value()),
                     static_cast<cell>(buf.here()));
}
```

`IF` emits a branch with a sentinel operand &#x2014; `-1`, which can never be a real instruction index &#x2014; and pushes the branch's own position onto the data stack. `THEN` pops that position back off and overwrites the sentinel with wherever the code space happens to be now. The data stack is doing Forth-2012's own "orig/dest" bookkeeping here, not holding program data; that's the standard's own permission, not a shortcut I invented to avoid building a second stack.

`DO~/~LOOP~/~+LOOP~/~LEAVE` are the same discipline with one more moving part, because a loop needs to patch every early exit inside it, not just one branch:

```cpp
case control_builtin::do_: {
        if (st.state() == 0) {
            return compile_only();
        }
        auto setup_r = buf.emit(op::do_setup, cell{0}, pos);
        if (!setup_r.has_value()) {
            return setup_r.error();
        }
        auto pr = st.machine().data().push(static_cast<cell>(buf.here()));
        if (!pr.has_value()) {
            return pr;
        }
        ++cctx.loop_depth;
        return std::monostate{};
}
case control_builtin::loop_:
case control_builtin::plus_loop_: {
        if (st.state() == 0) {
            return compile_only();
        }
        if (cctx.loop_depth <= 0) {
            return foundation::parse_error{pos,
                                           which == control_builtin::loop_
                                               ? "LOOP without a matching DO"
                                               : "+LOOP without a matching DO"};
        }
        auto dest = st.machine().data().pop();
        if (!dest.has_value()) {
            return dest.error();
        }
        int const dest_idx = static_cast<int>(dest.value());
        auto r = buf.emit(which == control_builtin::loop_ ? op::loop_step
                                                          : op::plus_loop_step,
                          static_cast<cell>(dest_idx), pos);
        if (!r.has_value()) {
            return r.error();
        }
        int const loop_exit = buf.here();
        for (int k = dest_idx; k < loop_exit; ++k) {
            auto &code_k = buf.program().code[k];
            if (code_k.code == op::leave && code_k.operand == cell{-1}) {
                code_k.operand = static_cast<cell>(loop_exit);
            }
        }
        --cctx.loop_depth;
        return std::monostate{};
}
case control_builtin::leave_: {
        if (st.state() == 0) {
            return compile_only();
        }
        if (cctx.loop_depth <= 0) {
            return foundation::parse_error{pos, "LEAVE outside a DO ... LOOP"};
        }
        auto r = buf.emit(op::leave, cell{-1}, pos);
        if (!r.has_value()) {
            return r.error();
        }
        return std::monostate{};
}
```

`LEAVE` still writes its own `-1` and walks away without knowing yet where the loop ends &#x2014; exactly the sentinel scan Part 12 pointed at as evidence that this project already had a patch stream before it had an outer interpreter. What's different tonight is only the boundary of the scan. Under the old tree walker, the scan ran over a node range the tree itself delimited. Here, `LOOP` scans from `DO`'s own remembered position to wherever the code space is now &#x2014; a slice of the **instruction array** instead of a slice of a tree &#x2014; and patches every `LEAVE` it finds still carrying its sentinel. An inner loop's own `LOOP` already resolved its own ~LEAVE~s first, so the bound never reaches past where it should. Same scan, same sentinel, a different thing measuring where to stop.

Part 12 predicted this, in words: "move the same emit-and-patch code one layer up, so `IF` calls it when the text interpreter reaches `IF` rather than when a codegen pass reaches an `IF` node, and nothing about the patch stream changes." I believed that when I wrote it. Tonight is where I got to find out whether it was true, and the loop opcodes, the two termination rules for `LOOP` and `+LOOP`, and the `LEAVE` scan itself all carried over without a single change to their own logic. What changed was who calls them and what bounds them. That's a smaller victory than it sounds like stated flatly, and it's also exactly the victory that was on offer.


# One rule, not five special cases

Every entry in this dictionary before tonight got compiled or executed by a chain of direct-name comparisons: is this token `:`? Is it `;`? `EXIT`? `RECURSE`? `VARIABLE`? Each one its own branch in `interpret`'s own loop, because each one needed different treatment and there was no shared rule underneath them yet. D13's actual rule &#x2014; execute a word when interpreting, or when it's immediate; otherwise compile it &#x2014; was true of all of them the whole time, and I wasn't using it.

```cpp
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut, int MaxWords,
          int MaxCode, int MaxBufWords, int MaxName>
[[nodiscard]] constexpr auto
execute_entry(machine::dictionary_entry<MaxName> const &entry,
              forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut, MaxName> &st,
              machine::dictionary<MaxWords, MaxName> &dict,
              compile_buffer<MaxCode, MaxBufWords> &buf,
              compiling_context<MaxName> &cctx, foundation::source_pos pos,
              int vm_fuel) -> machine::status {
    if (auto const *op = std::get_if<machine::primitive>(&entry.binding)) {
        return machine::apply_primitive(*op, st.machine());
    }
    if (auto const *cw =
            std::get_if<machine::compiled_colon_word>(&entry.binding)) {
        return call_word(buf, st.machine(), cw->entry_point, vm_fuel);
    }
    if (auto const *vw = std::get_if<machine::variable_word>(&entry.binding)) {
        return st.machine().data().push(
            static_cast<machine::cell>(vw->address));
    }
    if (auto const *cnw = std::get_if<machine::constant_word>(&entry.binding)) {
        return st.machine().data().push(cnw->value);
    }
    if (auto const *ctl = std::get_if<machine::control_word>(&entry.binding)) {
        return apply_control_word(ctl->which, st, dict, buf, cctx, pos);
    }
    return foundation::parse_error{
        pos, "word is not executable yet (F25: primitives and colon "
             "words only)"};
}
```

`execute_entry` pairs with a twin, `compile_entry`, that appends the same four cases' compiled form instead of running them. `interpret`'s own loop is two lines now: call `execute_entry` unconditionally while interpreting, and while compiling, call `execute_entry` for an immediate entry or `compile_entry` for an ordinary one. Every primitive, every colon word, every variable and constant, and now every control word, goes through the same two functions. Fewer special cases came out of this step than went in, which is not the direction special-case counts usually move.


# A control word has nothing to point at

Every other kind of entry in this dictionary compiles into something with an address: a primitive inlines as one instruction, a colon word has real instructions sitting in the code space waiting to be called. A control word has neither, because its entire output **is** a mutation of the code space being built, not an addition to it that could be called later. There is no instruction `call THEN` could mean, because by the time `THEN` has run, the branch it patched is already sitting there finished.

That has a real consequence, not a hypothetical one: `POSTPONE` of a control word works, but only under one condition.

```forth
\ (shape, not verbatim)
: ENDIF POSTPONE THEN ; IMMEDIATE
: ABS2 DUP 0< IF NEGATE ENDIF ;
-7 ABS2
```

That leaves `7` on the stack, and `ENDIF` is not a colon word that happens to produce `THEN`'s effect &#x2014; it's a dictionary entry carrying the literal tag `then_`, indistinguishable from `THEN` itself for every purpose afterward. `POSTPONE` gets there by refusing to compile anything: seeing that its target has no compiled form, it just records the tag and lets `;` install the word being closed as a plain alias. That only works when the postponed control word is the **entire** body of the definition. `: BAD POSTPONE THEN DROP ;` is diagnosed, not silently miscompiled, because mixing an alias with ordinary compiled code would need a single representation that covers both, and a control word still doesn't have one. I want that named as a real limitation, sitting there open, not something I've talked myself into believing is fine.


# The check that had to come back out

The obvious way to catch an unclosed `IF` or `BEGIN` at `;` is to compare the data stack's depth when `;` runs against its depth when the matching `:` started. The orig/dest markers live on that stack; a leftover one means something never got resolved. I wrote exactly that check first, because it's the check anyone would reach for, and it's wrong.

```forth
\ (shape, not verbatim)
: DOUBLE-IT 21 ; IMMEDIATE
: USES-IT DOUBLE-IT ;
```

`DOUBLE-IT` is immediate, so meeting it while compiling `USES-IT` runs it right then, not later. Running it pushes a real `21` onto the data stack, during `USES-IT`'s own compilation, and that `21` is legitimately sitting there when `USES-IT`'s own `;` runs &#x2014; nothing is unresolved, an immediate word just did what immediate words do. A depth comparison can't tell that apart from a genuinely dangling `IF`. Neither can it tell apart the value a `[ ... ] LITERAL` bracketed computation leaves for `LITERAL` to pick up, which is the same shape of "real data, deliberately left on the stack, during compilation" for a completely different reason.

I didn't catch this by staring at the design. I caught it because a test I had already written &#x2014; one asserting that an immediate word executes at compile time rather than compiling a call to itself, which is the entire point of `IMMEDIATE` existing &#x2014; started failing against my own draft. The check I'd added was rejecting a program that was correct.

What replaced it is smaller on purpose. A private counter, incremented by `DO` and decremented by `LOOP~/~+LOOP`, that never touches the data stack at all, catches an unresolved `DO` at `;`. `THEN` and `REPEAT` with no matching `IF` or `BEGIN` get no dedicated check whatsoever &#x2014; they're caught later and less precisely, when they try to pop an orig that isn't there and the data stack tells them, correctly, that it's empty. That's a real stack-underflow diagnosis doing double duty as a control-flow diagnosis, one layer down from where I originally wanted it. Less precise than the check I deleted, and it never rejects a program that was actually fine, which the check I deleted did. I'll take the trade.


# What's still open

Nothing about stack effects changed tonight. There is still no checking of any kind for whether a word does what its own declared effect comment says it does &#x2014; the comment after a colon header gets captured, verbatim, off the source text, and checked against nothing at all. `: BAD ( -- ) DROP ;` still compiles clean and still underflows the first time anything calls it. That gap was open the evening Part 15 landed and it's open tonight too; nothing in this entry touches it.

The corpus this project has been accumulating since Part 7 &#x2014; `ABS`, `COUNTDOWN`, the budget-exhaustion program that used to be called `SPIN`, `SUMTO`, `FIND5`, `TENS`, the `+LOOP` program, the one that does `UNLOOP EXIT` out of a loop, both memory-word programs &#x2014; runs verbatim now, compile time and runtime both, through the interpreter directly and through the public one-shot API. That was the whole reason it got kept around instead of deleted with the tests that used to hold it. It finally did the job.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 15 - The Cut](post-15-the-cut.md) | [Part 17 - Something to Point At →](post-17-something-to-point-at.md)

</nav>


# References
