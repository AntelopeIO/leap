#define BOOST_TEST_MODULE hotstuff

#include <boost/test/included/unit_test.hpp>

#include <fc/exception/exception.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/io/varint.hpp>

#include <boost/dynamic_bitset.hpp>

#include <eosio/hotstuff/test_pacemaker.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

#include <fc/crypto/bls_utils.hpp>

using namespace eosio::hotstuff;

using std::cout;

std::vector<block_id_type> ids{ block_id_type("00000001d49031dba775bd2b44fd339a329ef462aaf019e5b75b4cd9609a0c39"),
   block_id_type("0000000202b23f86652ae43cba4bec5579c8c7133c14011a6f8d93b316530684"),
   block_id_type("00000003a5a001518358977e84a3f6abf87bf32a6e739ced9a7a3f6b0b8bf330")};

std::vector<block_id_type> alternate_ids{ block_id_type("00000001d49031dba775bd2b44fd339a329ef462aaf019e5b75b4cd9609a0c31"),
   block_id_type("0000000202b23f86652ae43cba4bec5579c8c7133c14011a6f8d93b316530681"),
   block_id_type("00000003a5a001518358977e84a3f6abf87bf32a6e739ced9a7a3f6b0b8bf331")};

//list of unique replicas for our test
std::vector<name> unique_replicas {
   "bpa"_n, "bpb"_n, "bpc"_n,
   "bpd"_n, "bpe"_n, "bpf"_n,
   "bpg"_n, "bph"_n, "bpi"_n,
   "bpj"_n, "bpk"_n, "bpl"_n,
   "bpm"_n, "bpn"_n, "bpo"_n,
   "bpp"_n, "bpq"_n, "bpr"_n,
   "bps"_n, "bpt"_n, "bpu"_n };

class hotstuff_test_handler {
public:

   std::vector<std::pair<name, std::shared_ptr<qc_chain>>> _qc_chains;

   void initialize_qc_chains(test_pacemaker& tpm, std::vector<name> info_loggers, std::vector<name> error_loggers, std::vector<name> replicas){

      _qc_chains.clear();

      // These used to be able to break the tests. Might be useful at some point.
      _qc_chains.reserve( 100 );
      //_qc_chains.reserve( 10000 );
      //_qc_chains.reserve( 15 );
      //_qc_chains.reserve( replicas.size() );

      for (name r : replicas) {

         bool log = std::find(info_loggers.begin(), info_loggers.end(), r) != info_loggers.end();
         bool err = std::find(error_loggers.begin(), error_loggers.end(), r) != error_loggers.end();

         //If you want to force logging everything
         //log = err = true;

         qc_chain *qcc_ptr = new qc_chain(r, &tpm, {r}, log, err);
         std::shared_ptr<qc_chain> qcc_shared_ptr(qcc_ptr);

         _qc_chains.push_back( std::make_pair(r, qcc_shared_ptr) );

         tpm.register_qc_chain( r, qcc_shared_ptr );
      }
  }

   void print_msgs(std::vector<test_pacemaker::hotstuff_message> msgs ){

      size_t proposals_count = 0;
      size_t votes_count = 0;
      size_t new_blocks_count = 0;
      size_t new_views_count = 0;

      auto msg_itr = msgs.begin();

      while (msg_itr!=msgs.end()){

         size_t v_index = msg_itr->second.index();

         if(v_index==0) proposals_count++;
         if(v_index==1) votes_count++;
         if(v_index==2) new_blocks_count++;
         if(v_index==3) new_views_count++;

         msg_itr++;
      }

      std::cout << "\n";

      std::cout << "  message queue size : " << msgs.size() << "\n";
      std::cout << "    proposals : " << proposals_count << "\n";
      std::cout << "    votes : " << votes_count << "\n";
      std::cout << "    new_blocks : " << new_blocks_count << "\n";
      std::cout << "    new_views : " << new_views_count << "\n";

      std::cout << "\n";
   }

   void print_msg_queue(test_pacemaker &tpm){
      print_msgs(tpm._pending_message_queue);
   }

   void print_pm_state(test_pacemaker &tpm){
      std::cout << "\n";
      std::cout << "  leader : " << tpm.get_leader() << "\n";
      std::cout << "  next leader : " << tpm.get_next_leader() << "\n";
      std::cout << "  proposer : " << tpm.get_proposer() << "\n";
      std::cout << "  current block id : " << tpm.get_current_block_id().str() << "\n";
      std::cout << "\n";
   }

   void print_bp_state(name bp, std::string message){

      std::cout << "\n";
      std::cout << message;
      std::cout << "\n";

      auto qcc_entry = std::find_if(_qc_chains.begin(), _qc_chains.end(), [&](const auto& q){ return q.first == bp; });

      qc_chain & qcc = *qcc_entry->second.get();
      const hs_proposal_message *leaf = qcc.get_proposal( qcc._b_leaf );
      const hs_proposal_message *qc   = qcc.get_proposal( qcc._high_qc.proposal_id );
      const hs_proposal_message *lock = qcc.get_proposal( qcc._b_lock );
      const hs_proposal_message *exec = qcc.get_proposal( qcc._b_exec );

      if (leaf != nullptr) std::cout << "  - " << bp.to_string() << " current _b_leaf is : " << qcc._b_leaf.str() << " block_num : " << leaf->block_num() << ", phase : " << unsigned(leaf->phase_counter) << "\n";
      else std::cout << "  - No b_leaf value " << "\n";

      if (qc != nullptr) std::cout << "  - " << bp.to_string() << " current high_qc is : " << qcc._high_qc.proposal_id.str() << " block_num : " << qc->block_num() << ", phase : " << unsigned(qc->phase_counter) <<  "\n";
      else std::cout << "  - No high_qc value " << "\n";

      if (lock != nullptr) std::cout << "  - " << bp.to_string() << " current _b_lock is : " << qcc._b_lock.str() << " block_num : " << lock->block_num() << ", phase : " << unsigned(lock->phase_counter) <<  "\n";
      else std::cout << "  - No b_lock value " << "\n";

      if (exec != nullptr) std::cout << "  - " << bp.to_string() << " current _b_exec is : " << qcc._b_exec.str() << " block_num : " << exec->block_num() << ", phase : " << unsigned(exec->phase_counter) <<  "\n";
      else std::cout << "  - No b_exec value " << "\n";

      std::cout << "\n";
   }
};


BOOST_AUTO_TEST_SUITE(hotstuff)

BOOST_AUTO_TEST_CASE(hotstuff_bitset) try {

   boost::dynamic_bitset b( 8, 0 );

   uint32_t c = b.to_ulong();

   b.flip(0); //least significant bit
   b.flip(1);
   b.flip(2);
   b.flip(3);
   b.flip(4);
   b.flip(5);
   b.flip(6);
   b.flip(7); //most significant bit

   uint32_t d = b.to_ulong();

   for (boost::dynamic_bitset<>::size_type i = 0; i < b.size(); ++i){
      b.flip(i);
   }

   uint32_t e = b.to_ulong();

   std::cout << "c : " << c << "\n";
   std::cout << "d : " << d << "\n";
   std::cout << "e : " << e << "\n";

} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_CASE(hotstuff_1) try {

   //test optimistic responsiveness (3 confirmations per block)

   test_pacemaker tpm;

   hotstuff_test_handler ht;

   ht.initialize_qc_chains(tpm, {"bpa"_n, "bpb"_n}, {"bpa"_n, "bpb"_n}, unique_replicas);

   tpm.set_proposer("bpa"_n);
   tpm.set_leader("bpa"_n);
   tpm.set_next_leader("bpa"_n);
   tpm.set_finalizers(unique_replicas);

   auto qcc_bpa = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });
   auto qcc_bpb = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpb"_n; });

   ht.print_bp_state("bpa"_n, "");

   tpm.set_current_block_id(ids[0]); //first block

   tpm.beat(); //produce first block and associated proposal

   tpm.dispatch(""); //send proposal to replicas (prepare on first block)

   ht.print_bp_state("bpa"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on first block)

   tpm.dispatch(""); //send proposal to replicas (precommit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on first block)

   tpm.dispatch(""); //send proposal to replicas (commit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //propagating votes on new proposal (commitQC on first block)

   tpm.dispatch(""); //send proposal to replicas (decide on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm.dispatch(""); //propagating votes on new proposal (decide on first block)

   tpm.set_current_block_id(ids[1]); //second block

   tpm.beat(); //produce second block and associated proposal

   tpm.dispatch(""); //send proposal to replicas (prepare on second block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on second block)

   tpm.dispatch(""); //send proposal to replicas (precommit on second block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on second block)

   tpm.dispatch(""); //send proposal to replicas (commit on second block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("ab04f499892ad5ebd209d54372fd5c0bda0288410a084b55c70eda40514044f3"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));

   tpm.dispatch(""); //propagating votes on new proposal (commitQC on second block)

   tpm.dispatch(""); //send proposal to replicas (decide on second block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("ab04f499892ad5ebd209d54372fd5c0bda0288410a084b55c70eda40514044f3"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));

   tpm.dispatch(""); //send proposal to replicas (decide on second block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));

   //check bpb as well
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("ab04f499892ad5ebd209d54372fd5c0bda0288410a084b55c70eda40514044f3"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_finality_violation.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hotstuff_2) try {

   //test slower network (1 confirmation per block)

   test_pacemaker tpm;

   hotstuff_test_handler ht;

   ht.initialize_qc_chains(tpm, {"bpa"_n}, {"bpa"_n}, unique_replicas);

   tpm.set_proposer("bpa"_n);
   tpm.set_leader("bpa"_n);
   tpm.set_next_leader("bpa"_n);
   tpm.set_finalizers(unique_replicas);

   auto qcc_bpa = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });
   auto qcc_bpb = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpb"_n; });

   tpm.set_current_block_id(ids[0]); //first block

   tpm.beat(); //produce first block and associated proposal

   tpm.dispatch(""); //send proposal to replicas (prepare on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on first block)

   tpm.dispatch(""); //send proposal to replicas (precommit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.set_current_block_id(ids[1]); //second block

   tpm.beat(); //produce second block and associated proposal

   tpm.dispatch(""); //send proposal to replicas (prepare on second block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on second block)

   tpm.dispatch(""); //send proposal to replicas (precommit on second block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm.set_current_block_id(ids[2]); //second block

   tpm.beat(); //produce third block and associated proposal

   tpm.dispatch(""); //propagating votes on new proposal (prepare on third block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on third block)

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on third block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("0d77972a81cefce394736f23f8b4d97de3af5bd160376626bdd6a77de89ee324"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

   //check bpb as well
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_finality_violation.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hotstuff_3) try {

   //test leader rotation

   test_pacemaker tpm;

   hotstuff_test_handler ht;

   ht.initialize_qc_chains(tpm, {"bpa"_n, "bpb"_n}, {"bpa"_n, "bpb"_n},unique_replicas);

   tpm.set_proposer("bpa"_n);
   tpm.set_leader("bpa"_n);
   tpm.set_next_leader("bpa"_n);
   tpm.set_finalizers(unique_replicas);

   auto qcc_bpa = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });
   auto qcc_bpb = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpb"_n; });
   auto qcc_bpc = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpc"_n; });

   tpm.set_current_block_id(ids[0]); //first block

   tpm.beat(); //produce first block and associated proposal

   tpm.dispatch(""); //send proposal to replicas (prepare on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on first block)

   tpm.dispatch(""); //send proposal to replicas (precommit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on first block)

   tpm.dispatch(""); //send proposal to replicas (commit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.set_next_leader("bpb"_n); //leader is set to rotate on next block

   tpm.dispatch(""); //propagating votes on new proposal (commitQC on first block)

   tpm.dispatch(""); //send proposal to replicas (decide on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm.dispatch(""); //propagating votes on new proposal (decide on first block)

   tpm.set_proposer("bpb"_n); //leader has rotated
   tpm.set_leader("bpb"_n);

   tpm.set_current_block_id(ids[1]); //second block

   tpm.beat(); //produce second block and associated proposal

   tpm.dispatch(""); //send proposal to replicas (prepare on second block)

   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_leaf.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on second block)

   tpm.dispatch(""); //send proposal to replicas (precommit on second block)

   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_leaf.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on second block)

   tpm.dispatch(""); //send proposal to replicas (commit on second block)

   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_leaf.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));

   tpm.dispatch(""); //propagating votes on new proposal (commitQC on second block)

   tpm.dispatch(""); //send proposal to replicas (decide on second block)

   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_leaf.str(), std::string("89f468a127dbadd81b59076067238e3e9c313782d7d83141b16d9da4f2c2b078"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));

   //check bpa as well
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));

   //check bpc as well
   BOOST_CHECK_EQUAL(qcc_bpc->second->_high_qc.proposal_id.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
   BOOST_CHECK_EQUAL(qcc_bpc->second->_b_lock.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpc->second->_b_exec.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_finality_violation.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hotstuff_4) try {

   //test loss and recovery of liveness on new block

   test_pacemaker tpm;

   hotstuff_test_handler ht;

   ht.initialize_qc_chains(tpm, {"bpa"_n, "bpb"_n}, {"bpa"_n, "bpb"_n}, unique_replicas);

   tpm.set_proposer("bpa"_n);
   tpm.set_leader("bpa"_n);
   tpm.set_next_leader("bpa"_n);
   tpm.set_finalizers(unique_replicas);

   auto qcc_bpa = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });
   auto qcc_bpb = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpb"_n; });
   auto qcc_bpi = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpi"_n; });

   tpm.set_current_block_id(ids[0]); //first block

   tpm.beat(); //produce first block and associated proposal

   tpm.dispatch(""); //send proposal to replicas (prepare on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on first block)

   tpm.dispatch(""); //send proposal to replicas (precommit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on first block)

//ht.print_bp_state("bpa"_n, "before deactivate");

   tpm.deactivate("bpb"_n); //loss of liveness as 7 finalizers out of 21 go offline
   tpm.deactivate("bpc"_n);
   tpm.deactivate("bpd"_n);
   tpm.deactivate("bpe"_n);
   tpm.deactivate("bpf"_n);
   tpm.deactivate("bpg"_n);
   tpm.deactivate("bph"_n);

   tpm.dispatch(""); //send proposal to replicas (commit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.set_next_leader("bpi"_n); //leader is set to rotate on next block

   tpm.dispatch(""); //propagating votes on new proposal (insufficient to reach quorum)

//ht.print_bp_state("bpa"_n, "before reactivate");

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.activate("bpb"_n);
   tpm.activate("bpc"_n);
   tpm.activate("bpd"_n);
   tpm.activate("bpe"_n);
   tpm.activate("bpf"_n);
   tpm.activate("bpg"_n);
   tpm.activate("bph"_n);

   tpm.set_proposer("bpi"_n);
   tpm.set_leader("bpi"_n);

   tpm.set_current_block_id(ids[1]); //second block

   tpm.beat(); //produce second block and associated proposal

   tpm.dispatch(""); //send proposal to replicas (prepare on second block)

//ht.print_bp_state("bpi"_n, "");

//ht.print_bp_state("bpa"_n, "");
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on second block)

   tpm.dispatch(""); //send proposal to replicas (precommit on second block)

//ht.print_bp_state("bpa"_n, "");
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on second block)

   tpm.dispatch(""); //send proposal to replicas (commit on second block)

//ht.print_bp_state("bpa"_n, "");
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

   tpm.dispatch(""); //propagating votes on new proposal (commitQC on second block)

   tpm.dispatch(""); //send proposal to replicas (decide on second block)

//ht.print_bp_state("bpa"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("747676c95a4c866c915ab2d2171dbcaf126a4f0aeef62bf9720c138f8e03add9"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("747676c95a4c866c915ab2d2171dbcaf126a4f0aeef62bf9720c138f8e03add9"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));

//ht.print_bp_state("bpb"_n, "");
   //check bpa as well
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("747676c95a4c866c915ab2d2171dbcaf126a4f0aeef62bf9720c138f8e03add9"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));

//ht.print_bp_state("bpi"_n, "");
   BOOST_CHECK_EQUAL(qcc_bpi->second->_high_qc.proposal_id.str(), std::string("747676c95a4c866c915ab2d2171dbcaf126a4f0aeef62bf9720c138f8e03add9"));
   BOOST_CHECK_EQUAL(qcc_bpi->second->_b_lock.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
   BOOST_CHECK_EQUAL(qcc_bpi->second->_b_exec.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_finality_violation.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hotstuff_5) try {

   //test finality violation

   std::vector<name> honest_replica_set_1 {
      "bpb"_n,
      "bpe"_n,
      "bph"_n,
      "bpk"_n,
      "bpn"_n,
      "bpq"_n };

   std::vector<name> honest_replica_set_2 {
      "bpa"_n,
      "bpd"_n,
      "bpg"_n,
      "bpj"_n,
      "bpm"_n,
      "bpp"_n };

   std::vector<name> byzantine_set        {
      "bpc"_n,
      "bpf"_n,
      "bpi"_n,
      "bpl"_n,
      "bpo"_n,
      "bpr"_n,
      "bpu"_n,
      "bps"_n,
      "bpt"_n };

   std::vector<name> replica_set_1;
   std::vector<name> replica_set_2;

   replica_set_1.reserve( honest_replica_set_1.size() + byzantine_set.size() );
   replica_set_2.reserve( honest_replica_set_2.size() + byzantine_set.size() );

   replica_set_1.insert( replica_set_1.end(), honest_replica_set_1.begin(), honest_replica_set_1.end() );
   replica_set_1.insert( replica_set_1.end(), byzantine_set.begin(), byzantine_set.end() );

   replica_set_2.insert( replica_set_2.end(), honest_replica_set_2.begin(), honest_replica_set_2.end() );
   replica_set_2.insert( replica_set_2.end(), byzantine_set.begin(), byzantine_set.end() );

   //simulating a fork, where
   test_pacemaker tpm1;
   test_pacemaker tpm2;

   hotstuff_test_handler ht1;
   hotstuff_test_handler ht2;

   ht1.initialize_qc_chains(tpm1, {"bpe"_n}, {"bpe"_n}, replica_set_1);

   ht2.initialize_qc_chains(tpm2, {}, {}, replica_set_2);

   tpm1.set_proposer("bpe"_n); //honest leader
   tpm1.set_leader("bpe"_n);
   tpm1.set_next_leader("bpe"_n);
   tpm1.set_finalizers(replica_set_1);

   tpm2.set_proposer("bpf"_n); //byzantine leader
   tpm2.set_leader("bpf"_n);
   tpm2.set_next_leader("bpf"_n);
   tpm2.set_finalizers(replica_set_2);

   auto qcc_bpe = std::find_if(ht1._qc_chains.begin(), ht1._qc_chains.end(), [&](const auto& q){ return q.first == "bpe"_n; });
   //auto qcc_bpf = std::find_if(ht2._qc_chains.begin(), ht2._qc_chains.end(), [&](const auto& q){ return q.first == "bpf"_n; });

   std::vector<test_pacemaker::hotstuff_message> msgs;

   tpm1.set_current_block_id(ids[0]); //first block
   tpm2.set_current_block_id(ids[0]); //first block

   tpm1.beat(); //produce first block and associated proposal
   tpm2.beat(); //produce first block and associated proposal

   tpm1.dispatch("");
   tpm1.dispatch("");

   tpm2.dispatch("");
   tpm2.dispatch("");

//ht1.print_bp_state("bpe"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm1.dispatch("");
   tpm1.dispatch("");

   tpm2.dispatch("");
   tpm2.dispatch("");

//ht1.print_bp_state("bpe"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm1.dispatch("");
   tpm1.dispatch("");

   tpm2.dispatch("");
   tpm2.dispatch("");

//ht1.print_bp_state("bpe"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_leaf.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm1.dispatch("");
   tpm1.dispatch("");

   tpm2.dispatch("");
   tpm2.dispatch("");

//ht1.print_bp_state("bpe"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_leaf.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_high_qc.proposal_id.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm1.set_current_block_id(ids[1]); //first block
   tpm2.set_current_block_id(alternate_ids[1]); //first block

   tpm1.beat(); //produce second block and associated proposal
   tpm2.beat(); //produce second block and associated proposal

   tpm1.pipe(tpm2.dispatch(""));
   tpm1.dispatch("");

   tpm1.pipe(tpm2.dispatch(""));
   tpm1.dispatch("");

//ht1.print_bp_state("bpe"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_leaf.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_high_qc.proposal_id.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_lock.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_exec.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));

   tpm1.pipe(tpm2.dispatch(""));
   tpm1.dispatch("");

   tpm1.pipe(tpm2.dispatch(""));
   tpm1.dispatch("");

//ht1.print_bp_state("bpe"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_leaf.str(), std::string("ab04f499892ad5ebd209d54372fd5c0bda0288410a084b55c70eda40514044f3"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_high_qc.proposal_id.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_lock.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_exec.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));

   tpm1.pipe(tpm2.dispatch(""));
   tpm1.dispatch("");

   tpm1.pipe(tpm2.dispatch(""));
   tpm1.dispatch("");

//ht1.print_bp_state("bpe"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_leaf.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_high_qc.proposal_id.str(), std::string("ab04f499892ad5ebd209d54372fd5c0bda0288410a084b55c70eda40514044f3"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_lock.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_exec.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));

   tpm1.pipe(tpm2.dispatch(""));
   tpm1.dispatch("");

   tpm1.pipe(tpm2.dispatch(""));
   tpm1.dispatch("");

//ht1.print_bp_state("bpe"_n, "");

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_leaf.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_high_qc.proposal_id.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_lock.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_exec.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));

   BOOST_CHECK_EQUAL(qcc_bpe->second->_b_finality_violation.str(), std::string("5585accc44c753636d1381067c7f915d7fff2d33846aae04820abc055d952860"));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hotstuff_6) try {

   //test simple separation between the (single) proposer and the leader; includes one leader rotation

   test_pacemaker tpm;

   hotstuff_test_handler ht;

   ht.initialize_qc_chains(tpm, {"bpa"_n, "bpb"_n}, {"bpa"_n, "bpb"_n},unique_replicas);

   tpm.set_proposer("bpg"_n); // can be any proposer that's not the leader for this test
   tpm.set_leader("bpa"_n);
   tpm.set_next_leader("bpa"_n);
   tpm.set_finalizers(unique_replicas);

   auto qcc_bpa = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });
   auto qcc_bpb = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpb"_n; });
   auto qcc_bpc = std::find_if(ht._qc_chains.begin(), ht._qc_chains.end(), [&](const auto& q){ return q.first == "bpc"_n; });

   tpm.set_current_block_id(ids[0]); //first block

   tpm.beat(); //produce first block

   tpm.dispatch(""); //get the first block from the proposer to the leader

   tpm.dispatch(""); //send proposal to replicas (prepare on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on first block)

   tpm.dispatch(""); //send proposal to replicas (precommit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on first block)

   tpm.dispatch(""); //send proposal to replicas (commit on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   tpm.set_next_leader("bpb"_n); //leader is set to rotate on next block

   tpm.dispatch(""); //propagating votes on new proposal (commitQC on first block)

   tpm.dispatch(""); //send proposal to replicas (decide on first block)

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_leaf.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm.dispatch(""); //propagating votes on new proposal (decide on first block)

   tpm.set_proposer("bpm"_n); // can be any proposer that's not the leader for this test
   tpm.set_leader("bpb"_n);   //leader has rotated

   tpm.set_current_block_id(ids[1]); //second block

   tpm.beat(); //produce second block

   tpm.dispatch(""); //get the second block from the proposer to the leader

   tpm.dispatch(""); //send proposal to replicas (prepare on second block)

   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_leaf.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   tpm.dispatch(""); //send votes on proposal (prepareQC on second block)

   tpm.dispatch(""); //send proposal to replicas (precommit on second block)

   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_leaf.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

   tpm.dispatch(""); //propagating votes on new proposal (precommitQC on second block)

   tpm.dispatch(""); //send proposal to replicas (commit on second block)

   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_leaf.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));

   tpm.dispatch(""); //propagating votes on new proposal (commitQC on second block)

   tpm.dispatch(""); //send proposal to replicas (decide on second block)

   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_leaf.str(), std::string("89f468a127dbadd81b59076067238e3e9c313782d7d83141b16d9da4f2c2b078"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_high_qc.proposal_id.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_lock.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpb->second->_b_exec.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));

   //check bpa as well
   BOOST_CHECK_EQUAL(qcc_bpa->second->_high_qc.proposal_id.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_lock.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_exec.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));

   //check bpc as well
   BOOST_CHECK_EQUAL(qcc_bpc->second->_high_qc.proposal_id.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
   BOOST_CHECK_EQUAL(qcc_bpc->second->_b_lock.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
   BOOST_CHECK_EQUAL(qcc_bpc->second->_b_exec.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));

   BOOST_CHECK_EQUAL(qcc_bpa->second->_b_finality_violation.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_SUITE_END()
