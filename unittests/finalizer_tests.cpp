#include <eosio/chain/hotstuff/finalizer.hpp>

#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/testing/bls_utils.hpp>

#include "mock_utils.hpp"

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;

using tstamp  = block_timestamp_type;
using fsi_t   = finalizer_safety_information;

struct bls_keys_t {
   bls_private_key privkey;
   bls_public_key  pubkey;
   std::string     privkey_str;
   std::string     pubkey_str;

   bls_keys_t(name n) {
      bls_signature pop;
      std::tie(privkey, pubkey, pop)    = eosio::testing::get_bls_key(n);
      std::tie(privkey_str, pubkey_str) = std::pair{ privkey.to_string(), pubkey.to_string() };
   }
};

template<class FSI>
std::vector<FSI> create_random_fsi(size_t count) {
   std::vector<FSI> res;
   res.reserve(count);
   for (size_t i=0; i<count; ++i) {
      res.push_back(FSI{tstamp(i),
                        proposal_ref{sha256::hash((const char *)"vote"), tstamp(i*100 + 3)},
                        proposal_ref{sha256::hash((const char *)"lock"), tstamp(i*100)} });
      if (i)
         assert(res.back() != res[0]);
   }
   return res;
}

std::vector<proposal_ref> create_proposal_refs(size_t count) {
   std::vector<proposal_ref> res;
   res.reserve(count);
   for (size_t i=0; i<count; ++i) {
      std::string id_str {"vote"};
      id_str += std::to_string(i);
      res.push_back(proposal_ref{sha256::hash(id_str.c_str()), tstamp(i)});
   }
   return res;
}

std::vector<bls_keys_t> create_keys(size_t count) {
   std::vector<bls_keys_t> res;
   res.reserve(count);
   for (size_t i=0; i<count; ++i) {
      std::string s("alice");
      s.append(3, 'a'+i);
      res.push_back(bls_keys_t(name(s)));
      if (i)
         assert(res.back().privkey != res[0].privkey);
   }
   return res;
}

template <size_t... I>
bls_pub_priv_key_map_t create_local_finalizers(const std::vector<bls_keys_t>& keys) {
   bls_pub_priv_key_map_t res;
   ((res[keys[I].pubkey_str] = keys[I].privkey_str), ...);
   return res;
}

template <class FSI_VEC, size_t... I>
void set_fsi(my_finalizers_t& fset, const std::vector<bls_keys_t>& keys, const FSI_VEC& fsi) {
   ((fset.set_fsi(keys[I].pubkey, fsi[I])), ...);
}

BOOST_AUTO_TEST_SUITE(finalizer_tests)

BOOST_AUTO_TEST_CASE( basic_finalizer_safety_file_io ) try {
   fc::temp_directory tempdir;
   auto safety_file_path = tempdir.path() / "finalizers" / "safety.dat";
   auto proposals { create_proposal_refs(10) };

   fsi_t fsi { .last_vote_range_start = tstamp(0),
               .last_vote = proposals[6],
               .lock = proposals[2] };

   bls_keys_t k("alice"_n);
   bls_pub_priv_key_map_t local_finalizers = { { k.pubkey_str, k.privkey_str } };

   {
      my_finalizers_t fset{.t_startup = block_timestamp_type{}, .persist_file_path = safety_file_path};
      fset.set_keys(local_finalizers);

      fset.set_fsi(k.pubkey, fsi);
      fset.save_finalizer_safety_info();

      // at this point we have saved the finalizer safety file
      // so destroy the my_finalizers_t object
   }

   {
      my_finalizers_t fset{.t_startup = block_timestamp_type{}, .persist_file_path = safety_file_path};
      fset.set_keys(local_finalizers); // that's when the finalizer safety file is read

      // make sure the safety info for our finalizer that we saved above is restored correctly
      BOOST_CHECK_EQUAL(fset.get_fsi(k.pubkey), fsi);
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( finalizer_safety_file_io ) try {
   fc::temp_directory tempdir;
   auto safety_file_path = tempdir.path() / "finalizers" / "safety.dat";

   std::vector<fsi_t> fsi = create_random_fsi<fsi_t>(10);
   std::vector<bls_keys_t> keys = create_keys(10);

   {
      my_finalizers_t fset{.t_startup  = block_timestamp_type{}, .persist_file_path = safety_file_path};
      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<1, 3, 5, 6>(keys);
      fset.set_keys(local_finalizers);

      set_fsi<decltype(fsi), 1, 3, 5, 6>(fset, keys, fsi);
      fset.save_finalizer_safety_info();

      // at this point we have saved the finalizer safety file, containing a specific fsi for finalizers <1, 3, 5, 6>
      // so destroy the my_finalizers_t object
   }

   {
      my_finalizers_t fset{.t_startup = block_timestamp_type{}, .persist_file_path = safety_file_path};
      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<3>(keys);
      fset.set_keys(local_finalizers);

      // make sure the safety info for our finalizer that we saved above is restored correctly
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[3].pubkey), fsi[3]);

      // OK, simulate a couple rounds of voting
      fset.set_fsi(keys[3].pubkey, fsi[4]);
      fset.save_finalizer_safety_info();

      // now finalizer 3 should have fsi[4] saved
   }

   {
      my_finalizers_t fset{.t_startup = block_timestamp_type{}, .persist_file_path = safety_file_path};
      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<3>(keys);
      fset.set_keys(local_finalizers);

      // make sure the safety info for our finalizer that we saved above is restored correctly
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[3].pubkey), fsi[4]);
   }

   // even though we didn't activate finalizers 1, 5, or 6 in the prior test, and we wrote the safety file,
   // make sure we have not lost the fsi that was set originally for these finalizers.
   {
      my_finalizers_t fset{.t_startup = block_timestamp_type{}, .persist_file_path = safety_file_path};
      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<1, 5, 6>(keys);
      fset.set_keys(local_finalizers);

      // make sure the safety info for our previously inactive finalizer was preserved
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[1].pubkey), fsi[1]);
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[5].pubkey), fsi[5]);
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[6].pubkey), fsi[6]);
   }

} FC_LOG_AND_RETHROW()

#if 0
// real finalizer, using mock::forkdb and mock::bsp
// using test_finalizer_t = finalizer_tpl<mock_utils::forkdb_t>;

block_state_ptr make_bsp(const mock_utils::proposal_t& p, const block_state_ptr& head,
                         std::optional<qc_claim_t> claim = {}) {
   block_header_state bhs;
   auto id = p.calculate_id();
   // genesis block
   block_header_state_core new_core;
   if (p.block_num() > 0)
      new_core = claim ? head->core.next(*claim) : head->core;
   bhs = block_header_state{ .block_id = id,
                             .header = block_header(),
                             .activated_protocol_features = {},
                             .core = new_core  };
   block_state_ptr bsp = std::make_shared<block_state>(block_state{bhs, {}, {}, {}});
   return bsp;
}

// ---------------------------------------------------------------------------------------
template <class FORKDB, class PROPOSAL>
struct simulator_t {
   using finalizer_t = finalizer_tpl<FORKDB>;
   using bs = typename FORKDB::bs;
   using bsp = typename FORKDB::bsp;

   bls_keys_t  keys;
   FORKDB      forkdb;
   finalizer_t finalizer;

   simulator_t() :
      keys("alice"_n),
      finalizer(keys.privkey) {

      auto genesis = make_bsp(mock_utils::proposal_t{0, "n0", block_timestamp_type{0}}, bsp());
      forkdb.add(genesis);

      proposal_ref genesis_ref(genesis);
      finalizer.fsi = fsi_t{block_timestamp_type(0), genesis_ref, {}};
   }

   std::optional<bool> vote(const bsp& p) {
      auto decision = finalizer.decide_vote(p, forkdb);
      switch(decision) {
      case finalizer_t::vote_decision::strong_vote: return true;
      case finalizer_t::vote_decision::weak_vote:   return false;
      default: break;
      }
      return {};
   }

   std::optional<bool> propose(const PROPOSAL& p) {
      bsp h = make_bsp(p, forkdb.head());
      forkdb.add(h);
      auto v = vote(h);
      return v;
   }

   std::pair<bsp, bhs_core::qc_claim> add(const PROPOSAL& p, std::optional<bhs_core::qc_claim> _claim = {}) {
      bsp h = forkdb.head();
      bhs_core::qc_claim old_claim = _claim ? *_claim : bhs_core::qc_claim{h->last_qc_block_num(), false};
      bsp new_bsp = make_bsp(p, h, _claim);
      forkdb.add(new_bsp);

      auto v = vote(new_bsp);
      if (v)
         return {forkdb.head(), new_bsp->latest_qc_claim()};
      return {forkdb.head(), old_claim};
   }
};

#if 0
// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_monotony_check ) try {
   auto proposals { create_proposal_refs(10) };
   fsi_t fsi      { .last_vote_range_start = tstamp(0),
                    .last_vote = proposals[6],
                    .lock = proposals[2] };
   using namespace mock_utils;
   simulator_t<fork_database_if_t, proposal_t> sim;

   auto vote = sim.propose(proposal_t{1, "n0", block_timestamp_type{1}});
   BOOST_CHECK(vote && *vote);
   //bls_keys_t     k("alice"_n);
   //test_finalizer_t finalizer{k.privkey, fsi};


} FC_LOG_AND_RETHROW()
#endif

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( proposal_sim_1 ) try {
   using namespace mock_utils;

   simulator_t<forkdb_t, proposal_t> sim;

   auto [head1, claim1] = sim.add(proposal_t{1, "n0", block_timestamp_type{1}}, bhs_core::qc_claim{0, false});
   BOOST_CHECK_EQUAL(claim1.block_num, 1);

   auto [head2, claim2] = sim.add(proposal_t{2, "n0", block_timestamp_type{2}}, claim1);
   BOOST_CHECK_EQUAL(claim2.block_num, 2);

} FC_LOG_AND_RETHROW()
#endif

BOOST_AUTO_TEST_SUITE_END()
