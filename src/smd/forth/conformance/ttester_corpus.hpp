// src/smd/forth/conformance/ttester_corpus.hpp                       -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Adapted (not copied verbatim) from John Hayes' TESTER.FR (1995, Johns
// Hopkins University/Applied Physics Laboratory, "may be distributed freely
// as long as this copyright notice remains") as revised by Anton Ertl into
// ttester.fs (2007, public domain); both found locally at
// /usr/share/gforth/0.7.3/test/ttester.fs. See DIV-0020 for exactly what
// this adaptation removes and why.
#ifndef SRC_SMD_FORTH_CONFORMANCE_TTESTER_CORPUS_HPP
#define SRC_SMD_FORTH_CONFORMANCE_TTESTER_CORPUS_HPP

#include <string_view>

// Step F32 (docs/forth-plan-2.md), D14/D23: "Hayes ttester compiled by the
// session itself" -- this header's whole job is to be Forth source text
// this project's own text interpreter compiles, not C++ that imitates
// T{/->/}T's behavior. DIV-0020 records the adaptation in full; the short
// version: upstream's own HAS-FLOATING-STACK conditional is always false
// here (D12: no floating point in scope at all), so every one of upstream's
// own floating-point branches collapses to the no-op upstream itself already
// defines for that case (`: F{ ; : F-> ; : F} ; : EMPTY-FSTACK ;` etc.) --
// this adaptation simply omits those calls rather than compiling no-op
// words this project has no `[IF]`/`ENVIRONMENT?` to select them with (both
// out of scope, D12). The `X.../R.../...}T` combinator family (`X}T`,
// `RRRX}T`, and so on) exists solely so one test can specify a mix of float
// and cell results sharing one stack; with no floats at all, that
// distinction cannot arise, so this adaptation keeps only the plain,
// unsuffixed `T{ ... -> ... }T`, which already compares every result as an
// ordinary cell. `ERROR1` also drops upstream's own `SOURCE TYPE` (this
// project has no `SOURCE` word yet, D14's own leg (b)/(c) work does not
// need one) and the `TESTING` word (a verbose-mode comment echoer built on
// `SOURCE`/`>IN`, neither needed nor buildable here) -- Catch2 test names
// serve `TESTING`'s own commentary purpose instead. Every remaining word
// below -- `T{`/`->`/`}T`, `EMPTY-STACK`, `ERROR`/`ERROR1` -- is the
// identical logic upstream defines, not a reimplementation: the zero-trip
// `DO` guard every loop below already has (`START-DEPTH @ > IF ... DO ...
// LOOP THEN`, never a bare `count 0 DO`) is upstream's own guard against
// Forth-2012's own "DO does not skip when limit equals index" rule, kept
// verbatim because dropping it would make the tester loop until fuel
// exhaustion on every test with zero saved results -- one of the more
// common shapes there is (see this header's own doc comment on
// SMD_FORTH_TTESTER_SOURCE below for where each guard sits).
//
// SMD_FORTH_TTESTER_SOURCE is a macro, not a `constexpr` variable, so it can
// be pasted via adjacent string-literal concatenation directly into another
// translation unit's own `compiled_forth<Source>` non-type template
// argument (each shard needs the *combined* text of ttester-plus-its-own-
// tests as a single literal at the point `compiled_forth` deduces `Source`'s
// own length; a `constexpr std::string_view` cannot be spliced into another
// literal that way, but two adjacent string literals are just one literal
// as far as the compiler's own token-level concatenation is concerned).
// Every shard file starts with `SMD_FORTH_TTESTER_SOURCE` immediately
// followed by its own literal, e.g.
// `smd::forth::compiled_forth<SMD_FORTH_TTESTER_SOURCE "T{ 1 1 + -> 2 }T">`.
// clang-format off
#define SMD_FORTH_TTESTER_SOURCE \
    "VARIABLE ACTUAL-DEPTH "                                                  \
    "CREATE ACTUAL-RESULTS 20 CELLS ALLOT "                                   \
    "VARIABLE START-DEPTH "                                                   \
    "VARIABLE ERROR-XT "                                                      \
    ": ERROR ERROR-XT @ EXECUTE ; "                                           \
    ": EMPTY-STACK "                                                          \
    "    DEPTH START-DEPTH @ < IF "                                          \
    "        DEPTH START-DEPTH @ SWAP DO 0 LOOP "                            \
    "    THEN "                                                              \
    "    DEPTH START-DEPTH @ > IF "                                          \
    "        DEPTH START-DEPTH @ DO DROP LOOP "                             \
    "    THEN ; "                                                            \
    ": ERROR1 "                                                              \
    "    TYPE CR "                                                           \
    "    EMPTY-STACK ; "                                                     \
    "' ERROR1 ERROR-XT ! "                                                   \
    ": T{ "                                                                  \
    "    DEPTH START-DEPTH ! ; "                                             \
    ": -> "                                                                  \
    "    DEPTH DUP ACTUAL-DEPTH ! "                                          \
    "    START-DEPTH @ > IF "                                                \
    "        DEPTH START-DEPTH @ - 0 DO ACTUAL-RESULTS I CELLS + ! LOOP "     \
    "    THEN ; "                                                            \
    ": }T "                                                                  \
    "    DEPTH ACTUAL-DEPTH @ = IF "                                         \
    "        DEPTH START-DEPTH @ > IF "                                      \
    "            DEPTH START-DEPTH @ - 0 DO "                                \
    "                ACTUAL-RESULTS I CELLS + @ "                            \
    "                <> IF S\" INCORRECT RESULT: \" ERROR LEAVE THEN "        \
    "            LOOP "                                                     \
    "        THEN "                                                         \
    "    ELSE "                                                              \
    "        S\" WRONG NUMBER OF RESULTS: \" ERROR "                         \
    "    THEN ; "
// clang-format on

namespace smd::forth::conformance {

/// The same text as @ref SMD_FORTH_TTESTER_SOURCE, as an ordinary
/// @c std::string_view -- for callers that want to run the ttester alone
/// through @ref interpreter::build_session at runtime (a `source_literal`
/// NTTP is fixed once deduced, so it cannot compose with a caller-chosen
/// runtime string the way @c build_session's own @c std::string_view
/// parameter can).
inline constexpr std::string_view ttester_source = SMD_FORTH_TTESTER_SOURCE;

} // namespace smd::forth::conformance

#endif
