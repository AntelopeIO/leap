#pragma once

#include <cstdint>
#include <vector>
#include <variant>

namespace fc {
    using bytes = std::vector<char>;

    enum class modular_arithmetic_error : int32_t {
        modulus_len_zero,
    };

    std::variant<modular_arithmetic_error, bytes> modexp(const bytes& _base, const bytes& _exponent, const bytes& _modulus);
}
