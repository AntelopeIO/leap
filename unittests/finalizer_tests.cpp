#include <eosio/chain/controller.hpp>
#include <eosio/chain/hotstuff/finalizer.hpp>
#include <eosio/chain/finality_core.hpp>

#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/testing/bls_utils.hpp>
#include <fc/bitutil.hpp>

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

using eosio::chain::finality_core;
using eosio::chain::block_ref;
using bs   = eosio::chain::block_state;
using bsp  = eosio::chain::block_state_ptr;
using bhs  = eosio::chain::block_header_state;
using bhsp = eosio::chain::block_header_state_ptr;
using vote_decision = finalizer::vote_decision;
using vote_result   = finalizer::vote_result;


// ---------------------------------------------------------------------------------------
inline block_id_type calc_id(block_id_type id, uint32_t block_number) {
   id._hash[0] &= 0xffffffff00000000;
   id._hash[0] += fc::endian_reverse_u32(block_number);
   return id;
}

// ---------------------------------------------------------------------------------------
struct proposal_t {
   uint32_t             block_number;
   std::string          proposer_name;
   block_timestamp_type block_timestamp;

   proposal_t(uint32_t block_number, const char* proposer, std::optional<uint32_t> timestamp = {}) :
      block_number(block_number), proposer_name(proposer), block_timestamp(timestamp ? *timestamp : block_number)
   {}

   const std::string&   proposer()  const { return proposer_name; }
   block_timestamp_type timestamp() const { return block_timestamp; }
   uint32_t             block_num() const { return block_number; }

   block_id_type calculate_id() const
   {
      std::string   id_str = proposer_name + std::to_string(block_number);
      return calc_id(fc::sha256::hash(id_str.c_str()), block_number);
   }

   explicit operator block_ref() const {
      return block_ref{calculate_id(), timestamp()};
   }
};

// ---------------------------------------------------------------------------------------
bsp make_bsp(const proposal_t& p, const bsp& previous, finalizer_policy_ptr finpol,
             std::optional<qc_claim_t> claim = {}) {
   auto makeit = [](bhs &&h) {
      bs new_bs;
      dynamic_cast<bhs&>(new_bs) = std::move(h);
      new_bs.validated = true;
      return std::make_shared<bs>(std::move(new_bs));
   };

   if (p.block_num() == 0) {
      // special case of genesis block
      block_ref ref{calc_id(fc::sha256::hash("genesis"), 0), block_timestamp_type{0}};
      bhs new_bhs { ref.block_id, block_header{ref.timestamp}, {},
                    finality_core::create_core_for_genesis_block(0), {}, {}, std::move(finpol) };
      return makeit(std::move(new_bhs));
   }

   assert(claim);
   block_ref ref{previous->id(), previous->timestamp()};
   bhs new_bhs { p.calculate_id(), block_header{p.block_timestamp, {}, {}, previous->id()}, {}, previous->core.next(ref, *claim),
                 {}, {}, std::move(finpol) };
   return makeit(std::move(new_bhs));
}

// ---------------------------------------------------------------------------------------
// simulates one finalizer voting on its own proposals "n0", and other proposals received
// from the network.
struct simulator_t {
   using core = finality_core;

   bls_keys_t           keys;
   finalizer            my_finalizer;
   fork_database_if_t   forkdb;
   finalizer_policy_ptr finpol;
   std::vector<bsp>     bsp_vec;

   struct result {
      bsp         new_bsp;
      vote_result vote;

      qc_claim_t new_claim() const {
         if (vote.decision == vote_decision::no_vote)
            return new_bsp->core.latest_qc_claim();
         return { new_bsp->block_num(), vote.decision == vote_decision::strong_vote };
      }
   };

   simulator_t() :
      keys("alice"_n),
      my_finalizer(keys.privkey) {

      finalizer_policy fin_policy;
      fin_policy.threshold = 0;
      fin_policy.finalizers.push_back({"n0", 1, keys.pubkey});
      finpol = std::make_shared<finalizer_policy>(fin_policy);

      auto genesis = make_bsp(proposal_t{0, "n0"}, bsp(), finpol);
      bsp_vec.push_back(genesis);
      forkdb.reset(*genesis);

      block_ref genesis_ref(genesis->id(), genesis->timestamp());
      my_finalizer.fsi = fsi_t{block_timestamp_type(0), genesis_ref, genesis_ref};
   }

   vote_result vote(const bhsp& p) {
      auto vote_res = my_finalizer.decide_vote(p->core, p->id(), p->timestamp());
      return vote_res;
   }

   vote_result propose(const proposal_t& p, std::optional<qc_claim_t> _claim = {}) {
      bsp h = forkdb.head();
      qc_claim_t old_claim = _claim ? *_claim : h->core.latest_qc_claim();
      bsp new_bsp = make_bsp(p, h, finpol, old_claim);
      bsp_vec.push_back(new_bsp);
      auto v = vote(new_bsp);
      return v;
   }

   result add(const proposal_t& p, std::optional<qc_claim_t> _claim = {}, const bsp& parent = {}) {
      bsp h = parent ? parent : forkdb.head();
      qc_claim_t old_claim = _claim ? *_claim : h->core.latest_qc_claim();
      bsp new_bsp = make_bsp(p, h, finpol, old_claim);
      bsp_vec.push_back(new_bsp);
      forkdb.add(new_bsp);

      auto v = vote(new_bsp);
      return { new_bsp, v };
   }
};

#if 0
   auto proposals { create_proposal_refs(10) };
   fsi_t fsi      { .last_vote_range_start = tstamp(0),
                    .last_vote = proposals[6],
                    .lock = proposals[2] };
#endif

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_basic ) try {
   simulator_t sim;
   // this proposal verifies all properties and extends genesis => expect strong vote
   auto res = sim.add({1, "n0"});
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_no_vote_if_finalizer_safety_lock_empty ) try {
   simulator_t sim;
   sim.my_finalizer.fsi.lock = {};    // force lock empty... finalizer should not vote
   auto res = sim.add({1, "n0"});
   BOOST_CHECK(res.vote.decision == vote_decision::no_vote);
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_normal_vote_sequence ) try {
   simulator_t sim;
   qc_claim_t new_claim { 0, true };
   for (uint32_t i=1; i<10; ++i) {
      auto res = sim.add({i, "n0"}, new_claim);
      BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
      BOOST_CHECK_EQUAL(new_claim, res.new_bsp->core.latest_qc_claim());
      new_claim = { res.new_bsp->block_num(), res.vote.decision == vote_decision::strong_vote };

      auto lib { res.new_bsp->core.last_final_block_num() };
      BOOST_CHECK_EQUAL(lib, i <= 2 ? 0 : i - 3);

      auto final_on_strong_qc { res.new_bsp->core.final_on_strong_qc_block_num };
      BOOST_CHECK_EQUAL(final_on_strong_qc, i <= 1 ? 0 : i - 2);
   }
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_monotony_check ) try {
   simulator_t sim;

   auto res = sim.add({1, "n0", 1});
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);

   auto res2 = sim.add({2, "n0", 1});
   BOOST_CHECK_EQUAL(res2.vote.monotony_check, false);
   BOOST_CHECK(res2.vote.decision == vote_decision::no_vote); // use same timestamp as previous proposal => should not vote

} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_liveness_check ) try {
   simulator_t sim;
   qc_claim_t new_claim { 0, true };
   for (uint32_t i=1; i<10; ++i) {
      auto res = sim.add({i, "n0", i}, new_claim);
      BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
      BOOST_CHECK_EQUAL(new_claim, res.new_bsp->core.latest_qc_claim());
      new_claim = res.new_claim();

      auto lib { res.new_bsp->core.last_final_block_num() };
      BOOST_CHECK_EQUAL(lib, i <= 2 ? 0 : i - 3);

      auto final_on_strong_qc { res.new_bsp->core.final_on_strong_qc_block_num };
      BOOST_CHECK_EQUAL(final_on_strong_qc, i <= 1 ? 0 : i - 2);

      if (i > 2)
         BOOST_CHECK_EQUAL(sim.my_finalizer.fsi.lock.block_id, sim.bsp_vec[i-2]->id());
   }

   // we just issued proposal #9. Verify we are locked on proposal #7 and our last_vote is #9
   BOOST_CHECK_EQUAL(sim.my_finalizer.fsi.lock.block_id, sim.bsp_vec[7]->id());
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 9);

   // proposal #6 from "n0" is final (although "n1" may not know it yet).
   // proposal #7 would be final if it receives a strong QC

   // let's have "n1" build on proposal #6. Default will use timestamp(7) so we will fail the monotony check
   auto res = sim.add({7, "n1"}, {}, sim.bsp_vec[6]);
   BOOST_CHECK(res.vote.decision == vote_decision::no_vote);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, false);

   // les's vote for a couple more proposale, and finally when we'll reach timestamp 10 the
   // monotony check will pass (both liveness and safety check should still fail)
   // ------------------------------------------------------------------------------------
   res = sim.add({8, "n1"}, {}, res.new_bsp);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, false);

   res = sim.add({9, "n1"}, {}, res.new_bsp);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, false);

   res = sim.add({10, "n1"}, {}, res.new_bsp);
   BOOST_CHECK(res.vote.decision == vote_decision::no_vote);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, false);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false);

   // No matter how long we keep voting on this branch without a new qc claim, we will never achieve
   // liveness or safety again
   // ----------------------------------------------------------------------------------------------
   for (uint32_t i=11; i<20; ++i) {
      res = sim.add({i, "n1"}, {}, res.new_bsp);

      BOOST_CHECK(res.vote.decision == vote_decision::no_vote);
      BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
      BOOST_CHECK_EQUAL(res.vote.liveness_check, false);
      BOOST_CHECK_EQUAL(res.vote.safety_check,   false);
   }

   // Now suppose we receive a qc in a block that was created in the "n0" branch, for example the qc from
   // proposal 8. We can get it from sim.bsp_vec[9]->core.latest_qc_claim()
   // liveness should be restored, because core.latest_qc_block_timestamp() > fsi.lock.timestamp
   // ---------------------------------------------------------------------------------------------------
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 9);
   new_claim = sim.bsp_vec[9]->core.latest_qc_claim();
   res = sim.add({20, "n1"}, new_claim, res.new_bsp);


   BOOST_CHECK(res.vote.decision == vote_decision::weak_vote); // because !time_range_disjoint and fsi.last_vote == 9
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 20);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, true);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false); // because liveness_check is true, safety is not checked.

   new_claim = res.new_claim();
   res = sim.add({21, "n1"}, new_claim, res.new_bsp);
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote); // because core.extends(fsi.last_vote.block_id);
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 21);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, true);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false); // because liveness_check is true, safety is not checked.

   // this new proposal we just voted strong on was just building on proposal #6 and we had not advanced
   // the core until the last proposal which provided a new qc_claim_t.
   // as a result we now have a final_on_strong_qc = 5 (because the vote on 20 was weak)
   // --------------------------------------------------------------------------------------------------
   auto final_on_strong_qc = res.new_bsp->core.final_on_strong_qc_block_num;
   BOOST_CHECK_EQUAL(final_on_strong_qc, 5);

   // Our finalizer should still be locked on the initial proposal 7 (we have not updated our lock because
   // `(final_on_strong_qc_block_ref.timestamp > fsi.lock.timestamp)` is false
   // ----------------------------------------------------------------------------------------------------
   BOOST_CHECK_EQUAL(sim.my_finalizer.fsi.lock.block_id, sim.bsp_vec[7]->id());

   new_claim = res.new_claim();
   res = sim.add({22, "n1"}, new_claim, res.new_bsp);
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 22);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, true);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false); // because liveness_check is true, safety is not checked.
   final_on_strong_qc = res.new_bsp->core.final_on_strong_qc_block_num;
   BOOST_CHECK_EQUAL(final_on_strong_qc, 20);


#if 0
   std::cout << final_on_strong_qc << '\n';
   auto& ref { res.new_bsp->core.get_block_reference(final_on_strong_qc) };
   BOOST_CHECK_EQUAL(sim.my_finalizer.fsi.lock.timestamp,  ref.timestamp);
   BOOST_CHECK_EQUAL(sim.my_finalizer.fsi.lock.block_id, ref.block_id);


   //res = sim.add({10, "n1", 12}, {}, res.new_bsp);

   std::cout << res.vote << '\n';
#endif
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()
