#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/webassembly/return_codes.hpp>

#include <fc/variant_object.hpp>

#include <boost/test/unit_test.hpp>

#include <test_contracts.hpp>

#include "fork_test_utilities.hpp"

using namespace eosio::chain;
using namespace eosio::testing;
using namespace eosio::chain::webassembly;
using namespace std::literals;

using bytes = std::vector<char>;

bytes h2bin(const std::string& source) {
   bytes output(source.length()/2);
   fc::from_hex(source, output.data(), output.size());
   return output;
}

BOOST_AUTO_TEST_SUITE(crypto_primitives_tests)

BOOST_AUTO_TEST_CASE( alt_bn128_add_test ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, int32_t, std::string>;
   const std::vector<test_add> tests = {
        //test (2 valid points, both on curve)
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            return_code::success,
            "16c7c4042e3a725ddbacf197c519c3dcad2bc87dfd9ac7e1e1631154ee0b7d9c19cd640dd28c9811ebaaa095a16b16190d08d6906c4f926fce581985fe35be0e"
        },

        //test (2 valid points, P1 not on curve)
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "2a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce46498441bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f5332",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test (invalid P1 length)
        {
            "2a",
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //|Fp| = 0x30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47
        //test (P1.x=|Fp|)
        {
            "30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd472976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test (P1=(0,0))
        {
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            return_code::success,
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844"
        },

        // test bigger P1 length
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae200",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        // test bigger P2 length
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce464984400",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        // test smaller P2 length
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce46498",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        // test smaller result length
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            return_code::failure,
            "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        // test bigger result length
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae2",
            "1bd20beca3d8d28e536d2b5bd3bf36d76af68af5e6c96ca6e5519ba9ff8f53322a53edf6b48bcf5cb1c0b4ad1d36dfce06a79dcd6526f1c386a14d8ce4649844",
            return_code::success,
            "16c7c4042e3a725ddbacf197c519c3dcad2bc87dfd9ac7e1e1631154ee0b7d9c19cd640dd28c9811ebaaa095a16b16190d08d6906c4f926fce581985fe35be0e00"
        },
   };

   for(const auto& test : tests) {
      auto op1 = h2bin(std::get<0>(test));
      auto op2 = h2bin(std::get<1>(test));
      auto expected_error = std::get<2>(test);
      auto expected_result = h2bin(std::get<3>(test));

      c.push_action( tester1_account, "testadd"_n, tester1_account, mutable_variant_object()
         ("op1", op1)
         ("op2", op2)
         ("expected_error", expected_error)
         ("expected_result", expected_result)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( alt_bn128_mul_test ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   using test_mul = std::tuple<std::string, std::string, int32_t, std::string>;
   const std::vector<test_mul> tests = {
        //test (valid point on curve, scalar size = 256 bits)
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            return_code::success,
            "2d66cdeca5e1715896a5a924c50a149be87ddd2347b862150fbb0fd7d0b1833c11c76319ebefc5379f7aa6d85d40169a612597637242a4bbb39e5cd3b844becd"
        },

        //test (scalar size < 256 bits)
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be",
            "01",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test (P1 not on curve)
        {
            "0db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f9136",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test (invalid P1 length)
        {
            "222480c9f95409bfa4ac6ae890b9c150bc88542b87b352e92950c340458b0c092976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8a",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //|Fp| = 0x30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47
        //test (P1.y=|Fp|)
        {
            "2976efd698cf23b414ea622b3f720dd9080d679042482ff3668cb2e32cad8ae230644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47",
            "0100010001000100010001000100010001000100010001000100010001000100",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test (P1=(0,0))
        {
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            return_code::success,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test bigger P1 length
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be00",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test bigger scalar length
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b00",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test smaller scalar length
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c",
            return_code::failure,
            "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test smaller result length
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            return_code::failure,
            "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        },

        //test bigger result length
        {
            "007c43fcd125b2b13e2521e395a81727710a46b34fe279adbf1b94c72f7f91360db2f980370fb8962751c6ff064f4516a6a93d563388518bb77ab9a6b30755be",
            "0312ed43559cf8ecbab5221256a56e567aac5035308e3f1d54954d8b97cd1c9b",
            return_code::success,
            "2d66cdeca5e1715896a5a924c50a149be87ddd2347b862150fbb0fd7d0b1833c11c76319ebefc5379f7aa6d85d40169a612597637242a4bbb39e5cd3b844becd00"
        },
   };

   for(const auto& test : tests) {
      auto point = h2bin(std::get<0>(test));
      auto scalar = h2bin(std::get<1>(test));
      auto expected_error = std::get<2>(test);
      auto expected_result = h2bin(std::get<3>(test));

      c.push_action( tester1_account, "testmul"_n, tester1_account, mutable_variant_object()
         ("point", point)
         ("scalar", scalar)
         ("expected_error", expected_error)
         ("expected_result", expected_result)
      );
   }


} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( alt_bn128_pair_test ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   using g1g2_pair = std::vector<std::string>;
   using pair_test = std::tuple<std::vector<g1g2_pair>, int32_t>;

   const std::vector<pair_test> tests =
   {
      //test1: 2 pairs => (G1_a,G2_a),(G1_b,G2_b)
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
         0 // With these input pairs, alt_bn128_pair should return 0 indicating a pair result of true.
      },

      //test2: 1 pair => (G1_a,G2_a) G1_a not on curve
      {
         {
               { //G1_a G2_a
                  "16da2f5cb6be7a0aa72c440c53c9bbdfec6c36c7d515536431b3a865468acbba", //G1_a.x
                  "0f25929bcb43d5a57391564615c9e70a992b10eafa4db109709649cf48c50dd2", //G1_a.y
                  "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_b.x
                  "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                  "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_b.y
                  "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"
               },

         },
         return_code::failure
      },

      //test3: 1 pair => (G1_a,G2_a) ; G1_a.x wrong length
      {
         {
               { //G1_a G2_a
                  "000000000000000000000000000000000000000000000000000000000000001",  //G1_a.x
                  "0000000000000000000000000000000000000000000000000000000000000002", //G1_a.y
                  "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_b.x
                  "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                  "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_b.y
                  "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"
               },

         },
         return_code::failure
      },

      //test4: 1 pair => (G1_a,G2_a) ; G1_a=(0,0)
      {
         {
               { //G1_a G2_a
                  "0000000000000000000000000000000000000000000000000000000000000000", //G1_a.x
                  "0000000000000000000000000000000000000000000000000000000000000000", //G1_a.y
                  "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_a.x
                  "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                  "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_a.y
                  "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"
               },

         },
         0 // With these input pairs, alt_bn128_pair should return 0 indicating a pair result of true.
      },

      //test5: 1 pair => (G1_a,G2_a) ; G1_a.x == |Fp|
      {
         {
               { //G1_a G2_a
                  "30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47", //G1_a.x
                  "0000000000000000000000000000000100000000000000000000000000000000", //G1_a.y
                  "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2", //G2_b.x
                  "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed",
                  "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b", //G2_b.y
                  "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"
               },

         },
         return_code::failure
      }
   };

   auto concat = [&](const std::string& s, bytes& buffer) {
      auto res = h2bin(s);
      buffer.insert( buffer.end(), res.begin(), res.end());
   };

   for(const auto& test : tests) {
      const auto& pairs           = std::get<0>(test);
      const auto& expected_error  = std::get<1>(test);

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

      c.push_action( tester1_account, "testpair"_n, tester1_account, mutable_variant_object()
         ("g1_g2_pairs", g1_g2_pairs)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( modexp_test ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   using modexp_test = std::tuple<std::vector<string>, int32_t, std::string>;
   const std::vector<modexp_test> tests {
      //test1
      {
         {
               "03",
               "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e",
               "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f",
         },
         return_code::success,
         "0000000000000000000000000000000000000000000000000000000000000001",
      },

      //test2
      {
         {
               "",
               "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2e",
               "fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f",
         },
         return_code::success,
         "0000000000000000000000000000000000000000000000000000000000000000",
      },

      //test3
      {
         {
               "01",
               "ff",
               "",
         },
         return_code::failure,
         "",
      },

   };

   for(const auto& test : tests) {
      const auto& parts           = std::get<0>(test);
      const auto& expected_error  = std::get<1>(test);
      const auto& expected_result = std::get<2>(test);

      auto base = h2bin(parts[0]);
      auto exponent = h2bin(parts[1]);
      auto modulus = h2bin(parts[2]);

      c.push_action( tester1_account, "testmodexp"_n, tester1_account, mutable_variant_object()
         ("base", base)
         ("exp", exponent)
         ("modulo", modulus)
         ("expected_error", expected_error)
         ("expected_result", expected_result)
      );
   }

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE( modexp_subjective_limit_test ) { try {

   // Given the need to respect the deadline timer and the current limitation that the deadline timer is not plumbed into the
   // inner loops of the implementation of mod_exp (which currently exists in the gmp shared library), only a small enough duration for
   // mod_exp can be tolerated to avoid going over the deadline timer by too much. A good threshold for small may be less than 5 ms.
   // Based on benchmarks within the test_modular_arithmetic test within fc, the following constraints are subjectively enforced on the
   // base, exp, and modulus input arguments of the mod_exp host function:
   //    1. exp.size() <= std::max(base.size(), modulus.size())
   //    2. 5 * ceil(log2(exp.size())) + 8 * ceil(log2(std::max(base.size(), modulus.size()))) <= 101

   // This test case verifies that the above constraints on mod_exp are subjectively enforced properly within libchain.

   // To allow mod_exp to be more useful, the limits on bit size need to be removed and the deadline timer plumbing into the implementation
   // needs to occur. When that happens, this test case can be removed.

   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   auto exponent = h2bin("010001");

   BOOST_CHECK_EXCEPTION(c.push_action(tester1_account, "testmodexp"_n, tester1_account, mutable_variant_object()
                                       ("base", h2bin("01"))
                                       ("exp", exponent)
                                       ("modulo", h2bin("0F"))
                                       ("expected_error", static_cast<int32_t>(return_code::success))
                                       ("expected_result", h2bin("01"))),
                         eosio::chain::subjective_block_production_exception,
                         fc_exception_message_is("mod_exp restriction: exponent bit size cannot exceed bit size of either base or modulus")
   );

   std::vector<char> modulus(4096 - 1);
   std::vector<char> expected_result(modulus.size());
   modulus.push_back(0x0F);
   expected_result.push_back(0x01);

    auto ceil_log2 = [](uint32_t n) -> uint32_t {
        if (n <= 1) {
            return 0;
        }
        return 32 - __builtin_clz(n - 1);
    };

   BOOST_CHECK(5 * ceil_log2(exponent.size()) + 8 * ceil_log2(modulus.size()) == 106);

   c.push_action( tester1_account, "testmodexp"_n, tester1_account, mutable_variant_object()
      ("base", h2bin("01"))
      ("exp", exponent)
      ("modulo", modulus)
      ("expected_error", static_cast<int32_t>(return_code::success))
      ("expected_result", expected_result)
   );

   modulus.pop_back();
   expected_result.pop_back();

   modulus.resize(4096);
   expected_result.resize(modulus.size());
   modulus.push_back(0x0F);
   expected_result.push_back(0x01);

   BOOST_CHECK(5 * ceil_log2(exponent.size()) + 8 * ceil_log2(modulus.size()) == 114);

   BOOST_CHECK_EXCEPTION(c.push_action(tester1_account, "testmodexp"_n, tester1_account, mutable_variant_object()
                                       ("base", h2bin("01"))
                                       ("exp", exponent)
                                       ("modulo", modulus)
                                       ("expected_error", static_cast<int32_t>(return_code::success))
                                       ("expected_result", expected_result)),
                         eosio::chain::subjective_block_production_exception,
                         fc_exception_message_is("mod_exp restriction: bit size too large for input arguments")
   );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( blake2f_test ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   using compress_test = std::tuple<std::vector<string>, int32_t, std::string>;
   const std::vector<compress_test> tests {
      //test1
      {
         {
               "00000000",
               "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b6",
               "61626300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
               "03000000000000000",
               "00000000000000000",
               "01",
         },
         return_code::success,
         "08c9bcf367e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d282e6ad7f520e511f6c3e2b8c68059b9442be0454267ce079217e1319cde05b",
      },

      //test2
      {
         {
               "0000000c",
               "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b6",
               "61626300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
               "03000000000000000",
               "00000000000000000",
               "01",
         },
         return_code::success,
         "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"
      },

      //test3
      {
         {
               "0000000c",
               "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b6",
               "61626300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
               "03000000000000000",
               "00000000000000000",
               "00",
         },
         return_code::success,
         "75ab69d3190a562c51aef8d88f1c2775876944407270c42c9844252c26d2875298743e7f6d5ea2f2d3e8d226039cd31b4e426ac4f2d3d666a610c2116fde4735"
      },

      //test4
      {
         {
               "00000001",
               "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b6",
               "61626300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
               "03000000000000000",
               "00000000000000000",
               "01",
         },
         return_code::success,
         "b63a380cb2897d521994a85234ee2c181b5f844d2c624c002677e9703449d2fba551b3a8333bcdf5f2f7e08993d53923de3d64fcc68c034e717b9293fed7a421"
      },

      //test5
      {
         {
               "00000000",
               "c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b6",
               "61626300000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
               "03000000000000000",
               "00000000000000000",
               "01",
         },
         return_code::failure,
         "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
      }
   };

   auto to_uint32 = [](const std::string& s) -> uint32_t{
      size_t l = s.size();
      return (uint32_t)std::stoul(s.c_str(), &l, 16);
   };

   for(const auto& test : tests) {

      const auto& params          = std::get<0>(test);
      const auto& expected_error  = std::get<1>(test);
      const auto& expected_result = std::get<2>(test);

      BOOST_REQUIRE(params.size() == 6);

      uint32_t rounds  = to_uint32(params[0] );
      bytes    state   = h2bin( params[1] );
      bytes    message = h2bin( params[2] );
      bytes    t0      = h2bin( params[3] );
      bytes    t1      = h2bin( params[4] );
      bool     final   = params[5] == "00" ? false : true;

      c.push_action( tester1_account, "testblake2f"_n, tester1_account, mutable_variant_object()
         ("rounds", rounds)
         ("state", state)
         ("message", message)
         ("t0", t0)
         ("t1", t1)
         ("final", final)
         ("expected_error", expected_error)
         ("expected_result", expected_result)
      );

   }

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE( keccak256_test ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   using test_keccak256 = std::tuple<std::string, std::string>;
   const std::vector<test_keccak256> tests {
      //test
      {
         "",
         "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470",
      },

      //test
      {
         "abc",
         "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45",
      },

      //test
      {
         "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "45d3b367a6904e6e8d502ee04999a7c27647f91fa845d456525fd352ae3d7371",
      }
   };

   for(const auto& test : tests) {
      auto tmp = std::get<0>(test);
      auto input = bytes{tmp.data(), tmp.data()+tmp.size()};
      auto expected_result = h2bin(std::get<1>(test));
      c.push_action( tester1_account, "testkeccak"_n, tester1_account, mutable_variant_object()
         ("input", input)
         ("expected_result", expected_result)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( sha3_test ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   using test_sha3 = std::tuple<std::string, std::string>;
   const std::vector<test_sha3> tests {
      //test
      {
         "",
         "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a",
      },

      //test
      {
         "abc",
         "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532",
      },

      //test
      {
         "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376",
      }
   };

   for(const auto& test : tests) {
      auto tmp = std::get<0>(test);
      auto input = bytes{tmp.data(), tmp.data()+tmp.size()};
      auto expected_result = h2bin(std::get<1>(test));
      c.push_action( tester1_account, "testsha3"_n, tester1_account, mutable_variant_object()
         ("input", input)
         ("expected_result", expected_result)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( k1_recover_test ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::crypto_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::crypto_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::crypto_primitives_test_abi() );
   c.produce_block();

   using test_k1_recover = std::tuple<std::string, std::string, int32_t, std::string>;
   const std::vector<test_k1_recover> tests {
      //test
      {
         "1b174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c074f67be67e631d33aa7",
         "45fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399",
         return_code::success,
         "0407521b8289ec7b603bd60b1d7efc5f7ad91cda280a6bebbe6d95d0ac96ef93fb12f99b751dba9238cd35e3c43b44b11474d2a6561afe331ec48c77cd287e438b",
      },

      //test (invalid signature v)
      {
         "01174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c074f67be67e631d33aa7",
         "45fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399",
         return_code::failure,
         "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
      },

      //test (invalid signature len)
      {
         "174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c074f67be67e631d33aa7",
         "45fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399",
         return_code::failure,
         "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
      },

      //test (invalid digest len)
      {
         "00174de755b55bd29026d626f7313a5560353dc5175f29c78d79d961b81a0c04360d833ca789bc16d4ee714a6d1a19461d890966e0ec5c074f67be67e631d33aa7",
         "fd65f6dd062fe7020f11d19fe5c35dc4d425e1479c0968c8e932c208f25399",
         return_code::failure,
         "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
      },

   };

   for(const auto& test : tests) {
      const auto& signature       = h2bin(std::get<0>(test));
      const auto& digest          = h2bin(std::get<1>(test));
      const auto& expected_error  = std::get<2>(test);
      const auto& expected_result = h2bin(std::get<3>(test));

      c.push_action( tester1_account, "testecrec"_n, tester1_account, mutable_variant_object()
         ("signature", signature)
         ("digest", digest)
         ("expected_error", expected_error)
         ("expected_result", expected_result)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
