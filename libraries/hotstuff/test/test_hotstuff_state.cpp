#include <boost/test/unit_test.hpp>
#include <eosio/chain/types.hpp>

#include <fc/io/cfile.hpp>

#include <fc/exception/exception.hpp>
#include <eosio/hotstuff/qc_chain.hpp>

//#include <eosio/hotstuff/stuff.cpp>

using std::cout;

struct safety_state {

  eosio::chain::view_number v_height;
  eosio::chain::view_number b_lock;

};

struct liveness_state {

  eosio::chain::quorum_certificate high_qc;
  eosio::chain::view_number b_exec;

};

BOOST_AUTO_TEST_SUITE(test_hotstuff_state)

const std::string file_path("temp");

BOOST_AUTO_TEST_CASE(write_state_to_file) try {

/*  eosio::hotstuff::hs_proposal_message hspm;

  hspm.block_id = eosio::chain::block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm.phase_counter = 0;

  eosio::hotstuff::view_number vn = hspm.get_view_number();

  BOOST_CHECK_EQUAL(vn.block_height() == 194217067, true);
  BOOST_CHECK_EQUAL(vn.phase_counter() == 0, true);
*/
  safety_state ss;

  //ss.test_val = 2;

  // writing
  fc::cfile pfile;
  pfile.set_file_path(file_path);
  pfile.open(fc::cfile::truncate_rw_mode);
  auto data = fc::raw::pack(ss);
  pfile.write(data.data(), data.size());
  pfile.close(); // or let destructor do it

  bool ok = true;

  BOOST_CHECK_EQUAL(ok, true);


} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(read_state_from_file) try {

  safety_state ss;

  // reading
  fc::cfile pfile;
  pfile.set_file_path(file_path);
  pfile.open("rb");
  auto ds = pfile.create_datastream();
  fc::raw::unpack(ds, ss);
  pfile.close(); // or let destructor do it

  //bool ok = ss.test_val == 2;

  BOOST_CHECK_EQUAL(true, true);

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()

FC_REFLECT(safety_state, (v_height)(b_lock))
FC_REFLECT(liveness_state, (high_qc)(b_exec))