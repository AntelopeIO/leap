#include <boost/test/unit_test.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_header.hpp>

#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <eosio/chain/hotstuff/qc_chain.hpp>

BOOST_AUTO_TEST_CASE(view_number_tests) try {
  using namespace eosio::chain;

  hs_proposal_message hspm_1;
  hs_proposal_message hspm_2;
  hs_proposal_message hspm_3;
  hs_proposal_message hspm_4;
  hs_proposal_message hspm_5;

  hspm_1.block_id = block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm_1.phase_counter = 0;

  hspm_2.block_id = block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm_2.phase_counter = 1;

  hspm_3.block_id = block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217068
  hspm_3.phase_counter = 0;

  hspm_4.block_id = block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217068
  hspm_4.phase_counter = 1;

  hspm_5.block_id = block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217068
  hspm_5.phase_counter = 2;

  view_number vn_1 = hspm_1.get_view_number();
  view_number vn_2 = hspm_2.get_view_number();
  view_number vn_3 = hspm_3.get_view_number();
  view_number vn_4 = hspm_4.get_view_number();
  view_number vn_5 = hspm_5.get_view_number();

  //test getters
  BOOST_CHECK_EQUAL(vn_1.block_height(), 194217067);
  BOOST_CHECK_EQUAL(vn_1.phase_counter(), 0);

  BOOST_CHECK_NE(vn_1, vn_2);
  BOOST_CHECK_LT(vn_1, vn_2);
  BOOST_CHECK_LT(vn_2, vn_3);
  BOOST_CHECK_LT(vn_3, vn_4);
  BOOST_CHECK_LT(vn_4, vn_5);
  BOOST_CHECK_LE(vn_4, vn_5);
  BOOST_CHECK_LE(vn_2, vn_3);

//test constructor

  view_number vn_6 = view_number(194217068, 2);

  BOOST_CHECK_EQUAL(vn_5, vn_6);

} FC_LOG_AND_RETHROW();


// -----------------------------------------------------------------------------
//            Allow boost to print `pending_quorum_certificate::state_t`
// -----------------------------------------------------------------------------
namespace std {
   using state_t = eosio::chain::pending_quorum_certificate::state_t;
   std::ostream& operator<<(std::ostream& os, state_t s)
   {
      switch(s) {
      case state_t::unrestricted:   os << "unrestricted"; break;
      case state_t::restricted:     os << "restricted"; break;
      case state_t::weak_achieved:  os << "weak_achieved"; break;
      case state_t::weak_final:     os << "weak_final"; break;
      case state_t::strong:         os << "strong"; break;
      }
      return os;
   }
}

BOOST_AUTO_TEST_CASE(qc_state_transitions) try {
   using namespace eosio::chain;
   using namespace fc::crypto::blslib;
   using state_t = pending_quorum_certificate::state_t;

   digest_type d(fc::sha256("0000000000000000000000000000001"));
   std::vector<uint8_t> digest(d.data(), d.data() + d.data_size());

   std::vector<bls_private_key> sk {
      bls_private_key("PVT_BLS_r4ZpChd87ooyzl6MIkw23k7PRX8xptp7TczLJHCIIW88h/hS"),
      bls_private_key("PVT_BLS_/l7xzXANaB+GrlTsbZEuTiSOiWTtpBoog+TZnirxUUSaAfCo"),
      bls_private_key("PVT_BLS_3FoY73Q/gED3ejyg8cvnGqHrMmx4cLKwh/e0sbcsCxpCeqn3"),
      bls_private_key("PVT_BLS_warwI76e+pPX9wLFZKPFagngeFM8bm6J8D5w0iiHpxW7PiId"),
      bls_private_key("PVT_BLS_iZFwiqdogOl9RNr1Hv1z+Rd6AwD9BIoxZcU1EPX+XFSFmm5p"),
      bls_private_key("PVT_BLS_Hmye7lyiCrdF54/nF/HRU0sY/Hrse1ls/yqojIUOVQsxXUIK")
   };

   std::vector<bls_public_key> pubkey;
   pubkey.reserve(sk.size());
   for (const auto& k : sk)
      pubkey.push_back(k.get_public_key());

   auto weak_vote = [&](pending_quorum_certificate& qc, const std::vector<uint8_t>& digest, size_t index) {
      return qc.add_weak_vote(digest, index, pubkey[index], sk[index].sign(digest));
   };

   auto strong_vote = [&](pending_quorum_certificate& qc, const std::vector<uint8_t>& digest, size_t index) {
      return qc.add_strong_vote(digest, index, pubkey[index], sk[index].sign(digest));
   };

   {
      pending_quorum_certificate qc(2, 1); // 2 finalizers, quorum = 1
      BOOST_CHECK_EQUAL(qc._state, state_t::unrestricted);

      // add one weak vote
      // -----------------
      weak_vote(qc, digest, 0);
      BOOST_CHECK_EQUAL(qc._state, state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      // add duplicate weak vote
      // -----------------------
      bool ok = weak_vote(qc, digest, 0);
      BOOST_CHECK(!ok); // vote was a duplicate
      BOOST_CHECK_EQUAL(qc._state, state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      // add another weak vote
      // ---------------------
      weak_vote(qc, digest, 1);
      BOOST_CHECK_EQUAL(qc._state, state_t::weak_final);
   }

   {
      pending_quorum_certificate qc(2, 1); // 2 finalizers, quorum = 1
      BOOST_CHECK_EQUAL(qc._state, state_t::unrestricted);

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 0);
      BOOST_CHECK_EQUAL(qc._state, state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1);
      BOOST_CHECK_EQUAL(qc._state, state_t::strong);
      BOOST_CHECK(qc.is_quorum_met());
   }

   {
      pending_quorum_certificate qc(2, 1); // 2 finalizers, quorum = 1
      BOOST_CHECK_EQUAL(qc._state, state_t::unrestricted);

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1);
      BOOST_CHECK_EQUAL(qc._state, state_t::strong);
      BOOST_CHECK(qc.is_quorum_met());

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1);
      BOOST_CHECK_EQUAL(qc._state, state_t::strong);
      BOOST_CHECK(qc.is_quorum_met());
   }

   {
      pending_quorum_certificate qc(3, 2); // 3 finalizers, quorum = 2

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 0);
      BOOST_CHECK_EQUAL(qc._state, state_t::unrestricted);
      BOOST_CHECK(!qc.is_quorum_met());

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1);
      BOOST_CHECK_EQUAL(qc._state, state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      {
         pending_quorum_certificate qc2(qc);

         // add a weak vote
         // ---------------
         weak_vote(qc2, digest, 2);
         BOOST_CHECK_EQUAL(qc2._state, state_t::weak_final);
         BOOST_CHECK(qc2.is_quorum_met());
      }

      {
         pending_quorum_certificate qc2(qc);

         // add a strong vote
         // -----------------
         strong_vote(qc2, digest, 2);
         BOOST_CHECK_EQUAL(qc2._state, state_t::strong);
         BOOST_CHECK(qc2.is_quorum_met());
      }
   }

   {
      pending_quorum_certificate qc(3, 2); // 3 finalizers, quorum = 2

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 0);
      BOOST_CHECK_EQUAL(qc._state, state_t::unrestricted);
      BOOST_CHECK(!qc.is_quorum_met());

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 1);
      BOOST_CHECK_EQUAL(qc._state, state_t::weak_final);
      BOOST_CHECK(qc.is_quorum_met());

      {
         pending_quorum_certificate qc2(qc);

         // add a weak vote
         // ---------------
         weak_vote(qc2, digest, 2);
         BOOST_CHECK_EQUAL(qc2._state, state_t::weak_final);
         BOOST_CHECK(qc2.is_quorum_met());
      }

      {
         pending_quorum_certificate qc2(qc);

         // add a strong vote
         // -----------------
         strong_vote(qc2, digest, 2);
         BOOST_CHECK_EQUAL(qc2._state, state_t::weak_final);
         BOOST_CHECK(qc2.is_quorum_met());
      }
   }

} FC_LOG_AND_RETHROW();
