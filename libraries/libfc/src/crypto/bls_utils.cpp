#include <fc/crypto/bls_utils.hpp>

using namespace bls12_381;

namespace fc { 

  void to_variant(const bls12_381::g1& var, fc::variant& vo, const fc::yield_function_t& yield)
  {
      vo = bytesToHex<144>(var.toJacobianBytesLE(true));
  }

  void from_variant(const fc::variant& var, bls12_381::g1& vo)
  {
      vo = g1::fromJacobianBytesLE(hexToBytes(var.as_string()), false, true);
  }

  void to_variant(const bls12_381::g2& var, fc::variant& vo, const fc::yield_function_t& yield)
  {
      vo = bytesToHex<288>(var.toJacobianBytesLE(true));
  }

  void from_variant(const fc::variant& var, bls12_381::g2& vo)
  {
      vo = g2::fromJacobianBytesLE(hexToBytes(var.as_string()), false, true);
  }

  void to_variant(const bls12_381::fp& var, fc::variant& vo, const fc::yield_function_t& yield)
  {
      vo = bytesToHex<48>(var.toBytesLE(true));
  }

  void from_variant(const fc::variant& var, bls12_381::fp& vo)
  {
      vo = fp::fromBytesLE(hexToBytes(var.as_string()), false, true);
  }

  void to_variant(const bls12_381::fp2& var, fc::variant& vo, const fc::yield_function_t& yield)
  {
      vo = bytesToHex<96>(var.toBytesLE(true));
  }

  void from_variant(const fc::variant& var, bls12_381::fp2& vo)
  {
      vo = fp2::fromBytesLE(hexToBytes(var.as_string()), false, true);
  }

  void to_variant(const bls12_381::fp6& var, fc::variant& vo, const fc::yield_function_t& yield)
  {
      vo = bytesToHex<288>(var.toBytesLE(true));
  }

  void from_variant(const fc::variant& var, bls12_381::fp6& vo)
  {
      vo = fp6::fromBytesLE(hexToBytes(var.as_string()), false, true);
  }

  void to_variant(const bls12_381::fp12& var, fc::variant& vo, const fc::yield_function_t& yield)
  {
      vo = bytesToHex<576>(var.toBytesLE(true));
  }

  void from_variant(const fc::variant& var, bls12_381::fp12& vo)
  {
      vo = fp12::fromBytesLE(hexToBytes(var.as_string()), false, true);
  }

  void to_variant(const std::array<uint64_t, 4>& var, fc::variant& vo, const fc::yield_function_t& yield)
  {
      vo = bytesToHex<32>(scalar::toBytesLE<4>(var));
  }

  void from_variant(const fc::variant& var, std::array<uint64_t, 4>& vo)
  {
      vo = scalar::fromBytesLE<4>(hexToBytes(var.as_string()));
  }
}

