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

auto make_unique_trx_meta_data( const chain_id_type& chain_id, const fc::microseconds& expiration, uint64_t id ) {
   return transaction_metadata::create_no_recover_keys( make_unique_trx(chain_id, expiration, id),
                                                        transaction_metadata::trx_type::input );
}

uint64_t get_id( const transaction& trx ) {
   testit t = trx.actions.at(0).data_as<testit>();
   return t.id;
}

uint64_t get_id( const packed_transaction_ptr& ptr ) {
   return get_id( ptr->get_transaction() );
}

uint64_t get_id( const transaction_metadata_ptr& meta_ptr ) {
   return get_id( meta_ptr->packed_trx() );
}


auto make_block_state( uint32_t block_num, std::vector<chain::packed_transaction> trxs ) {
   name producer = "kevinh"_n;
   chain::signed_block_ptr block = std::make_shared<chain::signed_block>();
   for( auto& trx : trxs ) {
      block->transactions.emplace_back( trx );
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
         std::vector<chain::transaction_metadata_ptr>(),
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
      trx_retry_db trx_retry( *chain, max_mem_usage_size, retry_interval, max_expiration_time );

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
      // signal block nothing should be expired as now has not changed
      auto bsp1 = make_block_state(1, {});
      trx_retry.on_block_start(1);
      trx_retry.on_accepted_block(bsp1);
      BOOST_CHECK(!trx_1_expired);
      BOOST_CHECK(!trx_2_expired);
      // increase time by 3 seconds to expire first
      pnow += boost::posix_time::seconds(3);
      fc::mock_time_traits::set_now(pnow);
      // signal block, first transaction should expire
      auto bsp2 = make_block_state(2, {});
      trx_retry.on_block_start(2);
      trx_retry.on_accepted_block(bsp2);
      BOOST_CHECK(trx_1_expired);
      BOOST_CHECK(!trx_2_expired);
      // increase time by 2 seconds to expire second
      pnow += boost::posix_time::seconds(2);
      fc::mock_time_traits::set_now(pnow);
      // signal block, second transaction should expire
      auto bsp3 = make_block_state(3, {});
      trx_retry.on_block_start(3);
      trx_retry.on_accepted_block(bsp3);
      BOOST_CHECK(trx_1_expired);
      BOOST_CHECK(trx_2_expired);

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



      appbase::app().quit();
      app_thread.join();

   } catch ( ... ) {
      boost::filesystem::remove_all( temp );
      throw;
   }
   boost::filesystem::remove_all( temp );
}


BOOST_AUTO_TEST_SUITE_END()
