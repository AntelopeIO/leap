// Snark - Wrapper for alt_bn128 add mul pair and modexp

#pragma once

#include <cstdint>
#include <variant>
#include <vector>

namespace fc {
    using bytes = std::vector<char>;

    enum class k1_recover_error : int32_t {
        init_error,
        input_error,
        invalid_signature,
        recover_error,
    };

    std::variant<k1_recover_error, bytes> k1_recover(const bytes& signature, const bytes& digest);
} // fc
