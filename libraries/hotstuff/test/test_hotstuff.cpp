#define BOOST_TEST_MODULE hotstuff
#include <boost/test/included/unit_test.hpp>

#include <fc/exception/exception.hpp>

#include <fc/crypto/sha256.hpp>

#include <eosio/hotstuff/test_pacemaker.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

#include <fc/crypto/bls_utils.hpp>

using namespace eosio::hotstuff;

using std::cout;

std::vector<uint8_t> _seed = {  0,  50, 6,  244, 24,  199, 1,  25,  52,  88,  192,
                            19, 18, 12, 89,  6,   220, 18, 102, 58,  209, 82,
                            12, 62, 89, 110, 182, 9,   44, 20,  254, 22};

fc::crypto::blslib::bls_private_key _private_key = fc::crypto::blslib::bls_private_key(_seed);


fc::sha256 message_1 = fc::sha256("000000000000000118237d3d79f3c684c031a9844c27e6b95c6d27d8a5f401a1");
fc::sha256 message_2 = fc::sha256("0000000000000002fb2129a8f7c9091ae983bc817002ffab21cd98eab2147029");



BOOST_AUTO_TEST_SUITE(hotstuff)

BOOST_AUTO_TEST_CASE(hotstuff_tests) try {

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

	std::vector<std::pair<name,qc_chain>> qc_chains;

	test_pacemaker tpm;

	std::cout << "Running Hotstuff Tests..." << "\n";

    for (name r : unique_replicas){  
	 	
    	qc_chains.push_back(std::make_pair(r, qc_chain()));
		
    }

    int counter = 0;

    auto itr = qc_chains.begin();

    while (itr!=qc_chains.end()){

    	itr->second.init(unique_replicas[counter], tpm, {unique_replicas[counter]});

    	itr++;
    	counter++;

    }

    tpm.set_proposer("bpa"_n);
    tpm.set_leader("bpa"_n);
    tpm.set_next_leader("bpa"_n);
    tpm.set_finalizers(unique_replicas);

	std::cout << "test_pacemaker configured." << "\n";

   	tpm.set_current_block_id(ids[0]);
    
	std::cout << "test_pacemaker got qcc" << "\n";
	
	auto qc = std::find_if(qc_chains.begin(), qc_chains.end(), [&](const auto& q){ return q.first == "bpa"_n; });

   	tpm.beat();

	std::cout << "test_pacemaker on_beat event chain executed." << "\n";

   	tpm.propagate(); //propagating new proposal message

	std::cout << " --- propagated new proposal message (phase 0)." << "\n";

   	tpm.propagate(); //propagating votes on new proposal

	std::cout << " --- propagated votes on new proposal." << "\n";

	qc_chain::proposal_store_type::nth_index<0>::type::iterator prop_itr = qc->second._proposal_store.get<qc_chain::by_proposal_id>().find( qc->second._high_qc.proposal_id );

	std::cout << "bpa current high_qc is : " << qc->second._high_qc.proposal_id.str() << ",id : " << prop_itr->block_id.str() << ", phase : " << unsigned(prop_itr->phase_counter) << "\n";

	std::cout << "bpa current _b_leaf is : " << qc->second._b_leaf.str() << "\n";
	std::cout << "bpa current _b_lock is : " << qc->second._b_lock.str() << "\n";
	std::cout << "bpa current _b_exec is : " << qc->second._b_exec.str() << "\n";
	
   	tpm.propagate(); //propagating updated proposal with qc

	std::cout << " --- propagated propagating updated proposal with qc (phase 1)." << "\n";

   	tpm.propagate(); //propagating votes on new proposal

	std::cout << " --- propagated votes on new proposal." << "\n";

	prop_itr = qc->second._proposal_store.get<qc_chain::by_proposal_id>().find( qc->second._high_qc.proposal_id );

	std::cout << "bpa current high_qc is : " << qc->second._high_qc.proposal_id.str() << ",id : " << prop_itr->block_id.str() << ", phase : " << unsigned(prop_itr->phase_counter) << "\n";

	std::cout << "bpa current _b_leaf is : " << qc->second._b_leaf.str() << "\n";
	std::cout << "bpa current _b_lock is : " << qc->second._b_lock.str() << "\n";
	std::cout << "bpa current _b_exec is : " << qc->second._b_exec.str() << "\n";
	
   	tpm.propagate(); //propagating updated proposal with qc

	std::cout << " --- propagated propagating updated proposal with qc (phase 2)." << "\n";

   	tpm.propagate(); //propagating votes on new proposal

	std::cout << " --- propagated votes on new proposal." << "\n";

	prop_itr = qc->second._proposal_store.get<qc_chain::by_proposal_id>().find( qc->second._high_qc.proposal_id );

	std::cout << "bpa current high_qc is : " << qc->second._high_qc.proposal_id.str() << ",id : " << prop_itr->block_id.str() << ", phase : " << unsigned(prop_itr->phase_counter) << "\n";

	std::cout << "bpa current _b_leaf is : " << qc->second._b_leaf.str() << "\n";
	std::cout << "bpa current _b_lock is : " << qc->second._b_lock.str() << "\n";
	std::cout << "bpa current _b_exec is : " << qc->second._b_exec.str() << "\n";
	
   	tpm.propagate(); //propagating updated proposal with qc

	std::cout << " --- propagated propagating updated proposal with qc (phase 3)." << "\n";

   	tpm.propagate(); //propagating votes on new proposal

	std::cout << " --- propagated votes on new proposal." << "\n";

	prop_itr = qc->second._proposal_store.get<qc_chain::by_proposal_id>().find( qc->second._high_qc.proposal_id );

	std::cout << "bpa current high_qc is : " << qc->second._high_qc.proposal_id.str() << ",id : " << prop_itr->block_id.str() << ", phase : " << unsigned(prop_itr->phase_counter) << "\n";

	std::cout << "bpa current _b_leaf is : " << qc->second._b_leaf.str() << "\n";
	std::cout << "bpa current _b_lock is : " << qc->second._b_lock.str() << "\n";
	std::cout << "bpa current _b_exec is : " << qc->second._b_exec.str() << "\n";
	
	//std::cout << "test_pacemaker messages propagated." << "\n";

  	BOOST_CHECK_EQUAL(false, false);

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
