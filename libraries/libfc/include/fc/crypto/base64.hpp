#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace fc {
std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len);
inline std::string base64_encode(char const* bytes_to_encode, unsigned int in_len) { return base64_encode( (unsigned char const*)bytes_to_encode, in_len); }
std::string base64_encode( const std::string& enc );
std::vector<char> base64_decode( std::string_view encoded_string);

std::string base64url_encode(unsigned char const* bytes_to_encode, unsigned int in_len);
inline std::string base64url_encode(char const* bytes_to_encode, unsigned int in_len) { return base64url_encode( (unsigned char const*)bytes_to_encode, in_len); }
std::string base64url_encode( const std::string& enc );
std::vector<char> base64url_decode( std::string_view encoded_string);
}  // namespace fc
