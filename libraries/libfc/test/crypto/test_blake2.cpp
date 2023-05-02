#include <boost/test/unit_test.hpp>

#include <fc/exception/exception.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/crypto/blake2.hpp>
#include <fc/utility.hpp>

using namespace fc;

#include "test_utils.hpp"

namespace std {
std::ostream& operator<<(std::ostream& st, const std::variant<fc::blake2b_error, bytes>& err)
{
    if(std::holds_alternative<fc::blake2b_error>(err))
        st << static_cast<int32_t>(std::get<fc::blake2b_error>(err));
    else
        st << fc::to_hex(std::get<bytes>(err));
    return st;
}
}

BOOST_AUTO_TEST_SUITE(blake2)
BOOST_AUTO_TEST_CASE(compress) try {

    using compress_test = std::tuple<std::vector<std::string>, std::variant<fc::blake2b_error, bytes>>;

    const std::vector<compress_test> tests {
        //test1
        {
            {
                "00000000",
                "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b",
                "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
                "0300000000000000",
                "0000000000000000",
                "01",
            },
            to_bytes("08c9bcf367e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d282e6ad7f520e511f6c3e2b8c68059b9442be0454267ce079217e1319cde05b")
        },

        //test2
        {
            {
                "0000000c",
                "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b",
                "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
                "0300000000000000",
                "0000000000000000",
                "01",
            },
            to_bytes("ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923")
        },

        //test3
        {
            {
                "0000000c",
                "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b",
                "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
                "0300000000000000",
                "0000000000000000",
                "00",
            },
            to_bytes("75ab69d3190a562c51aef8d88f1c2775876944407270c42c9844252c26d2875298743e7f6d5ea2f2d3e8d226039cd31b4e426ac4f2d3d666a610c2116fde4735")
        },

        //test4
        {
            {
                "00000001",
                "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b",
                "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
                "0300000000000000",
                "0000000000000000",
                "01",
            },
            to_bytes("b63a380cb2897d521994a85234ee2c181b5f844d2c624c002677e9703449d2fba551b3a8333bcdf5f2f7e08993d53923de3d64fcc68c034e717b9293fed7a421")
        },

        //test5
        {
            {
                "00000000",
                "c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b",
                "6162630000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
                "0300000000000000",
                "0000000000000000",
                "01",
            },
            blake2b_error::input_len_error
        },
    };

    yield_function_t yield = [](){};

    for(const auto& test : tests) {
        
        const auto& params          = std::get<0>(test);
        const auto& expected_result = std::get<1>(test);

        BOOST_REQUIRE(params.size() == 6);

        uint32_t _rounds    = to_uint32(params[0] );
        bytes    _h         = to_bytes( params[1] );
        bytes    _m         = to_bytes( params[2] );
        bytes    _t0_offset = to_bytes( params[3] );
        bytes    _t1_offset = to_bytes( params[4] );
        bool     _f         = params[5] == "00" ? false : true;

        auto res = blake2b(_rounds, _h, _m, _t0_offset, _t1_offset, _f, yield);

        BOOST_CHECK_EQUAL(res, expected_result);
    }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()