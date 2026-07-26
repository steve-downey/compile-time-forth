// src/smd/forth/interpreter/control_flow_corpus.hpp                 -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef SRC_SMD_FORTH_INTERPRETER_CONTROL_FLOW_CORPUS_HPP
#define SRC_SMD_FORTH_INTERPRETER_CONTROL_FLOW_CORPUS_HPP

#include <string_view>

namespace smd::forth::interpreter::corpus {

// Step F26 (docs/forth-plan-2.md, "the cut"): this header preserves the
// F13/F16/F17 program battery -- source text, plus its expected observable
// result recorded as a doc comment -- that lived only inside the R1-pipeline
// tests this step deletes (machine/vm.test.cpp, machine/eval_direct.test.cpp).
//
// F26's own retargeted compiled_forth<Source> (forth.hpp) cannot run most of
// these yet: every program below except the two memory-word ones uses
// control flow (IF/THEN, BEGIN/UNTIL/WHILE/REPEAT, DO/LOOP/+LOOP), and F25's
// interpret() has none -- those words are simply unknown to it (see this
// step's own DIV-0014). F27 ("immediacy and control flow") is what makes
// interpret() recognize them; F27's own merge criterion is that every
// program below still passes, verbatim, through the interpreter path once it
// does. Losing the *programs* here (as opposed to the R1-pipeline *tests*
// that exercised them, which correctly die with the pipeline they tested)
// would make that merge criterion unverifiable, so this header's only job is
// to keep the source strings and their expected results alive across the
// cut, not to predict the shape of F27's own test assertions against them.
//
// The two memory-word programs (VARIABLE/CONSTANT, CREATE/ALLOT) use no
// control flow and already pass through F26's own retargeted compiled_forth
// (forth.test.cpp's ForthTest - MemoryWordsMergeCriterion,
// interp.test.cpp's InterpTest - MemoryWordsMergeCriterion and
// - CreateAllotMergeCriterion). They are kept here too, so this corpus is
// the complete F13/F16/F17 set in one place, not one entry short of it.

/// `: ABS DUP 0< IF NEGATE THEN ;  -7 ABS` -- stack `[7]`.
inline constexpr std::string_view abs_program =
    ": ABS DUP 0< IF NEGATE THEN ;  -7 ABS";

/// `: COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  3 COUNTDOWN` -- stack
/// `[]`, output `"3 2 1 "`.
inline constexpr std::string_view countdown_program =
    ": COUNTDOWN BEGIN DUP . 1- DUP 0= UNTIL DROP ;  3 COUNTDOWN";

/// `: SPIN BEGIN FALSE UNTIL ; SPIN` -- never terminates; diagnoses budget
/// exhaustion under a small fuel (e.g. 10) rather than hanging.
inline constexpr std::string_view spin_program =
    ": SPIN BEGIN FALSE UNTIL ; SPIN";

/// `: UPTO3 0 BEGIN DUP 3 < WHILE 1 + REPEAT ; UPTO3` -- stack `[3]`.
/// BEGIN...WHILE...REPEAT as real repetition.
inline constexpr std::string_view upto3_program =
    ": UPTO3 0 BEGIN DUP 3 < WHILE 1 + REPEAT ; UPTO3";

/// `: INNER DUP 0< IF EXIT THEN DROP ; : OUTER INNER 99 ; -3 OUTER` -- stack
/// `[-3 99]` (bottom to top). EXIT unwinds only to its own call boundary.
inline constexpr std::string_view exit_boundary_program =
    ": INNER DUP 0< IF EXIT THEN DROP ; : OUTER INNER 99 ; -3 OUTER";

/// `: SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO` -- stack `[15]`.
inline constexpr std::string_view sumto_program =
    ": SUMTO 0 SWAP 1+ 0 DO I + LOOP ;  5 SUMTO";

/// `: FIND5 10 0 DO I 5 = IF I LEAVE THEN LOOP ;  FIND5` -- stack `[5]`.
inline constexpr std::string_view find5_program =
    ": FIND5 10 0 DO I 5 = IF I LEAVE THEN LOOP ;  FIND5";

/// `: TENS 0 3 0 DO 3 0 DO J + LOOP LOOP ;  TENS` -- stack `[9]`: nested
/// loops plus `J`, summing the outer index over 3x3 iterations.
inline constexpr std::string_view tens_program =
    ": TENS 0 3 0 DO 3 0 DO J + LOOP LOOP ;  TENS";

/// `: SUMEVEN 0 10 0 DO I + 2 +LOOP ;  SUMEVEN` -- stack `[20]`
/// (0+2+4+6+8), exercising `+LOOP`.
inline constexpr std::string_view sumeven_program =
    ": SUMEVEN 0 10 0 DO I + 2 +LOOP ;  SUMEVEN";

/// `: FIRST 10 0 DO I DUP DUP * 8 > IF UNLOOP EXIT THEN DROP LOOP -1 ;
/// FIRST` -- stack `[3]`: the first index whose square exceeds 8, left via
/// `UNLOOP EXIT` from inside a counted loop.
inline constexpr std::string_view first_program =
    ": FIRST 10 0 DO I DUP DUP * 8 > IF UNLOOP EXIT "
    "THEN DROP LOOP -1 ;  FIRST";

/// `VARIABLE X  5 X !  X @ 3 + X !  X @  7 CONSTANT LUCKY  LUCKY LUCKY +`
/// -- stack `[8 14]` (bottom to top); F16's own combined merge criterion.
/// No control flow.
inline constexpr std::string_view memory_words_program =
    "VARIABLE X  5 X !  X @ 3 + X !  X @ "
    "7 CONSTANT LUCKY  LUCKY LUCKY +";

/// `CREATE BUF 4 ALLOT 10 BUF ! 20 BUF 3 + ! BUF @ BUF 3 + @ +` -- stack
/// `[30]`. No control flow.
inline constexpr std::string_view create_allot_program =
    "CREATE BUF 4 ALLOT "
    "10 BUF ! 20 BUF 3 + ! "
    "BUF @ BUF 3 + @ +";

} // namespace smd::forth::interpreter::corpus

#endif
