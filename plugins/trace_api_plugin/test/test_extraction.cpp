#include <boost/test/unit_test.hpp>

#include <eosio/chain/types.hpp>
#include <eosio/chain/contract_types.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/block_state_legacy.hpp>

#include <eosio/trace_api/test_common.hpp>
#include <eosio/trace_api/chain_extraction.hpp>

#include <fc/bitutil.hpp>

using namespace eosio;
using namespace eosio::trace_api;
using namespace eosio::trace_api::test_common;
using eosio::chain::name;
using eosio::chain::digest_type;

namespace {
   chain::transaction_trace_ptr make_transaction_trace( const chain::transaction_id_type& id, uint32_t block_number,
         uint32_t slot, chain::transaction_receipt_header::status_enum status, std::vector<chain::action_trace>&& actions ) {
      return std::make_shared<chain::transaction_trace>(chain::transaction_trace{
         id,
         block_number,
         chain::block_timestamp_type(slot),
         {},
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

   chain::bytes make_onerror_data( const chain::onerror& one ) {
      fc::datastream<size_t> ps;
      fc::raw::pack(ps, one);
      chain::bytes result(ps.tellp());

      if( result.size() ) {
         fc::datastream<char*>  ds( result.data(), size_t(result.size()) );
         fc::raw::pack(ds, one);
      }
      return result;
   }

   auto make_transfer_action( chain::name from, chain::name to, chain::asset quantity, std::string memo ) {
      return chain::action( std::vector<chain::permission_level> {{from, chain::config::active_name}},
                            "eosio.token"_n, "transfer"_n, make_transfer_data( from, to, quantity, std::move(memo) ) );
   }

   auto make_onerror_action( chain::name creator, chain::uint128_t sender_id ) {
      return chain::action( std::vector<chain::permission_level>{{creator, chain::config::active_name}},
                                chain::onerror{ sender_id, "test ", 4 });
   }

   auto make_packed_trx( std::vector<chain::action> actions ) {
      chain::signed_transaction trx;
      trx.actions = std::move( actions );
      return packed_transaction( std::move(trx) );
   }

    auto make_trx_header( const chain::transaction& trx ) {
        chain::transaction_header th;
        th.expiration = trx.expiration;
        th.ref_block_num = trx.ref_block_num;
        th.ref_block_prefix = trx.ref_block_prefix;
        th.max_net_usage_words = trx.max_net_usage_words;
        th.max_cpu_usage_ms = trx.max_cpu_usage_ms;
        th.delay_sec = trx.delay_sec;
        return th;
    }

   chain::action_trace make_action_trace( uint64_t global_sequence, chain::action act, chain::name receiver ) {
      chain::action_trace result;
      // don't think we need any information other than receiver and global sequence
      result.receipt.emplace(chain::action_receipt{
         receiver,
         digest_type::hash(act),
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

}

struct extraction_test_fixture {
   /**
    * MOCK implementation of the logfile input API
    */
   struct mock_logfile_provider_type {
      mock_logfile_provider_type(extraction_test_fixture& fixture)
      :fixture(fixture)
      {}

      /**
       * append an entry to the data store
       *
       * @param entry : the entry to append
       */
      template <typename BlockTrace>
      void append( const BlockTrace& entry ) {
         fixture.data_log.emplace_back(entry);
      }

      void append_lib( uint32_t lib ) {
         fixture.max_lib = std::max(fixture.max_lib, lib);
      }

      void append_trx_ids(const block_trxs_entry& tt){
         fixture.id_log[tt.block_num] = tt.ids;
      }

      extraction_test_fixture& fixture;
   };

   extraction_test_fixture()
   : extraction_impl(mock_logfile_provider_type(*this), exception_handler{} )
   {
   }

   void signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx ) {
      extraction_impl.signal_applied_transaction(trace, ptrx);
   }

   void signal_accepted_block( const chain::block_state_legacy_ptr& bsp ) {
      extraction_impl.signal_accepted_block(bsp->block, bsp->id);
   }

   // fixture data and methods
   uint32_t max_lib = 0;
   std::vector<data_log_entry> data_log = {};
   std::unordered_map<uint32_t, std::vector<chain::transaction_id_type>> id_log;

   chain_extraction_impl_type<mock_logfile_provider_type> extraction_impl;
};


BOOST_AUTO_TEST_SUITE(block_extraction)

   BOOST_FIXTURE_TEST_CASE(basic_single_transaction_block, extraction_test_fixture)
   {
      auto act1 = make_transfer_action( "alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" );
      auto act2 = make_transfer_action( "alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" );
      auto act3 = make_transfer_action( "alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" );
      auto actt1 = make_action_trace( 0, act1, "eosio.token"_n );
      auto actt2 = make_action_trace( 1, act2, "alice"_n );
      auto actt3 = make_action_trace( 2, act3, "bob"_n );
      auto ptrx1 = make_packed_trx( { act1, act2, act3 } );

      // apply a basic transfer
      signal_applied_transaction(
            make_transaction_trace( ptrx1.id(), 1, 1, chain::transaction_receipt_header::executed,
                  { actt1, actt2, actt3 } ),
            std::make_shared<packed_transaction>(ptrx1) );
      
      // accept the block with one transaction
      auto bsp1 = make_block_state( chain::block_id_type(), 1, 1, "bp.one"_n,
            { chain::packed_transaction(ptrx1) } );
      signal_accepted_block( bsp1 );
      
      const std::vector<action_trace_v1> expected_action_traces {
         {
            {
               0,
               "eosio.token"_n, "eosio.token"_n, "transfer"_n,
               {{"alice"_n, "active"_n}},
               make_transfer_data("alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!")
            },
            {}
         },
         {
            {
               1,
               "alice"_n, "eosio.token"_n, "transfer"_n,
               {{"alice"_n, "active"_n}},
               make_transfer_data("alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!")
            },
            {}
         },
         {
            {
               2,
               "bob"_n, "eosio.token"_n, "transfer"_n,
               {{"alice"_n, "active"_n}},
               make_transfer_data("alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!")
            },
            {}
         }
      };

      const transaction_trace_v3 expected_transaction_trace {
         {
            ptrx1.id(),
            expected_action_traces,
            fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{bsp1->block->transactions[0].status},
            bsp1->block->transactions[0].cpu_usage_us,
            bsp1->block->transactions[0].net_usage_words,
            ptrx1.get_signatures(),
            make_trx_header(ptrx1.get_transaction())
         }
      };

      const block_trace_v2 expected_block_trace {
         bsp1->id,
         1,
         bsp1->prev(),
         chain::block_timestamp_type(1),
         "bp.one"_n,
         bsp1->block->transaction_mroot,
         bsp1->block->action_mroot,
         bsp1->block->schedule_version,
         std::vector<transaction_trace_v3> {
            expected_transaction_trace
         }
      };

      BOOST_REQUIRE_EQUAL(max_lib, 0u);
      BOOST_REQUIRE(data_log.size() == 1u);
      BOOST_REQUIRE(std::holds_alternative<block_trace_v2>(data_log.at(0)));
      BOOST_REQUIRE_EQUAL(std::get<block_trace_v2>(data_log.at(0)), expected_block_trace);
      BOOST_REQUIRE_EQUAL(id_log.at(bsp1->block_num).size(),  bsp1->block->transactions.size());
   }

   BOOST_FIXTURE_TEST_CASE(basic_multi_transaction_block, extraction_test_fixture) {
      auto act1 = make_transfer_action( "alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" );
      auto act2 = make_transfer_action( "bob"_n, "alice"_n, "0.0001 SYS"_t, "Memo!" );
      auto act3 = make_transfer_action( "fred"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" );
      auto actt1 = make_action_trace( 0, act1, "eosio.token"_n );
      auto actt2 = make_action_trace( 1, act2, "bob"_n );
      auto actt3 = make_action_trace( 2, act3, "fred"_n );
      auto ptrx1 = make_packed_trx( { act1 } );
      auto ptrx2 = make_packed_trx( { act2 } );
      auto ptrx3 = make_packed_trx( { act3 } );

      signal_applied_transaction(
            make_transaction_trace( ptrx1.id(), 1, 1, chain::transaction_receipt_header::executed,
                  { actt1 } ),
            std::make_shared<packed_transaction>( ptrx1 ) );
      signal_applied_transaction(
            make_transaction_trace( ptrx2.id(), 1, 1, chain::transaction_receipt_header::executed,
                  { actt2 } ),
            std::make_shared<packed_transaction>( ptrx2 ) );
      signal_applied_transaction(
            make_transaction_trace( ptrx3.id(), 1, 1, chain::transaction_receipt_header::executed,
                  { actt3 } ),
            std::make_shared<packed_transaction>( ptrx3 ) );

      // accept the block with three transaction
      auto bsp1 = make_block_state( chain::block_id_type(), 1, 1, "bp.one"_n,
            { chain::packed_transaction(ptrx1), chain::packed_transaction(ptrx2), chain::packed_transaction(ptrx3) } );
      signal_accepted_block( bsp1 );

      const std::vector<action_trace_v1> expected_action_trace1 {
         {
            {
               0,
               "eosio.token"_n, "eosio.token"_n, "transfer"_n,
               {{"alice"_n, "active"_n}},
               make_transfer_data("alice"_n, "bob"_n, "0.0001 SYS"_t, "Memo!")
            },
            {}
         }
      };

      const std::vector<action_trace_v1> expected_action_trace2 {
         {
            {
               1,
               "bob"_n, "eosio.token"_n, "transfer"_n,
               {{ "bob"_n, "active"_n }},
               make_transfer_data( "bob"_n, "alice"_n, "0.0001 SYS"_t, "Memo!" )
            },
            {}
         }
      };

      const std::vector<action_trace_v1> expected_action_trace3 {
         {
            {
               2,
               "fred"_n, "eosio.token"_n, "transfer"_n,
               {{ "fred"_n, "active"_n }},
               make_transfer_data( "fred"_n, "bob"_n, "0.0001 SYS"_t, "Memo!" )
            },
            {}
         }
      };

      const std::vector<transaction_trace_v3> expected_transaction_traces {
         {
            {
               ptrx1.id(),
               expected_action_trace1,
               fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{bsp1->block->transactions[0].status},
               bsp1->block->transactions[0].cpu_usage_us,
               bsp1->block->transactions[0].net_usage_words,
               ptrx1.get_signatures(),
               make_trx_header(ptrx1.get_transaction())
            }
         },
         {
            {
               ptrx2.id(),
               expected_action_trace2,
               fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{bsp1->block->transactions[1].status},
               bsp1->block->transactions[1].cpu_usage_us,
               bsp1->block->transactions[1].net_usage_words,
               ptrx2.get_signatures(),
               make_trx_header(ptrx2.get_transaction())
            }
         },
         {
            {
               ptrx3.id(),
               expected_action_trace3,
               fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{bsp1->block->transactions[2].status},
               bsp1->block->transactions[2].cpu_usage_us,
               bsp1->block->transactions[2].net_usage_words,
               ptrx3.get_signatures(),
               make_trx_header(ptrx3.get_transaction())
            }
         }
      };

      const block_trace_v2 expected_block_trace {
         bsp1->id,
         1,
         bsp1->prev(),
         chain::block_timestamp_type(1),
         "bp.one"_n,
         bsp1->block->transaction_mroot,
         bsp1->block->action_mroot,
         bsp1->block->schedule_version,
         expected_transaction_traces
      };

      BOOST_REQUIRE_EQUAL(max_lib, 0u);
      BOOST_REQUIRE(data_log.size() == 1u);
      BOOST_REQUIRE(std::holds_alternative<block_trace_v2>(data_log.at(0)));
      BOOST_REQUIRE_EQUAL(std::get<block_trace_v2>(data_log.at(0)), expected_block_trace);
   }

   BOOST_FIXTURE_TEST_CASE(onerror_transaction_block, extraction_test_fixture)
   {
      auto onerror_act = make_onerror_action( "alice"_n, 1 );
      auto actt1 = make_action_trace( 0, onerror_act, "eosio.token"_n );
      auto ptrx1 = make_packed_trx( { onerror_act } );

      auto act2 = make_transfer_action( "bob"_n, "alice"_n, "0.0001 SYS"_t, "Memo!" );
      auto actt2 = make_action_trace( 1, act2, "bob"_n );
      auto transfer_trx = make_packed_trx( { act2 } );

      auto onerror_trace = make_transaction_trace( ptrx1.id(), 1, 1, chain::transaction_receipt_header::executed,
                              { actt1 } );
      auto transfer_trace = make_transaction_trace( transfer_trx.id(), 1, 1, chain::transaction_receipt_header::soft_fail,
                                                   { actt2 } );
      onerror_trace->failed_dtrx_trace = transfer_trace;

      signal_applied_transaction( onerror_trace, std::make_shared<packed_transaction>( transfer_trx ) );

      auto bsp1 = make_block_state( chain::block_id_type(), 1, 1, "bp.one"_n,
            { chain::packed_transaction(transfer_trx) } );
      signal_accepted_block( bsp1 );

      const std::vector<action_trace_v1> expected_action_trace {
         {
            {
               0,
               "eosio.token"_n, "eosio"_n, "onerror"_n,
               {{ "alice"_n, "active"_n }},
               make_onerror_data( chain::onerror{ 1, "test ", 4 } )
            },
            {}
         }
      };

      const std::vector<transaction_trace_v3> expected_transaction_traces {
         {
            {
               transfer_trx.id(), // transfer_trx.id() because that is the trx id known to the user
               expected_action_trace,
               fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{bsp1->block->transactions[0].status},
               bsp1->block->transactions[0].cpu_usage_us,
               bsp1->block->transactions[0].net_usage_words,
               transfer_trx.get_signatures(),
               make_trx_header(transfer_trx.get_transaction())
            }
         }
      };

      const block_trace_v2 expected_block_trace {
         bsp1->id,
         1,
         bsp1->prev(),
         chain::block_timestamp_type(1),
         "bp.one"_n,
         bsp1->block->transaction_mroot,
         bsp1->block->action_mroot,
         bsp1->block->schedule_version,
         expected_transaction_traces
      };

      BOOST_REQUIRE_EQUAL(max_lib, 0u);
      BOOST_REQUIRE(data_log.size() == 1u);
      BOOST_REQUIRE(std::holds_alternative<block_trace_v2>(data_log.at(0)));
      BOOST_REQUIRE_EQUAL(std::get<block_trace_v2>(data_log.at(0)), expected_block_trace);
   }

BOOST_AUTO_TEST_SUITE_END()
