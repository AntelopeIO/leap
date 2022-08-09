#pragma once

#include<vector>
#include<eosio/chain/transaction.hpp>
#include<eosio/chain/block.hpp>
#include<boost/asio/ip/tcp.hpp>
#include<fc/network/message_buffer.hpp>
#include<eosio/chain/thread_utils.hpp>

namespace eosio {
   using namespace chain;
   using namespace fc;

   static_assert(sizeof(std::chrono::system_clock::duration::rep) >= 8, "system_clock is expected to be at least 64 bits");
   typedef std::chrono::system_clock::duration::rep tstamp;

   struct chain_size_message {
      uint32_t                   last_irreversible_block_num = 0;
      block_id_type              last_irreversible_block_id;
      uint32_t                   head_num = 0;
      block_id_type              head_id;
   };

   // Longest domain name is 253 characters according to wikipedia.
   // Addresses include ":port" where max port is 65535, which adds 6 chars.
   // We also add our own extentions of "[:trx|:blk] - xxxxxxx", which adds 14 chars, total= 273.
   // Allow for future extentions as well, hence 384.
   constexpr size_t max_p2p_address_length = 253 + 6;
   constexpr size_t max_handshake_str_length = 384;

   struct handshake_message {
      uint16_t                   network_version = 0; ///< incremental value above a computed base
      chain_id_type              chain_id; ///< used to identify chain
      fc::sha256                 node_id; ///< used to identify peers and prevent self-connect
      chain::public_key_type     key; ///< authentication key; may be a producer or peer key, or empty
      int64_t                    time{0}; ///< time message created in nanoseconds from epoch
      fc::sha256                 token; ///< digest of time to prove we own the private key of the key above
      chain::signature_type      sig; ///< signature for the digest
      string                     p2p_address;
      uint32_t                   last_irreversible_block_num = 0;
      block_id_type              last_irreversible_block_id;
      uint32_t                   head_num = 0;
      block_id_type              head_id;
      string                     os;
      string                     agent;
      int16_t                    generation = 0;
   };


   enum go_away_reason {
      no_reason, ///< no reason to go away
      self, ///< the connection is to itself
      duplicate, ///< the connection is redundant
      wrong_chain, ///< the peer's chain id doesn't match
      wrong_version, ///< the peer's network version doesn't match
      forked, ///< the peer's irreversible blocks are different
      unlinkable, ///< the peer sent a block we couldn't use
      bad_transaction, ///< the peer sent a transaction that failed verification
      validation, ///< the peer sent a block that failed validation
      benign_other, ///< reasons such as a timeout. not fatal but warrant resetting
      fatal_other, ///< a catch-all for errors we don't have discriminated
      authentication ///< peer failed authenicatio
   };

   constexpr auto reason_str( go_away_reason rsn ) {
      switch (rsn ) {
         case no_reason : return "no reason";
         case self : return "self connect";
         case duplicate : return "duplicate";
         case wrong_chain : return "wrong chain";
         case wrong_version : return "wrong version";
         case forked : return "chain is forked";
         case unlinkable : return "unlinkable block received";
         case bad_transaction : return "bad transaction";
         case validation : return "invalid block";
         case authentication : return "authentication failure";
         case fatal_other : return "some other failure";
         case benign_other : return "some other non-fatal condition, possibly unknown block";
         default : return "some crazy reason";
      }
   }

   struct go_away_message {
      go_away_message(go_away_reason r = no_reason) : reason(r), node_id() {}
      go_away_reason reason{no_reason};
      fc::sha256 node_id; ///< for duplicate notification
   };

   struct time_message {
      tstamp  org{0};       //!< origin timestamp
      tstamp  rec{0};       //!< receive timestamp
      tstamp  xmt{0};       //!< transmit timestamp
      mutable tstamp  dst{0};       //!< destination timestamp
   };

   enum id_list_modes {
      none,
      catch_up,
      last_irr_catch_up,
      normal
   };

   constexpr auto modes_str( id_list_modes m ) {
      switch( m ) {
         case none : return "none";
         case catch_up : return "catch up";
         case last_irr_catch_up : return "last irreversible";
         case normal : return "normal";
         default: return "undefined mode";
      }
   }

   template<typename T>
   struct select_ids {
      select_ids() : mode(none),pending(0),ids() {}
      id_list_modes  mode{none};
      uint32_t       pending{0};
      vector<T>      ids;
      bool           empty () const { return (mode == none || ids.empty()); }
   };

   using ordered_txn_ids = select_ids<transaction_id_type>;
   using ordered_blk_ids = select_ids<block_id_type>;

   struct notice_message {
      notice_message() : known_trx(), known_blocks() {}
      ordered_txn_ids known_trx;
      ordered_blk_ids known_blocks;
   };

   struct request_message {
      request_message() : req_trx(), req_blocks() {}
      ordered_txn_ids req_trx;
      ordered_blk_ids req_blocks;
   };

   struct sync_request_message {
      uint32_t start_block{0};
      uint32_t end_block{0};
   };

   using net_message = std::variant<handshake_message,
         chain_size_message,
         go_away_message,
         time_message,
         notice_message,
         request_message,
         sync_request_message,
         signed_block,         // which = 7
         packed_transaction>;  // which = 8

} // namespace eosio

/**
    *  For a while, network version was a 16 bit value equal to the second set of 16 bits
    *  of the current build's git commit id. We are now replacing that with an integer protocol
    *  identifier. Based on historical analysis of all git commit identifiers, the larges gap
    *  between ajacent commit id values is shown below.
    *  these numbers were found with the following commands on the master branch:
    *
    *  git log | grep "^commit" | awk '{print substr($2,5,4)}' | sort -u > sorted.txt
    *  rm -f gap.txt; prev=0; for a in $(cat sorted.txt); do echo $prev $((0x$a - 0x$prev)) $a >> gap.txt; prev=$a; done; sort -k2 -n gap.txt | tail
    *
    *  DO NOT EDIT net_version_base OR net_version_range!
    */
constexpr uint16_t net_version_base = 0x04b5;
constexpr uint16_t net_version_range = 106;
/**
 *  If there is a change to network protocol or behavior, increment net version to identify
 *  the need for compatibility hooks
 */
constexpr uint16_t proto_base = 0;
constexpr uint16_t proto_explicit_sync = 1;       // version at time of eosio 1.0
constexpr uint16_t proto_block_id_notify = 2;     // reserved. feature was removed. next net_version should be 3
constexpr uint16_t proto_pruned_types = 3;        // eosio 2.1: supports new signed_block & packed_transaction types
constexpr uint16_t proto_heartbeat_interval = 4;        // eosio 2.1: supports configurable heartbeat interval
constexpr uint16_t proto_dup_goaway_resolution = 5;     // eosio 2.1: support peer address based duplicate connection resolution
constexpr uint16_t proto_dup_node_id_goaway = 6;        // eosio 2.1: support peer node_id based duplicate connection resolution
constexpr uint16_t proto_mandel_initial = 7;            // mandel client, needed because none of the 2.1 versions are supported

constexpr uint16_t net_version_max = proto_mandel_initial;
/**
 * default value initializers
 */
constexpr auto     def_send_buffer_size_mb = 4;
constexpr auto     def_send_buffer_size = 1024*1024*def_send_buffer_size_mb;
constexpr auto     def_max_write_queue_size = def_send_buffer_size*10;
constexpr auto     def_max_trx_in_progress_size = 100*1024*1024; // 100 MB
constexpr auto     def_max_consecutive_immediate_connection_close = 9; // back off if client keeps closing
constexpr auto     def_max_clients = 25; // 0 for unlimited clients
constexpr auto     def_max_nodes_per_host = 1;
constexpr auto     def_conn_retry_wait = 30;
constexpr auto     def_txn_expire_wait = std::chrono::seconds(3);
constexpr auto     def_resp_expected_wait = std::chrono::seconds(5);
constexpr auto     def_sync_fetch_span = 100;
constexpr auto     def_keepalive_interval = 10000;

constexpr auto     message_header_size = sizeof(uint32_t);
constexpr uint32_t packed_transaction_which = fc::get_index<eosio::net_message, eosio::packed_transaction>(); // see protocol net_message


namespace eosio::testing {

   struct simple_trx_generator {
      void setup() {}
      void teardown() {}

      void generate(std::vector<chain::signed_transaction>& trxs, size_t requested) {

      }
   };
   class queued_buffer : boost::noncopyable {
   public:
      void clear_write_queue() {
         std::lock_guard<std::mutex> g( _mtx );
         _write_queue.clear();
         _sync_write_queue.clear();
         _write_queue_size = 0;
      }

      void clear_out_queue() {
         std::lock_guard<std::mutex> g( _mtx );
         while ( _out_queue.size() > 0 ) {
            _out_queue.pop_front();
         }
      }

      uint32_t write_queue_size() const {
         std::lock_guard<std::mutex> g( _mtx );
         return _write_queue_size;
      }

      bool is_out_queue_empty() const {
         std::lock_guard<std::mutex> g( _mtx );
         return _out_queue.empty();
      }

      bool ready_to_send() const {
         std::lock_guard<std::mutex> g( _mtx );
         // if out_queue is not empty then async_write is in progress
         return ((!_sync_write_queue.empty() || !_write_queue.empty()) && _out_queue.empty());
      }

      // @param callback must not callback into queued_buffer
      bool add_write_queue( const std::shared_ptr<vector<char>>& buff,
                            std::function<void( boost::system::error_code, std::size_t )> callback,
                            bool to_sync_queue ) {
         std::lock_guard<std::mutex> g( _mtx );
         if( to_sync_queue ) {
            _sync_write_queue.push_back( {buff, callback} );
         } else {
            _write_queue.push_back( {buff, callback} );
         }
         _write_queue_size += buff->size();
         if( _write_queue_size > 2 * def_max_write_queue_size ) {
            return false;
         }
         return true;
      }

      void fill_out_buffer( std::vector<boost::asio::const_buffer>& bufs ) {
         std::lock_guard<std::mutex> g( _mtx );
         if( _sync_write_queue.size() > 0 ) { // always send msgs from sync_write_queue first
            fill_out_buffer( bufs, _sync_write_queue );
         } else { // postpone real_time write_queue if sync queue is not empty
            fill_out_buffer( bufs, _write_queue );
            EOS_ASSERT( _write_queue_size == 0, plugin_exception, "write queue size expected to be zero" );
         }
      }

      void out_callback( boost::system::error_code ec, std::size_t w ) {
         std::lock_guard<std::mutex> g( _mtx );
         for( auto& m : _out_queue ) {
            m.callback( ec, w );
         }
      }

   private:
      struct queued_write;
      void fill_out_buffer( std::vector<boost::asio::const_buffer>& bufs,
                            deque<queued_write>& w_queue ) {
         while ( w_queue.size() > 0 ) {
            auto& m = w_queue.front();
            bufs.push_back( boost::asio::buffer( *m.buff ));
            _write_queue_size -= m.buff->size();
            _out_queue.emplace_back( m );
            w_queue.pop_front();
         }
      }

   private:
      struct queued_write {
         std::shared_ptr<vector<char>> buff;
         std::function<void( boost::system::error_code, std::size_t )> callback;
      };

      mutable std::mutex  _mtx;
      uint32_t            _write_queue_size{0};
      deque<queued_write> _write_queue;
      deque<queued_write> _sync_write_queue; // sync_write_queue will be sent first
      deque<queued_write> _out_queue;

   }; // queued_buffer


   class connection : public std::enable_shared_from_this<connection> {
   public:
      explicit connection(std::shared_ptr<eosio::chain::named_thread_pool> thread_pool, const string& endpoint );
      connection(std::shared_ptr<eosio::chain::named_thread_pool> thread_pool);

      ~connection() = default;

      bool start_session();

      bool socket_is_open() const { return socket_open.load(); } // thread safe, atomic
      const string& peer_address() const { return peer_addr; } // thread safe, const

      void set_connection_type( const string& peer_addr );
      bool is_transactions_only_connection()const { return connection_type == transactions_only; }
      bool is_blocks_only_connection()const { return connection_type == blocks_only; }
      void set_heartbeat_timeout(std::chrono::milliseconds msec) {
         std::chrono::system_clock::duration dur = msec;
         hb_timeout = dur.count();
      }

   private:
      static const string unknown;

      void update_endpoints();
      std::shared_ptr<eosio::chain::named_thread_pool> threads;
      std::atomic<bool>                         socket_open{false};

      const string            peer_addr;
      enum connection_types : char {
         both,
         transactions_only,
         blocks_only
      };

      std::atomic<connection_types>             connection_type{both};

   public:
      boost::asio::io_context::strand                 strand;
      std::shared_ptr<boost::asio::ip::tcp::socket>   socket; // only accessed through strand after construction

      fc::message_buffer<1024*1024>    pending_message_buffer;
      std::atomic<std::size_t>         outstanding_read_bytes{0}; // accessed only from strand threads

      queued_buffer           buffer_queue;

      fc::sha256              conn_node_id;
      string                  short_conn_node_id;
      string                  log_p2p_address;
      string                  log_remote_endpoint_ip;
      string                  log_remote_endpoint_port;
      string                  local_endpoint_ip;
      string                  local_endpoint_port;

      std::atomic<uint32_t>   trx_in_progress_size{0};
      const uint32_t          connection_id;
      int16_t                 sent_handshake_count = 0;
      std::atomic<bool>       connecting{true};
      std::atomic<bool>       syncing{false};

      std::atomic<uint16_t>   protocol_version = 0;
      uint16_t                net_version = net_version_max;
      std::atomic<uint16_t>   consecutive_immediate_connection_close = 0;

      std::mutex                            response_expected_timer_mtx;
      boost::asio::steady_timer             response_expected_timer;

      std::atomic<go_away_reason>           no_retry{no_reason};

      mutable std::mutex               conn_mtx; //< mtx for last_req .. remote_endpoint_ip
      std::optional<request_message>   last_req;
      handshake_message                last_handshake_recv;
      handshake_message                last_handshake_sent;
      block_id_type                    fork_head;
      uint32_t                         fork_head_num{0};
      fc::time_point                   last_close;
      string                           remote_endpoint_ip;


      /** \name Peer Timestamps
       *  Time message handling
       *  @{
       */
      // Members set from network data
      tstamp                         org{0};          //!< originate timestamp
      tstamp                         rec{0};          //!< receive timestamp
      tstamp                         dst{0};          //!< destination timestamp
      tstamp                         xmt{0};          //!< transmit timestamp
      /** @} */
      // timestamp for the lastest message
      tstamp                         latest_msg_time{0};
      tstamp                         hb_timeout{std::chrono::milliseconds{def_keepalive_interval}.count()};
      tstamp                         latest_blk_time{0};

      bool connected();
      bool current();

      /// @param reconnect true if we should try and reconnect immediately after close
      /// @param shutdown true only if plugin is shutting down
      void close( bool reconnect = true, bool shutdown = false );
   private:
      static void _close(connection* self, bool reconnect, bool shutdown ); // for easy capture

      bool process_next_block_message(uint32_t message_length);
      bool process_next_trx_message(uint32_t message_length);
   public:

      bool populate_handshake( handshake_message& hello );

      bool resolve_and_connect();
      void connect( const std::shared_ptr<boost::asio::ip::tcp::resolver>& resolver, boost::asio::ip::tcp::resolver::results_type endpoints );
      void start_read_message();

      /** \brief Process the next message from the pending message buffer
       *
       * Process the next message from the pending_message_buffer.
       * message_length is the already determined length of the data
       * part of the message that will handle the message.
       * Returns true is successful. Returns false if an error was
       * encountered unpacking or processing the message.
       */
      bool process_next_message(uint32_t message_length);

      void send_handshake();

      /** \name Peer Timestamps
       *  Time message handling
       */
      /**  \brief Check heartbeat time and send Time_message
       */
      void check_heartbeat( tstamp current_time );
      /**  \brief Populate and queue time_message
       */
      void send_time();
      /** \brief Populate and queue time_message immediately using incoming time_message
       */
      void send_time(const time_message& msg);
      /** \brief Read system time and convert to a 64 bit integer.
       *
       * There are only two calls on this routine in the program.  One
       * when a packet arrives from the network and the other when a
       * packet is placed on the send queue.  Calls the kernel time of
       * day routine and converts to a (at least) 64 bit integer.
       */
      static tstamp get_time() {
         return std::chrono::system_clock::now().time_since_epoch().count();
      }
      /** @} */

      void blk_send_branch( const block_id_type& msg_head_id );
      void blk_send_branch_impl( uint32_t msg_head_num, uint32_t lib_num, uint32_t head_num );
      void blk_send(const block_id_type& blkid);
      void stop_send();

      void enqueue( const net_message &msg );
      void enqueue_block( const signed_block_ptr& sb, bool to_sync_queue = false);
      void enqueue_buffer( const std::shared_ptr<std::vector<char>>& send_buffer,
                           go_away_reason close_after_send,
                           bool to_sync_queue = false);
      void cancel_sync(go_away_reason);
      void flush_queues();
      bool enqueue_sync_block();
      void request_sync_blocks(uint32_t start, uint32_t end);

      void cancel_wait();
      void sync_wait();
      void fetch_wait();
      void sync_timeout(boost::system::error_code ec);
      void fetch_timeout(boost::system::error_code ec);

      void queue_write(const std::shared_ptr<vector<char>>& buff,
                       std::function<void(boost::system::error_code, std::size_t)> callback,
                       bool to_sync_queue = false);
      void do_queue_write();

      bool is_valid( const handshake_message& msg ) const;

      void handle_message( const handshake_message& msg );
      void handle_message( const chain_size_message& msg );
      void handle_message( const go_away_message& msg );
      /** \name Peer Timestamps
       *  Time message handling
       *  @{
       */
      /** \brief Process time_message
       *
       * Calculate offset, delay and dispersion.  Note carefully the
       * implied processing.  The first-order difference is done
       * directly in 64-bit arithmetic, then the result is converted
       * to floating double.  All further processing is in
       * floating-double arithmetic with rounding done by the hardware.
       * This is necessary in order to avoid overflow and preserve precision.
       */
      void handle_message( const time_message& msg );
      /** @} */
      void handle_message( const notice_message& msg );
      void handle_message( const request_message& msg );
      void handle_message( const sync_request_message& msg );
      void handle_message( const signed_block& msg ) = delete; // signed_block_ptr overload used instead
      void handle_message( const block_id_type& id, signed_block_ptr msg );
      void handle_message( const packed_transaction& msg ) = delete; // packed_transaction_ptr overload used instead
      void handle_message( packed_transaction_ptr msg );

      void process_signed_block( const block_id_type& id, signed_block_ptr msg );

      fc::variant_object get_logger_variant() const {
         fc::mutable_variant_object mvo;
         mvo( "_name", log_p2p_address)
               ( "_cid", connection_id )
               ( "_id", conn_node_id )
               ( "_sid", short_conn_node_id )
               ( "_ip", log_remote_endpoint_ip )
               ( "_port", log_remote_endpoint_port )
               ( "_lip", local_endpoint_ip )
               ( "_lport", local_endpoint_port );
         return mvo;
      }
   };



   template<typename G, typename I> struct simple_tps_tester {
      G trx_generator;
      I trx_provider;
      size_t num_trxs = 1;

      std::vector<chain::signed_transaction> trxs;

      void run() {
         trx_generator.setup();
         trx_provider.setup();

         trx_generator.generate(trxs, num_trxs);
         trx_provider.send(trxs);

         trx_provider.teardown();
         trx_generator.teardown();
      }
   };

   struct p2p_connection {
      std::string _peer_endpoint;

      p2p_connection(std::string peer_endpoint) : _peer_endpoint(peer_endpoint) {

      }

      void connect();
      void disconnect();
      void send_transaction(const chain::signed_transaction trx);
   };

   struct p2p_trx_provider {
      std::shared_ptr<class connection> _peer_connection;

      p2p_trx_provider(std::shared_ptr<named_thread_pool> tp, std::string peer_endpoint="http://localhost:8080");

      void setup();
      void send(const std::vector<chain::signed_transaction>& trxs);
      void teardown();

   private:
      std::string peer_endpoint;

   };

}