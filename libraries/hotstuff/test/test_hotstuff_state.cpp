#include <stdio.h>

#include <boost/test/unit_test.hpp>
#include <eosio/chain/types.hpp>

#include <fc/io/cfile.hpp>

#include <fc/exception/exception.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

#include <fc/crypto/bls_signature.hpp>

using std::cout;

/*struct safety_state {

  eosio::chain::view_number v_height;
  fc::sha256 b_lock;

};

struct liveness_state {

  eosio::chain::quorum_certificate high_qc;
  fc::sha256 b_leaf;
  fc::sha256 b_exec;

};
*/
BOOST_AUTO_TEST_SUITE(test_hotstuff_state)

const std::string file_path_1("temp_hs_safety");
const std::string file_path_2("temp_hs_liveness");

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

  eosio::chain::safety_state ss(v_height, b_lock);

  // writing
  fc::cfile pfile;
  pfile.set_file_path(file_path_1);
  pfile.open(fc::cfile::truncate_rw_mode);
  auto data = fc::raw::pack(ss);
  pfile.write(data.data(), data.size());
  pfile.close();

  bool ok = true;

  BOOST_CHECK_EQUAL(ok, true);


} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(read_safety_state_from_file) try {

  eosio::chain::safety_state ss;

  // reading
  fc::cfile pfile;
  pfile.set_file_path(file_path_1);
  pfile.open("rb");
  auto ds = pfile.create_datastream();
  fc::raw::unpack(ds, ss);
  pfile.close();

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

  bool ok1 = ss.v_height == v_height;
  bool ok2 = ss.b_lock == b_lock;

  BOOST_CHECK_EQUAL(ok1, true);
  BOOST_CHECK_EQUAL(ok2, true);

} FC_LOG_AND_RETHROW();



BOOST_AUTO_TEST_CASE(write_liveness_state_to_file) try {

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

  eosio::chain::liveness_state ls(high_qc, b_leaf, b_exec);

  // writing
  fc::cfile pfile;
  pfile.set_file_path(file_path_2);
  pfile.open(fc::cfile::truncate_rw_mode);
  auto data = fc::raw::pack(ls);
  pfile.write(data.data(), data.size());
  pfile.close();

  bool ok = true;

  BOOST_CHECK_EQUAL(ok, true);


} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(read_liveness_state_from_file) try {

  eosio::chain::liveness_state ls;

  // reading
  fc::cfile pfile;
  pfile.set_file_path(file_path_2);
  pfile.open("rb");
  auto ds = pfile.create_datastream();
  fc::raw::unpack(ds, ls);
  pfile.close();

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

  bool ok1 = ls.high_qc == high_qc;
  bool ok2 = ls.b_exec == b_exec;
  bool ok3 = ls.b_leaf == b_leaf;

  BOOST_CHECK_EQUAL(ok1, true);
  BOOST_CHECK_EQUAL(ok2, true);
  BOOST_CHECK_EQUAL(ok3, true);

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()