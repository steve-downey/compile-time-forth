// src/smd/forth/foundation/source_span.hpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Adapted by copy from compile-time-scheme (smd::kit::foundation):
// src/smd/kit/foundation/source_span.hpp
// Extracted in step R8 from smd::cl::foundation::source_span, itself the
// reviewed union of two independently-drifted copies:
// src/smd/smdscheme/foundation/source_span.hpp (compile-time-scheme, at
// iteration/smdscheme-final) and src/smd/forth/foundation/source_span.hpp
// (compile-time-forth). Those two copies were identical apart from include
// guard, namespace, and comment — the zero-drift signal decision R8 acts on.
#ifndef SRC_SMD_FORTH_FOUNDATION_SOURCE_SPAN_HPP
#define SRC_SMD_FORTH_FOUNDATION_SOURCE_SPAN_HPP

#include <smd/forth/foundation/source_pos.hpp>

namespace smd::forth::foundation {

/// A half-open range in source text, defined by its first and last positions.
struct source_span {
    foundation::source_pos first{}; ///< Inclusive start of the span.
    foundation::source_pos last{};  ///< Exclusive end of the span.

    friend constexpr auto operator==(source_span, source_span)
        -> bool = default;
};

} // namespace smd::forth::foundation

#endif
