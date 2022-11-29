// Snark - Wrapper for alt_bn128 add mul pair and modexp

#pragma once

#include <functional>
#include <cstdint>
#include <variant>
#include <vector>
#include <fc/utility.hpp>

namespace fc {
    using bytes = std::vector<char>;

    enum class blake2b_error : int32_t {
        input_len_error
    };

    std::variant<blake2b_error, bytes> blake2b(uint32_t _rounds, const bytes& _h, const bytes& _m, const bytes& _t0_offset, const bytes& _t1_offset, bool _f, const yield_function_t& yield);

    struct blake2b_state {
        uint64_t h[8] = {0,0,0,0,0,0,0,0};
        uint64_t t[2] = {0,0};
        uint64_t f[1] = {0};
    };

    class blake2b_wrapper {
    public:
        enum blake2b_constant { BLAKE2B_BLOCKBYTES = 128 };
        void blake2b_compress(blake2b_state *S, const uint8_t block[BLAKE2B_BLOCKBYTES], size_t r, const yield_function_t& yield );
    
    private:
        uint64_t m[16];
        uint64_t v[16];
        size_t i;

        inline void G(uint8_t r, uint8_t i, uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d) noexcept;
        inline void ROUND(uint8_t r) noexcept;

        void blake2b_compress_init(blake2b_state *S, const uint8_t block[BLAKE2B_BLOCKBYTES], size_t r);
        void blake2b_compress_end(blake2b_state *S);
    };
}
