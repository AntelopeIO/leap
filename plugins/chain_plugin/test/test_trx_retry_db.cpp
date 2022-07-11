#define BOOST_TEST_MODULE transaction_retry
#include <boost/test/included/unit_test.hpp>

#include <eosio/chain_plugin/trx_retry_db.hpp>

#include <eosio/testing/tester.hpp>

#include <eosio/chain/controller.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/name.hpp>

#include <appbase/application.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <fc/mock_time.hpp>
#include <fc/bitutil.hpp>

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem.hpp>
#include <thread>
#include <condition_variable>
#include <deque>
#include <memory>

namespace eosio::test::detail {

using namespace eosio::chain;
using namespace eosio::chain::literals;

struct testit {
   uint64_t      id;
   testit( uint64_t id = 0 )
         :id(id){}

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

// simple thread-safe queue
template <typename T>
class blocking_queue
{
public:
   void push( T const& value ) {
      {
         std::unique_lock<std::mutex> lock( mtx );
         queue.push_front( value );
      }
      cond_v.notify_one();
   }

   T pop() {
      std::unique_lock<std::mutex> lock( mtx );
      if( !cond_v.wait_for( lock, std::chrono::seconds( 10 ),
                       [&] { return !queue.empty(); } ) ){
         throw std::runtime_error("timed out, nothing in queue");
      }
      T r( std::move( queue.back() ) );
      queue.pop_back();
      return r;
   }

   size_t size() const {
      std::unique_lock<std::mutex> lock( mtx );
      return queue.size();
   }
private:
   mutable std::mutex      mtx;
   std::condition_variable cond_v;
   std::deque<T>           queue;
};

auto get_private_key( chain::name keyname, std::string role = "owner" ) {
   auto secret = fc::sha256::hash( keyname.to_string() + role );
   return chain::private_key_type::regenerate<fc::ecc::private_key_shim>( secret );
}

auto get_public_key( chain::name keyname, std::string role = "owner" ) {
   return get_private_key( keyname, role ).get_public_key();
}

auto make_unique_trx( const chain_id_type& chain_id, const fc::microseconds& expiration, uint64_t id ) {

   account_name creator = config::system_account_name;
   signed_transaction trx;
   trx.expiration = fc::time_point::now() + expiration;
   trx.actions.emplace_back( vector<permission_level>{{creator, config::active_name}},
                             testit{ id } );
   trx.sign( get_private_key("test"_n), chain_id );

   return std::make_shared<packed_transaction>( std::move(trx), packed_transaction::compression_type::none);
}

chain::transaction_trace_ptr make_transaction_trace( const packed_transaction_ptr trx, uint32_t block_number,
                                                     chain::transaction_receipt_header::status_enum status = eosio::chain::transaction_receipt_header::executed ) {
   return std::make_shared<chain::transaction_trace>(chain::transaction_trace{
         trx->id(),
         block_number,
         chain::block_timestamp_type(fc::time_point::now()),
         trx->id(), // block_id, doesn't matter what it is for this test as long as it is set
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

uint64_t get_id( const transaction& trx ) {
   testit t = trx.actions.at(0).data_as<testit>();
   return t.id;
}

uint64_t get_id( const packed_transaction_ptr& ptr ) {
   return get_id( ptr->get_transaction() );
}

auto make_block_state( uint32_t block_num, std::vector<chain::packed_transaction_ptr> trxs ) {
   name producer = "kevinh"_n;
   chain::signed_block_ptr block = std::make_shared<chain::signed_block>();
   for( auto& trx : trxs ) {
      block->transactions.emplace_back( *trx );
   }
   block->producer = producer;
   block->timestamp = fc::time_point::now();
   chain::block_id_type previous;
   previous._hash[0] &= 0xffffffff00000000;
   previous._hash[0] += fc::endian_reverse_u32(block_num - 1);

   block->previous = previous;

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
   bsp->block_num = block_num;

   return bsp;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(trx_retry_db_test)

BOOST_AUTO_TEST_CASE(trx_retry_logic) {
   boost::filesystem::path temp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

   try {
      fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);

      // just need a controller for trx_retry_db, doesn't actually have to do anything
      std::optional<controller> chain;
      genesis_state gs{};
      {
         controller::config chain_config = controller::config();
         chain_config.blocks_dir = temp;
         chain_config.state_dir = temp;

         const auto& genesis_chain_id = gs.compute_chain_id();
         protocol_feature_set pfs;
         chain.emplace( chain_config, std::move( pfs ), genesis_chain_id );
         chain->add_indices();
      }

      // control time by using set_now, call before spawning any threads
      auto pnow = boost::posix_time::time_from_string("2022-04-04 4:44:44.000");
      fc::mock_time_traits::set_now(pnow);

      // run app() so that channels::transaction_ack work
      std::thread app_thread( [&]() {
         appbase::app().exec();
      } );

      size_t max_mem_usage_size = 5ul*1024*1024*1024;
      fc::microseconds retry_interval = fc::seconds(10);
      boost::posix_time::seconds pretry_interval = boost::posix_time::seconds(10);
      BOOST_REQUIRE(retry_interval.count() == pretry_interval.total_microseconds());
      fc::microseconds max_expiration_time = fc::hours(1);
      trx_retry_db trx_retry( *chain, max_mem_usage_size, retry_interval, max_expiration_time, fc::seconds(10) );

      // provide a subscriber for the transaction_ack channel
      blocking_queue<std::pair<fc::exception_ptr, packed_transaction_ptr>> transactions_acked;
      plugin_interface::compat::channels::transaction_ack::channel_type::handle incoming_transaction_ack_subscription =
            appbase::app().get_channel<plugin_interface::compat::channels::transaction_ack>().subscribe(
                  [&transactions_acked]( const std::pair<fc::exception_ptr, packed_transaction_ptr>& t){
                     transactions_acked.push( t );
                  } );


      // test get_max_expiration_time
      BOOST_CHECK( fc::time_point::now() + fc::hours(1) == fc::time_point( trx_retry.get_max_expiration_time() ) );

      //
      // test expired, not in a block
      //
      auto lib = std::optional<uint16_t>{};
      auto trx_1 = make_unique_trx(chain->get_chain_id(), fc::seconds(2), 1);
      bool trx_1_expired = false;
      trx_retry.track_transaction( trx_1, lib, [&trx_1_expired](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<fc::exception_ptr>(result) );
         BOOST_CHECK_EQUAL( std::get<fc::exception_ptr>(result)->code(), expired_tx_exception::code_value );
         trx_1_expired = true;
      } );
      auto trx_2 = make_unique_trx(chain->get_chain_id(), fc::seconds(4), 2);
      bool trx_2_expired = false;
      trx_retry.track_transaction( trx_2, lib, [&trx_2_expired](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<fc::exception_ptr>(result) );
         BOOST_CHECK_EQUAL( std::get<fc::exception_ptr>(result)->code(), expired_tx_exception::code_value );
         trx_2_expired = true;
      } );
      // signal block, nothing should be expired as now has not changed
      auto bsp1 = make_block_state(1, {});
      trx_retry.on_block_start(1);
      trx_retry.on_accepted_block(bsp1);
      trx_retry.on_irreversible_block(bsp1);
      BOOST_CHECK(!trx_1_expired);
      BOOST_CHECK(!trx_2_expired);
      // increase time by 3 seconds to expire first
      pnow += boost::posix_time::seconds(3);
      fc::mock_time_traits::set_now(pnow);
      // signal block, first transaction should expire
      auto bsp2 = make_block_state(2, {});
      trx_retry.on_block_start(2);
      trx_retry.on_accepted_block(bsp2);
      trx_retry.on_irreversible_block(bsp2);
      BOOST_CHECK(trx_1_expired);
      BOOST_CHECK(!trx_2_expired);
      // increase time by 2 seconds to expire second
      pnow += boost::posix_time::seconds(2);
      fc::mock_time_traits::set_now(pnow);
      // signal block, second transaction should expire
      auto bsp3 = make_block_state(3, {});
      trx_retry.on_block_start(3);
      trx_retry.on_accepted_block(bsp3);
      trx_retry.on_irreversible_block(bsp3);
      BOOST_CHECK(trx_1_expired);
      BOOST_CHECK(trx_2_expired);
      BOOST_CHECK_EQUAL(0, trx_retry.size());

      //
      // test resend trx if not in block
      //
      auto trx_3 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 3);
      bool trx_3_expired = false;
      trx_retry.track_transaction( trx_3, lib, [&trx_3_expired](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<fc::exception_ptr>(result) );
         BOOST_CHECK_EQUAL( std::get<fc::exception_ptr>(result)->code(), expired_tx_exception::code_value );
         trx_3_expired = true;
      } );
      // increase time by 1 seconds, so trx_4 retry interval diff than 3
      pnow += boost::posix_time::seconds(1);
      fc::mock_time_traits::set_now(pnow);
      auto trx_4 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 4);
      bool trx_4_expired = false;
      trx_retry.track_transaction( trx_4, lib, [&trx_4_expired](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<fc::exception_ptr>(result) );
         BOOST_CHECK_EQUAL( std::get<fc::exception_ptr>(result)->code(), expired_tx_exception::code_value );
         trx_4_expired = true;
      } );
      // increase time by interval to cause send
      pnow += (pretry_interval - boost::posix_time::seconds(1));
      fc::mock_time_traits::set_now(pnow);
      // signal block, transaction 3 should be sent
      auto bsp4 = make_block_state(4, {});
      trx_retry.on_block_start(4);
      trx_retry.on_accepted_block(bsp4);
      BOOST_CHECK( get_id(transactions_acked.pop().second) == 3 );
      BOOST_CHECK_EQUAL( 0, transactions_acked.size() );
      // increase time by 1 seconds, so trx_4 is sent
      pnow += boost::posix_time::seconds(1);
      fc::mock_time_traits::set_now(pnow);
      // signal block, transaction 4 should be sent
      auto bsp5 = make_block_state(5, {});
      trx_retry.on_block_start(5);
      trx_retry.on_accepted_block(bsp5);
      BOOST_CHECK( get_id(transactions_acked.pop().second) == 4 );
      BOOST_CHECK_EQUAL( 0, transactions_acked.size() );
      BOOST_CHECK(!trx_3_expired);
      BOOST_CHECK(!trx_4_expired);
      // go ahead and expire them now
      pnow += boost::posix_time::seconds(30);
      fc::mock_time_traits::set_now(pnow);
      auto bsp6 = make_block_state(6, {});
      trx_retry.on_block_start(6);
      trx_retry.on_accepted_block(bsp6);
      trx_retry.on_irreversible_block(bsp4);
      trx_retry.on_irreversible_block(bsp5);
      trx_retry.on_irreversible_block(bsp6);
      BOOST_CHECK(trx_3_expired);
      BOOST_CHECK(trx_4_expired);
      BOOST_CHECK_EQUAL(0, trx_retry.size());

      //
      // test reply to user
      //
      auto trx_5 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 5);
      bool trx_5_variant = false;
      trx_retry.track_transaction( trx_5, lib, [&trx_5_variant](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<std::unique_ptr<fc::variant>>(result) );
         BOOST_CHECK( !!std::get<std::unique_ptr<fc::variant>>(result) );
         trx_5_variant = true;
      } );
      // increase time by 1 seconds, so trx_6 retry interval diff than 5
      pnow += boost::posix_time::seconds(1);
      fc::mock_time_traits::set_now(pnow);
      auto trx_6 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 6);
      bool trx_6_variant = false;
      trx_retry.track_transaction( trx_6, std::optional<uint32_t>(2), [&trx_6_variant](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<std::unique_ptr<fc::variant>>(result) );
         BOOST_CHECK( !!std::get<std::unique_ptr<fc::variant>>(result) );
         trx_6_variant = true;
      } );
      // not in block 7, so not returned to user
      auto bsp7 = make_block_state(7, {});
      trx_retry.on_block_start(7);
      trx_retry.on_accepted_block(bsp7);
      BOOST_CHECK(!trx_5_variant);
      BOOST_CHECK(!trx_6_variant);
      // 5,6 in block 8
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      trx_retry.on_block_start(8);
      auto trace_5 = make_transaction_trace( trx_5, 8);
      auto trace_6 = make_transaction_trace( trx_6, 8);
      trx_retry.on_applied_transaction(trace_5, trx_5);
      trx_retry.on_applied_transaction(trace_6, trx_6);
      auto bsp8 = make_block_state(8, {trx_5, trx_6});
      trx_retry.on_accepted_block(bsp8);
      BOOST_CHECK(!trx_5_variant);
      BOOST_CHECK(!trx_6_variant);
      // need 2 blocks before 6 returned to user
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      auto bsp9 = make_block_state(9, {});
      trx_retry.on_block_start(9);
      trx_retry.on_accepted_block(bsp9);
      BOOST_CHECK(!trx_5_variant);
      BOOST_CHECK(!trx_6_variant);
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      auto bsp10 = make_block_state(10, {});
      trx_retry.on_block_start(10);
      trx_retry.on_accepted_block(bsp10);
      BOOST_CHECK(!trx_5_variant);
      BOOST_CHECK(trx_6_variant);
      // now signal lib for trx_6
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      auto bsp11 = make_block_state(11, {});
      trx_retry.on_block_start(11);
      trx_retry.on_accepted_block(bsp11);
      BOOST_CHECK(!trx_5_variant);
      BOOST_CHECK(trx_6_variant);
      trx_retry.on_irreversible_block(bsp7);
      BOOST_CHECK(!trx_5_variant);
      BOOST_CHECK(trx_6_variant);
      trx_retry.on_irreversible_block(bsp8);
      BOOST_CHECK(trx_5_variant);
      BOOST_CHECK(trx_6_variant);
      BOOST_CHECK_EQUAL(0, trx_retry.size());

      //
      // test forking
      //
      auto trx_7 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 7);
      bool trx_7_variant = false;
      trx_retry.track_transaction( trx_7, lib, [&trx_7_variant](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<std::unique_ptr<fc::variant>>(result) );
         BOOST_CHECK( !!std::get<std::unique_ptr<fc::variant>>(result) );
         trx_7_variant = true;
      } );
      // increase time by 1 seconds, so trx_8 retry interval diff than 7
      pnow += boost::posix_time::seconds(1);
      fc::mock_time_traits::set_now(pnow);
      auto trx_8 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 8);
      bool trx_8_variant = false;
      trx_retry.track_transaction( trx_8, std::optional<uint32_t>(3), [&trx_8_variant](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<std::unique_ptr<fc::variant>>(result) );
         BOOST_CHECK( !!std::get<std::unique_ptr<fc::variant>>(result) );
         trx_8_variant = true;
      } );
      // one to expire, will be forked out never to return
      auto trx_9 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 9);
      bool trx_9_expired = false;
      trx_retry.track_transaction( trx_9, lib, [&trx_9_expired](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<fc::exception_ptr>(result) );
         BOOST_CHECK_EQUAL( std::get<fc::exception_ptr>(result)->code(), expired_tx_exception::code_value );
         trx_9_expired = true;
      } );

      // not in block 12
      auto bsp12 = make_block_state(12, {});
      trx_retry.on_block_start(12);
      trx_retry.on_accepted_block(bsp12);
      BOOST_CHECK(!trx_7_variant);
      BOOST_CHECK(!trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      // 7,8 in block 13
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      trx_retry.on_block_start(13);
      auto trace_7 = make_transaction_trace( trx_7, 13);
      auto trace_8 = make_transaction_trace( trx_8, 13);
      auto trace_9 = make_transaction_trace( trx_9, 13);
      trx_retry.on_applied_transaction(trace_7, trx_7);
      trx_retry.on_applied_transaction(trace_8, trx_8);
      trx_retry.on_applied_transaction(trace_9, trx_9);
      auto bsp13 = make_block_state(13, {trx_7, trx_8, trx_9});
      trx_retry.on_accepted_block(bsp13);
      BOOST_CHECK(!trx_7_variant);
      BOOST_CHECK(!trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      // need 3 blocks before 8 returned to user
      pnow += boost::posix_time::seconds(1); // new block, new time, 1st block
      fc::mock_time_traits::set_now(pnow);
      auto bsp14 = make_block_state(14, {});
      trx_retry.on_block_start(14);
      trx_retry.on_accepted_block(bsp14);
      BOOST_CHECK(!trx_7_variant);
      BOOST_CHECK(!trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      pnow += boost::posix_time::seconds(1); // new block, new time, 2nd block
      fc::mock_time_traits::set_now(pnow);
      auto bsp15 = make_block_state(15, {});
      trx_retry.on_block_start(15);
      trx_retry.on_accepted_block(bsp15);
      BOOST_CHECK(!trx_7_variant);
      BOOST_CHECK(!trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      // now fork out including 13 which had traces, trx_9 will be forked out and not re-appear
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      trx_retry.on_block_start(13);
      // should still be tracking them
      BOOST_CHECK_EQUAL(3, trx_retry.size());
      // now produce an empty 13
      auto bsp13b = make_block_state(13, {}); // now 13 has no traces
      trx_retry.on_accepted_block(bsp13b);
      // produced another empty block
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      trx_retry.on_block_start(14);
      // now produce an empty 14
      auto bsp14b = make_block_state(14, {}); // empty
      trx_retry.on_accepted_block(bsp14b);
      // produce block with 7,8
      trx_retry.on_block_start(15);
      auto trace_7b = make_transaction_trace( trx_7, 15);
      auto trace_8b = make_transaction_trace( trx_8, 15);
      trx_retry.on_applied_transaction(trace_7b, trx_7);
      trx_retry.on_applied_transaction(trace_8b, trx_8);
      auto bsp15b = make_block_state(15, {trx_7, trx_8});
      trx_retry.on_accepted_block(bsp15b);
      // need 3 blocks before 8 returned to user
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      auto bsp16 = make_block_state(16, {});
      trx_retry.on_block_start(16);
      trx_retry.on_accepted_block(bsp16);
      BOOST_CHECK(!trx_7_variant);
      BOOST_CHECK(!trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      auto bsp17 = make_block_state(17, {});
      trx_retry.on_block_start(17);
      trx_retry.on_accepted_block(bsp17);
      BOOST_CHECK(!trx_7_variant);
      BOOST_CHECK(!trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      pnow += boost::posix_time::seconds(1); // new block, new time, 3rd one
      fc::mock_time_traits::set_now(pnow);
      auto bsp18 = make_block_state(18, {});
      trx_retry.on_block_start(18);
      trx_retry.on_accepted_block(bsp18);
      BOOST_CHECK(!trx_7_variant);
      BOOST_CHECK(trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      trx_retry.on_irreversible_block(bsp9);
      trx_retry.on_irreversible_block(bsp10);
      trx_retry.on_irreversible_block(bsp11);
      trx_retry.on_irreversible_block(bsp12);
      trx_retry.on_irreversible_block(bsp13b);
      trx_retry.on_irreversible_block(bsp14b);
      BOOST_CHECK(!trx_7_variant);
      BOOST_CHECK(trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      trx_retry.on_irreversible_block(bsp15b);
      BOOST_CHECK(trx_7_variant);
      BOOST_CHECK(trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      // verify trx_9 expires
      pnow += boost::posix_time::seconds(21); // new block, new time, before expire
      fc::mock_time_traits::set_now(pnow);
      auto bsp19 = make_block_state(19, {});
      trx_retry.on_block_start(19);
      trx_retry.on_accepted_block(bsp19);
      trx_retry.on_irreversible_block(bsp15);
      trx_retry.on_irreversible_block(bsp16);
      trx_retry.on_irreversible_block(bsp17);
      trx_retry.on_irreversible_block(bsp18);
      trx_retry.on_irreversible_block(bsp19);
      BOOST_CHECK(trx_7_variant);
      BOOST_CHECK(trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      pnow += boost::posix_time::seconds(1); // new block, new time, trx_9 now expired
      fc::mock_time_traits::set_now(pnow);
      auto bsp20 = make_block_state(20, {});
      trx_retry.on_block_start(20);
      trx_retry.on_accepted_block(bsp20);
      // waits for LIB
      BOOST_CHECK(trx_7_variant);
      BOOST_CHECK(trx_8_variant);
      BOOST_CHECK(!trx_9_expired);
      trx_retry.on_irreversible_block(bsp20);
      BOOST_CHECK(trx_7_variant);
      BOOST_CHECK(trx_8_variant);
      BOOST_CHECK(trx_9_expired);
      BOOST_CHECK_EQUAL(0, trx_retry.size());

      //
      // test reply to user for num_blocks == 0
      //
      auto trx_10 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 10);
      bool trx_10_variant = false;
      trx_retry.track_transaction( trx_10, std::optional<uint32_t>(0), [&trx_10_variant](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<std::unique_ptr<fc::variant>>(result) );
         BOOST_CHECK( !!std::get<std::unique_ptr<fc::variant>>(result) );
         trx_10_variant = true;
      } );
      auto trx_11 = make_unique_trx(chain->get_chain_id(), fc::seconds(30), 11);
      bool trx_11_variant = false;
      trx_retry.track_transaction( trx_11, std::optional<uint32_t>(1), [&trx_11_variant](const std::variant<fc::exception_ptr, std::unique_ptr<fc::variant>>& result){
         BOOST_REQUIRE( std::holds_alternative<std::unique_ptr<fc::variant>>(result) );
         BOOST_CHECK( !!std::get<std::unique_ptr<fc::variant>>(result) );
         trx_11_variant = true;
      } );
      // seen in block immediately
      trx_retry.on_block_start(21);
      auto trace_10 = make_transaction_trace( trx_10, 21);
      auto trace_11 = make_transaction_trace( trx_11, 21);
      trx_retry.on_applied_transaction(trace_10, trx_10);
      trx_retry.on_applied_transaction(trace_11, trx_11);
      auto bsp21 = make_block_state(21, {trx_10, trx_11});
      trx_retry.on_accepted_block(bsp21);
      BOOST_CHECK(trx_10_variant);
      BOOST_CHECK(!trx_11_variant);
      pnow += boost::posix_time::seconds(1); // new block, new time
      fc::mock_time_traits::set_now(pnow);
      auto bsp22 = make_block_state(22, {});
      trx_retry.on_block_start(22);
      trx_retry.on_accepted_block(bsp22);
      BOOST_CHECK(trx_10_variant);
      BOOST_CHECK(trx_11_variant);
      BOOST_CHECK_EQUAL(0, trx_retry.size());


      // shutdown
      appbase::app().quit();
      app_thread.join();

   } catch ( ... ) {
      boost::filesystem::remove_all( temp );
      throw;
   }
   boost::filesystem::remove_all( temp );
}


BOOST_AUTO_TEST_SUITE_END()
