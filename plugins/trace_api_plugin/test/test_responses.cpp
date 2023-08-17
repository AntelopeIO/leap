#include <boost/test/unit_test.hpp>

#include <fc/variant_object.hpp>

#include <eosio/trace_api/request_handler.hpp>
#include <eosio/trace_api/test_common.hpp>

using namespace eosio;
using namespace eosio::trace_api;
using namespace eosio::trace_api::test_common;

struct response_test_fixture {
   /**
    * MOCK implementation of the logfile input API
    */
   struct mock_logfile_provider {
      mock_logfile_provider(response_test_fixture& fixture)
      :fixture(fixture)
      {}

      /**
       * Read the trace for a given block
       * @param block_height : the height of the data being read
       * @return empty optional if the data cannot be read OTHERWISE
       *         optional containing a 2-tuple of the block_trace and a flag indicating irreversibility
       * @throws bad_data_exception : if the data is corrupt in some way
       */
      get_block_t get_block(uint32_t height) {
         return fixture.mock_get_block(height);
      }
      response_test_fixture& fixture;
   };

   constexpr static auto default_mock_data_handler_v0 = [](const action_trace_v0& a) ->std::tuple<fc::variant, std::optional<fc::variant>> {
      return {fc::mutable_variant_object()("hex" , fc::to_hex(a.data.data(), a.data.size())),{}};
   };

   constexpr static auto default_mock_data_handler_v1 = [](const action_trace_v1& a) -> std::tuple<fc::variant, std::optional<fc::variant>>{
      return {fc::mutable_variant_object()("hex" , fc::to_hex(a.data.data(), a.data.size())), {fc::mutable_variant_object()("hex" , fc::to_hex(a.return_value.data(), a.return_value.size()))}};
   };

   struct mock_data_handler_provider {
      mock_data_handler_provider(response_test_fixture& fixture)
      :fixture(fixture)
      {}

      template<typename ActionTrace>
      std::tuple<fc::variant, std::optional<fc::variant>> serialize_to_variant(const ActionTrace & action) {
         if constexpr(std::is_same_v<ActionTrace, action_trace_v0>){
            return fixture.mock_data_handler_v0(action);
         }
         else if constexpr(std::is_same_v<ActionTrace, action_trace_v1>){
            return fixture.mock_data_handler_v1(action);
         }
      }

      response_test_fixture& fixture;
   };

   using response_impl_type = request_handler<mock_logfile_provider, mock_data_handler_provider>;
   /**
    * TODO: initialize extraction implementation here with `mock_logfile_provider` as template param
    */
   response_test_fixture()
   : response_impl(mock_logfile_provider(*this), mock_data_handler_provider(*this),
                   [](const std::string& msg ){ fc_dlog( fc::logger::get(DEFAULT_LOGGER), msg );})
   {

   }

   fc::variant get_block_trace( uint32_t block_height ) {
      return response_impl.get_block_trace( block_height );
   }

   // fixture data and methods
   std::function<get_block_t(uint32_t)> mock_get_block;
   std::function<std::tuple<fc::variant, std::optional<fc::variant>>(const action_trace_v0&)> mock_data_handler_v0 = default_mock_data_handler_v0;
   std::function<std::tuple<fc::variant, std::optional<fc::variant>>(const action_trace_v1&)> mock_data_handler_v1 = default_mock_data_handler_v1;

   response_impl_type response_impl;

};

BOOST_AUTO_TEST_SUITE(trace_responses)
   BOOST_FIXTURE_TEST_CASE(basic_empty_block_response, response_test_fixture)
   {
      auto block_trace = block_trace_v1 {
         {
            "b000000000000000000000000000000000000000000000000000000000000001"_h,
            1,
            "0000000000000000000000000000000000000000000000000000000000000000"_h,
            chain::block_timestamp_type(0),
            "bp.one"_n
         },
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         0,
         {}
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants() )
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry{block_trace}, false);
      };

      fc::variant actual_response = get_block_trace( 1 );

      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(basic_block_response, response_test_fixture)
   {
      auto action_trace = action_trace_v0 {
         0,
         "receiver"_n, "contract"_n, "action"_n,
         {{ "alice"_n, "active"_n }},
         { 0x00, 0x01, 0x02, 0x03 }
      };

      auto transaction_trace = transaction_trace_v1 { {
         "0000000000000000000000000000000000000000000000000000000000000001"_h,
         {
            action_trace
         }},
         fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{chain::transaction_receipt_header::status_enum::executed},
         10,
         5,
         std::vector<chain::signature_type>{ chain::signature_type() },
         { chain::time_point_sec(), 1, 0, 100, 50, 0 }
      };

      auto block_trace = block_trace_v1 {
         {
            "b000000000000000000000000000000000000000000000000000000000000001"_h,
            1,
            "0000000000000000000000000000000000000000000000000000000000000000"_h,
            chain::block_timestamp_type(0),
            "bp.one"_n
         },
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         0,
         {
            transaction_trace
         }
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants({
            fc::mutable_variant_object()
               ("id", "0000000000000000000000000000000000000000000000000000000000000001")
               ("actions", fc::variants({
                  fc::mutable_variant_object()
                     ("global_sequence", 0)
                     ("receiver", "receiver")
                     ("account", "contract")
                     ("action", "action")
                     ("authorization", fc::variants({
                        fc::mutable_variant_object()
                           ("account", "alice")
                           ("permission", "active")
                     }))
                     ("data", "00010203")
                     ("params", fc::mutable_variant_object()
                           ("hex", "00010203"))
               }))
               ("status", "executed")
               ("cpu_usage_us", 10)
               ("net_usage_words", 5)
               ("signatures", fc::variants({"SIG_K1_111111111111111111111111111111111111111111111111111111111111111116uk5ne"}))
               ("transaction_header", fc::mutable_variant_object()
                  ("expiration", "1970-01-01T00:00:00")
                  ("ref_block_num", 1)
                  ("ref_block_prefix", 0)
                  ("max_net_usage_words", 100)
                  ("max_cpu_usage_ms", 50)
                  ("delay_sec", 0)
               )
         }))
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), false);
      };

      fc::variant actual_response = get_block_trace( 1 );

      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(basic_block_response_no_params, response_test_fixture)
   {
      auto block_trace = block_trace_v1 {
         {
            "b000000000000000000000000000000000000000000000000000000000000001"_h,
            1,
            "0000000000000000000000000000000000000000000000000000000000000000"_h,
            chain::block_timestamp_type(0),
            "bp.one"_n
         },
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         0,
         {
            {
               {
                  "0000000000000000000000000000000000000000000000000000000000000001"_h,
                  {
                     {
                        0,
                        "receiver"_n, "contract"_n, "action"_n,
                        {{ "alice"_n, "active"_n }},
                        { 0x00, 0x01, 0x02, 0x03 }
                     }
                  }
               },
               fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{chain::transaction_receipt_header::status_enum::executed},
               10,
               5,
               std::vector<chain::signature_type>{ chain::signature_type() },
               { chain::time_point_sec(), 1, 0, 100, 50, 0 }
            }
         }
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants({
            fc::mutable_variant_object()
               ("id", "0000000000000000000000000000000000000000000000000000000000000001")
               ("actions", fc::variants({
                  fc::mutable_variant_object()
                     ("global_sequence", 0)
                     ("receiver", "receiver")
                     ("account", "contract")
                     ("action", "action")
                     ("authorization", fc::variants({
                        fc::mutable_variant_object()
                           ("account", "alice")
                           ("permission", "active")
                     }))
                     ("data", "00010203")
               }))
               ("status", "executed")
               ("cpu_usage_us", 10)
               ("net_usage_words", 5)
               ("signatures", fc::variants({"SIG_K1_111111111111111111111111111111111111111111111111111111111111111116uk5ne"}))
               ("transaction_header", fc::mutable_variant_object()
                  ("expiration", "1970-01-01T00:00:00")
                  ("ref_block_num", 1)
                  ("ref_block_prefix", 0)
                  ("max_net_usage_words", 100)
                  ("max_cpu_usage_ms", 50)
                  ("delay_sec", 0)
               )
         }))
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), false);
      };

      // simulate an inability to parse the parameters
      mock_data_handler_v0 = [](const action_trace_v0&) -> std::tuple<fc::variant, std::optional<fc::variant>> {
         return {};
      };

      fc::variant actual_response = get_block_trace( 1 );

      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(basic_block_response_unsorted, response_test_fixture)
   {
      auto block_trace = block_trace_v1 {
         {
            "b000000000000000000000000000000000000000000000000000000000000001"_h,
            1,
            "0000000000000000000000000000000000000000000000000000000000000000"_h,
            chain::block_timestamp_type(0),
            "bp.one"_n
         },
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         0,
         {
            {
               {
                  "0000000000000000000000000000000000000000000000000000000000000001"_h,
                  {
                     {
                        1,
                        "receiver"_n, "contract"_n, "action"_n,
                        {{ "alice"_n, "active"_n }},
                        { 0x01, 0x01, 0x01, 0x01 }
                     },
                     {
                        0,
                        "receiver"_n, "contract"_n, "action"_n,
                        {{ "alice"_n, "active"_n }},
                        { 0x00, 0x00, 0x00, 0x00 }
                     },
                     {
                        2,
                        "receiver"_n, "contract"_n, "action"_n,
                        {{ "alice"_n, "active"_n }},
                        { 0x02, 0x02, 0x02, 0x02 }
                     }
                  }
               },
               fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{chain::transaction_receipt_header::status_enum::executed},
               10,
               5,
               { chain::signature_type() },
               { chain::time_point_sec(), 1, 0, 100, 50, 0 }
            }
         }
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants({
            fc::mutable_variant_object()
               ("id", "0000000000000000000000000000000000000000000000000000000000000001")
               ("actions", fc::variants({
                  fc::mutable_variant_object()
                     ("global_sequence", 0)
                     ("receiver", "receiver")
                     ("account", "contract")
                     ("action", "action")
                     ("authorization", fc::variants({
                        fc::mutable_variant_object()
                           ("account", "alice")
                           ("permission", "active")
                     }))
                     ("data", "00000000")
                  ,
                  fc::mutable_variant_object()
                     ("global_sequence", 1)
                     ("receiver", "receiver")
                     ("account", "contract")
                     ("action", "action")
                     ("authorization", fc::variants({
                        fc::mutable_variant_object()
                           ("account", "alice")
                           ("permission", "active")
                     }))
                     ("data", "01010101")
                  ,
                  fc::mutable_variant_object()
                     ("global_sequence", 2)
                     ("receiver", "receiver")
                     ("account", "contract")
                     ("action", "action")
                     ("authorization", fc::variants({
                        fc::mutable_variant_object()
                           ("account", "alice")
                           ("permission", "active")
                     }))
                     ("data", "02020202")
               }))
               ("status", "executed")
               ("cpu_usage_us", 10)
               ("net_usage_words", 5)
               ("signatures", fc::variants({"SIG_K1_111111111111111111111111111111111111111111111111111111111111111116uk5ne"}))
               ("transaction_header", fc::mutable_variant_object()
                  ("expiration", "1970-01-01T00:00:00")
                  ("ref_block_num", 1)
                  ("ref_block_prefix", 0)
                  ("max_net_usage_words", 100)
                  ("max_cpu_usage_ms", 50)
                  ("delay_sec", 0)
               )
         }))
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), false);
      };

      // simulate an inability to parse the parameters
      mock_data_handler_v0 = [](const action_trace_v0&) -> std::tuple<fc::variant, std::optional<fc::variant>> {
         return {};
      };

      fc::variant actual_response = get_block_trace( 1 );

      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(lib_response, response_test_fixture)
   {
      auto block_trace = block_trace_v1{
         {
            "b000000000000000000000000000000000000000000000000000000000000001"_h,
            1,
            "0000000000000000000000000000000000000000000000000000000000000000"_h,
            chain::block_timestamp_type(0),
            "bp.one"_n
         },
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         0,
         {}
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "irreversible")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants() )
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), true);
      };

      fc::variant response = get_block_trace( 1 );
      BOOST_TEST(to_kv(expected_response) == to_kv(response), boost::test_tools::per_element());

   }

   BOOST_FIXTURE_TEST_CASE(corrupt_block_data, response_test_fixture)
   {
      mock_get_block = []( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         throw bad_data_exception("mock exception");
      };

      BOOST_REQUIRE_THROW(get_block_trace( 1 ), bad_data_exception);
   }

   BOOST_FIXTURE_TEST_CASE(missing_block_data, response_test_fixture)
   {
      mock_get_block = []( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return {};
      };

      fc::variant null_response = get_block_trace( 1 );

      BOOST_TEST(null_response.is_null());
   }

   BOOST_FIXTURE_TEST_CASE(old_version_block_response, response_test_fixture)
   {
      auto block_trace = block_trace_v0 {
         "b000000000000000000000000000000000000000000000000000000000000001"_h,
         1,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         chain::block_timestamp_type(0),
         "bp.one"_n,
         {
            {
               "0000000000000000000000000000000000000000000000000000000000000001"_h,
               {
                  {
                     0,
                     "receiver"_n, "contract"_n, "action"_n,
                     {{ "alice"_n, "active"_n }},
                     { 0x00, 0x01, 0x02, 0x03 }
                  }
               }
            }
         }
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transactions", fc::variants({
            fc::mutable_variant_object()
               ("id", "0000000000000000000000000000000000000000000000000000000000000001")
               ("actions", fc::variants({
                  fc::mutable_variant_object()
                     ("global_sequence", 0)
                     ("receiver", "receiver")
                     ("account", "contract")
                     ("action", "action")
                     ("authorization", fc::variants({
                        fc::mutable_variant_object()
                           ("account", "alice")
                           ("permission", "active")
                     }))
                     ("data", "00010203")
                     ("params", fc::mutable_variant_object()
                        ("hex", "00010203")
                     )
               }))
         }))
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), false);
      };

      fc::variant actual_response = get_block_trace( 1 );

      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(basic_empty_block_response_v2, response_test_fixture)
   {
      auto block_trace = block_trace_v2 {
         "b000000000000000000000000000000000000000000000000000000000000001"_h,  // block id
         1,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,  // previous id
         chain::block_timestamp_type(0),
         "bp.one"_n,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,  // transaction mroot
         "0000000000000000000000000000000000000000000000000000000000000000"_h,  // action mroot
         0,  // schedule version
         {} // transactions
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants() )
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry{block_trace}, false);
      };

      fc::variant actual_response = get_block_trace( 1 );

      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(basic_block_response_v2, response_test_fixture)
   {
      auto action_trace = action_trace_v1 {
         {
            0,
            "receiver"_n, "contract"_n, "action"_n,
            {{ "alice"_n, "active"_n }},
            { 0x00, 0x01, 0x02, 0x03 }
         },
         { 0x04, 0x05, 0x06, 0x07 }
      };

      auto transaction_trace = transaction_trace_v2 {//trn
         "0000000000000000000000000000000000000000000000000000000000000001"_h,
         std::vector<action_trace_v1> {
            action_trace
         },
         fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{chain::transaction_receipt_header::status_enum::executed},
         10,  // cpu_usage_us
         5,    // net_usage_words
         std::vector<chain::signature_type>{ chain::signature_type() },  // signatures
         { chain::time_point_sec(), 1, 0, 100, 50, 0 }  //   trx_header
      };// trn end

      auto block_trace = block_trace_v2 {
         "b000000000000000000000000000000000000000000000000000000000000001"_h,  // block id
         1,
         "0000000000000000000000000000000000000000000000000000000000000000"_h, // previous id
         chain::block_timestamp_type(0),
         "bp.one"_n,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,   // transaction mroot
         "0000000000000000000000000000000000000000000000000000000000000000"_h,   //action mroot
         0,  // schedule version
         std::vector<transaction_trace_v2> {
            transaction_trace
         }
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants({
            fc::mutable_variant_object()
               ("id", "0000000000000000000000000000000000000000000000000000000000000001")
               ("actions", fc::variants({
                  fc::mutable_variant_object()
                     ("global_sequence", 0)
                     ("receiver", "receiver")
                     ("account", "contract")
                     ("action", "action")
                     ("authorization", fc::variants({
                        fc::mutable_variant_object()
                           ("account", "alice")
                           ("permission", "active")
                        }))
                     ("data", "00010203")
                     ("return_value", "04050607")
                     ("params", fc::mutable_variant_object()
                        ("hex", "00010203"))
                        ("return_data", fc::mutable_variant_object()
                           ("hex", "04050607"))
                           }))
                        ("status", "executed")
                        ("cpu_usage_us", 10)
                        ("net_usage_words", 5)
                        ("signatures", fc::variants({"SIG_K1_111111111111111111111111111111111111111111111111111111111111111116uk5ne"}))
                        ("transaction_header", fc::mutable_variant_object()
                           ("expiration", "1970-01-01T00:00:00")
                           ("ref_block_num", 1)
                           ("ref_block_prefix", 0)
                           ("max_net_usage_words", 100)
                           ("max_cpu_usage_ms", 50)
                           ("delay_sec", 0)
                        )
         }))
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), false);
      };

      fc::variant actual_response = get_block_trace( 1 );
      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(basic_block_response_no_params_v2, response_test_fixture)
   {
      auto action_trace = action_trace_v1 {
         {
            0,
            "receiver"_n, "contract"_n, "action"_n,
            {{ "alice"_n, "active"_n }},
            { 0x00, 0x01, 0x02, 0x03 }
         },
         { 0x04, 0x05, 0x06, 0x07 }
      };

      auto transaction_trace = transaction_trace_v2 {
         "0000000000000000000000000000000000000000000000000000000000000001"_h,
         std::vector<action_trace_v1> {
            action_trace
         },
         fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{chain::transaction_receipt_header::status_enum::executed},
         10,
         5,
         std::vector<chain::signature_type>{ chain::signature_type() },
         { chain::time_point_sec(), 1, 0, 100, 50, 0 }
      };

      auto block_trace = block_trace_v2 {
         "b000000000000000000000000000000000000000000000000000000000000001"_h,
         1,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         chain::block_timestamp_type(0),
         "bp.one"_n,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         0,
         std::vector<transaction_trace_v2> {
            transaction_trace
         }
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants({
            fc::mutable_variant_object()
               ("id", "0000000000000000000000000000000000000000000000000000000000000001")
               ("actions", fc::variants({
                  fc::mutable_variant_object()
                     ("global_sequence", 0)
                     ("receiver", "receiver")
                     ("account", "contract")
                     ("action", "action")
                     ("authorization", fc::variants({
                        fc::mutable_variant_object()
                        ("account", "alice")
                        ("permission", "active")
                     }))
                     ("data", "00010203")
                     ("return_value", "04050607")
                }))
                ("status", "executed")
                ("cpu_usage_us", 10)
                ("net_usage_words", 5)
                ("signatures", fc::variants({"SIG_K1_111111111111111111111111111111111111111111111111111111111111111116uk5ne"}))
                ("transaction_header", fc::mutable_variant_object()
                   ("expiration", "1970-01-01T00:00:00")
                   ("ref_block_num", 1)
                   ("ref_block_prefix", 0)
                   ("max_net_usage_words", 100)
                   ("max_cpu_usage_ms", 50)
                   ("delay_sec", 0))
            })
      );

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), false);
      };

      // simulate an inability to parse the parameters and return_data
      mock_data_handler_v1 = [](const action_trace_v1&) -> std::tuple<fc::variant, std::optional<fc::variant>> {
         return {};
      };

      fc::variant actual_response = get_block_trace( 1 );

      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(basic_block_response_unsorted_v2, response_test_fixture)
   {
      std::vector<action_trace_v1> actions = {
         {
            {
               1,
               "receiver"_n, "contract"_n, "action"_n,
               {{ "alice"_n, "active"_n }},
               { 0x01, 0x01, 0x01, 0x01 },
            },
            { 0x05, 0x05, 0x05, 0x05 }
         },
         {
            {
               0,
               "receiver"_n, "contract"_n, "action"_n,
               {{ "alice"_n, "active"_n }},
               { 0x00, 0x00, 0x00, 0x00 }
            },
            { 0x04, 0x04, 0x04, 0x04}
         },
         {
            {
               2,
               "receiver"_n, "contract"_n, "action"_n,
               {{ "alice"_n, "active"_n }},
               { 0x02, 0x02, 0x02, 0x02 }
            },
            { 0x06, 0x06, 0x06, 0x06 }
         }
      };

      auto transaction_trace = transaction_trace_v2 {
         "0000000000000000000000000000000000000000000000000000000000000001"_h,
         actions,
         fc::enum_type<uint8_t, chain::transaction_receipt_header::status_enum>{chain::transaction_receipt_header::status_enum::executed},
         10,
         5,
         { chain::signature_type() },
         { chain::time_point_sec(), 1, 0, 100, 50, 0 }
      };

      auto block_trace = block_trace_v2 {
         "b000000000000000000000000000000000000000000000000000000000000001"_h,
         1,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         chain::block_timestamp_type(0),
         "bp.one"_n,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         0,
         std::vector<transaction_trace_v2> {
            transaction_trace
         }
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "pending")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants({
            fc::mutable_variant_object()
            ("id", "0000000000000000000000000000000000000000000000000000000000000001")
            ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("global_sequence", 0)
                  ("receiver", "receiver")
                  ("account", "contract")
                  ("action", "action")
                  ("authorization", fc::variants({
                     fc::mutable_variant_object()
                        ("account", "alice")
                        ("permission", "active")
                     }))
                  ("data", "00000000")
                  ("return_value","04040404")
               ,
               fc::mutable_variant_object()
                  ("global_sequence", 1)
                  ("receiver", "receiver")
                  ("account", "contract")
                  ("action", "action")
                  ("authorization", fc::variants({
                     fc::mutable_variant_object()
                        ("account", "alice")
                        ("permission", "active")
                     }))
                  ("data", "01010101")
                  ("return_value", "05050505")
               ,
               fc::mutable_variant_object()
                  ("global_sequence", 2)
                  ("receiver", "receiver")
                  ("account", "contract")
                  ("action", "action")
                  ("authorization", fc::variants({
                     fc::mutable_variant_object()
                        ("account", "alice")
                        ("permission", "active")
                        }))
                  ("data", "02020202")
                  ("return_value", "06060606")
                  }))
               ("status", "executed")
               ("cpu_usage_us", 10)
               ("net_usage_words", 5)
               ("signatures", fc::variants({"SIG_K1_111111111111111111111111111111111111111111111111111111111111111116uk5ne"}))
               ("transaction_header", fc::mutable_variant_object()
                  ("expiration", "1970-01-01T00:00:00")
                  ("ref_block_num", 1)
                  ("ref_block_prefix", 0)
                  ("max_net_usage_words", 100)
                  ("max_cpu_usage_ms", 50)
                  ("delay_sec", 0)
               )
      }))
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), false);
      };

      // simulate an inability to parse the parameters and return_data
      mock_data_handler_v1 = [](const action_trace_v1&) -> std::tuple<fc::variant, std::optional<fc::variant>> {
         return {};
      };

      fc::variant actual_response = get_block_trace( 1 );

      BOOST_TEST(to_kv(expected_response) == to_kv(actual_response), boost::test_tools::per_element());
   }

   BOOST_FIXTURE_TEST_CASE(lib_response_v2, response_test_fixture)
   {
      auto block_trace = block_trace_v2 {
         "b000000000000000000000000000000000000000000000000000000000000001"_h,
         1,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         chain::block_timestamp_type(0),
         "bp.one"_n,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         "0000000000000000000000000000000000000000000000000000000000000000"_h,
         0,
         {}
      };

      fc::variant expected_response = fc::mutable_variant_object()
         ("id", "b000000000000000000000000000000000000000000000000000000000000001")
         ("number", 1)
         ("previous_id", "0000000000000000000000000000000000000000000000000000000000000000")
         ("status", "irreversible")
         ("timestamp", "2000-01-01T00:00:00.000Z")
         ("producer", "bp.one")
         ("transaction_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("action_mroot", "0000000000000000000000000000000000000000000000000000000000000000")
         ("schedule_version", 0)
         ("transactions", fc::variants() )
      ;

      mock_get_block = [&block_trace]( uint32_t height ) -> get_block_t {
         BOOST_TEST(height == 1u);
         return std::make_tuple(data_log_entry(block_trace), true);
      };

      fc::variant response = get_block_trace( 1 );
      BOOST_TEST(to_kv(expected_response) == to_kv(response), boost::test_tools::per_element());

   }

BOOST_AUTO_TEST_SUITE_END()
