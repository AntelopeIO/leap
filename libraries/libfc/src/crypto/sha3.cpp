#include <fc/crypto/hex.hpp>
#include <fc/crypto/hmac.hpp>
#include <fc/fwd_impl.hpp>
#include <openssl/sha.h>
#include <string.h>
#include <cmath>
#include <fc/crypto/sha3.hpp>
#include <fc/variant.hpp>
#include <fc/exception/exception.hpp>
#include "_digest_common.hpp"

namespace fc
{

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

#if defined(__BYTE_ORDER__)
#if defined(__ORDER_LITTLE_ENDIAN__)
inline constexpr bool is_little_endian = true;
#else
inline constexpr bool is_little_endian = false;
#endif
#else
#error "sha3 implementation needs __BYTE_ORDER__ and __ORDER_LITTLE/BIG_ENDIAN__ defined"
#endif

#if defined(__builtin_rotateleft64)
__attribute__ ((always_inline))
inline uint64_t rotl64(uint64_t x, uint64_t y) { return __builtin_rotateleft64(x, y); }
#else
// gcc should recognize this better than clang
__attribute__ ((always_inline))
inline uint64_t rotl64(uint64_t x, uint64_t y) { return (x << y) | (x >> (64-y)); }
#endif

#ifdef __clang__
   #define VECTORIZE_HINT _Pragma("clang loop vectorize(enable) interleave(enable)")
#else
   #define VECTORIZE_HINT
#endif

/* This implementation is an amalgam from tiny_sha3 (https://github.com/mjosaarinen/tiny_sha3) and
 * SHA3UIF (https://github.com/brainhub/SHA3UIF) and documentation.
 * This implementation is quite slow comparative to openssl 1.1.1. Once we require a version greater
 * than or equal to 1.1.1, we should replace with their primitives.
 */
struct sha3_impl {
	sha3_impl() { init(); }

	static constexpr uint8_t number_of_rounds = 24;
	static constexpr uint8_t number_of_words = 25;
	static constexpr uint8_t digest_size = 32;
	static constexpr uint64_t round_constants[number_of_rounds] = {
		 UINT64_C(0x0000000000000001), UINT64_C(0x0000000000008082), UINT64_C(0x800000000000808a), UINT64_C(0x8000000080008000),
		 UINT64_C(0x000000000000808b), UINT64_C(0x0000000080000001), UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008009),
		 UINT64_C(0x000000000000008a), UINT64_C(0x0000000000000088), UINT64_C(0x0000000080008009), UINT64_C(0x000000008000000a),
		 UINT64_C(0x000000008000808b), UINT64_C(0x800000000000008b), UINT64_C(0x8000000000008089), UINT64_C(0x8000000000008003),
		 UINT64_C(0x8000000000008002), UINT64_C(0x8000000000000080), UINT64_C(0x000000000000800a), UINT64_C(0x800000008000000a),
		 UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008080), UINT64_C(0x0000000080000001), UINT64_C(0x8000000080008008)};
	static constexpr uint8_t rot_constants[number_of_rounds] = {1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};
	static constexpr uint8_t pi_lanes[number_of_rounds] = {10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

// Disable  "-Wpass-failed=loop-vectorize" for `rho pi` and `chi` loops
#if defined(__clang__)
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wpass-failed"
#endif  
	void update_step()
	{
		uint64_t bc[5];

		if constexpr (!is_little_endian)
		{
			uint8_t *v;
			// convert the buffer to little endian
			for (std::size_t i; i < number_of_words; i++)
			{
				v = reinterpret_cast<uint8_t *>(words + i);
				words[i] = ((uint64_t)v[0]) | (((uint64_t)v[1]) << 8) |
							  (((uint64_t)v[2]) << 16) | (((uint64_t)v[3]) << 24) |
							  (((uint64_t)v[4]) << 32) | (((uint64_t)v[5]) << 40) |
							  (((uint64_t)v[6]) << 48) | (((uint64_t)v[7]) << 56);
			}
		}

		VECTORIZE_HINT for (std::size_t i = 0; i < number_of_rounds; i++)
		{
			// theta
			VECTORIZE_HINT for (std::size_t j = 0; j < 5; j++)
				bc[j] = words[j] ^ words[j + 5] ^ words[j + 10] ^ words[j + 15] ^ words[j + 20];

			uint64_t t;
			VECTORIZE_HINT for (std::size_t j = 0; j < 5; j++)
			{
				t = bc[(j + 4) % 5] ^ rotl64(bc[(j + 1) % 5], 1);
				VECTORIZE_HINT for (std::size_t k = 0; k < number_of_words; k += 5)
					words[k + j] ^= t;
			}

			// rho pi
			t = words[1];
			VECTORIZE_HINT for (std::size_t j = 0; j < number_of_rounds; j++)
			{
				uint8_t p = pi_lanes[j];
				bc[0] = words[p];
				words[p] = rotl64(t, rot_constants[j]);
				t = bc[0];
			}

			// chi
			VECTORIZE_HINT for (std::size_t j = 0; j < number_of_words; j += 5)
			{
				VECTORIZE_HINT for (std::size_t k = 0; k < 5; k++)
					bc[k] = words[k + j];
				VECTORIZE_HINT for (std::size_t k = 0; k < 5; k++)
					words[k + j] ^= (~bc[(k + 1) % 5]) & bc[(k + 2) % 5];
			}

			// iota
			words[0] ^= round_constants[i];
		}

		if constexpr (!is_little_endian)
		{
			uint8_t *v;
			uint64_t tmp;
			// convert back to big endian
			for (std::size_t i = 0; i < sizeof(words); i++)
			{
				v = (uint8_t *)(words + i);
				tmp = words[i];
				v[0] = tmp & 0xFF;
				v[1] = (tmp >> 8) & 0xFF;
				v[2] = (tmp >> 16) & 0xFF;
				v[3] = (tmp >> 24) & 0xFF;
				v[4] = (tmp >> 32) & 0xFF;
				v[5] = (tmp >> 40) & 0xFF;
				v[6] = (tmp >> 48) & 0xFF;
				v[7] = (tmp >> 56) & 0xFF;
			}
		}
	}
// Re-enable disabled warnings
#if defined(__clang__)
# pragma clang diagnostic pop
#endif

	void init() {
		memset((char *)this, 0, sizeof(*this));
		size = 136;
	}

	void update(const uint8_t* data, std::size_t len) {
		int j = point;
		for (std::size_t i = 0; i < len; i++)
		{
			bytes[j++] ^= data[i];
			if (j >= size)
			{
				update_step();
				j = 0;
			}
		}
		point = j;
	}

	void finalize(char* buffer) {
		bytes[point] ^= keccak ? 0x01 : 0x06;
		bytes[size-1] ^= 0x80;
		update_step();
	 memcpy(buffer, (const char*)bytes, digest_size);
	}

	union {
		uint8_t bytes[number_of_words*8];
		uint64_t words[number_of_words*5]; // this is greater than 25, because in the theta portion we need a wide berth
	};
	bool keccak = false;
	int point;
	int size;
};

sha3::sha3()
{
	memset(_hash, 0, sizeof(_hash));
}
sha3::sha3(const char *data, size_t size)
{
	if (size != sizeof(_hash))
		FC_THROW_EXCEPTION(exception, "sha3: size mismatch");
	memcpy(_hash, data, size);
}
sha3::sha3(const std::string &hex_str)
{
	auto bytes_written = fc::from_hex(hex_str, (char *)_hash, sizeof(_hash));
	if (bytes_written < sizeof(_hash))
		memset((char *)_hash + bytes_written, 0, (sizeof(_hash) - bytes_written));
}

std::string sha3::str() const
{
	return fc::to_hex((char *)_hash, sizeof(_hash));
}
sha3::operator std::string() const { return str(); }

const char *sha3::data() const { return (const char *)&_hash[0]; }
char *sha3::data() { return (char *)&_hash[0]; }

struct sha3::encoder::impl
{
	sha3_impl ctx;
};

sha3::encoder::~encoder() {}
sha3::encoder::encoder()
{
	reset();
}

void sha3::encoder::write(const char *d, uint32_t dlen)
{
	my->ctx.update((const uint8_t*)d, dlen);
}
sha3 sha3::encoder::result(bool is_nist)
{
	sha3 h;
	my->ctx.keccak = !is_nist;
	my->ctx.finalize((char*)h.data());
	return h;
}
void sha3::encoder::reset()
{
	my->ctx.init();
}

sha3 operator<<(const sha3 &h1, uint32_t i)
{
	sha3 result;
	fc::detail::shift_l(h1.data(), result.data(), result.data_size(), i);
	return result;
}
sha3 operator>>(const sha3 &h1, uint32_t i)
{
	sha3 result;
	fc::detail::shift_r(h1.data(), result.data(), result.data_size(), i);
	return result;
}
sha3 operator^(const sha3 &h1, const sha3 &h2)
{
	sha3 result;
	result._hash[0] = h1._hash[0] ^ h2._hash[0];
	result._hash[1] = h1._hash[1] ^ h2._hash[1];
	result._hash[2] = h1._hash[2] ^ h2._hash[2];
	result._hash[3] = h1._hash[3] ^ h2._hash[3];
	return result;
}
bool operator>=(const sha3 &h1, const sha3 &h2)
{
	return memcmp(h1._hash, h2._hash, sizeof(h1._hash)) >= 0;
}
bool operator>(const sha3 &h1, const sha3 &h2)
{
	return memcmp(h1._hash, h2._hash, sizeof(h1._hash)) > 0;
}
bool operator<(const sha3 &h1, const sha3 &h2)
{
	return memcmp(h1._hash, h2._hash, sizeof(h1._hash)) < 0;
}
bool operator!=(const sha3 &h1, const sha3 &h2)
{
	return !(h1 == h2);
}
bool operator==(const sha3 &h1, const sha3 &h2)
{
	// idea to not use memcmp, from:
	//   https://lemire.me/blog/2018/08/22/avoid-lexicographical-comparisons-when-testing-for-string-equality/
	return h1._hash[0] == h2._hash[0] &&
			 h1._hash[1] == h2._hash[1] &&
			 h1._hash[2] == h2._hash[2] &&
			 h1._hash[3] == h2._hash[3];
}

void to_variant(const sha3 &bi, variant &v)
{
	v = std::vector<char>((const char *)&bi, ((const char *)&bi) + sizeof(bi));
}
void from_variant(const variant &v, sha3 &bi)
{
	const auto &ve = v.as<std::vector<char>>();
	if (ve.size())
		memcpy(bi.data(), ve.data(), fc::min<size_t>(ve.size(), sizeof(bi)));
	else
		memset(bi.data(), char(0), sizeof(bi));
}
} // namespace fc
