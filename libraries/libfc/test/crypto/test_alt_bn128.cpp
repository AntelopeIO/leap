#define BOOST_TEST_MODULE altbn_128_tests
#include <boost/test/included/unit_test.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/crypto/alt_bn128.hpp>
#include <fc/utility.hpp>

using namespace fc;
#include "test_utils.hpp"

namespace std {
std::ostream& operator<<(std::ostream& st, const std::variant<fc::alt_bn128_error, bytes>& err)
{
    if( std::holds_alternative<fc::alt_bn128_error>(err) )
        st << static_cast<int32_t>(std::get<fc::alt_bn128_error>(err));
    else
        st << fc::to_hex(std::get<bytes>(err));
    return st;
}

std::ostream& operator<<(std::ostream& st, const std::variant<fc::alt_bn128_error, bool>& err)
{
    if( std::holds_alternative<fc::alt_bn128_error>(err) )
        st << static_cast<int32_t>(std::get<fc::alt_bn128_error>(err));
    else
        st << std::get<bool>(err);
    return st;
}
}

BOOST_AUTO_TEST_SUITE(altbn_128_tests)
BOOST_AUTO_TEST_CASE(add) try {

    using test_add = std::tuple<std::string, std::string, std::variant<fc::alt_bn128_error, bytes>>;
    const std::vector<test_add> tests = {
        //test (2 valid points, both on curve)
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            to_bytes("16c7c4042e3a725ddbacf197c519c3dcad2bc87dfd9ac7e1e1631154ee0b7d9c19cd640dd28c9811ebaaa095a16b16190d08d6906c4f926fce581985fe35be0e")
        },

        //test (2 valid points, P1 not on curve)
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "2a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce46498441bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f5332",
            alt_bn128_error::operand_not_in_curve
        },

        //test (invalid P1 length)
        {
            "2a",
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            alt_bn128_error::input_len_error
        },

        //|Fp| = 0x30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47
        //test (P1.x=|Fp|)
        {
            "30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd472976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            alt_bn128_error::operand_component_invalid
        },

        //test (P1=(0,0))
        {
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            to_bytes("1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844")
        },
    };

    for(const auto& test : tests) {
        auto op1 = to_bytes(std::get<0>(test));
        auto op2 = to_bytes(std::get<1>(test));
        auto expected_result = std::get<2>(test);

        auto res = alt_bn128_add(op1, op2);
        BOOST_CHECK_EQUAL(res, expected_result);
    }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(mul) try {

    using test_mul = std::tuple<std::string, std::string, std::variant<fc::alt_bn128_error, bytes>>;
    const std::vector<test_mul> tests = {
        //test (valid point on curve, scalar size = 256 bits)
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            to_bytes("2d66cdeca5e1715896a5a924c50a149be87ddd2347b862150fbb0fd7d0b1833c11c76319ebefc5379f7aa6d85d40169a612597637242a4bbb39e5cd3b844becd")
        },

        //test (scalar size < 256 bits)
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be",
            "01",
            alt_bn128_error::invalid_scalar_size,
        },

        //test (P1 not on curve)
        {
            "0db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f9136",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            alt_bn128_error::operand_not_in_curve,
        },
        
        //test (invalid P1 length)
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8a",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            alt_bn128_error::input_len_error,
        },

        //|Fp| = 0x30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47
        //test (P1.y=|Fp|)
        {
            "2976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae230644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47",
            "0100010001000100010001000100010001000100010001000100010001000100",
            alt_bn128_error::operand_component_invalid,
        },

        //test (P1=(0,0))
        {
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            to_bytes("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000")
        },

    };

    for(const auto& test : tests) {
        auto point = to_bytes(std::get<0>(test));
        auto scalar = to_bytes(std::get<1>(test));
        auto expected_result = std::get<2>(test);

        auto res = alt_bn128_mul(point, scalar);
        BOOST_CHECK_EQUAL(res, expected_result);
    }

} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_CASE(pair) try {

    using g1g2_pair = std::vector<std::string>;
    using pair_test = std::tuple<std::vector<g1g2_pair>, std::variant<fc::alt_bn128_error, bool>>;

    const std::vector<pair_test> tests =
    {
        //test1: 2 pairs => (G1_a,G2_a),(G1_b,G2_b) ; alt_bn128_error::none ; true
        {
            {
                { //G1_a G2_a
                    "0f25929bcb43d5a57391564615c9e70a992b10eafa4db109709649cf48c50dd2", //G1_a.x
                    "16da2f5cb6be7a0aa72c440c53c9bbdfec6c36c7d515536431b3a865468acbba", //G1_a.y
                    "2e89718ad33c8bed92e210e81d1853435399a271913a6520736a4729cf0d51eb", //G2_a.x
                    "01a9e2ffa2e92599b68e44de5bcf354fa2642bd4f26b259daa6f7ce3ed57aeb3",
                    "14a9a87b789a58af499b314e13c3d65bede56c07ea2d418d6874857b70763713", //G2_a.y
                    "178fb49a2d6cd347dc58973ff49613a20757d0fcc22079f9abd10c3baee24590",
                },

                { //G1_b G2_b
                    "1b9e027bd5cfc2cb5db82d4dc9677ac795ec500ecd47deee3b5da006d6d049b8", //G1_b.x
                    "11d7511c78158de484232fc68daf8a45cf217d1c2fae693ff5871e8752d73b21", //G1_b.y
                    "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_b.x
                    "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                    "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_b.y
                    "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa",
                }
            },
            true
        },

        //test2: 1 pair => (G1_a,G2_a) ; alt_bn128_error::none; false
        {
            {
                { //G1_a G2_a
                    "0000000000000000000000000000000000000000000000000000000000000001", //G1_a.x
                    "0000000000000000000000000000000000000000000000000000000000000002", //G1_a.y
                    "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_b.x
                    "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                    "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_b.y
                    "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"
                },

            },
            false
        },

        //test3: 1 pair => (G1_a,G2_a) ; alt_bn128_error::pairing_list_size_error; false
        {
            {
                { //G1_a G2_a
                    "00000000000000000000000000000000000000000000000000000000000001", //G1_a.x
                    "0000000000000000000000000000000000000000000000000000000000000002", //G1_a.y
                    "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_b.x
                    "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                    "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_b.y
                    "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"
                },

            },
            alt_bn128_error::pairing_list_size_error
        },

        //test5: 1 pair => (G1_a,G2_a) ; alt_bn128_error::operand_not_in_curve; false
        {
            {
                { //G1_a G2_a
                    "0000000000000000000000000000000000000000000000000000000000000000", //G1_a.x
                    "0000000000000000000000000000000100000000000000000000000000000000", //G1_a.y
                    "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_b.x
                    "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                    "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_b.y
                    "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"
                },

            },
            alt_bn128_error::operand_not_in_curve
        },

        //test6: 1 pair => (G1_a,G2_a) ; alt_bn128_error::operand_component_invalid; false
        {
            {
                { //G1_a G2_a
                    "30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47", //G1_a.x == |Fp|
                    "0000000000000000000000000000000100000000000000000000000000000000", //G1_a.y
                    "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_b.x
                    "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                    "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_b.y
                    "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"
                },

            },
            alt_bn128_error::operand_component_invalid
        }
    };

    auto concat = [&](const std::string& s, bytes& buffer) {
        auto res = to_bytes(s);
        buffer.insert( buffer.end(), res.begin(), res.end());
    };

    yield_function_t yield = [](){};

    for(const auto& test : tests) {
        const auto& pairs               = std::get<0>(test);
        const auto& expected_result     = std::get<1>(test);

        bytes g1_g2_pairs;
        for(const auto& pair : pairs) {
            BOOST_REQUIRE(pair.size() == 6);
            concat(pair[0], g1_g2_pairs);
            concat(pair[1], g1_g2_pairs);
            concat(pair[2], g1_g2_pairs);
            concat(pair[3], g1_g2_pairs);
            concat(pair[4], g1_g2_pairs);
            concat(pair[5], g1_g2_pairs);
        }

        auto res = alt_bn128_pair(g1_g2_pairs, yield);
        BOOST_CHECK_EQUAL(res, expected_result);
    }

} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_SUITE_END()
