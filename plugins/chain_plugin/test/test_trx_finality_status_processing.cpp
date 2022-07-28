#define BOOST_TEST_MODULE transaction_finality_status
#include <boost/test/included/unit_test.hpp>

#include <eosio/chain_plugin/trx_finality_status_processing.hpp>

#include <eosio/testing/tester.hpp>

#include <eosio/chain/block_header.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/trace.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <fc/mock_time.hpp>
#include <fc/bitutil.hpp>

#include <deque>
#include <memory>

namespace eosio::test::detail {

using namespace eosio::chain;
using namespace eosio::chain::literals;

struct testit {
   uint64_t      id;
   testit( uint64_t id = 0 )
   : id(id){}

   static account_name get_account() {
      return chain::config::system_account_name;
   }
   static action_name get_name() {
      return "testit"_n;
   }
};

} // eosio::test::detail
FC_REFLECT( eosio::test::detail::testit, (id) )

namespace {

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::chain_apis;
using namespace eosio::test::detail;

auto get_private_key( chain::name keyname, std::string role = "owner" ) {
   auto secret = fc::sha256::hash( keyname.to_string() + role );
   return chain::private_key_type::regenerate<fc::ecc::private_key_shim>( secret );
}

auto get_public_key( chain::name keyname, std::string role = "owner" ) {
   return get_private_key( keyname, role ).get_public_key();
}

auto make_unique_trx( const fc::microseconds& expiration ) {

   static uint64_t unique_id = 0;
   ++unique_id;

   genesis_state gs{};
   const auto& chain_id = gs.compute_chain_id();
   account_name creator = config::system_account_name;
   signed_transaction trx;
   const auto now_exp = fc::time_point::now() + expiration;
   trx.expiration = now_exp;
   trx.actions.emplace_back( vector<permission_level>{{creator, config::active_name}},
                             testit{ unique_id } );
   trx.sign( get_private_key("test"_n), chain_id );

   return std::make_shared<packed_transaction>( std::move(trx), packed_transaction::compression_type::none);
}

chain::block_id_type make_block_id( uint32_t block_num ) {
   chain::block_id_type block_id;
   block_id._hash[0] &= 0xffffffff00000000;
   block_id._hash[0] += fc::endian_reverse_u32(block_num);
   return block_id;
}

chain::transaction_trace_ptr make_transaction_trace( const packed_transaction_ptr trx, uint32_t block_number, const eosio::chain::block_state_ptr& bs_ptr,
                                                     chain::transaction_receipt_header::status_enum status = eosio::chain::transaction_receipt_header::executed ) {
   return std::make_shared<chain::transaction_trace>(chain::transaction_trace{
         trx->id(),
         block_number,
         chain::block_timestamp_type(fc::time_point::now()),
         bs_ptr ? bs_ptr->id : std::optional<block_id_type> {},
         chain::transaction_receipt_header{status},
         fc::microseconds(0),
         0,
         false,
         {}, // actions
         {},
         {},
         {},
         {},
         {}
   });
}

auto make_block_state( uint32_t block_num ) {
   static uint64_t unique_num = 0;
   ++unique_num;
   chain::block_id_type block_id = make_block_id(block_num);
   block_id._hash[3] = unique_num;
   name producer = "brianj"_n;
   chain::signed_block_ptr block = std::make_shared<chain::signed_block>();
   block->producer = producer;
   block->timestamp = fc::time_point::now();

   block->previous = make_block_id(block_num - 1);

   auto priv_key = get_private_key( block->producer, "active" );
   auto pub_key = get_public_key( block->producer, "active" );

   auto prev = std::make_shared<chain::block_state>();
   auto header_bmroot = chain::digest_type::hash( std::make_pair( block->digest(), prev->blockroot_merkle.get_root()));
   auto sig_digest = chain::digest_type::hash( std::make_pair( header_bmroot, prev->pending_schedule.schedule_hash ));
   block->producer_signature = priv_key.sign( sig_digest );

   std::vector<chain::private_key_type> signing_keys;
   signing_keys.emplace_back( priv_key );
   auto signer = [&]( chain::digest_type d ) {
      std::vector<chain::signature_type> result;
      result.reserve( signing_keys.size());
      for( const auto& k: signing_keys )
         result.emplace_back( k.sign( d ));
      return result;
   };
   chain::pending_block_header_state pbhs;
   pbhs.producer = block->producer;
   pbhs.timestamp = block->timestamp;
   pbhs.previous = block->previous;
   chain::producer_authority_schedule schedule =
         {0, {chain::producer_authority{block->producer,
                                        chain::block_signing_authority_v0{1, {{pub_key, 1}}}}}};
   pbhs.active_schedule = schedule;
   pbhs.valid_block_signing_authority = chain::block_signing_authority_v0{1, {{pub_key, 1}}};
   auto bsp = std::make_shared<chain::block_state>(
         std::move( pbhs ),
         std::move( block ),
         deque<chain::transaction_metadata_ptr>(),
         chain::protocol_feature_set(),
         []( chain::block_timestamp_type timestamp,
             const fc::flat_set<chain::digest_type>& cur_features,
             const std::vector<chain::digest_type>& new_features ) {},
         signer
   );
   bsp->id = block_id;
   bsp->block_num = block_num;

   return bsp;
}

std::string set_now(const char* date, const char* time) {
   std::string date_time = std::string(date) + " " + time;
   auto pnow = boost::posix_time::time_from_string(date_time);
   fc::mock_time_traits::set_now(pnow);
   return std::string(date) + "T" + time;
};

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(trx_finality_status_processing_test)

BOOST_AUTO_TEST_CASE(trx_finality_status_logic) { try {
   const auto pre_block_20_time = set_now("2022-04-04", "04:44:44.450");
   fc::microseconds max_success_duration = fc::seconds(25);
   fc::microseconds max_failure_duration = fc::seconds(45);
   trx_finality_status_processing status(10'000, max_success_duration, max_failure_duration);

   using trx_deque = eosio::chain::deque< std::tuple< chain::transaction_trace_ptr, packed_transaction_ptr > >;

   uint32_t bn = 20;
   auto add = [&bn, &status](trx_deque& trx_pairs, const eosio::chain::block_state_ptr& bs_ptr) {
      auto trx = make_unique_trx(fc::seconds(2));
      auto trace = make_transaction_trace( trx, bn, bs_ptr);
      trx_pairs.push_back(std::tuple(trace, trx));
      status.signal_applied_transaction(trace, trx);
   };

   trx_deque trx_pairs_20;

   // Create speculative block to begin applying transactions locally
   status.signal_block_start(bn);
   const eosio::chain::block_state_ptr no_bs;

   add(trx_pairs_20, no_bs);
   add(trx_pairs_20, no_bs);
   add(trx_pairs_20, no_bs);
   add(trx_pairs_20, no_bs);

   auto cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == eosio::chain::block_id_type{});
   BOOST_TEST(!std::get<0>(trx_pairs_20[0])->producer_block_id.has_value());
   BOOST_CHECK(cs.head_block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == eosio::chain::block_id_type{});

   using op_ts = std::optional<eosio::chain_apis::trx_finality_status_processing::trx_state>;

   op_ts ts = status.get_trx_state(std::get<1>(trx_pairs_20[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[2])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[3])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   // Simulate situation where the last 2 trxs do not make it into the block.
   trx_deque hold_pairs;
   std::vector<chain::packed_transaction_ptr> holds;
   hold_pairs.push_back(trx_pairs_20[2]);
   hold_pairs.push_back(trx_pairs_20[3]);
   trx_pairs_20.pop_back();
   trx_pairs_20.pop_back();

   //Make a real block start.  Pull these before any updates to the trx/trace objects.
   // send block 20
   const auto bs_20 = make_block_state(bn);
   status.signal_block_start(bn);

   for (const auto& trx_tuple : trx_pairs_20) {
      const auto& trace = std::get<0>(trx_tuple);
      const auto& txn = std::get<1>(trx_tuple);

      trace->producer_block_id = bs_20->id;
      trace->block_time = bs_20->block->timestamp;

      status.signal_applied_transaction(trace, txn);
   }

   // and 2 new transactions
   const auto block_20_time = set_now("2022-04-04", "04:44:44.500");
   add(trx_pairs_20, bs_20);
   add(trx_pairs_20, bs_20);
   status.signal_accepted_block(bs_20);


   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == bs_20->id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_20[0])->producer_block_id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_20[1])->producer_block_id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_20[2])->producer_block_id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_20[3])->producer_block_id);
   BOOST_CHECK(cs.head_block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == bs_20->id);

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK(fc::time_point_sec(ts->expiration) == (std::get<1>(trx_pairs_20[1])->expiration()));
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[2])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[3])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(hold_pairs[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(hold_pairs[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");



   // send block 21
   const auto block_21_time = set_now("2022-04-04", "04:44:45.000");
   trx_deque trx_pairs_21;
   bn = 21;
   const auto bs_21 = make_block_state(bn);
   status.signal_block_start(bn);
   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);

   add(trx_pairs_21, bs_21);
   status.signal_accepted_block(bs_21);

   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == bs_21->id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_21[0])->producer_block_id);
   BOOST_CHECK(cs.head_block_timestamp == bs_21->block->timestamp);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == bs_20->id);

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[2])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[3])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(hold_pairs[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(hold_pairs[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_21[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_21->id);
   BOOST_CHECK(ts->block_timestamp == bs_21->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_21_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");



   // send block 22
   const auto block_22_time = set_now("2022-04-04", "04:44:45.500");
   trx_deque trx_pairs_22;
   bn = 22;

   const auto bs_22 = make_block_state(bn);
   status.signal_block_start(bn);

   add(trx_pairs_22, bs_22);
   status.signal_accepted_block(bs_22);

   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == bs_22->id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_22[0])->producer_block_id);
   BOOST_CHECK(cs.head_block_timestamp == bs_22->block->timestamp);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == bs_20->id);


   ts = status.get_trx_state(std::get<1>(trx_pairs_20[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[2])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[3])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(hold_pairs[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(hold_pairs[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_21[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_21->id);
   BOOST_CHECK(ts->block_timestamp == bs_21->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_21_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_22[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22->id);
   BOOST_CHECK(ts->block_timestamp == bs_22->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");




   // send block 22
   const auto block_22_alt_time = set_now("2022-04-04", "04:44:46.000");
   trx_deque trx_pairs_22_alt;
   bn = 22;

   const auto bs_22_alt = make_block_state(bn);
   status.signal_block_start(bn);

   add(trx_pairs_22_alt, bs_22_alt);
   status.signal_accepted_block(bs_22_alt);

   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == bs_22_alt->id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_22_alt[0])->producer_block_id);
   BOOST_CHECK(cs.head_block_timestamp == bs_22_alt->block->timestamp);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == bs_20->id);


   ts = status.get_trx_state(std::get<1>(trx_pairs_20[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[2])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[3])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(hold_pairs[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(hold_pairs[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_21[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_21->id);
   BOOST_CHECK(ts->block_timestamp == bs_21->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_21_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_22[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22->id);
   BOOST_CHECK(ts->block_timestamp == bs_22->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_time);
   BOOST_CHECK_EQUAL(ts->status, "FORKED_OUT");

   ts = status.get_trx_state(std::get<1>(trx_pairs_22_alt[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_22_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_alt_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");



   // send block 19 (forking out previous blocks.)
   // Testing that code handles getting blocks before when it started
   const auto block_19_time = set_now("2022-04-04", "04:44:47.000");
   trx_deque trx_pairs_19;
   bn = 19;

   const auto bs_19 = make_block_state(bn);
   status.signal_block_start(bn);

   add(trx_pairs_19, bs_19);
   status.signal_accepted_block(bs_19);

   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == bs_19->id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_19[0])->producer_block_id);
   BOOST_CHECK(cs.head_block_timestamp == bs_19->block->timestamp);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == bs_19->id);


   ts = status.get_trx_state(std::get<1>(trx_pairs_20[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[2])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[3])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_20->id);
   BOOST_CHECK(ts->block_timestamp == bs_20->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(hold_pairs[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(hold_pairs[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_21[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_21->id);
   BOOST_CHECK(ts->block_timestamp == bs_21->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_21_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
   ts = status.get_trx_state(std::get<1>(trx_pairs_22[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22->id);
   BOOST_CHECK(ts->block_timestamp == bs_22->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_time);
   BOOST_CHECK_EQUAL(ts->status, "FAILED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_22_alt[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_22_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_alt_time);
   BOOST_CHECK_EQUAL(ts->status, "FORKED_OUT");

   ts = status.get_trx_state(std::get<1>(trx_pairs_19[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19->id);
   BOOST_CHECK(ts->block_timestamp == bs_19->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_19_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");



   // send block 19 alternate
   const auto block_19_alt_time = set_now("2022-04-04", "04:44:44.000");
   trx_deque trx_pairs_19_alt;
   bn = 19;
   trx_pairs_19_alt.push_back(trx_pairs_19[0]);
   trx_pairs_19_alt.push_back(trx_pairs_20[0]);
   trx_pairs_19_alt.push_back(trx_pairs_20[1]);
   trx_pairs_19_alt.push_back(trx_pairs_20[2]);
   trx_pairs_19_alt.push_back(trx_pairs_20[3]);
   trx_pairs_19_alt.push_back(hold_pairs[0]);

   const auto bs_19_alt = make_block_state(bn);
   // const auto bs_19_alt = make_block_state(make_block_id(bn), std::vector<chain::packed_transaction_ptr>{});
   status.signal_block_start(bn);

   for (const auto& trx_tuple : trx_pairs_19_alt) {
      const auto& trace = std::get<0>(trx_tuple);
      const auto& txn = std::get<1>(trx_tuple);

      trace->producer_block_id = bs_19_alt->id;
      trace->block_time = bs_19_alt->block->timestamp;

      status.signal_applied_transaction(trace, txn);
   }

   status.signal_accepted_block(bs_19_alt);

   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == bs_19_alt->id);
   BOOST_CHECK(cs.head_id == *std::get<0>(trx_pairs_19[0])->producer_block_id);
   BOOST_CHECK(cs.head_block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == bs_19_alt->id);


   ts = status.get_trx_state(std::get<1>(trx_pairs_20[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[2])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[3])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(hold_pairs[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");

   ts = status.get_trx_state(std::get<1>(hold_pairs[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_21[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_21->id);
   BOOST_CHECK(ts->block_timestamp == bs_21->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_21_time);
   BOOST_CHECK_EQUAL(ts->status, "FORKED_OUT");

   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
   ts = status.get_trx_state(std::get<1>(trx_pairs_22[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22->id);
   BOOST_CHECK(ts->block_timestamp == bs_22->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_time);
   BOOST_CHECK_EQUAL(ts->status, "FORKED_OUT");

   ts = status.get_trx_state(std::get<1>(trx_pairs_22_alt[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_22_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_alt_time);
   BOOST_CHECK_EQUAL(ts->status, "FORKED_OUT");

   ts = status.get_trx_state(std::get<1>(trx_pairs_19[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_19_time);
   BOOST_CHECK_EQUAL(ts->status, "IN_BLOCK");


   // look for unknown transaction
   auto trx = make_unique_trx(fc::seconds(2));

   ts = status.get_trx_state(trx->id());
   BOOST_REQUIRE(!ts);

   // irreversible
   status.signal_irreversible_block(bs_19_alt);

   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == bs_19_alt->id);
   BOOST_CHECK(cs.irr_id == bs_19_alt->id);
   BOOST_CHECK(cs.irr_block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK(cs.earliest_tracked_block_id == bs_19_alt->id);


   ts = status.get_trx_state(std::get<1>(trx_pairs_20[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IRREVERSIBLE");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IRREVERSIBLE");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[2])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IRREVERSIBLE");

   ts = status.get_trx_state(std::get<1>(trx_pairs_20[3])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IRREVERSIBLE");

   ts = status.get_trx_state(std::get<1>(hold_pairs[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "IRREVERSIBLE");

   ts = status.get_trx_state(std::get<1>(hold_pairs[1])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == eosio::chain::block_id_type{});
   BOOST_CHECK(ts->block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK_EQUAL(std::string(ts->received), pre_block_20_time);
   BOOST_CHECK_EQUAL(ts->status, "LOCALLY_APPLIED");

   ts = status.get_trx_state(std::get<1>(trx_pairs_21[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_21->id);
   BOOST_CHECK(ts->block_timestamp == bs_21->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_21_time);
   BOOST_CHECK_EQUAL(ts->status, "FORKED_OUT");

   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
   ts = status.get_trx_state(std::get<1>(trx_pairs_22[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22->id);
   BOOST_CHECK(ts->block_timestamp == bs_22->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_time);
   BOOST_CHECK_EQUAL(ts->status, "FORKED_OUT");

   ts = status.get_trx_state(std::get<1>(trx_pairs_22_alt[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_22_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_22_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_22_alt_time);
   BOOST_CHECK_EQUAL(ts->status, "FORKED_OUT");

   ts = status.get_trx_state(std::get<1>(trx_pairs_19[0])->id());
   BOOST_REQUIRE(ts);
   BOOST_CHECK(ts->block_id == bs_19_alt->id);
   BOOST_CHECK(ts->block_timestamp == bs_19_alt->block->timestamp);
   BOOST_CHECK_EQUAL(std::string(ts->received), block_19_time);
   BOOST_CHECK_EQUAL(ts->status, "IRREVERSIBLE");

} FC_LOG_AND_RETHROW() }

namespace {
   using trx_deque = eosio::chain::deque< std::tuple< chain::transaction_trace_ptr, packed_transaction_ptr > >;
   const eosio::chain::block_state_ptr no_bs;

   struct block_frame {
      static uint32_t last_used_block_num;
      static const uint32_t num = 5;
      trx_finality_status_processing& status;
      const uint32_t bn;
      const std::string time;
      trx_deque pre_block;
      trx_deque block;
      chain::block_state_ptr bs;
      std::string context;

      block_frame(trx_finality_status_processing& finality_status, const char* block_time, uint32_t block_num = 0)
      : status(finality_status),
        bn(block_num == 0 ? block_frame::last_used_block_num + 1 : block_num),
        time(set_now("2022-04-04", block_time)) {
         block_frame::last_used_block_num = bn;
         for (uint32_t i = 0; i < block_frame::num; ++i) {
            auto trx = make_unique_trx(fc::seconds(30));
            auto trace = make_transaction_trace( trx, bn, no_bs);
            pre_block.push_back(std::tuple(trace, trx));
            status.signal_applied_transaction(trace, trx);
         }
         bs = make_block_state(bn);
         for (uint32_t i = 0; i < block_frame::num; ++i) {
            auto trx = make_unique_trx(fc::seconds(30));
            auto trace = make_transaction_trace( trx, bn, bs);
            block.push_back(std::tuple(trace, trx));
            status.signal_applied_transaction(trace, trx);
         }
      }

      void verify_block(uint32_t begin = 0, uint32_t end = std::numeric_limits<uint32_t>::max()) {
         context = "verify_block";
         verify(block, bs, begin, end);
      }

      void verify_block_not_there(uint32_t begin = 0, uint32_t end = std::numeric_limits<uint32_t>::max()) {
         context = "verify_block_not_there";
         verify_not_there(block, begin, end);
      }

      void verify_spec_block(uint32_t begin = 0, uint32_t end = std::numeric_limits<uint32_t>::max()) {
         context = "verify_spec_block";
         verify(pre_block, no_bs, begin, end);
      }

      void verify_spec_block_not_there(uint32_t begin = 0, uint32_t end = std::numeric_limits<uint32_t>::max()) {
         context = "verify_spec_block_not_there";
         verify_not_there(pre_block, begin, end);
      }

      void send_block() {
         status.signal_block_start(bn);

         for (const auto& trx_tuple : block)
         {
            const auto& trace = std::get<0>(trx_tuple);
            const auto& txn = std::get<1>(trx_tuple);

            status.signal_applied_transaction(trace, txn);
         }

         status.signal_accepted_block(bs);
      }

      void send_spec_block() {
         status.signal_block_start(bn);

         for (const auto& trx_tuple : pre_block)
         {
            const auto& trace = std::get<0>(trx_tuple);
            const auto& txn = std::get<1>(trx_tuple);

            status.signal_applied_transaction(trace, txn);
         }
      }

   private:
      void verify(const trx_deque& trx_pairs, const chain::block_state_ptr& bs, uint32_t begin, uint32_t end) {
         if (end == std::numeric_limits<uint32_t>::max()) {
            end = block.size();
         }
         const auto id = bs ? bs->id : eosio::chain::transaction_id_type{};
         for (auto i = begin; i < end; ++i) {
            const auto& trx_pair = trx_pairs[i];
            std::string msg = context + ": block_num==" + std::to_string(bn) + ", i==" + std::to_string(i) + ", id: " + std::string(std::get<1>(trx_pair)->id());
            auto ts = status.get_trx_state(std::get<1>(trx_pair)->id());
            BOOST_REQUIRE_MESSAGE(ts, msg);
            BOOST_CHECK_MESSAGE(ts->block_id == id, msg);
         }
      }
      void verify_not_there(const trx_deque& trx_pairs, uint32_t begin, uint32_t end) {
         if (end == std::numeric_limits<uint32_t>::max()) {
            end = block.size();
         }
         for (auto i = begin; i < end; ++i) {
            std::string msg = context + "block_num==" + std::to_string(bn) + " i==" + std::to_string(i);
            const auto& trx_pair = trx_pairs[i];
            auto ts = status.get_trx_state(std::get<1>(trx_pair)->id());
            BOOST_REQUIRE_MESSAGE(!ts, msg);
         }
      }

   };
   uint32_t block_frame::last_used_block_num = 0;
}

BOOST_AUTO_TEST_CASE(trx_finality_status_storage_reduction) { try {
   set_now("2022-04-04", "04:44:44.450");
   fc::microseconds max_success_duration = fc::seconds(25);
   fc::microseconds max_failure_duration = fc::seconds(45);
   const uint64_t max_storage = 10'000;
   trx_finality_status_processing status(max_storage, max_success_duration, max_failure_duration);

   // auto verify_trx = [&status](trx_deque& trx_pairs, const eosio::chain::block_state_ptr& bs) {
   //    const auto id = bs ? bs->id : eosio::chain::transaction_id_type{};
   //    for (const auto& trx_pair : trx_pairs) {
   //       auto ts = status.get_trx_state(std::get<1>(trx_pair)->id());
   //       BOOST_REQUIRE(ts);
   //       BOOST_CHECK(ts->block_id == id);
   //    }
   // };

   block_frame b_01(status, "04:44:00.500", 1);
   b_01.send_spec_block();
   b_01.verify_spec_block();

   b_01.send_block();
   b_01.verify_block();

   const auto block_and_speculative_size = status.get_storage_memory_size();
   // test expects to not hit the storage limitation till the 12th block
   BOOST_REQUIRE(max_storage / 11 > block_and_speculative_size);
   BOOST_REQUIRE(max_storage / 12 < block_and_speculative_size);


   block_frame b_02(status, "04:44:01.500");
   b_02.send_spec_block();
   b_02.verify_spec_block();

   b_02.send_block();
   b_02.verify_block();


   block_frame b_03(status, "04:44:02.500");
   b_03.send_spec_block();
   b_03.verify_spec_block();

   b_03.send_block();
   b_03.verify_block();


   block_frame b_04(status, "04:44:03.500");
   b_04.send_spec_block();
   b_04.verify_spec_block();

   b_04.send_block();
   b_04.verify_block();


   block_frame b_05(status, "04:44:04.500");
   b_05.send_spec_block();
   b_05.verify_spec_block();

   b_05.send_block();
   b_05.verify_block();


   block_frame b_06(status, "04:44:05.500");
   b_06.send_spec_block();
   b_06.verify_spec_block();

   b_06.send_block();
   b_06.verify_block();


   block_frame b_07(status, "04:44:06.500");
   b_07.send_spec_block();
   b_07.verify_spec_block();

   b_07.send_block();
   b_07.verify_block();


   block_frame b_08(status, "04:44:07.500");
   b_08.send_spec_block();
   b_08.verify_spec_block();

   b_08.send_block();
   b_08.verify_block();


   block_frame b_09(status, "04:44:08.500");
   b_09.send_spec_block();
   b_09.verify_spec_block();

   b_09.send_block();
   b_09.verify_block();


   block_frame b_10(status, "04:44:09.500");
   b_10.send_spec_block();
   b_10.verify_spec_block();

   b_10.send_block();
   b_10.verify_block();


   block_frame b_11(status, "04:44:10.500");
   b_11.send_spec_block();
   b_11.verify_spec_block();

   b_11.send_block();
   b_11.verify_block();



   auto cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == b_11.bs->id);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == b_01.bs->id);

   // Test expects the next block range to exceed max_storage. Need to adjust
   // this test if this fails.
   BOOST_REQUIRE(status.get_storage_memory_size() + block_and_speculative_size > max_storage);


   block_frame b_12(status, "04:44:11.500");
   b_12.send_spec_block();
   b_12.verify_spec_block();

   b_12.send_block();
   b_12.verify_block();

   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == b_12.bs->id);
   BOOST_CHECK(cs.head_block_timestamp == b_12.bs->block->timestamp);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.irr_block_timestamp == eosio::chain::block_timestamp_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == b_03.bs->id);


   b_01.verify_spec_block_not_there();
   b_01.verify_block_not_there();

   b_02.verify_spec_block_not_there();
   b_02.verify_block_not_there();

   b_03.verify_spec_block();
   b_03.verify_block();

   b_04.verify_spec_block();
   b_04.verify_block();

   b_05.verify_spec_block();
   b_05.verify_block();

   b_06.verify_spec_block();
   b_06.verify_block();

   b_07.verify_spec_block();
   b_07.verify_block();

   b_08.verify_spec_block();
   b_08.verify_block();

   b_09.verify_spec_block();
   b_09.verify_block();

   b_10.verify_spec_block();
   b_10.verify_block();

   b_11.verify_spec_block();
   b_11.verify_block();

   b_12.verify_spec_block();
   b_12.verify_block();

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE(trx_finality_status_lifespan) { try {
   set_now("2022-04-04", "04:44:44.450");
   fc::microseconds max_success_duration = fc::seconds(25);
   fc::microseconds max_failure_duration = fc::seconds(35);
   const uint64_t max_storage = 10'000;
   trx_finality_status_processing status(max_storage, max_success_duration, max_failure_duration);

   // auto verify_trx = [&status](trx_deque& trx_pairs, const eosio::chain::block_state_ptr& bs) {
   //    const auto id = bs ? bs->id : eosio::chain::transaction_id_type{};
   //    for (const auto& trx_pair : trx_pairs) {
   //       auto ts = status.get_trx_state(std::get<1>(trx_pair)->id());
   //       BOOST_REQUIRE(ts);
   //       BOOST_CHECK(ts->block_id == id);
   //    }
   // };


   block_frame b_01(status, "04:44:00.500", 1);
   b_01.send_spec_block();
   b_01.verify_spec_block();

   b_01.send_block();
   b_01.verify_block();


   block_frame b_02(status, "04:44:05.500");
   b_02.send_spec_block();
   b_02.verify_spec_block();

   b_02.send_block();
   b_02.verify_block();


   block_frame b_03(status, "04:44:10.500");
   b_03.send_spec_block();
   b_03.verify_spec_block();

   b_03.send_block();
   b_03.verify_block();


   block_frame b_04(status, "04:44:15.500");
   b_04.send_spec_block();
   b_04.verify_spec_block();

   b_04.send_block();
   b_04.verify_block();


   block_frame b_05(status, "04:44:20.500");
   b_05.send_spec_block();
   b_05.verify_spec_block();

   b_05.send_block();
   b_05.verify_block();

   // should be still available
   b_01.verify_block();
   b_01.verify_spec_block(); // still available and will continue till failure time


   block_frame b_06(status, "04:44:25.500");
   b_06.send_spec_block();
   b_06.verify_spec_block();

   b_06.send_block();
   b_06.verify_block();

   // block 1 now removed
   b_01.verify_block_not_there();
   b_02.verify_block();
   b_01.verify_spec_block();

   auto cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == b_06.bs->id);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == b_02.bs->id);


   block_frame b_07(status, "04:44:30.500");
   b_07.send_spec_block();
   b_07.verify_spec_block();

   b_07.send_block();
   b_07.verify_block();

   // block 2 now removed
   b_02.verify_block_not_there();
   b_03.verify_block();
   b_01.verify_spec_block();
   b_02.verify_spec_block();

   cs = status.get_chain_state();
   BOOST_CHECK(cs.head_id == b_07.bs->id);
   BOOST_CHECK(cs.irr_id == eosio::chain::block_id_type{});
   BOOST_CHECK(cs.earliest_tracked_block_id == b_03.bs->id);


   block_frame b_08(status, "04:44:35.500");
   b_08.send_spec_block();
   b_08.verify_spec_block();

   b_08.send_block();
   b_08.verify_block();

   // block 3 now removed and speculative's from block 1 time frame
   b_03.verify_block_not_there();
   b_04.verify_block();
   b_01.verify_spec_block_not_there();
   b_02.verify_spec_block();
   b_03.verify_spec_block();


   block_frame b_09(status, "04:44:40.500");
   b_09.send_spec_block();
   b_09.verify_spec_block();

   b_09.send_block();
   b_09.verify_block();

   // block 4 now removed and speculative's from block 2 time frame
   b_04.verify_block_not_there();
   b_05.verify_block();
   b_02.verify_spec_block_not_there();
   b_03.verify_spec_block();
   b_04.verify_spec_block();


   block_frame b_10(status, "04:44:45.500");
   b_10.send_spec_block();
   b_10.verify_spec_block();

   b_10.send_block();
   b_10.verify_block();

   // block 5 now removed and speculative's from block 3 time frame
   b_05.verify_block_not_there();
   b_06.verify_block();
   b_03.verify_spec_block_not_there();
   b_04.verify_spec_block();
   b_05.verify_spec_block();

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
