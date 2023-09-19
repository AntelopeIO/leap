#include <fc/crypto/bigint.hpp>
#include <fc/crypto/modular_arithmetic.hpp>

namespace fc {

   std::variant<modular_arithmetic_error, bytes> modexp(const bytes& _base, const bytes& _exponent, const bytes& _modulus) {
      if(_modulus.empty())
         return modular_arithmetic_error::modulus_len_zero;

      bigint base(_base);
      bigint exponent(_exponent);
      bigint modulus(_modulus);

      if(!modulus)
         return bytes(_modulus.size());

      return base.modexp(exponent, modulus).padded_be_bytes(_modulus.size());
   }
}
