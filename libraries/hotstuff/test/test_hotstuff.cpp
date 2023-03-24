#define BOOST_TEST_MODULE hotstuff
#include <boost/test/included/unit_test.hpp>

#include <fc/exception/exception.hpp>

#include <fc/crypto/sha256.hpp>

#include <eosio/hotstuff/test_pacemaker.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

#include <fc/crypto/bls_utils.hpp>

using namespace eosio::hotstuff;

using std::cout;

std::vector<block_id_type> ids{	block_id_type("00000001d49031dba775bd2b44fd339a329ef462aaf019e5b75b4cd9609a0c39"),
								block_id_type("0000000202b23f86652ae43cba4bec5579c8c7133c14011a6f8d93b316530684"),
								block_id_type("00000003a5a001518358977e84a3f6abf87bf32a6e739ced9a7a3f6b0b8bf330"),
								block_id_type("00000004235f391d91d5da938cfa8c4738d92da6c007da596f1db05c37d38866"),
								block_id_type("00000005485fa018c16b6150aed839bdd4cbc2149f70191e89f2b19fe711b1c0"),
								block_id_type("00000006161b9c79797059bbdcbf49614bbdca33d35b8099ffa250583dc41d9d"),
								block_id_type("00000007ffd04a602236843f842827c2ac2aa61d586b7ebc8cc3c276921b55d9"),
								block_id_type("000000085e8b9b158801fea3f7b2b627734805b9192568b67d7d00d676e427e3"),
								block_id_type("0000000979b05f273f2885304f952aaa6f47d56985e003ec35c22472682ad3a2"),
								block_id_type("0000000a703d6a104c722b8bc2d7227b90a35d08835343564c2fd66eb9dcf999"),
								block_id_type("0000000ba7ef2e432d465800e53d1da982f2816c051153f9054960089d2f37d8") };

//list of unique replicas for our test
std::vector<name> unique_replicas{	"bpa"_n, "bpb"_n, "bpc"_n,
									"bpd"_n, "bpe"_n, "bpf"_n,
									"bpg"_n, "bph"_n, "bpi"_n,
									"bpj"_n, "bpk"_n ,"bpl"_n,
									"bpm"_n, "bpn"_n, "bpo"_n,
									"bpp"_n, "bpq"_n, "bpr"_n,
									"bps"_n, "bpt"_n, "bpu"_n };

std::vector<std::pair<name,qc_chain>> _qc_chains;

void initialize_qc_chains(test_pacemaker& tpm, std::vector<name> loggers, std::vector<name> replicas){

	
	_qc_chains.clear();

	for (name r : replicas){  
	 	
		_qc_chains.push_back(std::make_pair(r, qc_chain()));
		
	}

	int counter = 0;

	auto itr = _qc_chains.begin();

	while (itr!=_qc_chains.end()){

		bool log = false;

		auto found = std::find(loggers.begin(), loggers.end(), replicas[counter]);

		if (found!=loggers.end()) log = true;

		itr->second.init(replicas[counter], tpm, {replicas[counter]}, log);

		itr++;
		counter++;

	}

}

void print_msg_queue_size(test_pacemaker &tpm){

	std::cout << "\n";

	std::cout << "  message queue size : " << tpm._pending_message_queue.size() << "\n";

	std::cout << "\n";

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

	auto qcc = std::find_if(_qc_chains.begin(), _qc_chains.end(), [&](const auto& q){ return q.first == bp; });

	auto leaf_itr = qcc->second._proposal_store.get<qc_chain::by_proposal_id>().find( qcc->second._b_leaf );
	auto qc_itr = qcc->second._proposal_store.get<qc_chain::by_proposal_id>().find( qcc->second._high_qc.proposal_id );
	auto lock_itr = qcc->second._proposal_store.get<qc_chain::by_proposal_id>().find( qcc->second._b_lock );
	auto exec_itr = qcc->second._proposal_store.get<qc_chain::by_proposal_id>().find( qcc->second._b_exec );
		
	if (leaf_itr != qcc->second._proposal_store.get<qc_chain::by_proposal_id>().end()) std::cout << "  - " << bp.to_string() << " current _b_leaf is : " << qcc->second._b_leaf.str() << " block_num : " << leaf_itr->block_num() << ", phase : " << unsigned(leaf_itr->phase_counter) << "\n";
	else std::cout << "  - No b_leaf value " << "\n";

	if (qc_itr != qcc->second._proposal_store.get<qc_chain::by_proposal_id>().end()) std::cout << "  - " << bp.to_string() << " current high_qc is : " << qcc->second._high_qc.proposal_id.str() << " block_num : " << qc_itr->block_num() << ", phase : " << unsigned(qc_itr->phase_counter) <<  "\n";
	else std::cout << "  - No high_qc value " << "\n";

	if (lock_itr != qcc->second._proposal_store.get<qc_chain::by_proposal_id>().end()) std::cout << "  - " << bp.to_string() << " current _b_lock is : " << qcc->second._b_lock.str() << " block_num : " << lock_itr->block_num() << ", phase : " << unsigned(lock_itr->phase_counter) <<  "\n";
	else std::cout << "  - No b_lock value " << "\n";

	if (exec_itr != qcc->second._proposal_store.get<qc_chain::by_proposal_id>().end()) std::cout << "  - " << bp.to_string() << " current _b_exec is : " << qcc->second._b_exec.str() << " block_num : " << exec_itr->block_num() << ", phase : " << unsigned(exec_itr->phase_counter) <<  "\n";
	else std::cout << "  - No b_exec value " << "\n";

	std::cout << "\n";

}

BOOST_AUTO_TEST_SUITE(hotstuff)

BOOST_AUTO_TEST_CASE(hotstuff_1) try {

	test_pacemaker tpm;

	initialize_qc_chains(tpm, {"bpa"_n}, unique_replicas);

	tpm.set_proposer("bpa"_n);
	tpm.set_leader("bpa"_n);
	tpm.set_next_leader("bpa"_n);
	tpm.set_finalizers(unique_replicas);

	auto qcc_bpa = std::find_if(_qc_chains.begin(), _qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });
	auto qcc_bpb = std::find_if(_qc_chains.begin(), _qc_chains.end(), [&](const auto& q){ return q.first == "bpb"_n; });

   	tpm.set_current_block_id(ids[0]); //first block

   	tpm.beat(); //produce first block and associated proposal

   	tpm.propagate(); //propagate proposal to replicas (prepare on first block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   	tpm.propagate(); //propagate votes on proposal (prepareQC on first block)

   	tpm.propagate(); //propagate proposal to replicas (precommit on first block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   	tpm.propagate(); //propagating votes on new proposal (precommitQC on first block)

   	tpm.propagate(); //propagate proposal to replicas (commit on first block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   	tpm.propagate(); //propagating votes on new proposal (commitQC on first block)

   	tpm.propagate(); //propagate proposal to replicas (decide on first block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   	tpm.propagate(); //propagating votes on new proposal (decide on first block)

   	tpm.set_current_block_id(ids[1]); //second block

   	tpm.beat(); //produce second block and associated proposal

   	tpm.propagate(); //propagate proposal to replicas (prepare on second block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

   	tpm.propagate(); //propagate votes on proposal (prepareQC on second block)

   	tpm.propagate(); //propagate proposal to replicas (precommit on second block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));

   	tpm.propagate(); //propagating votes on new proposal (precommitQC on second block)

   	tpm.propagate(); //propagate proposal to replicas (commit on second block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("ab04f499892ad5ebd209d54372fd5c0bda0288410a084b55c70eda40514044f3"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));

   	tpm.propagate(); //propagating votes on new proposal (commitQC on second block)

   	tpm.propagate(); //propagate proposal to replicas (decide on second block)

//print_bp_state("bpa"_n, "");
//print_bp_state("bpb"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("ab04f499892ad5ebd209d54372fd5c0bda0288410a084b55c70eda40514044f3"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));

//print_msg_queue_size(tpm);

	tpm.propagate(); //propagate proposal to replicas (decide on second block)

//print_bp_state("bpa"_n, "");
//print_bp_state("bpb"_n, "");


  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("9eeffb58a16133517d8d2f6f90b8a3420269de3356362677055b225a44a7c151"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));

  	//check bpb as well
  	BOOST_CHECK_EQUAL(qcc_bpb->second._high_qc.proposal_id.str(), std::string("ab04f499892ad5ebd209d54372fd5c0bda0288410a084b55c70eda40514044f3"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_lock.str(), std::string("4af7c22e5220a61ac96c35533539e65d398e9f44de4c6e11b5b0279e7a79912f"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_exec.str(), std::string("a8c84b7f9613aebf2ae34f457189d58de95a6b0a50d103a4c9e6405180d6fffb"));


} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hotstuff_2) try {

	test_pacemaker tpm;

	initialize_qc_chains(tpm, {"bpa"_n}, unique_replicas);

	tpm.set_proposer("bpa"_n);
	tpm.set_leader("bpa"_n);
	tpm.set_next_leader("bpa"_n);
	tpm.set_finalizers(unique_replicas);

	auto qcc_bpa = std::find_if(_qc_chains.begin(), _qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });
	auto qcc_bpb = std::find_if(_qc_chains.begin(), _qc_chains.end(), [&](const auto& q){ return q.first == "bpb"_n; });

   	tpm.set_current_block_id(ids[0]); //first block

   	tpm.beat(); //produce first block and associated proposal

   	tpm.propagate(); //propagate proposal to replicas (prepare on first block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   	tpm.propagate(); //propagate votes on proposal (prepareQC on first block)

   	tpm.propagate(); //propagate proposal to replicas (precommit on first block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   	tpm.set_current_block_id(ids[1]); //second block

   	tpm.beat(); //produce second block and associated proposal

   	tpm.propagate(); //propagate proposal to replicas (prepare on second block)

 	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   	tpm.propagate(); //propagate votes on proposal (prepareQC on second block)

   	tpm.propagate(); //propagate proposal to replicas (precommit on second block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   	tpm.set_current_block_id(ids[2]); //second block

   	tpm.beat(); //produce third block and associated proposal

   	tpm.propagate(); //propagating votes on new proposal (precommitQC on third block)

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   	tpm.propagate(); //propagating votes on new proposal (precommitQC on third block)

   	tpm.propagate(); //propagating votes on new proposal (precommitQC on third block)

 	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("0d77972a81cefce394736f23f8b4d97de3af5bd160376626bdd6a77de89ee324"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

  	//check bpb as well
  	BOOST_CHECK_EQUAL(qcc_bpb->second._high_qc.proposal_id.str(), std::string("f1cc5d8add3db0c0f13271815c4e08eec5e8730b0e3ba24ab7b7990981b9b338"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_lock.str(), std::string("a56ae5316e731168f5cfea5a85ffa3467b29094c2e5071019a1b89cd7fa49d98"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hotstuff_3) try {

	test_pacemaker tpm;

	initialize_qc_chains(tpm, {"bpa"_n, "bpb"_n}, unique_replicas);

	tpm.set_proposer("bpa"_n);
	tpm.set_leader("bpa"_n);
	tpm.set_next_leader("bpa"_n);
	tpm.set_finalizers(unique_replicas);

	auto qcc_bpa = std::find_if(_qc_chains.begin(), _qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });
	auto qcc_bpb = std::find_if(_qc_chains.begin(), _qc_chains.end(), [&](const auto& q){ return q.first == "bpb"_n; });

   	tpm.set_current_block_id(ids[0]); //first block

   	tpm.beat(); //produce first block and associated proposal

   	tpm.propagate(); //propagate proposal to replicas (prepare on first block)
print_bp_state("bpa"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   	tpm.propagate(); //propagate votes on proposal (prepareQC on first block)

   	tpm.propagate(); //propagate proposal to replicas (precommit on first block)
print_bp_state("bpa"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

   	tpm.propagate(); //propagating votes on new proposal (precommitQC on first block)

   	tpm.propagate(); //propagate proposal to replicas (commit on first block)
print_bp_state("bpa"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("0000000000000000000000000000000000000000000000000000000000000000"));

	tpm.set_next_leader("bpb"_n);

   	tpm.propagate(); //propagating votes on new proposal (commitQC on first block)

   	tpm.propagate(); //propagate proposal to replicas (decide on first block)
print_bp_state("bpa"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_leaf.str(), std::string("487e5fcbf2c515618941291ae3b6dcebb68942983d8ac3f61c4bdd9901dadbe7"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   	tpm.propagate(); //propagating votes on new proposal (decide on first block)

	tpm.set_proposer("bpb"_n);
	tpm.set_leader("bpb"_n);

   	tpm.set_current_block_id(ids[1]); //second block

   	tpm.beat(); //produce second block and associated proposal

   	tpm.propagate(); //propagate proposal to replicas (prepare on second block)
print_bp_state("bpb"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_leaf.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._high_qc.proposal_id.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_lock.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_exec.str(), std::string("a252070cd26d3b231ab2443b9ba97f57fc72e50cca04a020952e45bc7e2d27a8"));

   	tpm.propagate(); //propagate votes on proposal (prepareQC on second block)

   	tpm.propagate(); //propagate proposal to replicas (precommit on second block)
print_bp_state("bpb"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_leaf.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._high_qc.proposal_id.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_lock.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_exec.str(), std::string("4b43fb144a8b5e874777f61f3b37d7a8b06c33fbc48db464ce0e8788ff4edb4f"));

   	tpm.propagate(); //propagating votes on new proposal (precommitQC on second block)

   	tpm.propagate(); //propagate proposal to replicas (commit on second block)
print_bp_state("bpb"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_leaf.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._high_qc.proposal_id.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_lock.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_exec.str(), std::string("aedf8bb1ee70bd6e743268f7fe0f8171418aa43a68bb9c6e7329ffa856896c09"));

   	tpm.propagate(); //propagating votes on new proposal (commitQC on second block)

   	tpm.propagate(); //propagate proposal to replicas (decide on second block)
print_bp_state("bpb"_n, "");
print_bp_state("bpa"_n, "");

  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_leaf.str(), std::string("89f468a127dbadd81b59076067238e3e9c313782d7d83141b16d9da4f2c2b078"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._high_qc.proposal_id.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_lock.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
  	BOOST_CHECK_EQUAL(qcc_bpb->second._b_exec.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));

  	//check bpa as well
  	BOOST_CHECK_EQUAL(qcc_bpa->second._high_qc.proposal_id.str(), std::string("fd77164bf3898a6a8f27ccff440d17ef6870e75c368fcc93b969066cec70939c"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_lock.str(), std::string("6462add7d157da87931c859cb689f722003a20f30c0f1408d11b872020903b85"));
  	BOOST_CHECK_EQUAL(qcc_bpa->second._b_exec.str(), std::string("1511035fdcbabdc5e272a3ac19356536252884ed77077cf871ae5029a7502279"));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
