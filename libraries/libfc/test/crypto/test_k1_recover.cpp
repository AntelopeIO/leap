#include <boost/test/unit_test.hpp>

#include <fc/exception/exception.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/crypto/k1_recover.hpp>
#include <fc/utility.hpp>

using namespace fc;

#include "test_utils.hpp"

namespace std {
std::ostream& operator<<(std::ostream& st, const std::variant<fc::k1_recover_error, bytes>& err)
{
   if(std::holds_alternative<fc::k1_recover_error>(err))
      st << static_cast<int32_t>(std::get<fc::k1_recover_error>(err));
   else
      st << fc::to_hex(std::get<bytes>(err));
    return st;
}
}

BOOST_AUTO_TEST_SUITE(k1_recover)
BOOST_AUTO_TEST_CASE(recover) try {

   using test_k1_recover = std::tuple<std::string, std::string, std::variant<fc::k1_recover_error, bytes>>;
   const std::vector<test_k1_recover> tests {
      //test
      {
         "1b323dd47a1dd5592c296ee2ee12e0af38974087a475e99098a440284f19c1f7642fa0baa10a8a3ab800dfdbe987dee68a09b6fa3db45a5cc4f3a5835a1671d4dd",
         "92390316873c5a9d520b28aba61e7a8f00025ac069acd9c4d2a71d775a55fa5f",
         to_bytes("044424982f5c4044aaf27444965d15b53f219c8ad332bf98a98a902ebfb05d46cb86ea6fe663aa83fd4ce0a383855dfae9bf7a07b779d34c84c347fec79d04c51e")
      },

      //test (invalid signature v)
      {
         "01174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c074f67be67e631d33aa7",
         "45fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399",
         fc::k1_recover_error::invalid_signature
      },

      //test (invalid signature len)
      {
         "174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c074f67be67e631d33aa7",
         "45fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399",
         fc::k1_recover_error::input_error
      },

      //test (invalid digest len)
      {
         "00174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c074f67be67e631d33aa7",
         "fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399",
         fc::k1_recover_error::input_error
      },

   };

    for(const auto& test : tests) {
        
        const auto& signature       = to_bytes(std::get<0>(test));
        const auto& digest          = to_bytes(std::get<1>(test));
        const auto& expected_result = std::get<2>(test);

        auto res = fc::k1_recover(signature, digest);
        BOOST_CHECK_EQUAL(res, expected_result);
    }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()