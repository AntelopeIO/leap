#include <eosio/chain/block_header.hpp>

#include <boost/test/unit_test.hpp>

using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(block_header_tests)

// test for block header without extension
BOOST_AUTO_TEST_CASE(block_header_without_extension_test)
{
   block_header header;
   std::optional<block_header_extension> ext = header.extract_header_extension(instant_finality_extension::extension_id());
   BOOST_REQUIRE(!ext);
}

// test for empty instant_finality_extension
BOOST_AUTO_TEST_CASE(instant_finality_extension_with_empty_values_test)
{
   block_header header;
   constexpr uint32_t                    last_qc_block_num {0};
   constexpr bool                        is_last_qc_strong {false};
   const std::optional<finalizer_policy> new_finalizer_policy;
   const std::optional<proposer_policy>  new_proposer_policy;

   emplace_extension(
      header.header_extensions,
      instant_finality_extension::extension_id(),
      fc::raw::pack( instant_finality_extension{last_qc_block_num, is_last_qc_strong, new_finalizer_policy, new_proposer_policy} )
   );

   std::optional<block_header_extension> ext = header.extract_header_extension(instant_finality_extension::extension_id());
   BOOST_REQUIRE( !!ext );

   const auto& if_extension = std::get<instant_finality_extension>(*ext);
   BOOST_REQUIRE_EQUAL( if_extension.last_qc_block_num, last_qc_block_num );
   BOOST_REQUIRE_EQUAL( if_extension.is_last_qc_strong, is_last_qc_strong );
   BOOST_REQUIRE( !if_extension.new_finalizer_policy );
   BOOST_REQUIRE( !if_extension.new_proposer_policy );
}

// test for instant_finality_extension uniqueness
BOOST_AUTO_TEST_CASE(instant_finality_extension_uniqueness_test)
{
   block_header header;

   emplace_extension(
      header.header_extensions,
      instant_finality_extension::extension_id(),
      fc::raw::pack( instant_finality_extension{0, false, {std::nullopt}, {std::nullopt}} )
   );

   std::vector<finalizer_authority> finalizers { {"test description", 50, fc::crypto::blslib::bls_public_key{"PUB_BLS_MPPeebAPxt/ibL2XPuZVGpADjGn+YEVPPoYmTZeBD6Ok2E19M8SnmDGSdZBf2qwSuJim+8H83EsTpEn3OiStWBiFeJYfVRLlEsZuSF0SYYwtVteY48n+KeE1IWzlSAkSyBqiGA==" }} };
   finalizer_policy new_finalizer_policy;
   new_finalizer_policy.generation = 1;
   new_finalizer_policy.threshold = 100;
   new_finalizer_policy.finalizers = finalizers;

   proposer_policy new_proposer_policy {1, block_timestamp_type{200}, {} };

   emplace_extension(
      header.header_extensions,
      instant_finality_extension::extension_id(),
      fc::raw::pack( instant_finality_extension{100, true, new_finalizer_policy, new_proposer_policy} )
   );
   
   BOOST_CHECK_THROW(header.validate_and_extract_header_extensions(), invalid_block_header_extension);
}

// test for instant_finality_extension with values
BOOST_AUTO_TEST_CASE(instant_finality_extension_with_values_test)
{
   block_header header;
   constexpr uint32_t                        last_qc_block_num {10};
   constexpr bool                            is_last_qc_strong {true};
   
   std::vector<finalizer_authority> finalizers { {"test description", 50, fc::crypto::blslib::bls_public_key{"PUB_BLS_MPPeebAPxt/ibL2XPuZVGpADjGn+YEVPPoYmTZeBD6Ok2E19M8SnmDGSdZBf2qwSuJim+8H83EsTpEn3OiStWBiFeJYfVRLlEsZuSF0SYYwtVteY48n+KeE1IWzlSAkSyBqiGA==" }} };
   finalizer_policy new_finalizer_policy;
   new_finalizer_policy.generation = 1;
   new_finalizer_policy.threshold = 100;
   new_finalizer_policy.finalizers = finalizers;

   proposer_policy new_proposer_policy {1, block_timestamp_type{200}, {} };

   emplace_extension(
      header.header_extensions,
      instant_finality_extension::extension_id(),
      fc::raw::pack( instant_finality_extension{last_qc_block_num, is_last_qc_strong, new_finalizer_policy, new_proposer_policy} )
   );

   std::optional<block_header_extension> ext = header.extract_header_extension(instant_finality_extension::extension_id());
   BOOST_REQUIRE( !!ext );

   const auto& if_extension = std::get<instant_finality_extension>(*ext);

   BOOST_REQUIRE_EQUAL( if_extension.last_qc_block_num, last_qc_block_num );
   BOOST_REQUIRE_EQUAL( if_extension.is_last_qc_strong, is_last_qc_strong );

   BOOST_REQUIRE( !!if_extension.new_finalizer_policy );
   BOOST_REQUIRE_EQUAL(if_extension.new_finalizer_policy->generation, 1u);
   BOOST_REQUIRE_EQUAL(if_extension.new_finalizer_policy->threshold, 100u);
   BOOST_REQUIRE_EQUAL(if_extension.new_finalizer_policy->finalizers[0].description, "test description");
   BOOST_REQUIRE_EQUAL(if_extension.new_finalizer_policy->finalizers[0].weight, 50u);
   BOOST_REQUIRE_EQUAL(if_extension.new_finalizer_policy->finalizers[0].public_key.to_string(), "PUB_BLS_MPPeebAPxt/ibL2XPuZVGpADjGn+YEVPPoYmTZeBD6Ok2E19M8SnmDGSdZBf2qwSuJim+8H83EsTpEn3OiStWBiFeJYfVRLlEsZuSF0SYYwtVteY48n+KeE1IWzlSAkSyBqiGA==");

   BOOST_REQUIRE( !!if_extension.new_proposer_policy );
   BOOST_REQUIRE_EQUAL(if_extension.new_proposer_policy->schema_version, 1u);
   fc::time_point t = (fc::time_point)(if_extension.new_proposer_policy->active_time);
   BOOST_REQUIRE_EQUAL(t.time_since_epoch().to_seconds(), 946684900ll);
}

BOOST_AUTO_TEST_SUITE_END()
