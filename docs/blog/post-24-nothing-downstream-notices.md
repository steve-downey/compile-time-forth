<div class="abstract" id="org967637d">
<p>
Tonight's step gives Forth a way to call out to something that isn't Forth: a
foreign word is an ordinary C++ function taking the same machine state a
primitive already gets, installed with a dictionary entry and an execution
token like any other word, and reached by one new opcode. Nothing downstream
needed to learn what a foreign word is: resolving an execution token,
executing one, catching what it throws, postponing it, checking its declared
effect, all five reach one with no FFI-aware branch anywhere, because the
machinery they use was already general enough. What the plan's own signature
couldn't specify was where the function pointer actually lives, since nothing
already built has a slot shaped for it; and what nobody could have predicted
going in is that the most ordinary check imaginable, comparing a function's
address to null, is not a constant expression at all once the sanitizer this
project builds with by default is switched on. One check moved to where it
could still be made. A criterion I wrote for both backends at once got read
narrower than written, for a reason settled two entries ago and only now
collided with. And the interface runs in both directions: a result computed
once, at compile time, and the identical compiled word called again at
runtime, with arguments nobody chose until the program actually started.
</p>

</div>

{{TEASER\_END}}

<nav style="margin-bottom: 2em; border-bottom: 1px solid #ccc; padding-bottom: 1em">

[↑ Series Index](index.md) | [Part 23 - Renamed, Not Reimplemented ←](post-23-renamed-not-reimplemented.md)

</nav>


# A function pointer, and nowhere obvious to put it

Everything this project has compiled so far has been Forth calling Forth. Tonight a word's body can call out to C++ instead, and the shape of that call is the plan's own, delivered without having to bend it: a foreign word is a plain function pointer taking the live machine state by reference and handing back the same result type a primitive already reports through.

```cpp
/// A foreign word's own implementation: a plain function pointer taking the
/// live @ref forth_state by reference and reporting success or a diagnosed
/// failure through @ref status, exactly like @ref apply_primitive does for a
/// built-in primitive (D7: every misuse is diagnosed, never UB).
///
/// A `constexpr` function pointer is a perfectly ordinary constant
/// expression, so a foreign word written `constexpr` participates in a
/// compile-time session with no special handling anywhere; one that is not
/// `constexpr` (or one that is, but takes a runtime-only branch through
/// `std::is_constant_evaluated`) is simply unusable at compile time and
/// diagnosed as such by the compiler at the point the session is built.
template <int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
using foreign_fn =
    status (*)(forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut> &);

/// A foreign word's optional declared data-stack effect (D20).
///
/// Undeclared (the default, @ref known false) is the `unknown` lattice value
/// F30's own effect checker already uses for `EXECUTE`, `CATCH`, and every
/// other construct whose shape it cannot see through
/// (`interpreter::effect_lint.hpp`); a declared effect participates in that
/// lint like any other word's, and is carried on the dictionary entry (@ref
/// foreign_word) rather than here, since the checker only ever has the
/// dictionary to consult.
struct foreign_effect {
    bool known = false; ///< True iff @ref inputs/@ref outputs are meaningful.
    int inputs = 0;     ///< Cells consumed. Meaningful only when @ref known.
    int outputs = 0;    ///< Cells produced. Meaningful only when @ref known.

    friend constexpr auto operator==(foreign_effect const &,
                                     foreign_effect const &) -> bool = default;
};

/// A declared `( inputs -- outputs )` effect for @ref foreign_dictionary::
/// with_foreign; the default-constructed @ref foreign_effect is the
/// undeclared (`unknown`) one.
[[nodiscard]] constexpr auto declared_effect(int inputs, int outputs)
    -> foreign_effect {
    return foreign_effect{.known = true, .inputs = inputs, .outputs = outputs};
}

/// The foreign-function registry: a flat, fixed-capacity, trivially
/// destructible array of @ref foreign_fn "function pointers" (D3), indexed by
/// exactly the handle @ref foreign_word::index carries and @ref op::foreign
/// names as its own operand.
///
/// @tparam MaxForeign Registry capacity, in functions.
/// @tparam MaxDepth   The registered functions' own @ref forth_state data
///                    stack capacity; likewise @p MaxRDepth, @p MaxData, and
///                    @p MaxOut. A session and every later re-run of it must
///                    use a @ref forth_state with exactly these capacities:
///                    the function pointers are typed on them.
template <int MaxForeign, int MaxDepth, int MaxRDepth, int MaxData, int MaxOut>
class foreign_vocabulary {
  public:
    /// The registered function-pointer type.
    using function_type = foreign_fn<MaxDepth, MaxRDepth, MaxData, MaxOut>;
    /// The state type those functions run against.
    using state_type = forth_state<MaxDepth, MaxRDepth, MaxData, MaxOut>;

    constexpr foreign_vocabulary() = default;

    /// Registers @p fn and returns its own index (its @ref foreign_word::
    /// index, and the operand @ref op::foreign will carry). Diagnoses a full
    /// registry rather than overflowing.
    ///
    /// A null @p fn is **not** rejected here, and cannot be: under GCC's own
    /// UndefinedBehaviorSanitizer (`-fsanitize=undefined`, this project's
    /// default `Asan` config) the address of a function is not usable in a
    /// constant expression as an *operand of a comparison* -- `fn == nullptr`
    /// alone makes any enclosing `static_assert` fail to evaluate, even
    /// though calling through the same pointer is perfectly
    /// constant-evaluable. A registration is exactly where that comparison
    /// *would* be evaluated during constant evaluation (a `constexpr`
    /// vocabulary is built by calling this function), so a check here would
    /// make every compile-time FFI session uncompilable in the configuration
    /// this project builds in.
    ///
    /// It is @ref call that diagnoses a null implementation instead, where
    /// the check can be arranged never to be evaluated at compile time -- so
    /// nothing is left undiagnosed, only moved to the one place it can be
    /// checked. See that function's own doc comment and DIV-0029.
    constexpr auto add(function_type fn) -> foundation::result<int>;

    /// The number of functions registered so far.
    [[nodiscard]] constexpr auto size() const -> int;

    /// The function registered at @p index.
    /// @pre 0 <= index < size()
    [[nodiscard]] constexpr auto function_at(int index) const -> function_type;

    /// Calls the function registered at @p index against @p state, returning
    /// whatever it reports. Diagnoses an out-of-range @p index rather than
    /// indexing past @ref size, and a null registered implementation rather
    /// than calling through it (D7: all misuse is a diagnosed error via
    /// @ref foundation::result, never UB).
    ///
    /// The null check is deliberately spelled
    /// `!std::is_constant_evaluated() && fn == nullptr`, and the order
    /// matters. A bare `fn == nullptr` would be a *non-constant condition*
    /// under GCC's own UndefinedBehaviorSanitizer (see @ref add) and would
    /// break every compile-time FFI session; short-circuiting means the
    /// comparison is simply never evaluated during constant evaluation, so
    /// the obstruction never fires, while at ordinary runtime it is an
    /// ordinary null check. Nothing is lost at compile time: a genuinely
    /// null pointer reached during constant evaluation falls through to the
    /// call below, and calling through a null function pointer is not a
    /// constant expression -- a hard compile error, which is a strictly
    /// better diagnosis than a returned @ref foundation::result would be.
    [[nodiscard]] constexpr auto call(int index, state_type &state) const
        -> status;

  private:
    foundation::static_vector<function_type, MaxForeign> functions_{};
};
```

"Direct access to the underlying stacks, data space, and output buffer" is what I wanted going in, and it's what this gives: a foreign word reads and writes the same two stacks a primitive does, through the same handle, with no marshalling step translating a Forth cell into something else and back. Push, pop, done.

What the signature alone couldn't tell me was where the pointer goes once a word is registered. My first instinct was the obvious one: put it right on the dictionary entry, next to a constant's value or a variable's address, and it doesn't type-check. The machine state a foreign function takes is parameterized on four capacities; the dictionary is parameterized on two, and neither of those two is a state capacity. A dictionary entry, as this project has built it since the fifth entry, has no way to **name** the type of the pointer it would need to hold. So the pointers live one hop away, in a flat registry sized independently of the dictionary, and a foreign word's own dictionary entry carries nothing but an index into it.


# A dictionary entry like any other

That index is the whole binding, plus the one thing the effect checker needs and nowhere else has a slot for:

```cpp
/// A foreign-function binding (step F34, D18/D20; the slot F9 reserved and
/// R1's own F19 named): @ref index is a handle into a @ref
/// machine::foreign_vocabulary "foreign vocabulary"
/// (`machine/foreign.hpp`), the flat registry that actually carries the
/// `status (*)(forth_state &)` function pointers.
///
/// The pointer itself cannot live here: @ref forth_state is
/// capacity-parameterized and @ref dictionary is not, so this header has no
/// way to name the function-pointer type. @ref machine::foreign_dictionary
/// is what keeps the two structures' own indices in agreement by construction
/// (one `with_foreign` call appends to both); DIV-0029 records the design,
/// including the two rejected alternatives.
///
/// @ref effect_known / @ref effect_inputs / @ref effect_outputs are D20's own
/// *optional declared* effect, carried here rather than on the registry entry
/// because F30's own effect checker (`interpreter::effect_lint.hpp`) only
/// ever has the dictionary to consult: undeclared (the default) is the
/// `unknown` lattice value `EXECUTE`/`CATCH` already get; a declared effect
/// participates in the lint exactly like a @ref compiled_colon_word's own
/// computed one. Deliberately mirrors @ref compiled_colon_word's own three
/// effect fields rather than inventing a second spelling.
struct foreign_word {
    int index = -1;
    bool effect_known = false;
    int effect_inputs = 0;
    int effect_outputs = 0;

    friend constexpr auto operator==(foreign_word const &, foreign_word const &)
        -> bool = default;
};
```

An undeclared foreign word gets the same `unknown` lattice value `EXECUTE` and `CATCH` already carry through the checker built four entries back; a declared `( a b -- c )` is trusted and folds into a caller's own computed effect exactly the way a colon word's does. Nothing new had to be taught to the checker. It was already asking "does this instruction have a known effect," and a foreign word answering "yes, two in, one out" is just one more truthful answer to a question it already knew how to ask.

Two structures, one call: registering a function appends to the registry and installs this header in the same step, so the two can never drift out of agreement with each other. I'd rather have that invariant hold by construction than write a test that checks it holds.


# One opcode, and the failure path it borrows

Dispatching a foreign word is one new case in the VM's own switch, and the interesting part isn't the call itself. It's what happens when the call fails:

```cpp
case op::foreign: {
    // The foreign function interface (step F34, D18): call the
    // registered C++ function whose index this instruction carries,
    // against the very same @p state every primitive already gets --
    // no marshalling, no separate calling convention, and the same
    // @ref status channel, so a foreign word's own failure is
    // diagnosed and (below) routed through the identical
    // machine-fault/`ABORT"` mapping `op::prim` uses. That last part
    // is what makes `CATCH` work over a foreign word for free:
    // whatever a foreign word diagnoses is caught exactly like a
    // primitive's own diagnosis would be.
    if (foreign == nullptr) {
        return foundation::parse_error{
            foundation::source_pos{},
            "foreign word: no vocabulary available to this VM run"};
    }
    auto r = foreign->call(static_cast<int>(in.operand), state);
    if (!r.has_value()) {
        if (is_abort_quote_condition(r.error())) {
            auto th = perform_throw(state, cell{-2}, ip);
            if (!th.has_value()) {
                return th;
            }
            break;
        }
        if (state.handler_depth() >= 0) {
            auto mapped = machine_fault_throw_code(r.error());
            if (mapped.has_value()) {
                auto th = perform_throw(state, mapped.value(), ip);
                if (!th.has_value()) {
                    return th;
                }
                break;
            }
        }
        return r;
    }
    ++ip;
    break;
}
```

That's the primitive's own failure path, character for character: `ABORT"`'s distinguished condition always routes through `THROW -2`, and everything else maps to its standard Forth-2012 throw code when a handler is active. I didn't write a second version of that mapping for foreign words. I called the first one from one more place. Which means `CATCH`, landed five entries back to unwind an arbitrary mix of call frames and loop frames off the return stack, already knows how to unwind out of a foreign word's own failure: an empty-stack pop inside `GCD` becomes a caught `-4`, with no code anywhere that says "and also handle the foreign case."

The same held for everything else I expected to need a new branch. `resolve_execution_token`, built seven entries back to give every resolvable word a real code-space address, gained one more case: a foreign word gets the identical guarded stub a primitive gets, one instruction long, the opcode above sitting where a primitive's own call would. That's what makes `' EXECUTE` work on a foreign word the same day it was written, no different from taking the token of any other word, and it's the reason the stub mechanism was worth building general in the first place. I said at the time that almost everything in the dictionary had something to point at, and the thing that didn't wasn't a gap so much as the shape of what a control word is. A foreign word was never going to be a control word. It just needed to join the everything.


# The comparison that turns out not to be one

Registering a function is a runtime concern and a compile-time one both, because `constexpr` is the whole point of this project, and I wanted a full session (foreign calls included) to build inside one constant evaluation the way every other session has. Registering a null function pointer is the kind of mistake this project's own discipline says must be diagnosed, never left as a silent trap for later.

I wrote that check where it seemed to belong, at registration, and GCC's own UndefinedBehaviorSanitizer (part of this project's default build, not an optional extra) refused to constant-evaluate it. Not the call through the pointer; that's fine, always was. The **comparison**. `fn == nullptr` on a function pointer, sitting inside a `static_assert`, fails with "is not a constant expression," for a reason that has nothing to do with whether the pointer is actually null: under that sanitizer, the address of a function is not usable in a constant expression as an operand of an equality comparison at all. Every compile-time session with a foreign word in it would have stopped compiling, on a line that looks, read cold, like the single most harmless line in the file.

I didn't argue with the sanitizer. I moved the check to the one place the comparison doesn't have to run at compile time: the call itself, not the registration, spelled so that short-circuiting keeps the comparison out of constant evaluation entirely, `!std::is_constant_evaluated() && fn == nullptr`. At runtime it's an ordinary null check, reported through the same channel an out-of-range index already uses. At compile time the guard is simply never true, so the comparison never runs, and a pointer that really is null falls through to the call itself, which is not a constant expression either, for the ordinary reason calling through a null pointer never is. That's a harder failure than a diagnosed result, not a softer one. Nothing is left undiagnosed; the check just isn't where I would have put it if the toolchain hadn't had an opinion.

I confirmed it wasn't a fluke by compiling both spellings side by side under the same flags: the bare comparison fails to constant-evaluate, the guarded one doesn't. I'd rather have that be one paragraph in this entry than something the next person building this project rediscovers by staring at an error message that names a line with no null pointer anywhere near it.


# A criterion I wrote for both backends and could only run one through

I'd wanted, going into tonight, the same proof this project has leaned on since the second executor landed two entries back: a `static_assert` that computes through a foreign word twice, once on each backend, and agrees with itself. Half of that is what happened: three separate ~static\_assert~s, at the raw opcode, through the text interpreter, and through the public entry point, all computing a foreign call at compile time on the same fetch-execute loop that has run every other program in this project since the machine first existed.

The other half can't exist as written, and I already knew why before I opened the sender header: two entries back I confirmed that the primitive driving the sender backend's own execution, the thing that actually starts a chain of senders running, isn't usable in a constant expression itself. It throws; it uses a run loop; none of that constant-evaluates, and nothing about tonight's foreign words changes that fact one bit. A `static_assert` through the sender backend was never on the table, for the foreign case any more than for any other.

So the second half is a runtime check instead, honestly labeled as one: compile a word with a foreign call inside it once, run the same instruction range through both executors at ordinary runtime, and compare the final states. I did it twice: once with the foreign call sitting plainly in the open, once with it inside a `CATCH`-protected region, since a handler watching the return stack from one layer up is the kind of thing that could disagree quietly between two different ways of running the same program. Both agree. I'd rather have written "the criterion I set for myself doesn't fit the thing I already proved," and read it back, than let a runtime check silently stand in for a compile-time one without saying so.


# Both directions at once

None of the above is worth much without something that actually runs, so here's the whole interface at once: a compile-time call out to C++, a foreign word that behaves differently depending on whether the constant evaluator or an ordinary runtime is asking, and the same compiled session handed real arguments a second time, after the program has already started:

```cpp
/// The one `forth_state` shape this example's foreign words are typed on --
/// a foreign function pointer names its state's four capacities, so the
/// vocabulary, the session build, and every later runtime re-run must all
/// agree on them (`machine/foreign.hpp`).
using ffi_state = forth::machine::forth_state<64, 64, 1024, 4096>;

/// `GCD` ( a b -- gcd ) -- ordinary C++, with direct access to the data
/// stack. `constexpr`, so it runs during a compile-time session exactly like
/// a built-in primitive; every misuse it can hit (an empty stack here) is
/// reported through the same `machine::status` channel a primitive uses,
/// never as undefined behavior.
constexpr auto gcd_word(ffi_state &state) -> forth::machine::status {
    auto b = state.data().pop();
    if (!b.has_value()) {
        return b.error();
    }
    auto a = state.data().pop();
    if (!a.has_value()) {
        return a.error();
    }
    forth::machine::cell x = a.value() < 0 ? -a.value() : a.value();
    forth::machine::cell y = b.value() < 0 ? -b.value() : b.value();
    while (y != 0) {
        forth::machine::cell const t = x % y;
        x = y;
        y = t;
    }
    return state.data().push(x);
}

/// `TRACE` ( n -- n ) -- prints the top of the stack without consuming it,
/// but only when it is actually running at runtime. During the constant
/// evaluation that builds the session, `std::is_constant_evaluated()` is
/// true and this does nothing at all; the very same compiled instruction,
/// executed again from `main`, prints.
constexpr auto trace_word(ffi_state &state) -> forth::machine::status {
    auto top = state.data().peek(0);
    if (!top.has_value()) {
        return top.error();
    }
    if (!std::is_constant_evaluated()) {
        std::println("[trace] top of stack = {}", top.value());
    }
    return std::monostate{};
}

/// The vocabulary, registered pre-session: `machine::default_dictionary`
/// plus two foreign words, one with a declared `( a b -- c )` effect that
/// the F30 effect lint then checks like any other word's, one undeclared
/// (D20's `unknown`).
constexpr auto vocabulary =
    forth::machine::default_foreign_dictionary<256, 32, 4, 64, 64, 1024, 4096>()
        .with_foreign("GCD", &gcd_word, forth::machine::declared_effect(2, 1))
        .value()
        .with_foreign("TRACE", &trace_word)
        .value();

/// One `constexpr` initialization: compile `GCD3`, then run it. Every `GCD`
/// call below happens while this line is being evaluated, at compile time.
constexpr auto program =
    forth::compiled_forth_with(": GCD3 ( a b c -- d ) GCD TRACE GCD ;  "
                               "1071 462 210 GCD3",
                               vocabulary)
        .value();

/// The same source, kept as a session so `main` can run `GCD3` again with
/// arguments chosen at runtime (the reverse direction).
constexpr auto session =
    forth::interpreter::build_session_with_prelude<4096, 256, 1024, 4096, 32,
                                                   64, 64, 64, 8192, 4>(
        ": GCD3 ( a b c -- d ) GCD TRACE GCD ;", 100000, &vocabulary)
        .value();

auto main() -> int {
    // 1. The compile-time result, already computed. Note that no "[trace]"
    //    line was printed while building it.
    auto const built = program.stack();
    if (built.size() != 1) {
        std::println("unexpected stack depth: {}", built.size());
        return 1;
    }
    std::println("GCD3(1071, 462, 210) = {}  (computed at compile time)",
                 built[0]);

    // 3. The reverse direction: arguments from ordinary C++ variables, run
    //    the already-compiled word, read the result back. TRACE prints this
    //    time, from inside the running Forth word.
    int const a = 1998;
    int const b = 918;
    int const c = 486;

    auto image = session;
    ffi_state state{};
    if (!forth::interpreter::seed_from_session(image, state).has_value()) {
        std::println("could not seed the data space");
        return 1;
    }
    if (!state.data().push(a).has_value() ||
        !state.data().push(b).has_value() ||
        !state.data().push(c).has_value()) {
        std::println("could not push arguments");
        return 1;
    }
    auto ran = forth::interpreter::call_defined_word(
        image, state, "GCD3", 100000, &vocabulary.foreigns);
    if (!ran.has_value()) {
        std::println("GCD3 failed: {}", ran.error().message);
        return 1;
    }
    if (state.data().depth() != 1) {
        std::println("unexpected stack depth: {}", state.data().depth());
        return 1;
    }
    std::println("GCD3({}, {}, {}) = {}  (computed at runtime)", a, b, c,
                 state.data().peek().value());

    return (built[0] == 21 && state.data().peek().value() == 54) ? 0 : 1;
}
```

`GCD` is ordinary C++, `constexpr`, doing nothing a compiled colon word couldn't in principle do by itself. It's here because it's a clean, small example of C++ doing work Forth is handing off to it, not because the arithmetic needed C++'s help. `TRACE` is the more interesting of the two, and it's the whole reason `std::is_constant_evaluated()` shows up at all: the same compiled instruction is silent while the session is being built, because printing isn't something a constant evaluation can do in the first place, and chatty the moment that identical instruction runs again at ordinary runtime. One word, two behaviors, and neither one is a special case anywhere in the compiler; the branch lives inside the foreign function itself.

The reverse direction is the one I actually wanted the most going in, and it needed nothing new: a compiled session has been a trivially copyable value since the eighth entry, so `main` pushes three ordinary `int~s picked at runtime, calls the already-compiled ~GCD3` a second time, and reads the answer back off the stack. What it has to supply again, and what a session image never carries on its own, is the vocabulary itself: a session remembers a foreign word's **header**, never the function pointer behind it, so the program embedding this Forth hands the registry back in at every re-run, typed on the same four capacities the pointers were registered against. That asymmetry isn't a bug I'm noting for later. It's the same reason the registry couldn't live in the dictionary in the first place: a session is data, meant to outlive the C++ that built it, and a function pointer from one process has no business surviving into another one.


# What's still fenced off

One constraint came along for free with the design and stays written down rather than closed. The second executor decides, per compiled word, whether it's safe to run natively or whether it has to fall back to a slower, more careful path, and that decision is made by scanning a word's own instructions for anything that touches the return stack directly. A foreign call isn't an instruction that manipulates the return stack; it's a call out to C++, opaque to that scan by construction. Which means a foreign word that reached into the return stack from the C++ side (nothing in tonight's interface stops it from trying) could get lowered as if it were safe when it isn't, and the two executors could quietly disagree. Nothing in what I actually needed to build called for a foreign word shaped like that, so I haven't built the flag that would let the scan see it coming. I'm naming the gap instead of pretending it isn't there.

What I did get, and didn't expect walking in: a whole interface between this compiler and ordinary C++ that added exactly one opcode, one registry, and zero new cases in every mechanism that already existed to handle a resolvable word. The plan asked for a function pointer callable from Forth. What made it land this cleanly wasn't anything special about foreign functions: it's that the last several entries kept the design general enough that a kind of word nobody had written yet still had somewhere to fit.

<nav style="margin-top: 3em; border-top: 1px solid #ccc; padding-top: 1em">

[↑ Series Index](index.md) | [← Part 23 - Renamed, Not Reimplemented](post-23-renamed-not-reimplemented.md) | [Part 25 - Everything I Didn't Build →](post-25-everything-i-didnt-build.md)

</nav>


# References
