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

std::vector<char> hex2bin(const std::string& source) {
   std::vector<char> output(source.length()/2);
   fc::from_hex(source, output.data(), output.size());
   return output;
}

BOOST_AUTO_TEST_SUITE(bls_primitives_tests)

BOOST_AUTO_TEST_CASE( bls_testg1add ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (2 valid points, both on curve)
        {
            "160c53fd9087b35cf5ff769967fc1778c1a13b14c7954f1547e7d0f3cd6aaef040f4db21cc6eceed75fb0b9e417701127122e70cd593acba8efd18791a63228cce250757135f59dd945140502958ac51c05900ad3f8c1c0e6aa20850fc3ebc0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615",
            "160c53fd9087b35cf5ff769967fc1778c1a13b14c7954f1547e7d0f3cd6aaef040f4db21cc6eceed75fb0b9e417701127122e70cd593acba8efd18791a63228cce250757135f59dd945140502958ac51c05900ad3f8c1c0e6aa20850fc3ebc0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615",
            "2b90dabdf4613e10d269d70050e61bdc53c6d01ac517ad33cd8e82799d5515dfba05bc172fd280e2a73ff01aca77e30cbf82182b9005141106ef83a6d33dcda8bece738c9f9d6313f7e05945fd92c23a208efbe7a2048c6250f7f14df9f5c215e244ce19aa2759751dfb31f234c644189d4b0eae26beb2ba29a380a052b058a380b3005a7f18391cd44411a0f87d7817",
            return_code::success
        },

        //test (2 invalid points, garbage output)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "9bd600247dec313e88ed460013c49f7dec45c267571e241267687a35360fbee324b265772d89e38d7a886dfcf4b8eb16804724c7bf0f0e838c5795fffd6416be9fd8e9ff284548a79ae6163a8de08961c61110681a773dc976acea2cb2a8c20291e51670b204a82ff8f5ddffea2416e16ce5793251804c011309ccbda30ab8ca3efa52359820a18f39337698197cfc0f",
            return_code::success
        },
   };

   for(const auto& test : tests) {
      auto op1 = hex2bin(std::get<0>(test));
      auto op2 = hex2bin(std::get<1>(test));
      auto expected_result = hex2bin(std::get<2>(test));
      auto expected_error = std::get<3>(test);

      c.push_action( tester1_account, "testg1add"_n, tester1_account, mutable_variant_object()
         ("op1", op1)
         ("op2", op2)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( bls_testg2add ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (2 valid points, both on curve)
        {
            "100a9402a28ff2f51a96b48726fbf5b380e52a3eb593a8a1e9ae3c1a9d9994986b36631863b7676fd7bc50439291810506f6239e75c0a9a5c360cdbc9dc5a0aa067886e2187eb13b67b34185ccb61a1b478515f20eedb6c2f3ed6073092a92114a4c4960f80a734c5a9c365e1ffa7c595a630aaa6c85e6e75f490d6ee9b5efbba225eff075a9d307e5da807e8efd83005db064df92fcc0addc61142b0a27aa18a0ebe43b6aacad863aa33dc94e5c4979edca3ca4505817e7f21bde63a1c22b0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            "100a9402a28ff2f51a96b48726fbf5b380e52a3eb593a8a1e9ae3c1a9d9994986b36631863b7676fd7bc50439291810506f6239e75c0a9a5c360cdbc9dc5a0aa067886e2187eb13b67b34185ccb61a1b478515f20eedb6c2f3ed6073092a92114a4c4960f80a734c5a9c365e1ffa7c595a630aaa6c85e6e75f490d6ee9b5efbba225eff075a9d307e5da807e8efd83005db064df92fcc0addc61142b0a27aa18a0ebe43b6aacad863aa33dc94e5c4979edca3ca4505817e7f21bde63a1c22b0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            "1987cff592d8ae5c83ba9e9a1a016afd3a4e80d646e10a1274ba259b60a405c189945d07b3608d4d339a96429d60f80d8290d66f4e963c0e8c00e35db541ab46e93148fd7e41449f0be0a1883e36e4b56cf87121991d6d4778d499fdf1501c09a9220f11cdfe60560b15d7e6ec33825a8d9ce209fe8e20391d32210ba83dbd77ce4cd6019ca50465f8f5fed4a8a631048739c8d9b8fdc26a962b24f0306f8293a00d72d37fb2fb1b0643d9a8453cbf6a520463a54e25e8c42134d6798d7cce00949892c0f015e698b4386dbc3ef4f9b2b4c61454d90acdcfbf921adcd26bdf77454bdee1eb52a70fcab501fd1cfb0701ba60c9be25f9815bb9c32856144e543140d7c977d4585b0d75467b929db892f2da957948a1b02ecee537bcc742855716",
            return_code::success
        },

        //test (2 invalid points, garbage output)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "6084a42d13cd479f295f48a61cd03409ce2e6f646280a8b5674e2fc25adac7ad5ac5bde861a49b20adfb3c6d8bb0f0f73edf6fd4a2ad32eabfcccd59b6b73ccd94979f67a14c9d520782295a237d5221b59230433381188461d87d10a67617120be52ae01cc8d0d4ff49dd92e37991328c4caf4f089b447af0a17332a59cc1bd9e94816c44d5ef72f7ac017fd48d980282c8386dc30bd933dc455ba343d52b931009c04b631b377f3bb18465e350ee35263ee0736cec2286ec791c55becaf70d00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000027cb30e0640959d5f2ebc7c3e14920ae9423bbb8f998e1616b69eaeb9a6d3e0debe0fc0cc85b497b064b6d2bf756ef35",
            return_code::success
        },
   };

   for(const auto& test : tests) {
      auto op1 = hex2bin(std::get<0>(test));
      auto op2 = hex2bin(std::get<1>(test));
      auto expected_result = hex2bin(std::get<2>(test));
      auto expected_error = std::get<3>(test);

      c.push_action( tester1_account, "testg2add"_n, tester1_account, mutable_variant_object()
         ("op1", op1)
         ("op2", op2)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( bls_testg1mul ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (valid point, on curve)
        {
            "160c53fd9087b35cf5ff769967fc1778c1a13b14c7954f1547e7d0f3cd6aaef040f4db21cc6eceed75fb0b9e417701127122e70cd593acba8efd18791a63228cce250757135f59dd945140502958ac51c05900ad3f8c1c0e6aa20850fc3ebc0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615",
            "2a000000000000002a000000000000002a000000000000002a00000000000000",
            "de3e7eeee055abe12a480e58411508d51a356ff6692b14b43426d22cc354cd5d7469c41e0f1f5e40503c91e11419a30285cb057a62c93e2caaaff6c9c1dbc8f88c0a122157f51a617ce0e2890442cd9ce004a8ba972442e61bce9dabf1c6780c191984ae3c11ef21884a536f0d3450974df37295e9579d16cdb8dfdf9252091ca3cd9d05f4c6e645535add05ac197b08",
            return_code::success
        },

        //test (invalid point, garbage output)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "2a000000000000002a000000000000002a000000000000002a00000000000000",
            "759c3833cb6907e6f1a7fdd6a9c748957498bb8d0282cc641e6020bb18e070170140a4206477882e42a8b551dafe5d11a7e67cff45922bf2a49eea6d64fc5fecb26445170a6ec62233235cc2ac22bbecbf3271cf44f7a59ea0861376b7fe3f130b1865a3ee0d7f5a4bf360b6135c227f46644421caefe01ba82390a8756b7248e24ac4f89c94de92c385abfd6e8fbb11",
            return_code::success
        },
   };

   for(const auto& test : tests) {
      auto point = hex2bin(std::get<0>(test));
      auto scalar = hex2bin(std::get<1>(test));
      auto expected_result = hex2bin(std::get<2>(test));
      auto expected_error = std::get<3>(test);

      c.push_action( tester1_account, "testg1mul"_n, tester1_account, mutable_variant_object()
         ("point", point)
         ("scalar", scalar)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( bls_testg2mul ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (valid point, on curve)
        {
            "100a9402a28ff2f51a96b48726fbf5b380e52a3eb593a8a1e9ae3c1a9d9994986b36631863b7676fd7bc50439291810506f6239e75c0a9a5c360cdbc9dc5a0aa067886e2187eb13b67b34185ccb61a1b478515f20eedb6c2f3ed6073092a92114a4c4960f80a734c5a9c365e1ffa7c595a630aaa6c85e6e75f490d6ee9b5efbba225eff075a9d307e5da807e8efd83005db064df92fcc0addc61142b0a27aa18a0ebe43b6aacad863aa33dc94e5c4979edca3ca4505817e7f21bde63a1c22b0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            "2a000000000000002a000000000000002a000000000000002a00000000000000",
            "aa25f3f539b6f3215318d81b22d14bf9108294790e8a50545a404ae2057278304d8c3b5844271202b757767d1555a106ede90a9967cdc27b527d4a5720efda79a68b17072ad9402ed373ce9a2d28f5496106fc4cd23234b083181e8325734417f8330d2ced14040b815a006f7f905361f654d483c45abb90b4a958b4ca20ee2bb97cc1c9b6ff45644539abb32149610f33858c88450dad3d2adb82df72f9ed9c42dc2ef78e17f5a2a1abd0468853d55f05f6458179fdf5671db784795a686c0ffae139afaed0212d18d615b0ff90a9deba090f723190521dc8c822621b0a7e70a03b9f3faaeb862846dccd418855d70406fc57e73783c2da92433c1a3873640217539ec7c01f3d354506d86db49fddad225e82421506d99c19b749170aa4f805",
            return_code::success
        },

        //test (invalid point, garbage output)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "2a000000000000002a000000000000002a000000000000002a00000000000000",
            "50b11bda450431471dc45148066c514489bdc886a8353f382ea4992279d5f01ecec4b1b2782cdc2546cbf6bc070e3701123658149032e1f3af1e63a5ad402549900a42698de7064c9eb7a1b9a344b709073cf033649c01ac6249ab26909b1d194a8d4379563b2ed355b4b63047fc6c0ea9e9b99f75f474db88cee948733dbfd7ddf175d3e67badbce68e3b8515b8e504e666696422b760a10821a881e01d9249ba388eb86f5e8698ebf5cef4454c39144d89b050611a94685ce4505a19422009ef420ad74837caee3ad9a0128c8969e30c130ecbe76ca7157cb12fe59979b777a4df8411519e3137269a05866bd12b1964cc1e6474ea7555e03215a61b2997af51847b27c00fcda059b18be464d51ad8866ce243ca826a184ea25b03d0f9c10b",
            return_code::success
        },
   };

   for(const auto& test : tests) {
      auto point = hex2bin(std::get<0>(test));
      auto scalar = hex2bin(std::get<1>(test));
      auto expected_result = hex2bin(std::get<2>(test));
      auto expected_error = std::get<3>(test);

      c.push_action( tester1_account, "testg2mul"_n, tester1_account, mutable_variant_object()
         ("point", point)
         ("scalar", scalar)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( bls_testg1exp ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, uint32_t, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (two valid points, on curve)
        {
            "160c53fd9087b35cf5ff769967fc1778c1a13b14c7954f1547e7d0f3cd6aaef040f4db21cc6eceed75fb0b9e417701127122e70cd593acba8efd18791a63228cce250757135f59dd945140502958ac51c05900ad3f8c1c0e6aa20850fc3ebc0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615160c53fd9087b35cf5ff769967fc1778c1a13b14c7954f1547e7d0f3cd6aaef040f4db21cc6eceed75fb0b9e417701127122e70cd593acba8efd18791a63228cce250757135f59dd945140502958ac51c05900ad3f8c1c0e6aa20850fc3ebc0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615",
            "2a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a00000000000000",
            2,
            "9e80ed609e62978a3a7f0d6bf57e1df4070725c1bf4dab43a6b710adef7450fb41f0cf14a7895ec26fd8c830c327cc0e2c8fe7687d23ff84647b47bbad04cf77625dee1049e53ad5162fe772278e5fe3ceb0bdc472f31952343da7b75532f7016613af892092131ad77d13afc8c9192487ac19f8454ad932653145f06e25790d26b23ac6b83025326f3397efbd845511",
            return_code::success
        },

        //test (two invalid points, garbage output)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "2a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a00000000000000",
            2,
            "a4a01d651d82bd2e4f6f0df09b5f57254f2fb3294a83dddda170312f752c6107511739d935d6e5cf9f5ab245ff29b3057c41e220165e0a104493ead21fa8fee939026a77c031ba9f27edeb233375a2172f8cb0fae434365f6a65e1677ef72f00018da74a79be5f8e856f58b7b57f01cce6e1c082309a00612992714c5aed692dd8dba9061dfd04fb8e9dd61acaf3620f",
            return_code::success
        },
   };

   for(const auto& test : tests) {
      auto points = hex2bin(std::get<0>(test));
      auto scalars = hex2bin(std::get<1>(test));
      auto num = std::get<2>(test);
      auto expected_result = hex2bin(std::get<3>(test));
      auto expected_error = std::get<4>(test);

      c.push_action( tester1_account, "testg1exp"_n, tester1_account, mutable_variant_object()
         ("points", points)
         ("scalars", scalars)
         ("num", num)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( bls_testg2exp ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, uint32_t, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (two valid points, on curve)
        {
            "100a9402a28ff2f51a96b48726fbf5b380e52a3eb593a8a1e9ae3c1a9d9994986b36631863b7676fd7bc50439291810506f6239e75c0a9a5c360cdbc9dc5a0aa067886e2187eb13b67b34185ccb61a1b478515f20eedb6c2f3ed6073092a92114a4c4960f80a734c5a9c365e1ffa7c595a630aaa6c85e6e75f490d6ee9b5efbba225eff075a9d307e5da807e8efd83005db064df92fcc0addc61142b0a27aa18a0ebe43b6aacad863aa33dc94e5c4979edca3ca4505817e7f21bde63a1c22b0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100a9402a28ff2f51a96b48726fbf5b380e52a3eb593a8a1e9ae3c1a9d9994986b36631863b7676fd7bc50439291810506f6239e75c0a9a5c360cdbc9dc5a0aa067886e2187eb13b67b34185ccb61a1b478515f20eedb6c2f3ed6073092a92114a4c4960f80a734c5a9c365e1ffa7c595a630aaa6c85e6e75f490d6ee9b5efbba225eff075a9d307e5da807e8efd83005db064df92fcc0addc61142b0a27aa18a0ebe43b6aacad863aa33dc94e5c4979edca3ca4505817e7f21bde63a1c22b0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            "2a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a00000000000000",
            2,
            "595a2cbaa4315ecc0dd9838a81712e0cfb88cb2c4468640ff382abb321b3a9bedebb0aad985a9057f664fc52eb52cd194b1c9611b25ef4281e439a52d1832813decd66c22440821f0288dcb7a82151386aa0240e943b6905e61619be2e11c208fa03df16e11a1b1368abac90598bb237f785701d5d1d5cb0af6934ed633d366de28703431b8d70899d92797689207c0cd35c345460f971dff5d648d9ddec8f5fabef99b15ea7c4440ac1564d6e0326076f32a4ec9cf0d10059593d64afd35c03d10f16794821628b565c9319f4af3c96b98c4fb11bc0c04172bb57372531f6e76798142d4b00488adbea850a2649a305b71fa389c3c226f2df4a58c8bce5d90698452f4126046be0c82d8817b64162a53787f1cb73af27d969adc626c1392716",
            return_code::success
        },

        //test (two invalid points, garbage output)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "2a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a000000000000002a00000000000000",
            2,
            "71aa03189998d6bec41f070fe01d38efc3b60e77b6dd7a7676b3f0b8939e2fda916c4609b5d3313e04c382107aac7f014f65cf46d614729fc4255e6e045a1a13e851a81a2f03036973b211ac18152231e24d93805f4b64792d573f490791c20d451af61e3081a4b8f40919969a87bd015363da93d1b7f1ff4c625dae5fa6c95492fefb689e66a10a8d5e9f3d0190e9119e6c11550ac6a09c6ded21e9d67c0ba1e71bcc4ef28879a4d3af1beaf0a9394b7a01c28bd01d0b09818bb8274a275e11d0a3859c637c455a28eef74d4ba33e7a9f0550ef58e5f0e960a4c29b2800ed1697408c7331b198efcd5eda01705640118ef8a64c4e8eb7c5397b9d6e0d8799935be4e973ada583bddf13f2c17129f6ae60f0ec4f39971af53bc154c953312419",
            return_code::success
        },
   };

   for(const auto& test : tests) {
      auto points = hex2bin(std::get<0>(test));
      auto scalars = hex2bin(std::get<1>(test));
      auto num = std::get<2>(test);
      auto expected_result = hex2bin(std::get<3>(test));
      auto expected_error = std::get<4>(test);

      c.push_action( tester1_account, "testg2exp"_n, tester1_account, mutable_variant_object()
         ("points", points)
         ("scalars", scalars)
         ("num", num)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }
/*
BOOST_AUTO_TEST_CASE( bls_testpairing ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, uint32_t, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (three valid g1 and g2 points, on curve)
        {
            "160c53fd9087b35cf5ff769967fc1778c1a13b14c7954f1547e7d0f3cd6aaef040f4db21cc6eceed75fb0b9e417701127122e70cd593acba8efd18791a63228cce250757135f59dd945140502958ac51c05900ad3f8c1c0e6aa20850fc3ebc0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615160c53fd9087b35cf5ff769967fc1778c1a13b14c7954f1547e7d0f3cd6aaef040f4db21cc6eceed75fb0b9e417701127122e70cd593acba8efd18791a63228cce250757135f59dd945140502958ac51c05900ad3f8c1c0e6aa20850fc3ebc0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615160c53fd9087b35cf5ff769967fc1778c1a13b14c7954f1547e7d0f3cd6aaef040f4db21cc6eceed75fb0b9e417701127122e70cd593acba8efd18791a63228cce250757135f59dd945140502958ac51c05900ad3f8c1c0e6aa20850fc3ebc0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615",
            "100a9402a28ff2f51a96b48726fbf5b380e52a3eb593a8a1e9ae3c1a9d9994986b36631863b7676fd7bc50439291810506f6239e75c0a9a5c360cdbc9dc5a0aa067886e2187eb13b67b34185ccb61a1b478515f20eedb6c2f3ed6073092a92114a4c4960f80a734c5a9c365e1ffa7c595a630aaa6c85e6e75f490d6ee9b5efbba225eff075a9d307e5da807e8efd83005db064df92fcc0addc61142b0a27aa18a0ebe43b6aacad863aa33dc94e5c4979edca3ca4505817e7f21bde63a1c22b0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100a9402a28ff2f51a96b48726fbf5b380e52a3eb593a8a1e9ae3c1a9d9994986b36631863b7676fd7bc50439291810506f6239e75c0a9a5c360cdbc9dc5a0aa067886e2187eb13b67b34185ccb61a1b478515f20eedb6c2f3ed6073092a92114a4c4960f80a734c5a9c365e1ffa7c595a630aaa6c85e6e75f490d6ee9b5efbba225eff075a9d307e5da807e8efd83005db064df92fcc0addc61142b0a27aa18a0ebe43b6aacad863aa33dc94e5c4979edca3ca4505817e7f21bde63a1c22b0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000100a9402a28ff2f51a96b48726fbf5b380e52a3eb593a8a1e9ae3c1a9d9994986b36631863b7676fd7bc50439291810506f6239e75c0a9a5c360cdbc9dc5a0aa067886e2187eb13b67b34185ccb61a1b478515f20eedb6c2f3ed6073092a92114a4c4960f80a734c5a9c365e1ffa7c595a630aaa6c85e6e75f490d6ee9b5efbba225eff075a9d307e5da807e8efd83005db064df92fcc0addc61142b0a27aa18a0ebe43b6aacad863aa33dc94e5c4979edca3ca4505817e7f21bde63a1c22b0bfdff02000000097602000cc40b00f4ebba58c7535798485f455752705358ce776dec56a2971a075c93e480fac35ef615000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            3,
            "af587ca8f5f0760b888e4ec66748623d1b13986547b5de2042ce89233c2c837bbb080329a505285c948d8bb4c88a5914ae216a64db582ba084178db4364a7b3843ef7fb9056a90526d27dc9e252b38600fc54ce7a5ec15771c555816edc26d0166e75d3315789c846387c8c234b1c98b50baf233b7312aa317ae56a2bfa170b1bc43f0e046b4a1f45cb7aacea7d0ae0320496e3960d4dbc2039027ba8cfe1ca120ea98fa94cf25034ccb5e74033f447b837a78b03affd44b87f865f3d713000de78ab5629dbde8d0c8b7a28941717be3ebd23924cf16897144bb69730f68bff5a109fc2e6d563b15e2eb883cf4becd11edcbc2ec4402c93249389ed612fa0396ed6eadcbe4d703667d46b0150ee3a5158caef956791e4f527e1312c8402f8509acf7001dc0dc311549d76398c247e79b737b614d0a6663f7dbdb314e5b51eace14457ee7b1f3f54d5987c16c8f89d1022d91d4649f5f6a204047077e5791a654cd2277506cd0af77f9ea789278b364c115ec07ba14390a9c22d59aa9c97a9c09ce025b5e14443f3c4e4cd602ef34105fadd82837fc5ce60a461e6ea11b13ae67b82e3366a2b2d1bbe78b2579173b3c0c5aed88b2949403060c3e065782bcb742c55c559e75e373293d80dec54120773d80144b21b353ead58dc8427e5b9cbd0c1431f1a74caf5f57c4b55b89810029cdd24ef4a029797ced68ff882492a8f55f31fe52217356366983e35d2a5640e818cc8bddf9274de0100e624a6bf03e2b9ff22f8dda09a46b50b1b305cb6adfa2ae775febbce4a2b507cd2390db968ceb13",
            return_code::success
        },

        //test (three invalid g1 and g2 points, should fail)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            3,
            "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            return_code::failure
        },
   };

   for(const auto& test : tests) {
      auto g1_points = hex2bin(std::get<0>(test));
      auto g2_points = hex2bin(std::get<1>(test));
      auto num = std::get<2>(test);
      auto expected_result = hex2bin(std::get<3>(test));
      auto expected_error = std::get<4>(test);

      c.push_action( tester1_account, "testpairing"_n, tester1_account, mutable_variant_object()
         ("g1_points", g1_points)
         ("g2_points", g2_points)
         ("num", num)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }
*/
BOOST_AUTO_TEST_CASE( bls_testg1map ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (valid field element)
        {
            "c93f817b159bdf8404dc378514f845192bbae4faac7f4a568924f2d9725125000489408fd796461c288900add00d4618",
            "d8e09005badb694b1e5dba8476ba3012cd2bf0c472ee1811ac9b34c869638b47e669bce16933e98ce9b129f83d93d71890d6fd13b77d4ad8df1a5f1c6b81df2b3f305721b7b7874cd81eba23ca05e8bc974b14f7e4c2c77cbdf321c340ba4903a2a46af290abe078e426f913e492b8d9b997232dbe6198aea719a31e73b40e110c4efa9e42d737a42883e0ca5f344d02",
            return_code::success
        },

        //test (invalid field element, should fail)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            return_code::failure
        },
   };

   for(const auto& test : tests) {
      auto e = hex2bin(std::get<0>(test));
      auto expected_result = hex2bin(std::get<1>(test));
      auto expected_error = std::get<2>(test);

      c.push_action( tester1_account, "testg1map"_n, tester1_account, mutable_variant_object()
         ("e", e)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( bls_testg2map ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::bls_primitives );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, test_contracts::bls_primitives_test_wasm() );
   c.set_abi( tester1_account, test_contracts::bls_primitives_test_abi().data() );
   c.produce_block();

   using test_add = std::tuple<std::string, std::string, int32_t>;
   const std::vector<test_add> tests = {
        //test (valid field element)
        {
            "d4f2cfec99387809574fcc2dba105603d950d490e2bebe0c212c05e16b784745ef4fe8e70b554d0a52fe0bed5ea6690ade2348eb8972a96740a430df162d920e175f5923a76d18650ea24a8ec06d414c6d1d218d673dac3619a1a5c142785708",
            "9968ae9e9bf3d22ec6c9670efa64ea23d4966b41bb25f76ea2ef71b96fa35e031adb866b3df234065b9aa72d0b12ab14978678279eb944f05b5af53d91e700b57aa87076727def2e9f2fba3cf6784a25591ae1669c2cf15cdcf038b826d1e81178bd7b59b7e911e0c2d11d6805756222201e3364f8010fb651939eca63f7e77709042ee1030cd938f53905a714e815112a7dfeed207757d30382f69617014bc683a175d0dfbd74f2684de26c771f3f55a538e6d2f969eb282bddfec4fc08dd18f37df0889292f288dff631b02f07b88134410fd86526d234d75b9a329bc9a8b6e71c7ad516b87af9717d962cba5d2b19fd1b77746f1e484a54e6aec81ede148f01e2c8283c598a4976182d2ce287fe6888e23996ce03b03ce6709e4aa8e66416",
            return_code::success
        },

        //test (invalid field element, should fail)
        {
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
            "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
            return_code::failure
        },
   };

   for(const auto& test : tests) {
      auto e = hex2bin(std::get<0>(test));
      auto expected_result = hex2bin(std::get<1>(test));
      auto expected_error = std::get<2>(test);

      c.push_action( tester1_account, "testg2map"_n, tester1_account, mutable_variant_object()
         ("e", e)
         ("res", expected_result)
         ("expected_error", expected_error)
      );
   }

} FC_LOG_AND_RETHROW() }



BOOST_AUTO_TEST_SUITE_END()
