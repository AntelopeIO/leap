#include <stdio.h>

#include <boost/test/unit_test.hpp>
#include <eosio/chain/types.hpp>

#include <fc/io/cfile.hpp>

#include <fc/exception/exception.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

#include <fc/crypto/bls_signature.hpp>

using std::cout;

BOOST_AUTO_TEST_SUITE(test_hotstuff_state)

const std::string file_path_1("temp_hs_safety");
//const std::string file_path_2("temp_hs_liveness");

BOOST_AUTO_TEST_CASE(write_safety_state_to_file) try {

  eosio::hotstuff::hs_proposal_message hspm_1;
  eosio::hotstuff::hs_proposal_message hspm_2;

  hspm_1.block_id = eosio::chain::block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217067
  hspm_1.final_on_qc = eosio::chain::block_id_type();
  hspm_1.phase_counter = 2;

  eosio::hotstuff::view_number v_height = hspm_1.get_view_number();

  hspm_2.block_id = eosio::chain::block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm_2.final_on_qc = eosio::chain::block_id_type();
  hspm_2.phase_counter = 0;

  fc::sha256 b_lock = eosio::hotstuff::get_digest_to_sign(hspm_2.block_id, hspm_2.phase_counter, hspm_2.final_on_qc);

  eosio::hotstuff::safety_state ss;

  ss.set_v_height(fc::crypto::blslib::bls_public_key{}, v_height);
  ss.set_b_lock(fc::crypto::blslib::bls_public_key{}, b_lock);

  BOOST_CHECK( eosio::hotstuff::state_db_manager<eosio::hotstuff::safety_state>::write(file_path_1, ss) );

  //fc::cfile pfile;
  //pfile.set_file_path(file_path_1);
  //pfile.open(fc::cfile::truncate_rw_mode);
  //pfile.write("force garbage to fail read_safety_state_from_file", 20);
  //pfile.close();

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(read_safety_state_from_file) try {

  eosio::hotstuff::safety_state ss;

  BOOST_CHECK( eosio::hotstuff::state_db_manager<eosio::hotstuff::safety_state>::read(file_path_1, ss) );

  std::remove(file_path_1.c_str());

  //test correct values
  eosio::hotstuff::hs_proposal_message hspm_1;
  eosio::hotstuff::hs_proposal_message hspm_2;

  hspm_1.block_id = eosio::chain::block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217067
  hspm_1.final_on_qc = eosio::chain::block_id_type();
  hspm_1.phase_counter = 2;

  eosio::hotstuff::view_number v_height = hspm_1.get_view_number();

  hspm_2.block_id = eosio::chain::block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm_2.final_on_qc = eosio::chain::block_id_type();
  hspm_2.phase_counter = 0;

  fc::sha256 b_lock = eosio::hotstuff::get_digest_to_sign(hspm_2.block_id, hspm_2.phase_counter, hspm_2.final_on_qc);

  //std::pair<eosio::chain::view_number, fc::sha256> ss = get_safety_state(eosio::chain::name{""});

  BOOST_CHECK_EQUAL(ss.get_v_height(fc::crypto::blslib::bls_public_key{}), v_height);
  BOOST_CHECK_EQUAL(ss.get_b_lock(fc::crypto::blslib::bls_public_key{}), b_lock);

} FC_LOG_AND_RETHROW();

#warning TODO decide on liveness state file then implement it in qc_chain and then test it here
/*BOOST_AUTO_TEST_CASE(write_liveness_state_to_file) try {

  eosio::hotstuff::hs_proposal_message hspm_1;
  eosio::hotstuff::hs_proposal_message hspm_2;

  hspm_1.block_id = eosio::chain::block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217068
  hspm_1.final_on_qc = eosio::chain::block_id_type();
  hspm_1.phase_counter = 2;

  fc::sha256 b_exec = eosio::hotstuff::get_digest_to_sign(hspm_1.block_id, hspm_1.phase_counter, hspm_1.final_on_qc);

  hspm_2.block_id = eosio::chain::block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm_2.final_on_qc = eosio::chain::block_id_type();
  hspm_2.phase_counter = 1;

  fc::sha256 b_leaf = eosio::hotstuff::get_digest_to_sign(hspm_2.block_id, hspm_2.phase_counter, hspm_2.final_on_qc);

  //mock quorum_certificate
  eosio::hotstuff::quorum_certificate high_qc;

  high_qc.proposal_id = fc::sha256("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824");
  high_qc.active_finalizers = 1245;
  high_qc.active_agg_sig = fc::crypto::blslib::bls_signature("SIG_BLS_23PuSu1B72cPe6wxGkKjAaaZqA1Ph79zSoW7omsKKUrnprbA3cJCJVhT48QKUG6ofjYTTg4BA4TrVENWyrxjTomwLX6TGdVg2RYhKH7Kk9X23K5ohuhKQcWQ6AwJJGVSbSp4");

  eosio::hotstuff::liveness_state ls(high_qc, b_leaf, b_exec);

  eosio::hotstuff::write_state(file_path_2, ls);

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(read_liveness_state_from_file) try {

  eosio::hotstuff::liveness_state ls;

  eosio::hotstuff::read_state(file_path_2, ls);

  std::remove(file_path_2.c_str());

  //test correct values

  eosio::hotstuff::hs_proposal_message hspm_1;
  eosio::hotstuff::hs_proposal_message hspm_2;

  hspm_1.block_id = eosio::chain::block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217067
  hspm_1.final_on_qc = eosio::chain::block_id_type();
  hspm_1.phase_counter = 2;

  fc::sha256 b_exec = eosio::hotstuff::get_digest_to_sign(hspm_1.block_id, hspm_1.phase_counter, hspm_1.final_on_qc);

  hspm_2.block_id = eosio::chain::block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm_2.final_on_qc = eosio::chain::block_id_type();
  hspm_2.phase_counter = 1;

  fc::sha256 b_leaf = eosio::hotstuff::get_digest_to_sign(hspm_2.block_id, hspm_2.phase_counter, hspm_2.final_on_qc);

  //mock quorum_certificate
  eosio::hotstuff::quorum_certificate high_qc;

  high_qc.proposal_id = fc::sha256("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824");
  high_qc.active_finalizers = 1245;
  high_qc.active_agg_sig = fc::crypto::blslib::bls_signature("SIG_BLS_23PuSu1B72cPe6wxGkKjAaaZqA1Ph79zSoW7omsKKUrnprbA3cJCJVhT48QKUG6ofjYTTg4BA4TrVENWyrxjTomwLX6TGdVg2RYhKH7Kk9X23K5ohuhKQcWQ6AwJJGVSbSp4");

  BOOST_CHECK(ls.high_qc == high_qc);
  BOOST_CHECK(ls.b_exec == b_exec);
  BOOST_CHECK(ls.b_leaf == b_leaf);

} FC_LOG_AND_RETHROW();*/

BOOST_AUTO_TEST_SUITE_END()
