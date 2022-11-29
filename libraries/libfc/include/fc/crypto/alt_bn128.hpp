#pragma once

#include <functional>
#include <cstdint>
#include <variant>
#include <vector>
#include <fc/utility.hpp>

namespace fc {
    using bytes = std::vector<char>;

    enum class alt_bn128_error : int32_t {
        operand_component_invalid,
        operand_not_in_curve,
        pairing_list_size_error,
        operand_outside_g2,
        input_len_error,
        invalid_scalar_size
    };

    std::variant<alt_bn128_error, bytes> alt_bn128_add(const bytes& op1, const bytes& op2); 
    std::variant<alt_bn128_error, bytes> alt_bn128_mul(const bytes& g1_point, const bytes& scalar);
    std::variant<alt_bn128_error, bool>  alt_bn128_pair(const bytes& g1_g2_pairs, const yield_function_t& yield);

} // fc
