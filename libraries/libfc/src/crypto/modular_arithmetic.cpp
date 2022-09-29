#include <gmp.h>
#include <fc/crypto/modular_arithmetic.hpp>
#include <algorithm>

namespace fc {

    std::variant<modular_arithmetic_error, bytes> modexp(const bytes& _base, const bytes& _exponent, const bytes& _modulus)
    {
        if (_modulus.size() == 0) {
            return modular_arithmetic_error::modulus_len_zero;
        }

        auto output = bytes(_modulus.size(), '\0');

        mpz_t base, exponent, modulus;
        mpz_inits(base, exponent, modulus, nullptr);

        if (_base.size()) {
            mpz_import(base, _base.size(), 1, 1, 0, 0, _base.data());
        }

        if (_exponent.size()) {
            mpz_import(exponent, _exponent.size(), 1, 1, 0, 0, _exponent.data());
        }

        mpz_import(modulus, _modulus.size(), 1, 1, 0, 0, _modulus.data());

        if (mpz_sgn(modulus) == 0) {
            mpz_clears(base, exponent, modulus, nullptr);
            return output;
        }

        mpz_t result;
        mpz_init(result);

        mpz_powm(result, base, exponent, modulus);
        // export as little-endian
        mpz_export(output.data(), nullptr, -1, 1, 0, 0, result);
        // and convert to big-endian
        std::reverse(output.begin(), output.end());

        mpz_clears(base, exponent, modulus, result, nullptr);

        return output;
    }

}
