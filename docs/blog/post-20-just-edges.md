<div class="abstract" id="org6bb91c5">
<p>
Part 15 cut the elaborator and left a plain confession standing where the
stack-effect checker used to be: nothing checked, a declared effect stored
and never looked at again. Part 11 admitted a counted loop's own teardown
never verified the frame it was discarding; Part 19 narrowed that gap without
closing it. Tonight all three debts come due against the same mechanism: an
abstract interpreter over the instructions a colon definition actually
compiled, run at <code>;</code>, that assigns every reachable instruction a level and
requires every edge into it to agree. A branch's two arms and a loop's back
edge turn out to be the same thing under that rule, which is why the
loop-teardown gap closes for free instead of needing its own code. The first
version of this checker was also wrong, and I didn't find that by testing it
myself; a conformance suite that landed on this tree the same week rejected
real, standard Forth, and made me draw a distinction I hadn't thought I
needed.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 19 - The Gap That Stopped Mattering ←](post-19-the-gap-that-stopped-mattering.md)

</nav>


# A definition that used to compile

This one doesn't any more (shape, not verbatim code I've run):

```forth
: BAD 10 0 DO 5 >R LOOP ;
```

It pushes a cell with `>R` inside a counted loop and never takes it back before `LOOP` closes. Part 11 built the loop frame this depends on (a `limit` / `index` pair sitting on the return stack, popped by `LOOP`'s own teardown) and admitted, in the same entry, that nothing checked whether the two cells `LOOP` pops back off are actually its own. Part 19 built `THROW` on top of the same return stack and made the one path that has to unwind through an arbitrary mix of frames correct by never looking at what it discards, which closed the interesting half of that gap without touching the boring half: an ordinary `LOOP` still just pops two cells and trusts they're the right two. I said so at the time and moved on to other work.

`BAD` is that untouched half, made concrete. Tonight it stops compiling, and it stops for a reason that has nothing to do with loops specifically.


# Every edge, not every construct

The checker Part 6 wrote and Part 15 buried walked the elaborated tree by hand: a function for `IF`, a function for `DO`, a rule per node kind, each one trusting the last that whatever it recursed into had already balanced itself correctly. There's no tree to walk any more. What a colon definition leaves behind now is a run of instructions in the interpreter's own code space, between wherever `entry_point` landed and the `ret` that `;` just emitted, and this step's checker works over that range directly.

The first thing it needs is the edges. Every instruction in that range has one or two possible successors: a plain instruction falls through to the next one, a conditional branch has two targets, an unconditional one has a single target somewhere else, a terminal instruction has none. Nothing here cares whether an edge came from an `IF`'s else-arm or a `LOOP`'s own back edge to the top of the body:

```cpp
struct instr_edges {
    int a = -1;
    int b = -1;
};

/// Computes @p in's own @ref instr_edges, given its own instruction @p index
/// (needed only for the fallthrough case, `index + 1`).
///
/// Shared verbatim by @ref check_definition_effect's own worklist and by
/// @ref recover_basic_blocks (D24: "the recovered CFG is shared with F33's
/// sender lowering -- one analysis, two clients") -- this function *is*
/// that one shared analysis' own edge relation; the two clients differ only
/// in what they do with it (a per-instruction abstract interpretation here,
/// a leader-based block partition there).
[[nodiscard]] constexpr auto instruction_successors(machine::instr const &in,
                                                    int index) -> instr_edges {
    using machine::op;
    switch (in.code) {
    case op::ret:
    case op::does_enter:
    case op::halt:
        return {-1, -1};
    case op::branch:
        return {static_cast<int>(in.operand), -1};
    case op::branch0:
    case op::loop_step:
    case op::plus_loop_step:
        return {index + 1, static_cast<int>(in.operand)};
    case op::leave:
        return {static_cast<int>(in.operand), -1};
    default:
        return {index + 1, -1};
    }
}
```

That's the whole point, and it took me longer to see than it should have. A branch's two arms rejoining at `THEN` and a loop's own body rejoining itself at the top are the same shape: two edges landing on one instruction, which had better agree about what they're each carrying. Once the edges are just edges, one worklist can walk all of them the same way, instead of a tree-fold needing a special case for every construct that happens to join control flow back together.

What each edge carries is a level: a `(data-stack, return-stack)` depth pair, relative to the definition's own entry. The worklist starts at `entry` with both at zero, follows every edge outward, and whenever two edges land on the same instruction, their levels have to match. A node's own state only ever moves in one direction, unvisited to settled, known to unknown, so the walk terminates even across a loop's own back edge, which is the one place a naive recursive fold has nowhere obvious to stop.


# Two stacks, and they don't get the same rule

Here's where the two edges landing on one instruction actually get compared, and it's the part of this step I'd write differently if I'd understood the whole shape of the problem before I started:

```cpp
bool ret_changed = false;
if (!node.ret_visited) {
    node.ret_visited = true;
    node.ret_level = item.ret_level;
    ret_changed = true;
} else if (node.ret_level != item.ret_level) {
    return foundation::parse_error{
        diag_pos, "return stack is unbalanced across a branch or "
                  "loop boundary"};
}

bool data_changed = false;
if (!node.data_visited) {
    node.data_visited = true;
    node.data_known = item.data_known;
    node.data_level = item.data_level;
    data_changed = true;
} else if (node.data_known) {
    if (item.data_known) {
        if (node.data_level != item.data_level) {
            if (declared_effect_gate) {
                return foundation::parse_error{
                    diag_pos,
                    "stack effect is inconsistent across a branch "
                    "or loop boundary"};
            }
            // Advisory (D20, DIV-0019's own post-merge amendment):
            // undeclared, so a runtime-determined loop trip count
            // (the Hayes ttester's own `EMPTY-STACK`) may
            // legitimately make this join's own data-stack depth
            // unknowable statically -- collect the disagreement
            // rather than aborting, and poison this node to
            // unknown exactly like an EXECUTE/CATCH/LEAVE-caused
            // one (this header's own top comment).
            if (diagnostics.size() < MaxDiagnostics) {
                diagnostics.push_back(foundation::parse_error{
                    diag_pos,
                    "stack effect is inconsistent across a branch "
                    "or loop boundary (advisory: undeclared)"});
            }
            node.data_known = false;
            data_changed = true;
        }
    } else {
        node.data_known = false;
        data_changed = true;
    }
}
// else: node already unknown -- stays unknown regardless of item.
```

A return-stack disagreement is an unconditional error, full stop, every time. A data-stack disagreement checks `declared_effect_gate` first: hard error if the word declared a `( ... -- ... )` effect, otherwise collected onto a diagnostics list and the join poisoned to unknown, the same treatment `EXECUTE` or `CATCH` already get for being genuinely unknowable. `BAD` fails here, on the return-stack branch, with nothing conditional about it: the loop's back edge disagrees with the forward path on how deep `>R` left the return stack, and there's no declaration that would have changed that outcome. That's Part 11's gap, closed: a static rejection at `;`, nothing added to the loop's own runtime path.

I did not write that split on the first pass. I wrote one rule, "every join has to agree," and applied it to both stacks alike, because from where I sat that looked like the same requirement asked twice. It compiled every program in this project's own corpus. It also rejected a definition I hadn't written and had no reason to distrust.


# The tester that found the mistake

I'd had this checker sitting on a branch for a while by the time I went to bring it up to date with everything else that had landed on this tree in the meantime, and one of the things waiting for me there was a real Forth-2012 conformance suite: John Hayes' public-domain word-set tester, already adapted and already exercising this project's own core word set against hundreds of assertions. I rebased onto it expecting a clean merge. It didn't compile.

The definition that failed is the tester's own housekeeping, not anything exotic (shape, not verbatim code from this project):

```forth
: EMPTY-STACK
   DEPTH START-DEPTH @ < IF DEPTH START-DEPTH @ SWAP DO 0 LOOP THEN
   ... ;
```

It pads the data stack back up to a depth recorded earlier, by looping a runtime-determined number of times and pushing one cell per iteration. The trip count is a value computed at the moment the word runs, not anything visible to a static pass over the instructions, and there is no consistent level my worklist can assign across that loop's own back edge. Not because the tester got anything wrong. Forth-2012 places no requirement at all on a plain `DO...LOOP` body's own net data-stack effect, and this is a completely ordinary way to use that freedom. My checker rejected it anyway, because I'd made every join-consistency failure fatal, unconditionally, regardless of whether anyone had written down a contract for my checker to be enforcing.

The split shown above is what actually fixes it, and I want to be plain about what kind of bug it was. This project's own design already said the right thing before I ever wrote a line of this checker: advisory by default, promoted to a hard gate only when a definition declares an effect. I had the words for the rule already sitting in this project's own history and implemented something stricter anyway, because a single "every join must agree" check was simpler to write than two, and I didn't go looking for the case that would tell them apart until one showed up uninvited. The fix isn't a loosened checker. It's the same checker, corrected to do what it was supposed to do from the start: an undeclared loop whose net effect depends on a runtime value is a fact about what this checker can't see, not a fact about the program, and it has no business being fatal. `BAD` never had that excuse. Forth-2012 requires the return stack balanced before `LOOP` runs, unconditionally, whatever the trip count turns out to be ({Forth 200x Standardisation Committee}, 2014), which is the whole reason the fix is a split by stack and not a blanket retreat to "advisory everywhere." A blanket retreat would have made `BAD` pass again, quietly, and undone the one thing this entire step was supposed to close.


# What closes for free

Two other gaps fell out of the same worklist without asking for separate handling. Part 6 named a definition whose early `EXIT` leaves a different depth than its fall-through path, and admitted the old checker had no way to see it: it stopped folding the moment it hit `EXIT` and never came back to compare. A real control-flow graph doesn't have that blind spot: both paths, the early exit and the fall-through, are just more nodes this checker's own final pass walks, each with its own settled level. A declared effect now checks every reachable exit individually rather than whichever one the old fold happened to reach last; an undeclared word whose exits genuinely disagree gets an honest `unknown` instead of a guess.

Part 11's gap is the other one, and I've already shown the code that closes it. I'll say it again in plain terms, because it's the reason this entry exists: the checker never needed to know anything about loop frames specifically. It needed to know that a return-stack level at the top of a loop's back edge has to equal the return-stack level the forward path already established, and that requirement was already sitting in the worklist for every other kind of join. Loops weren't special. I had been treating them as if they were since Part 11, because I fixed that entry's own bug (`DO`'s two-cell entry cost, `+LOOP`'s net-one body) one loop construct at a time, without ever writing the rule that would have made the special-casing unnecessary.


# A number that used to be a lie

One more thing this checker leaves behind, on every word it accepts: how deep the data stack ever actually gets along the way, separate from where it nets out. A definition can have net effect zero and still need three cells of headroom to get there, and until tonight nothing in this project recorded that.

```cpp
/// Step F30 (D20): the *computed* (or, when @ref has_effect is true and
/// it parsed, declared-and-verified) net data-stack effect, filled by
/// `interpreter::effect_lint`'s own `;`-time check
/// (`interpreter::check_definition_effect`) -- the checker's one
/// durable, per-word deliverable, consulted by a later `call` to this
/// word from inside another definition's own effect check. `false`
/// means "not statically known" (the lattice's own top value): reached
/// only through `EXECUTE`/`CATCH`/an input-dependent primitive
/// (`?DUP`), or a branch/loop join this checker could not reconcile to
/// a single value (never a *rejected* definition -- those are diagnosed
/// compile errors at `;`, not installed at all).
bool effect_known = false;
/// Minimum data-stack depth this word requires at entry. Meaningful
/// only when @ref effect_known is true.
int effect_inputs = 0;
/// Data-stack depth this word leaves behind, measured from the same
/// entry point as @ref effect_inputs. Meaningful only when
/// @ref effect_known is true.
int effect_outputs = 0;
/// The greatest absolute data-stack depth reached anywhere in this
/// word's own body, assuming it is entered with exactly
/// @ref effect_inputs cells present (the tightest safe case) -- `-1`
/// ("not computed") unless @ref effect_known is also true: peak depth is
/// only reported for a word whose shape is fully known, never as a
/// partial/best-effort number for one that touches `unknown` anywhere
/// (DIV-0019's own disposition of DIV-0008's "peak depth" gap).
int peak_depth = -1;
```

I checked this one by hand before trusting the machinery, because a peak that isn't the same as the net effect is exactly the kind of thing a worklist can quietly get wrong:

```cpp
// A hand-computed required/peak-depth merge criterion: `DUP DUP DROP DROP`
// has net effect zero (one input, one output) but peaks at three cells
// (entry's own one, plus two more DUPs) before the two DROPs bring it back
// down -- required_depth == 1, peak_depth == required_depth + 2 == 3, by
// hand: entered with exactly one cell, DUP makes two, DUP again makes
// three (the peak), DROP DROP returns to one.
TEST_CASE(
    "EffectLintTest - PeakDepthIsARealNumberAssertedAgainstAHandComputation") {
    forth_state<64, 64, 1024, 256> st{": PEAKY DUP DUP DROP DROP ;"};
    auto dict = default_dictionary<>();
    compile_buffer<> buf;
    auto r = interpret(st, dict, buf);
    REQUIRE(r.has_value());
    auto const *entry = dict.lookup("PEAKY");
    REQUIRE(entry != nullptr);
    auto const *cw =
        std::get_if<smd::forth::machine::compiled_colon_word>(&entry->binding);
    REQUIRE(cw != nullptr);
    CHECK(cw->effect_known);
    CHECK(cw->effect_inputs == 1);
    CHECK(cw->effect_outputs == 1);
    CHECK(cw->peak_depth == 3);
    // DIV-0008's own capacity fields, finally filled (this step's own
    // merge criterion): a running maximum across every definition closed so
    // far in this session whose own peak was computable (this interp.hpp's
    // own doc comment explains why a single whole-`compiled_program` number
    // cannot mean what it did before F26's own cut). PEAKY is the first and
    // only definition in this fresh buf, so the running maximum is exactly
    // its own peak.
    CHECK(buf.program().required_stack_depth == 3);
}
```

`required_stack_depth` has been a `-1` placeholder on `compiled_program` since the codegen entry ever gave it a field to sit in. It's a running maximum across a whole session's own definitions now, not a single whole-program bound: this project shares one growing code space across every colon word compiled into it, and there's no longer a single "the program" for one number to describe the peak of. That's a coarser claim than the field's own name once implied, and I'd rather say so than let the name keep promising something the architecture underneath it stopped being able to deliver several entries ago.


# What's still only advisory

I want to end on the part of this that isn't a clean win, because it would be easy to write the last section and let the reader assume this closes the gap completely. It doesn't. An undeclared word with a genuinely broken data-stack effect (an actual bug, not a runtime-dependent shape like the tester's own padding loop) now installs with a collected diagnostic instead of failing to compile. That diagnostic is real: it lands on `compiled_program::diagnostics`, and a caller can read the whole session's own accumulated list back out. But nothing prints it, nothing surfaces it by default, and a program that never asks for it will never see it. A diagnostic nobody retrieves is not wrong to call advisory. It's also not much different from absent, from where the person running the program is standing.

The other loose thread is smaller and more mechanical: every diagnosis this checker raises is positioned at the closing `;`, because `machine::instr` carries no source position of its own to point at anything finer, and threading one through every opcode this project emits is a change well past what tonight's work was for. The old, deleted checker could at least point at the offending node. This one can only say which definition; it has no way to point at which instruction inside it. Coarser, and worth remembering the next time a diagnosis says less than I'd like it to.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 19 - The Gap That Stopped Mattering](post-19-the-gap-that-stopped-mattering.md)

</nav>


# References

{Forth 200x Standardisation Committee} (2014). *Forth 200x / Forth-2012 Standard*.
