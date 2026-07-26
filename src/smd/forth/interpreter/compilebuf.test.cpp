// src/smd/forth/interpreter/compilebuf.test.cpp                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/interpreter/compilebuf.hpp>
#include <smd/forth/interpreter/compilebuf.hpp> // test 2nd include OK

#include <smd/forth/foundation/source_pos.hpp>
#include <smd/forth/machine/cell.hpp>
#include <smd/forth/machine/forth_state.hpp>
#include <smd/forth/machine/instruction.hpp>

#include <catch2/catch_test_macros.hpp>

using smd::forth::foundation::source_pos;
using smd::forth::interpreter::call_word;
using smd::forth::interpreter::compile_buffer;
using smd::forth::machine::op;
using smd::forth::machine::primitive;

TEST_CASE("CompileBufTest - HeaderIsIdempotent") { REQUIRE(true); }

TEST_CASE("CompileBufTest - FreshBufferReservesHaltPad") {
    compile_buffer<64, 32> buf;
    CHECK(buf.halt_pad() == 0);
    CHECK(buf.here() == 1);
    REQUIRE(buf.program().code.size() == 1);
    CHECK(buf.program().code[0].code == op::halt);
}

TEST_CASE("CompileBufTest - EmitAppendsAtHere") {
    compile_buffer<64, 32> buf;
    int const entry = buf.here();
    auto r = buf.emit(op::push, 42, source_pos{});
    REQUIRE(r.has_value());
    CHECK(r.value() == entry);
    CHECK(buf.here() == entry + 1);
    CHECK(buf.program().code[static_cast<std::size_t>(entry)].operand == 42);
}

TEST_CASE("CompileBufTest - CallWordRunsAgainstLiveState") {
    compile_buffer<64, 32> buf;
    int const entry = buf.here();
    REQUIRE(buf.emit(op::prim,
                     static_cast<smd::forth::machine::cell>(primitive::dup),
                     source_pos{})
                .has_value());
    REQUIRE(buf.emit(op::prim,
                     static_cast<smd::forth::machine::cell>(primitive::star),
                     source_pos{})
                .has_value());
    REQUIRE(buf.emit(op::ret, 0, source_pos{}).has_value());

    smd::forth::machine::forth_state<64, 64, 1024, 256> state{};
    REQUIRE(state.data().push(4).has_value());
    auto r = call_word(buf, state, entry, 1000);
    REQUIRE(r.has_value());
    REQUIRE(state.data().depth() == 1);
    CHECK(state.data().peek().value() == 16);
}

TEST_CASE("CompileBufTest - CallWordDoesNotGrowDataSpace") {
    // call_word must not re-seed the live state's own data space on every
    // invocation (compile_buffer's own program.data_space_size stays 0) --
    // otherwise calling the same word repeatedly would grow data space
    // without bound.
    compile_buffer<64, 32> buf;
    int const entry = buf.here();
    REQUIRE(buf.emit(op::ret, 0, source_pos{}).has_value());

    smd::forth::machine::forth_state<64, 64, 64, 256> state{};
    CHECK(state.data_space().size() == 0);
    REQUIRE(call_word(buf, state, entry, 1000).has_value());
    REQUIRE(call_word(buf, state, entry, 1000).has_value());
    REQUIRE(call_word(buf, state, entry, 1000).has_value());
    CHECK(state.data_space().size() == 0);
}
