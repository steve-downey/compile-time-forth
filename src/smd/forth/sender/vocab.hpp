// src/smd/forth/sender/vocab.hpp                                    -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef INCLUDED_SMD_FORTH_SENDER_VOCAB
#define INCLUDED_SMD_FORTH_SENDER_VOCAB

#include <beman/execution26/execution.hpp>

/// Thin vocabulary aliases for Beman Execution26 senders.
///
/// Importing via `using` rather than `using namespace` keeps the
/// declarations explicit and avoids pulling the full Execution26 ADL surface
/// into downstream code.
///
/// Step F33 (docs/forth-plan-2.md), D24: `sender/lower.hpp` is this
/// component's first real consumer (this header existed, unused for its own
/// sake, since F2). The additions below are exactly what lowering needs:
/// `set_value`/`set_error`/`set_stopped` and the three `*_tag` concepts to
/// write @ref lower.hpp's own custom sender types directly against the
/// connect/start protocol (see `vendor/execution/examples/sender_demo.cpp`
/// for the same pattern this project follows); `upon_error`/`upon_stopped`
/// as the value-adapting combinators `CATCH` and the fuel-exhaustion path
/// are built from (D24: "CATCH as the error-to-value adapter"); `just_error`/
/// `just_stopped` for the handful of places a sender needs to *start*
/// already completed on the error/stopped channel rather than the value one.
namespace smd::forth::sender {
using beman::execution26::connect;
using beman::execution26::just;
using beman::execution26::just_error;
using beman::execution26::just_stopped;
using beman::execution26::let_value;
using beman::execution26::operation_state_tag;
using beman::execution26::receiver_tag;
using beman::execution26::sender_tag;
using beman::execution26::set_error;
using beman::execution26::set_error_t;
using beman::execution26::set_stopped;
using beman::execution26::set_stopped_t;
using beman::execution26::set_value;
using beman::execution26::set_value_t;
using beman::execution26::start;
using beman::execution26::sync_wait;
using beman::execution26::then;
using beman::execution26::upon_error;
using beman::execution26::upon_stopped;
using beman::execution26::when_all;
} // namespace smd::forth::sender

#endif // INCLUDED_SMD_FORTH_SENDER_VOCAB
