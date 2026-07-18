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
namespace smd::forth::sender {
using beman::execution26::just;
using beman::execution26::let_value;
using beman::execution26::sync_wait;
using beman::execution26::then;
using beman::execution26::when_all;
} // namespace smd::forth::sender

#endif // INCLUDED_SMD_FORTH_SENDER_VOCAB
