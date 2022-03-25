#define BOOST_TEST_MODULE signals_processor
#include <boost/test/included/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/signals_processor.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/asset.hpp>
#include <fc/bitutil.hpp>

using namespace eosio;
using namespace eosio::chain;

struct signals_backend {
   signals_backend() {
      at = [this](const trx_deque& td) {
         trxs = td;
      };
      ab = [this](const chain::block_state_ptr& bs) {
         accepted_block = bs;
      };
      ib = [this](const chain::block_state_ptr& bs) {
         irr_block = bs;
      };
      bs = [this](uint32_t bn) {
         block_num = bn;
      };
   }
   using trx_deque = chain::signals_processor::trx_deque;
   trx_deque trxs;
   chain::block_state_ptr accepted_block;
   chain::block_state_ptr irr_block;
   std::optional<uint32_t> block_num;

   chain::signals_processor::applied_transaction_func at;
   chain::signals_processor::accepted_block_func ab;
   chain::signals_processor::irreversible_block_func ib;
   chain::signals_processor::block_start_func bs;
};

namespace {
   chain::transaction_trace_ptr make_transaction_trace( const chain::transaction_id_type& id, uint32_t block_number,
         uint32_t slot, std::optional<chain::block_id_type> block_id, chain::transaction_receipt_header::status_enum status, std::vector<chain::action_trace>&& actions ) {
      return std::make_shared<chain::transaction_trace>(chain::transaction_trace{
         id,
         block_number,
         chain::block_timestamp_type(slot),
         block_id,
         chain::transaction_receipt_header{status},
         fc::microseconds(0),
         0,
         false,
         std::move(actions),
         {},
         {},
         {},
         {},
         {}
      });
   }

   chain::bytes make_transfer_data( chain::name from, chain::name to, chain::asset quantity, std::string&& memo) {
      fc::datastream<size_t> ps;
      fc::raw::pack(ps, from, to, quantity, memo);
      chain::bytes result( ps.tellp());

      if( result.size()) {
         fc::datastream<char *> ds( result.data(), size_t( result.size()));
         fc::raw::pack(ds, from, to, quantity, memo);
      }
      return result;
   }

   auto make_transfer_action( chain::name from, chain::name to, chain::asset quantity, std::string memo ) {
      return chain::action( std::vector<chain::permission_level> {{from, chain::config::active_name}},
                            "eosio.token"_n, "transfer"_n, make_transfer_data( from, to, quantity, std::move(memo) ) );
   }

   chain::action_trace make_action_trace( uint64_t global_sequence, chain::action act, chain::name receiver ) {
      chain::action_trace result;
      // don't think we need any information other than receiver and global sequence
      result.receipt.emplace(chain::action_receipt{
         receiver,
         chain::digest_type::hash(act),
         global_sequence,
         0,
         {},
         0,
         0
      });
      result.receiver = receiver;
      result.act = std::move(act);
      return result;
   }

   auto make_packed_trx( std::vector<chain::action> actions ) {
      chain::signed_transaction trx;
      trx.actions = std::move( actions );
      return packed_transaction( trx );
   }

   chain::asset operator"" _t(const char* input, std::size_t) {
      return chain::asset::from_string(input);
   }


   auto get_private_key( chain::name keyname, std::string role = "owner" ) {
      auto secret = fc::sha256::hash( keyname.to_string() + role );
      return chain::private_key_type::regenerate<fc::ecc::private_key_shim>( secret );
   }

   auto get_public_key( chain::name keyname, std::string role = "owner" ) {
      return get_private_key( keyname, role ).get_public_key();
   }

   auto make_block_state( chain::block_id_type previous, uint32_t height, uint32_t slot, chain::name producer,
                          std::vector<chain::packed_transaction> trxs ) {
      chain::signed_block_ptr block = std::make_shared<chain::signed_block>();
      for( auto& trx : trxs ) {
         block->transactions.emplace_back( trx );
      }
      block->producer = producer;
      block->timestamp = chain::block_timestamp_type(slot);
      // make sure previous contains correct block # so block_header::block_num() returns correct value
      if( previous == chain::block_id_type() ) {
         previous._hash[0] &= 0xffffffff00000000;
         previous._hash[0] += fc::endian_reverse_u32(height - 1);
      }
      block->previous = previous;

      auto priv_key = get_private_key( block->producer, "active" );
      auto pub_key = get_public_key( block->producer, "active" );

      auto prev = std::make_shared<chain::block_state>();
      auto header_bmroot = chain::digest_type::hash( std::make_pair( block->digest(), prev->blockroot_merkle.get_root()));
      auto sig_digest = chain::digest_type::hash( std::make_pair( header_bmroot, prev->pending_schedule.schedule_hash ));
      block->producer_signature = priv_key.sign( sig_digest );

      std::vector<chain::private_key_type> signing_keys;
      signing_keys.emplace_back( std::move( priv_key ));
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
      chain::producer_authority_schedule schedule = {0, {chain::producer_authority{block->producer,
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
      bsp->block_num = height;

      return bsp;
   }
}

BOOST_AUTO_TEST_SUITE(signals_processor_tests)

BOOST_AUTO_TEST_CASE(signals_test) { try {

   chain::signals_processor sig_proc;

   signals_backend be1;
   sig_proc.register_callbacks(be1.at, be1.ab, be1.ib, be1.bs);
   signals_backend be2;
   sig_proc.register_callbacks(be2.at, be2.ab, be2.ib, be2.bs);

   auto act1 = make_transfer_action( "alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" );
   auto act2 = make_transfer_action( "alice"_n, "jen"_n, "0.0002 SYS"_t, "Memo!" );
   auto actt1 = make_action_trace( 0, act1, "eosio.token"_n );
   auto actt2 = make_action_trace( 1, act2, "alice"_n );
   auto ptrx1 = make_packed_trx( { act1, act2 } );
   auto tt1 = make_transaction_trace( ptrx1.id(), 1, 1, {}, chain::transaction_receipt_header::executed, { actt1, actt2 } );
   sig_proc.signal_block_start(50);
   BOOST_CHECK_EQUAL(be1.trxs.size(), 0);
   BOOST_CHECK(!be1.accepted_block);
   BOOST_CHECK(!be1.irr_block);
   BOOST_CHECK(be1.block_num);
   BOOST_CHECK_EQUAL(*be1.block_num, 50);

   BOOST_CHECK_EQUAL(be2.trxs.size(), 0);
   BOOST_CHECK(!be2.accepted_block);
   BOOST_CHECK(!be2.irr_block);
   BOOST_CHECK(be2.block_num);
   BOOST_CHECK_EQUAL(*be2.block_num, 50);

   sig_proc.signal_applied_transaction( tt1, ptrx1.get_signed_transaction() );
   sig_proc.signal_applied_transaction( tt1, ptrx1.get_signed_transaction() );
   sig_proc.signal_applied_transaction( tt1, ptrx1.get_signed_transaction() );
   sig_proc.signal_applied_transaction( tt1, ptrx1.get_signed_transaction() );
   BOOST_CHECK_EQUAL(be1.trxs.size(), 0);
   BOOST_CHECK(!be1.accepted_block);
   BOOST_CHECK(!be1.irr_block);
   BOOST_CHECK(be1.block_num);
   BOOST_CHECK_EQUAL(*be1.block_num, 50);

   BOOST_CHECK_EQUAL(be2.trxs.size(), 0);
   BOOST_CHECK(!be2.accepted_block);
   BOOST_CHECK(!be2.irr_block);
   BOOST_CHECK(be2.block_num);
   BOOST_CHECK_EQUAL(*be2.block_num, 50);

   auto bsp1 = make_block_state( chain::block_id_type(), 1, 1, "bp.one"_n, { chain::packed_transaction(ptrx1) } );
   sig_proc.signal_accepted_block(bsp1);
   BOOST_CHECK_EQUAL(be1.trxs.size(), 4);
   BOOST_CHECK(be1.accepted_block);
   BOOST_CHECK(!be1.irr_block);
   BOOST_CHECK(be1.block_num);
   BOOST_CHECK_EQUAL(*be1.block_num, 50);

   BOOST_CHECK_EQUAL(be2.trxs.size(), 4);
   BOOST_CHECK(be2.accepted_block);
   BOOST_CHECK(!be2.irr_block);
   BOOST_CHECK(be2.block_num);
   BOOST_CHECK_EQUAL(*be2.block_num, 50);
   be1.accepted_block.reset();
   be2.accepted_block.reset();

   sig_proc.signal_irreversible_block(bsp1);
   BOOST_CHECK_EQUAL(be1.trxs.size(), 4);
   BOOST_CHECK(!be1.accepted_block);
   BOOST_CHECK(be1.irr_block);
   BOOST_CHECK(be1.block_num);
   BOOST_CHECK_EQUAL(*be1.block_num, 50);

   BOOST_CHECK_EQUAL(be2.trxs.size(), 4);
   BOOST_CHECK(!be2.accepted_block);
   BOOST_CHECK(be2.irr_block);
   BOOST_CHECK(be2.block_num);
   BOOST_CHECK_EQUAL(*be2.block_num, 50);
   be1.irr_block.reset();
   be2.irr_block.reset();

   sig_proc.signal_block_start(51);
   BOOST_CHECK_EQUAL(be1.trxs.size(), 4);
   BOOST_CHECK(!be1.accepted_block);
   BOOST_CHECK(!be1.irr_block);
   BOOST_CHECK(be1.block_num);
   BOOST_CHECK_EQUAL(*be1.block_num, 51);

   BOOST_CHECK_EQUAL(be2.trxs.size(), 4);
   BOOST_CHECK(!be2.accepted_block);
   BOOST_CHECK(!be2.irr_block);
   BOOST_CHECK(be2.block_num);
   BOOST_CHECK_EQUAL(*be2.block_num, 51);

   sig_proc.signal_applied_transaction( tt1, ptrx1.get_signed_transaction() );
   sig_proc.signal_applied_transaction( tt1, ptrx1.get_signed_transaction() );
   sig_proc.signal_applied_transaction( tt1, ptrx1.get_signed_transaction() );
   BOOST_CHECK_EQUAL(be1.trxs.size(), 4);
   BOOST_CHECK(!be1.accepted_block);
   BOOST_CHECK(!be1.irr_block);
   BOOST_CHECK(be1.block_num);
   BOOST_CHECK_EQUAL(*be1.block_num, 51);

   BOOST_CHECK_EQUAL(be2.trxs.size(), 4);
   BOOST_CHECK(!be2.accepted_block);
   BOOST_CHECK(!be2.irr_block);
   BOOST_CHECK(be2.block_num);
   BOOST_CHECK_EQUAL(*be2.block_num, 51);
   be1.trxs.clear();
   be2.trxs.clear();

   sig_proc.signal_block_start(52);
   BOOST_CHECK_EQUAL(be1.trxs.size(), 3);
   BOOST_CHECK(!be1.accepted_block);
   BOOST_CHECK(!be1.irr_block);
   BOOST_CHECK(be1.block_num);
   BOOST_CHECK_EQUAL(*be1.block_num, 52);

   BOOST_CHECK_EQUAL(be2.trxs.size(), 3);
   BOOST_CHECK(!be2.accepted_block);
   BOOST_CHECK(!be2.irr_block);
   BOOST_CHECK(be2.block_num);
   BOOST_CHECK_EQUAL(*be2.block_num, 52);

   sig_proc.signal_accepted_block(bsp1);
   BOOST_CHECK_EQUAL(be1.trxs.size(), 3);
   BOOST_CHECK(be1.accepted_block);
   BOOST_CHECK(!be1.irr_block);
   BOOST_CHECK(be1.block_num);
   BOOST_CHECK_EQUAL(*be1.block_num, 52);

   BOOST_CHECK_EQUAL(be2.trxs.size(), 3);
   BOOST_CHECK(be2.accepted_block);
   BOOST_CHECK(!be2.irr_block);
   BOOST_CHECK(be2.block_num);
   BOOST_CHECK_EQUAL(*be2.block_num, 52);

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

