// src/smd/forth/parser/parser_ops.hpp                               -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Adapted by copy from compile-time-scheme (smd::smdscheme):
// src/smd/smdscheme/parser/parser_ops.hpp
//
// DIV-0003 (docs/divergences/DIV-0003-parser-foundation-typeclass.md):
// the Scheme reference implementation defined its own local
// ParserApplicative/ParserAlternative CRTP layers and its own local
// parser_typeclass<T> lookup variable, entirely separate from
// smd::smdscheme::foundation's functor/applicative/alternative machinery.
// This project instead derives the parser typeclass-object directly from
// smd::forth::foundation::{derive_functor,derive_applicative,
// derive_alternative}, and registers parser<F> against foundation's own
// functor/applicative/alternative lookup variables, so
// foundation::fmap/foundation::invoke/foundation::alt dispatch to parsers
// through the same operation objects every other registered type uses.
#ifndef SRC_SMD_FORTH_PARSER_PARSER_OPS_HPP
#define SRC_SMD_FORTH_PARSER_PARSER_OPS_HPP

#include <smd/forth/foundation/alternative.hpp>
#include <smd/forth/foundation/applicative.hpp>
#include <smd/forth/foundation/functor.hpp>
#include <smd/forth/foundation/parse_error.hpp>
#include <smd/forth/foundation/typeclass_base.hpp>
#include <smd/forth/parser/alt.hpp>
#include <smd/forth/parser/parser.hpp>

#include <type_traits>
#include <utility>

namespace smd::forth::parser {

/// Layer 1: Impl — primitives that delegate to the free functions in
/// @c parser.hpp and @c alt.hpp. An alternate Impl could provide a
/// different parsing strategy while still plugging into the same
/// foundation CRTP bases.

/// Functor primitive for parsers: @c fmap.
struct parser_functor_impl {
    /// Applies @p f to the value produced by @p p.
    template <class F, class P>
    [[nodiscard]] constexpr auto fmap(this auto &&, F f, P p) {
        return ::smd::forth::parser::map(p, f);
    }
};

/// Applicative primitives for parsers: @c pure and @c apply (@c lift2
/// -derived).
///
/// This is Layer 1 of the typeclass-object pattern for parsers, feeding
/// @ref smd::forth::foundation::derive_applicative
/// "foundation::derive_applicative<Impl>" (Layer 2), which derives
/// @c invoke/@c lift_a2/@c ap/@c discard_first/@c discard_second from these
/// two primitives via the terminating-partial-application technique.
struct parser_applicative_impl {
    /// Succeeds unconditionally, yielding @p value and consuming no input.
    template <class T>
    [[nodiscard]] constexpr auto pure(this auto &&, T value) {
        return ::smd::forth::parser::pure(value);
    }

    /// Applies the parser-wrapped function @p pf to the value produced by
    /// @p pa, via @c lift2.
    template <class PF, class PA>
    [[nodiscard]] constexpr auto apply(this auto &&, PF pf, PA pa) {
        return ::smd::forth::parser::lift2(
            pf, pa, [](auto fn, auto val) { return fn(val); });
    }
};

/// Alternative primitive for parsers: @c alt, plus a per-value-type
/// @c zero.
///
/// @c foundation::derive_alternative<Impl> names @c zero and @c alt as its
/// basis (the Scheme reference's local parser typeclass provided only
/// @c alt; see DIV-0003). The identity is spelled @c zero, not @c empty:
/// in the kit @c empty is Foldable's predicate, and one namespace holds
/// the whole typeclass family.
struct parser_alternative_impl {
    /// Tries @p pa; if it fails without consuming input, tries @p pb.
    template <class PA, class PB>
    [[nodiscard]] constexpr auto alt(this auto &&, PA pa, PB pb) {
        return ::smd::forth::parser::alt(pa, pb);
    }

    /// Returns the identity element for @c alt: a parser of value type
    /// @p T that always fails, consuming no input.
    ///
    /// @c parser<F> is a family of types parameterized by the wrapped
    /// callable, not one concrete container type, so — unlike a concrete
    /// container's nullary @c zero() — this member cannot be reached
    /// through @ref smd::forth::foundation::zero "foundation::zero_fn":
    /// that operation object's calling convention (@c TC.zero(), no
    /// arguments) has nothing to deduce @p T from, and for the same reason
    /// this Impl does not satisfy @c foundation::alternative_impl, whose
    /// basis probe is the nullary @c impl.zero(). Call
    /// @code parser_v.template zero<T>() @endcode directly instead.
    template <class T>
    [[nodiscard]] constexpr auto zero(this auto &&) {
        return parser{[](cursor cur) -> parse_result<T> {
            return foundation::parse_error{cur.position(), "empty alternative"};
        }};
    }
};

/// Combined parser operations object: Layer 2, derived from
/// @c foundation::derive_functor, @c foundation::derive_applicative, and
/// @c foundation::derive_alternative over the Layer 1 @c Impl types above.
///
/// This is the "Map" in the typeclass-object pattern. Public inheritance
/// from all three foundation CRTP bases exposes their full derived
/// surfaces directly: @c fmap/@c replace, @c pure/@c apply/@c invoke/
/// @c lift_a2/@c ap/@c discard_first/@c discard_second, and
/// @c alt/@c combine/@c zero. The using-declarations re-expose each
/// Impl's own primitives at this level, the way the kit's own instance
/// maps do (@c result_functor_map and friends); the own-member rule
/// forbids them in a base, not in the map that closes the chain. The
/// repetition and whitespace combinators (@c many, @c some, @c optional,
/// @c lexeme) have no foundation counterpart and are added directly,
/// mirroring @c alt.hpp's free functions.
struct parser_ops : foundation::derive_functor<parser_functor_impl>,
                    foundation::derive_applicative<parser_applicative_impl>,
                    foundation::derive_alternative<parser_alternative_impl> {
    using parser_alternative_impl::alt;
    using parser_alternative_impl::zero;
    using parser_applicative_impl::apply;
    using parser_applicative_impl::pure;
    using parser_functor_impl::fmap;

    /// Applies @p p zero or more times, collecting up to @c Capacity
    /// results.
    template <int Capacity, class P>
    [[nodiscard]] constexpr auto many(this auto &&, P p) {
        return ::smd::forth::parser::many<Capacity>(p);
    }

    /// Applies @p p one or more times; fails if @p p does not match at
    /// least once.
    template <int Capacity, class P>
    [[nodiscard]] constexpr auto some(this auto &&, P p) {
        return ::smd::forth::parser::some<Capacity>(p);
    }

    /// Tries @p p, succeeding with @c std::nullopt if it fails without
    /// consuming.
    template <class P>
    [[nodiscard]] constexpr auto optional(this auto &&, P p) {
        return ::smd::forth::parser::optional(p);
    }

    /// Wraps @p p so it skips surrounding inter-token whitespace.
    template <class P>
    [[nodiscard]] constexpr auto lexeme(this auto &&, P p) {
        return ::smd::forth::parser::lexeme(p);
    }
};

/// A global, default-constructed @c parser_ops instance for direct use.
inline constexpr parser_ops parser_v{};

} // namespace smd::forth::parser

namespace smd::forth::foundation {

/// The element type of a @c parser<F> — the @c T of the @c parse_result<T>
/// its callable returns — for the kit's @c element_type trait, which the
/// @c *_impl and @c *_object concepts key on. @c parser<F> carries no
/// @c value_type of its own: the datatype does not know it is a Functor,
/// and the adaptation is where that knowledge lives.
template <class F>
struct element_type<::smd::forth::parser::parser<F>> {
    using type =
        decltype(std::declval<typename std::invoke_result_t<
                     F const &, ::smd::forth::parser::cursor>::value_type>()
                     .value);
};

/// Registers every @c parser<F> as having @ref smd::forth::parser::parser_ops
/// Functor behavior.
template <class F>
inline constexpr auto functor<::smd::forth::parser::parser<F>> =
    ::smd::forth::parser::parser_ops{};

/// Registers every @c parser<F> as having @ref smd::forth::parser::parser_ops
/// Applicative behavior.
template <class F>
inline constexpr auto applicative<::smd::forth::parser::parser<F>> =
    ::smd::forth::parser::parser_ops{};

/// Registers every @c parser<F> as having @ref smd::forth::parser::parser_ops
/// Alternative behavior.
template <class F>
inline constexpr auto alternative<::smd::forth::parser::parser<F>> =
    ::smd::forth::parser::parser_ops{};

} // namespace smd::forth::foundation

#endif
