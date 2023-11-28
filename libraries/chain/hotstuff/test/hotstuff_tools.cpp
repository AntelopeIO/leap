#include <boost/test/unit_test.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_header.hpp>

#include <fc/exception/exception.hpp>

#include <eosio/chain/hotstuff/qc_chain.hpp>

BOOST_AUTO_TEST_CASE(view_number_tests) try {

  eosio::chain::hs_proposal_message hspm_1;
  eosio::chain::hs_proposal_message hspm_2;
  eosio::chain::hs_proposal_message hspm_3;
  eosio::chain::hs_proposal_message hspm_4;
  eosio::chain::hs_proposal_message hspm_5;

  hspm_1.block_id = eosio::chain::block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm_1.phase_counter = 0;

  hspm_2.block_id = eosio::chain::block_id_type("0b93846ba73bdfdc9b2383863b64f8f921c8a2379d6dde4e05bdd2e434e9392a"); //UX Network block #194217067
  hspm_2.phase_counter = 1;

  hspm_3.block_id = eosio::chain::block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217068
  hspm_3.phase_counter = 0;

  hspm_4.block_id = eosio::chain::block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217068
  hspm_4.phase_counter = 1;

  hspm_5.block_id = eosio::chain::block_id_type("0b93846cf55a3ecbcd8f9bd86866b1aecc2e8bd981e40c92609ce3a68dbd0824"); //UX Network block #194217068
  hspm_5.phase_counter = 2;

  eosio::chain::view_number vn_1 = hspm_1.get_view_number();
  eosio::chain::view_number vn_2 = hspm_2.get_view_number();
  eosio::chain::view_number vn_3 = hspm_3.get_view_number();
  eosio::chain::view_number vn_4 = hspm_4.get_view_number();
  eosio::chain::view_number vn_5 = hspm_5.get_view_number();

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

  eosio::chain::view_number vn_6 = eosio::chain::view_number(194217068, 2);

  BOOST_CHECK_EQUAL(vn_5, vn_6);

} FC_LOG_AND_RETHROW();
