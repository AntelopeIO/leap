#include <eosio/chain/block_state.hpp>

#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(block_state_tests)

BOOST_AUTO_TEST_CASE(aggregate_vote_test) try {
   using namespace eosio::chain;
   using namespace fc::crypto::blslib;

   digest_type proposal_id(fc::sha256("0000000000000000000000000000001"));

   digest_type proposal_digest(fc::sha256("0000000000000000000000000000002"));
   std::vector<uint8_t> proposal_digest_data(proposal_digest.data(), proposal_digest.data() + proposal_digest.data_size());

   const size_t num_finalizers = 3;

   // initialize a set of private keys
   std::vector<bls_private_key> private_key {
      bls_private_key("PVT_BLS_r4ZpChd87ooyzl6MIkw23k7PRX8xptp7TczLJHCIIW88h/hS"),
      bls_private_key("PVT_BLS_/l7xzXANaB+GrlTsbZEuTiSOiWTtpBoog+TZnirxUUSaAfCo"),
      bls_private_key("PVT_BLS_3FoY73Q/gED3ejyg8cvnGqHrMmx4cLKwh/e0sbcsCxpCeqn3"),
   };

   // construct finalizers
   std::vector<bls_public_key> public_key(num_finalizers);
   std::vector<finalizer_authority> finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      public_key[i] = private_key[i].get_public_key();
      finalizers[i] = finalizer_authority{ "test", 1, public_key[i] };
   }

   {  // all finalizers can aggregate votes
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->pending_qc = pending_quorum_certificate{ proposal_id, proposal_digest, num_finalizers, 1 };

      for (size_t i = 0; i < num_finalizers; ++i) {
         bool strong = (i % 2 == 0); // alternate strong and weak
         hs_vote_message vote {proposal_id, strong, public_key[i], private_key[i].sign(proposal_digest_data) };
         BOOST_REQUIRE(bsp->aggregate_vote(vote));
      }
   }

   {  // public and private keys mismatched
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->pending_qc = pending_quorum_certificate{ proposal_id, proposal_digest, num_finalizers, 1 };

      hs_vote_message vote {proposal_id, true, public_key[0], private_key[1].sign(proposal_digest_data) };
      BOOST_REQUIRE(!bsp->aggregate_vote(vote));
   }

   {  // duplicate votes 
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->pending_qc = pending_quorum_certificate{ proposal_id, proposal_digest, num_finalizers, 1 };

      hs_vote_message vote {proposal_id, true, public_key[0], private_key[0].sign(proposal_digest_data) };
      BOOST_REQUIRE(bsp->aggregate_vote(vote));
      BOOST_REQUIRE(!bsp->aggregate_vote(vote));
   }

   {  // public key does not exit in finalizer set
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->pending_qc = pending_quorum_certificate{ proposal_id, proposal_digest, num_finalizers, 1 };

      bls_private_key new_private_key{ "PVT_BLS_warwI76e+pPX9wLFZKPFagngeFM8bm6J8D5w0iiHpxW7PiId" };
      bls_public_key new_public_key{ new_private_key.get_public_key() };

      hs_vote_message vote {proposal_id, true, new_public_key, private_key[0].sign(proposal_digest_data) };
      BOOST_REQUIRE(!bsp->aggregate_vote(vote));
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
