#pragma once

#include "../../../libraries/bls12_381/include/bls12_381.hpp"
#include <fc/variant.hpp>

namespace fc {
    void to_variant(const bls12_381::g1& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());
    void from_variant(const variant& var, bls12_381::g1& vo);

    void to_variant(const bls12_381::g2& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());
    void from_variant(const variant& var, bls12_381::g2& vo);

    void to_variant(const bls12_381::fp& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());
    void from_variant(const variant& var, bls12_381::fp& vo);

    void to_variant(const bls12_381::fp2& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());
    void from_variant(const variant& var, bls12_381::fp2& vo);

    void to_variant(const bls12_381::fp6& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());
    void from_variant(const variant& var, bls12_381::fp6& vo);

    void to_variant(const bls12_381::fp12& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());
    void from_variant(const variant& var, bls12_381::fp12& vo);

    void to_variant(const std::array<uint64_t, 4>& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());
    void from_variant(const variant& var, std::array<uint64_t, 4>& vo);
} // namespace fc

FC_REFLECT(bls12_381::g1,   (x)(y)(z) )
FC_REFLECT(bls12_381::g2,   (x)(y)(z) )
FC_REFLECT(bls12_381::fp,   (d) )
FC_REFLECT(bls12_381::fp2,  (c0)(c1) )
FC_REFLECT(bls12_381::fp6,  (c0)(c1)(c2) )
FC_REFLECT(bls12_381::fp12, (c0)(c1) )

