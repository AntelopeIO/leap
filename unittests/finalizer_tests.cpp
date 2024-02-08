#include <eosio/chain/hotstuff/finalizer.hpp>
#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/testing/bls_utils.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(finalizer_tests)

BOOST_AUTO_TEST_CASE( finalizer_safety_file_io ) try {
   using fsi_t        = finalizer::safety_information;
   using proposal_ref = finalizer::proposal_ref;
   using tstamp       = block_timestamp_type;

   fc::temp_directory tempdir;
   auto safety_file_path = tempdir.path() / "finalizers" / "safety.dat";

   fsi_t fsi { tstamp(0), proposal_ref{sha256::hash("vote"), tstamp(7)}, proposal_ref{sha256::hash("lock"), tstamp(3)} };

   auto [privkey, pubkey, pop] = eosio::testing::get_bls_key("alice"_n);
   auto [privkey_str, pubkey_str] = std::pair{ privkey.to_string(), pubkey.to_string() };
   bls_pub_priv_key_map_t local_finalizers = { { pubkey_str, privkey_str } };

   {
      finalizer_set fset{.t_startup = block_timestamp_type{}, .persist_file_path = safety_file_path};
      fset.set_keys(local_finalizers);

      fset.set_fsi(pubkey, fsi);
      fset.save_finalizer_safety_info();
      //fset.set_fsi(pubkey, fsi_t::unset_fsi());
   }

   {
      finalizer_set fset{.t_startup = block_timestamp_type{}, .persist_file_path = safety_file_path};
      fset.set_keys(local_finalizers);

      BOOST_CHECK_EQUAL(fset.get_fsi(pubkey), fsi);
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
