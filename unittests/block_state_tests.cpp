#include <eosio/chain/block_state.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(block_state_tests)

BOOST_AUTO_TEST_CASE(aggregate_vote_test) try {
   using namespace eosio::chain;
   using namespace fc::crypto::blslib;

   digest_type block_id(fc::sha256("0000000000000000000000000000001"));

   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   std::vector<uint8_t> strong_digest_data(strong_digest.data(), strong_digest.data() + strong_digest.data_size());

   digest_type weak_digest(fc::sha256("0000000000000000000000000000003"));
   std::vector<uint8_t> weak_digest_data(weak_digest.data(), weak_digest.data() + weak_digest.data_size());

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
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->strong_digest = strong_digest;
      bsp->weak_digest = weak_digest;
      bsp->pending_qc = pending_quorum_certificate{ num_finalizers, bsp->active_finalizer_policy->finalizer_weights(), 1 };

      for (size_t i = 0; i < num_finalizers; ++i) {
         bool strong = (i % 2 == 0); // alternate strong and weak
         auto sig = strong ? private_key[i].sign(strong_digest_data) : private_key[i].sign(weak_digest_data);
         vote_message vote{ block_id, strong, public_key[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(vote).first);
      }
   }

   {  // public and private keys mismatched
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->strong_digest = strong_digest;
      bsp->pending_qc = pending_quorum_certificate{ num_finalizers, bsp->active_finalizer_policy->finalizer_weights(), 1 };

      vote_message vote {block_id, true, public_key[0], private_key[1].sign(strong_digest_data) };
      BOOST_REQUIRE(!bsp->aggregate_vote(vote).first);
   }

   {  // duplicate votes 
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->strong_digest = strong_digest;
      bsp->pending_qc = pending_quorum_certificate{ num_finalizers, bsp->active_finalizer_policy->finalizer_weights(), 1 };

      vote_message vote {block_id, true, public_key[0], private_key[0].sign(strong_digest_data) };
      BOOST_REQUIRE(bsp->aggregate_vote(vote).first);
      BOOST_REQUIRE(!bsp->aggregate_vote(vote).first);
   }

   {  // public key does not exit in finalizer set
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->strong_digest = strong_digest;
      bsp->pending_qc = pending_quorum_certificate{ num_finalizers, bsp->active_finalizer_policy->finalizer_weights(), 1 };

      bls_private_key new_private_key{ "PVT_BLS_warwI76e+pPX9wLFZKPFagngeFM8bm6J8D5w0iiHpxW7PiId" };
      bls_public_key new_public_key{ new_private_key.get_public_key() };

      vote_message vote {block_id, true, new_public_key, private_key[0].sign(strong_digest_data) };
      BOOST_REQUIRE(!bsp->aggregate_vote(vote).first);
   }
} FC_LOG_AND_RETHROW();

void do_quorum_test(const std::vector<uint64_t>& weights,
                    uint64_t threshold,
                    bool strong,
                    const std::vector<bool>& to_vote,
                    bool expected_quorum) {
   using namespace eosio::chain;
   using namespace fc::crypto::blslib;

   digest_type block_id(fc::sha256("0000000000000000000000000000001"));
   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   std::vector<uint8_t> strong_digest_data(strong_digest.data(), strong_digest.data() + strong_digest.data_size());
   digest_type weak_digest(fc::sha256("0000000000000000000000000000003"));
   std::vector<uint8_t> weak_digest_data(weak_digest.data(), weak_digest.data() + weak_digest.data_size());

   // initialize a set of private keys
   std::vector<bls_private_key> private_key {
      bls_private_key("PVT_BLS_r4ZpChd87ooyzl6MIkw23k7PRX8xptp7TczLJHCIIW88h/hS"),
      bls_private_key("PVT_BLS_/l7xzXANaB+GrlTsbZEuTiSOiWTtpBoog+TZnirxUUSaAfCo"),
      bls_private_key("PVT_BLS_3FoY73Q/gED3ejyg8cvnGqHrMmx4cLKwh/e0sbcsCxpCeqn3"),
   };
   const size_t num_finalizers = private_key.size();

   // construct finalizers
   std::vector<bls_public_key> public_key(num_finalizers);
   std::vector<finalizer_authority> finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      public_key[i] = private_key[i].get_public_key();
      finalizers[i] = finalizer_authority{ "test", weights[i], public_key[i] };
   }

   block_state_ptr bsp = std::make_shared<block_state>();
   constexpr uint32_t generation = 1;
   bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( generation, threshold, finalizers );
   bsp->strong_digest = strong_digest;
   bsp->weak_digest = weak_digest;
   bsp->pending_qc = pending_quorum_certificate{ num_finalizers, bsp->active_finalizer_policy->finalizer_weights(), threshold };

   for (size_t i = 0; i < num_finalizers; ++i) {
      if( to_vote[i] ) {
         auto sig = strong ? private_key[i].sign(strong_digest_data) : private_key[i].sign(weak_digest_data);
         vote_message vote{ block_id, strong, public_key[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(vote).first);
      }
   }

   BOOST_REQUIRE_EQUAL(bsp->pending_qc.is_quorum_met(), expected_quorum);
}

BOOST_AUTO_TEST_CASE(quorum_test) try {
   std::vector<uint64_t> weights{1, 3, 5};
   constexpr uint64_t threshold = 4;

   { // 1 strong vote, quorum not met
      constexpr bool strong = true;
      std::vector<bool> to_vote{true, false, false}; // finalizer 0 voting
      constexpr bool expected_quorum_met = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 2 strong votes, quorum met
      constexpr bool strong = true;
      std::vector<bool> to_vote{true, true, false}; // finalizers 0 and 1 voting
      constexpr bool expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 1 strong vote, quorum met
      constexpr bool strong = true;
      std::vector<bool> to_vote{false, false, true}; // finalizer 2 voting
      constexpr bool expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 1 weak vote, quorum not met
      constexpr bool strong = false;
      std::vector<bool> to_vote{true, false, false}; // finalizer 0 voting
      constexpr bool expected_quorum_met = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 2 weak votes, quorum met
      constexpr bool strong = false;
      std::vector<bool> to_vote{true, true, false}; // finalizers 0 and 1 voting
      constexpr bool expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 1 weak vote, quorum met
      constexpr bool strong = false;
      std::vector<bool> to_vote{false, false, true}; // finalizer 2 voting
      constexpr bool expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }
} FC_LOG_AND_RETHROW();
  
BOOST_AUTO_TEST_CASE(verify_qc_test) try {
   using namespace eosio::chain;
   using namespace fc::crypto::blslib;

   // prepare digests
   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   std::vector<uint8_t> strong_digest_data(strong_digest.data(), strong_digest.data() + strong_digest.data_size());
   digest_type weak_digest(fc::sha256("0000000000000000000000000000003"));
   std::vector<uint8_t> weak_digest_data(weak_digest.data(), weak_digest.data() + weak_digest.        data_size());

   // initialize a set of private keys
   std::vector<bls_private_key> private_key {
      bls_private_key("PVT_BLS_r4ZpChd87ooyzl6MIkw23k7PRX8xptp7TczLJHCIIW88h/hS"),
      bls_private_key("PVT_BLS_/l7xzXANaB+GrlTsbZEuTiSOiWTtpBoog+TZnirxUUSaAfCo"),
      bls_private_key("PVT_BLS_3FoY73Q/gED3ejyg8cvnGqHrMmx4cLKwh/e0sbcsCxpCeqn3")
   };
   auto num_finalizers = private_key.size();
  
   // construct finalizers, with weight 1, 2, 3 respectively
   std::vector<bls_public_key> public_key(num_finalizers);
   std::vector<finalizer_authority> finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      public_key[i] = private_key[i].get_public_key();
      uint64_t weight = i + 1;
      finalizers[i] = finalizer_authority{ "test", weight, public_key[i] };
   }
  
   // consturct a test bsp
   block_state_ptr bsp = std::make_shared<block_state>();
   constexpr uint32_t generation = 1;
   constexpr uint64_t threshold = 4; // 2/3 of total weights of 6
   bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( generation, threshold, finalizers );
   bsp->strong_digest = strong_digest;
   bsp->weak_digest = weak_digest;

   auto bitset_to_vector = [](const hs_bitset& bs) {
      std::vector<uint32_t> r;
      r.resize(bs.num_blocks());
      boost::to_block_range(bs, r.begin());
      return r;
   };

   {  // valid strong QC
      hs_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = private_key[0].sign(strong_digest_data);
      bls_signature sig_2 = private_key[2].sign(strong_digest_data);
      bls_signature agg_sig;
      agg_sig = fc::crypto::blslib::aggregate({agg_sig, sig_0});
      agg_sig = fc::crypto::blslib::aggregate({agg_sig, sig_2});

      // create a valid_quorum_certificate
      valid_quorum_certificate qc({}, {}, bitset_to_vector(strong_votes), {}, agg_sig);

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid weak QC
      hs_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = private_key[0].sign(strong_digest_data);

      hs_bitset weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = private_key[2].sign(weak_digest_data);

      bls_signature agg_sig;
      agg_sig = fc::crypto::blslib::aggregate({agg_sig, strong_sig});
      agg_sig = fc::crypto::blslib::aggregate({agg_sig, weak_sig});

      valid_quorum_certificate qc({}, {}, bitset_to_vector(strong_votes), bitset_to_vector(weak_votes), agg_sig);
      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid strong QC signed by all finalizers
      hs_bitset strong_votes(num_finalizers);
      std::vector<bls_signature> sigs(num_finalizers);
      bls_signature agg_sig;

      for (auto i = 0u; i < num_finalizers; ++i) {
         strong_votes[i] = 1;
         sigs[i] = private_key[i].sign(strong_digest_data);
         agg_sig = fc::crypto::blslib::aggregate({agg_sig, sigs[i]});
      }

      // create a valid_quorum_certificate
      valid_quorum_certificate qc({}, {}, bitset_to_vector(strong_votes), {}, agg_sig);

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid weak QC signed by all finalizers
      hs_bitset weak_votes(num_finalizers);
      std::vector<bls_signature> sigs(num_finalizers);
      bls_signature agg_sig;

      for (auto i = 0u; i < num_finalizers; ++i) {
         weak_votes[i] = 1;
         sigs[i] = private_key[i].sign(weak_digest_data);
         agg_sig = fc::crypto::blslib::aggregate({agg_sig, sigs[i]});
      }

      // create a valid_quorum_certificate
      valid_quorum_certificate qc({}, {}, {}, bitset_to_vector(weak_votes), agg_sig);

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // strong QC quorem not met
      hs_bitset strong_votes(num_finalizers);
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3 (threshold is 4)

      bls_signature sig_2 = private_key[2].sign(strong_digest_data);
      bls_signature agg_sig = fc::crypto::blslib::aggregate({agg_sig, sig_2});

      // create a valid_quorum_certificate
      valid_quorum_certificate qc({}, {}, bitset_to_vector(strong_votes), {}, agg_sig);

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), block_validate_exception, eosio::testing::fc_exception_message_starts_with("strong quorum is not met") );
   }

   {  // weak QC quorem not met
      hs_bitset weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3 (threshold is 4)

      bls_signature sig_2 = private_key[2].sign(weak_digest_data);
      bls_signature agg_sig = fc::crypto::blslib::aggregate({agg_sig, sig_2});

      // create a valid_quorum_certificate
      valid_quorum_certificate qc({}, {}, {}, bitset_to_vector(weak_votes), agg_sig);

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), block_validate_exception, eosio::testing::fc_exception_message_starts_with("weak quorum is not met") );
   }

   {  // strong QC with a wrong signing private key
      hs_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = private_key[0].sign(strong_digest_data);
      bls_signature sig_2 = private_key[1].sign(strong_digest_data); // signed by finalizer 1 which is not set in strong_votes
      bls_signature sig;
      sig = fc::crypto::blslib::aggregate({sig, sig_0});
      sig = fc::crypto::blslib::aggregate({sig, sig_2});

      // create a valid_quorum_certificate
      valid_quorum_certificate qc({}, {}, bitset_to_vector(strong_votes), {}, sig);

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), block_validate_exception, eosio::testing::fc_exception_message_is("signature validation failed") );
   }

   {  // strong QC with a wrong digest
      hs_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = private_key[0].sign(weak_digest_data); // should have used strong digest
      bls_signature sig_2 = private_key[2].sign(strong_digest_data);
      bls_signature sig;
      sig = fc::crypto::blslib::aggregate({sig, sig_0});
      sig = fc::crypto::blslib::aggregate({sig, sig_2});

      // create a valid_quorum_certificate
      valid_quorum_certificate qc({}, {}, bitset_to_vector(strong_votes), {}, sig);

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), block_validate_exception, eosio::testing::fc_exception_message_is("signature validation failed") );
   }

   {  // weak QC with a wrong signing private key
      hs_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = private_key[0].sign(strong_digest_data);

      hs_bitset weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = private_key[1].sign(weak_digest_data); // wrong key

      bls_signature sig;
      sig = fc::crypto::blslib::aggregate({sig, strong_sig});
      sig = fc::crypto::blslib::aggregate({sig, weak_sig});

      valid_quorum_certificate qc({}, {}, bitset_to_vector(strong_votes), bitset_to_vector(weak_votes), sig);
      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), block_validate_exception, eosio::testing::fc_exception_message_is("signature validation failed") );
   }

   {  // weak QC with a wrong digest
      hs_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = private_key[0].sign(weak_digest_data); // wrong digest

      hs_bitset weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = private_key[2].sign(weak_digest_data);

      bls_signature sig;
      sig = fc::crypto::blslib::aggregate({sig, strong_sig});
      sig = fc::crypto::blslib::aggregate({sig, weak_sig});

      valid_quorum_certificate qc({}, {}, bitset_to_vector(strong_votes), bitset_to_vector(weak_votes), sig);
      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), block_validate_exception, eosio::testing::fc_exception_message_is("signature validation failed") );
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
