#include <eosio/chain/types.hpp>

#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/net_plugin/protocol.hpp>
#include <eosio/net_plugin/auto_bp_peering.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/chain/contract_types.hpp>

#include <fc/network/message_buffer.hpp>
#include <fc/io/json.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/appender.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/exception/exception.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <cmath>
#include <shared_mutex>

using namespace eosio::chain::plugin_interface;

namespace eosio {
   static auto _net_plugin = application::register_plugin<net_plugin>();

   using std::vector;

   using boost::asio::ip::tcp;
   using boost::asio::ip::address_v4;
   using boost::asio::ip::host_name;
   using boost::multi_index_container;

   using fc::time_point;
   using fc::time_point_sec;
   using eosio::chain::transaction_id_type;
   using eosio::chain::sha256_less;

   class connection;

   using connection_ptr = std::shared_ptr<connection>;
   using connection_wptr = std::weak_ptr<connection>;

   const fc::string logger_name("net_plugin_impl");
   fc::logger logger;
   std::string peer_log_format;

   template <typename Strand>
   void verify_strand_in_this_thread(const Strand& strand, const char* func, int line) {
      if( !strand.running_in_this_thread() ) {
         fc_elog( logger, "wrong strand: ${f} : line ${n}, exiting", ("f", func)("n", line) );
         app().quit();
      }
   }

   struct node_transaction_state {
      transaction_id_type id;
      time_point_sec  expires;        /// time after which this may be purged.
      uint32_t        connection_id = 0;
   };

   struct by_expiry;

   typedef multi_index_container<
      node_transaction_state,
      indexed_by<
         ordered_unique<
            tag<by_id>,
            composite_key< node_transaction_state,
               member<node_transaction_state, transaction_id_type, &node_transaction_state::id>,
               member<node_transaction_state, uint32_t, &node_transaction_state::connection_id>
            >,
            composite_key_compare< sha256_less, std::less<> >
         >,
         ordered_non_unique<
            tag< by_expiry >,
            member< node_transaction_state, fc::time_point_sec, &node_transaction_state::expires > >
         >
      >
   node_transaction_index;

   struct peer_block_state {
      block_id_type id;
      uint32_t      connection_id = 0;

      uint32_t block_num() const { return block_header::num_from_id(id); }
   };

   struct by_connection_id;

   typedef multi_index_container<
      eosio::peer_block_state,
      indexed_by<
         ordered_unique< tag<by_connection_id>,
               composite_key< peer_block_state,
                     const_mem_fun<peer_block_state, uint32_t , &eosio::peer_block_state::block_num>,
                     member<peer_block_state, block_id_type, &eosio::peer_block_state::id>,
                     member<peer_block_state, uint32_t, &eosio::peer_block_state::connection_id>
               >,
               composite_key_compare< std::less<>, sha256_less, std::less<> >
         >
      >
      > peer_block_state_index;


   class sync_manager {
   private:
      enum stages {
         lib_catchup,
         head_catchup,
         in_sync
      };

      static constexpr int64_t block_interval_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(config::block_interval_ms)).count();

      mutable std::mutex sync_mtx;
      uint32_t       sync_known_lib_num{0};
      uint32_t       sync_last_requested_num{0};
      uint32_t       sync_next_expected_num{0};
      uint32_t       sync_req_span{0};
      connection_ptr sync_source;
      std::atomic<stages> sync_state{in_sync};

   private:
      constexpr static auto stage_str( stages s );
      bool set_state( stages s );
      bool is_sync_required( uint32_t fork_head_block_num );
      void request_next_chunk( std::unique_lock<std::mutex> g_sync, const connection_ptr& conn = connection_ptr() );
      void start_sync( const connection_ptr& c, uint32_t target );
      bool verify_catchup( const connection_ptr& c, uint32_t num, const block_id_type& id );

   public:
      explicit sync_manager( uint32_t span );
      static void send_handshakes();
      bool syncing_with_peer() const { return sync_state == lib_catchup; }
      bool is_in_sync() const { return sync_state == in_sync; }
      void sync_reset_lib_num( const connection_ptr& conn, bool closing );
      void sync_reassign_fetch( const connection_ptr& c, go_away_reason reason );
      void rejected_block( const connection_ptr& c, uint32_t blk_num );
      void sync_recv_block( const connection_ptr& c, const block_id_type& blk_id, uint32_t blk_num, bool blk_applied );
      void sync_update_expected( const connection_ptr& c, const block_id_type& blk_id, uint32_t blk_num, bool blk_applied );
      void recv_handshake( const connection_ptr& c, const handshake_message& msg );
      void sync_recv_notice( const connection_ptr& c, const notice_message& msg );
      inline std::unique_lock<std::mutex> locked_sync_mutex() {
         return std::unique_lock<std::mutex>(sync_mtx);
      }
      inline void reset_last_requested_num(const std::unique_lock<std::mutex>& lock) {
         sync_last_requested_num = 0;
      }
   };

   class dispatch_manager {
      mutable std::mutex      blk_state_mtx;
      peer_block_state_index  blk_state;
      mutable std::mutex      local_txns_mtx;
      node_transaction_index  local_txns;

   public:
      boost::asio::io_context::strand  strand;

      explicit dispatch_manager(boost::asio::io_context& io_context)
      : strand( io_context ) {}

      void bcast_transaction(const packed_transaction_ptr& trx);
      void rejected_transaction(const packed_transaction_ptr& trx);
      void bcast_block( const signed_block_ptr& b, const block_id_type& id );
      void rejected_block(const block_id_type& id);

      void recv_block(const connection_ptr& conn, const block_id_type& msg, uint32_t bnum);
      void expire_blocks( uint32_t bnum );
      void recv_notice(const connection_ptr& conn, const notice_message& msg, bool generated);

      void retry_fetch(const connection_ptr& conn);

      bool add_peer_block( const block_id_type& blkid, uint32_t connection_id );
      bool peer_has_block(const block_id_type& blkid, uint32_t connection_id) const;
      bool have_block(const block_id_type& blkid) const;
      void rm_block(const block_id_type& blkid);

      bool add_peer_txn( const transaction_id_type id, const time_point_sec& trx_expires, uint32_t connection_id,
                         const time_point_sec& now = time_point::now() );
      bool have_txn( const transaction_id_type& tid ) const;
      void expire_txns();
   };

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
   constexpr uint32_t signed_block_which       = fc::get_index<net_message, signed_block>();       // see protocol net_message
   constexpr uint32_t packed_transaction_which = fc::get_index<net_message, packed_transaction>(); // see protocol net_message

   class net_plugin_impl : public std::enable_shared_from_this<net_plugin_impl>,
                           public auto_bp_peering::bp_connection_manager<net_plugin_impl, connection> {
    public:
      using connection_t = connection;
      unique_ptr<tcp::acceptor>        acceptor;
      std::atomic<uint32_t>            current_connection_id{0};

      unique_ptr< sync_manager >       sync_master;
      unique_ptr< dispatch_manager >   dispatcher;

      /**
       * Thread safe, only updated in plugin initialize
       *  @{
       */
      string                                p2p_address;
      string                                p2p_server_address;

      chain::flat_set<string>               supplied_peers;
      vector<chain::public_key_type>        allowed_peers; ///< peer keys allowed to connect
      std::map<chain::public_key_type,
               chain::private_key_type>     private_keys; ///< overlapping with producer keys, also authenticating non-producing nodes
      enum possible_connections : char {
         None = 0,
            Producers = 1 << 0,
            Specified = 1 << 1,
            Any = 1 << 2
            };
      possible_connections                  allowed_connections{None};

      boost::asio::steady_timer::duration   connector_period{0};
      boost::asio::steady_timer::duration   txn_exp_period{0};
      boost::asio::steady_timer::duration   resp_expected_period{0};
      std::chrono::milliseconds             keepalive_interval{std::chrono::milliseconds{def_keepalive_interval}};
      std::chrono::milliseconds             heartbeat_timeout{keepalive_interval * 2};

      int                                   max_cleanup_time_ms = 0;
      uint32_t                              max_client_count = 0;
      uint32_t                              max_nodes_per_host = 1;
      bool                                  p2p_accept_transactions = true;
      fc::microseconds                      p2p_dedup_cache_expire_time_us{};

      /// Peer clock may be no more than 1 second skewed from our clock, including network latency.
      const std::chrono::system_clock::duration peer_authentication_interval{std::chrono::seconds{1}};

      chain_id_type                         chain_id;
      fc::sha256                            node_id;
      string                                user_agent_name;

      chain_plugin*                         chain_plug = nullptr;
      producer_plugin*                      producer_plug = nullptr;
      bool                                  use_socket_read_watermark = false;
      /** @} */

      mutable std::shared_mutex             connections_mtx;
      std::set< connection_ptr >            connections;     // todo: switch to a thread safe container to avoid big mutex over complete collection

      std::mutex                            connector_check_timer_mtx;
      unique_ptr<boost::asio::steady_timer> connector_check_timer;
      int                                   connector_checks_in_flight{0};

      std::mutex                            expire_timer_mtx;
      unique_ptr<boost::asio::steady_timer> expire_timer;

      std::mutex                            keepalive_timer_mtx;
      unique_ptr<boost::asio::steady_timer> keepalive_timer;

      std::atomic<bool>                     in_shutdown{false};

      compat::channels::transaction_ack::channel_type::handle  incoming_transaction_ack_subscription;

      uint16_t                                    thread_pool_size = 4;
      eosio::chain::named_thread_pool<struct net> thread_pool;

      boost::asio::deadline_timer           accept_error_timer{thread_pool.get_executor()};

      net_plugin_metrics   metrics;

      struct chain_info_t {
         uint32_t      lib_num = 0;
         block_id_type lib_id;
         uint32_t      head_num = 0;
         block_id_type head_id;
      };

   private:
      mutable std::mutex            chain_info_mtx; // protects chain_info_t
      chain_info_t                  chain_info;

   public:
      void update_chain_info();
      chain_info_t get_chain_info() const;
      uint32_t get_chain_lib_num() const;
      uint32_t get_chain_head_num() const;

      void start_listen_loop();

      void on_accepted_block_header( const block_state_ptr& bs );
      void on_accepted_block( const block_state_ptr& bs );

      void transaction_ack(const std::pair<fc::exception_ptr, packed_transaction_ptr>&);
      void on_irreversible_block( const block_state_ptr& blk );

      void start_conn_timer(boost::asio::steady_timer::duration du, std::weak_ptr<connection> from_connection);
      void start_expire_timer();
      void start_monitors();

      void expire();
      void connection_monitor(std::weak_ptr<connection> from_connection, bool reschedule);
      /** \name Peer Timestamps
       *  Time message handling
       *  @{
       */
      /** \brief Peer heartbeat ticker.
       */
      void ticker();
      /** @} */
      /** \brief Determine if a peer is allowed to connect.
       *
       * Checks current connection mode and key authentication.
       *
       * \return False if the peer should not connect, true otherwise.
       */
      bool authenticate_peer(const handshake_message& msg) const;
      /** \brief Retrieve public key used to authenticate with peers.
       *
       * Finds a key to use for authentication.  If this node is a producer, use
       * the front of the producer key map.  If the node is not a producer but has
       * a configured private key, use it.  If the node is neither a producer nor has
       * a private key, returns an empty key.
       *
       * \note On a node with multiple private keys configured, the key with the first
       *       numerically smaller byte will always be used.
       */
      chain::public_key_type get_authentication_key() const;
      /** \brief Returns a signature of the digest using the corresponding private key of the signer.
       *
       * If there are no configured private keys, returns an empty signature.
       */
      chain::signature_type sign_compact(const chain::public_key_type& signer, const fc::sha256& digest) const;

      constexpr static uint16_t to_protocol_version(uint16_t v);

      connection_ptr find_connection(const string& host)const; // must call with held mutex
      string connect( const string& host );
      string disconnect( const string& endpoint );

      template <typename Function>
      void for_each_connection(Function&& fun) const;

      void plugin_shutdown();
      bool in_sync() const;
      fc::logger& get_logger() { return logger; }
   };

   // peer_[x]log must be called from thread in connection strand
#define peer_dlog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( logger.is_enabled( fc::log_level::debug ) ) { \
      verify_strand_in_this_thread( PEER->strand, __func__, __LINE__ ); \
      logger.log( FC_LOG_MESSAGE( debug, peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
   } \
  FC_MULTILINE_MACRO_END

#define peer_ilog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( logger.is_enabled( fc::log_level::info ) ) { \
      verify_strand_in_this_thread( PEER->strand, __func__, __LINE__ ); \
      logger.log( FC_LOG_MESSAGE( info, peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
   } \
  FC_MULTILINE_MACRO_END

#define peer_wlog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( logger.is_enabled( fc::log_level::warn ) ) { \
      verify_strand_in_this_thread( PEER->strand, __func__, __LINE__ ); \
      logger.log( FC_LOG_MESSAGE( warn, peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
   } \
  FC_MULTILINE_MACRO_END

#define peer_elog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( logger.is_enabled( fc::log_level::error ) ) { \
      verify_strand_in_this_thread( PEER->strand, __func__, __LINE__ ); \
      logger.log( FC_LOG_MESSAGE( error, peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
   } \
  FC_MULTILINE_MACRO_END


   template<class enum_type, class=typename std::enable_if<std::is_enum<enum_type>::value>::type>
   inline enum_type& operator|=(enum_type& lhs, const enum_type& rhs)
   {
      using T = std::underlying_type_t <enum_type>;
      return lhs = static_cast<enum_type>(static_cast<T>(lhs) | static_cast<T>(rhs));
   }

   static net_plugin_impl *my_impl;

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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
   constexpr uint16_t proto_base = 0;
   constexpr uint16_t proto_explicit_sync = 1;       // version at time of eosio 1.0
   constexpr uint16_t proto_block_id_notify = 2;     // reserved. feature was removed. next net_version should be 3
   constexpr uint16_t proto_pruned_types = 3;        // eosio 2.1: supports new signed_block & packed_transaction types
   constexpr uint16_t proto_heartbeat_interval = 4;        // eosio 2.1: supports configurable heartbeat interval
   constexpr uint16_t proto_dup_goaway_resolution = 5;     // eosio 2.1: support peer address based duplicate connection resolution
   constexpr uint16_t proto_dup_node_id_goaway = 6;        // eosio 2.1: support peer node_id based duplicate connection resolution
   constexpr uint16_t proto_leap_initial = 7;            // leap client, needed because none of the 2.1 versions are supported
#pragma GCC diagnostic pop

   constexpr uint16_t net_version_max = proto_leap_initial;

   /**
    * Index by start_block_num
    */
   struct peer_sync_state {
      explicit peer_sync_state(uint32_t start = 0, uint32_t end = 0, uint32_t last_acted = 0)
         :start_block( start ), end_block( end ), last( last_acted ),
          start_time(time_point::now())
      {}
      uint32_t     start_block;
      uint32_t     end_block;
      uint32_t     last; ///< last sent or received
      time_point   start_time; ///< time request made or received
   };

   // thread safe
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


   /// monitors the status of blocks as to whether a block is accepted (sync'd) or
   /// rejected. It groups consecutive rejected blocks in a (configurable) time
   /// window (rbw) and maintains a metric of the number of consecutive rejected block
   /// time windows (rbws).
   class block_status_monitor {
   private:
      bool in_accepted_state_ {true};              ///< indicates of accepted(true) or rejected(false) state
      fc::microseconds window_size_{2*1000};       ///< rbw time interval (2ms)
      fc::time_point   window_start_;              ///< The start of the recent rbw (0 implies not started)
      uint32_t         events_{0};                 ///< The number of consecutive rbws
      const uint32_t   max_consecutive_rejected_windows_{13};

   public:
      /// ctor
      ///
      /// @param[in] window_size          The time, in microseconds, of the rejected block window
      /// @param[in] max_rejected_windows The max consecutive number of rejected block windows
      /// @note   Copy ctor is not allowed
      explicit block_status_monitor(fc::microseconds window_size = fc::microseconds(2*1000),
            uint32_t max_rejected_windows = 13) :
         window_size_(window_size) {}
      block_status_monitor( const block_status_monitor& ) = delete;
      block_status_monitor( block_status_monitor&& ) = delete;
      ~block_status_monitor() = default;
      /// reset to initial state
      void reset();
      /// called when a block is accepted (sync_recv_block)
      void accepted() { reset(); }
      /// called when a block is rejected
      void rejected();
      /// returns number of consecutive rbws
      auto events() const { return events_; }
      /// indicates if the max number of consecutive rbws has been reached or exceeded
      bool max_events_violated() const { return events_ >= max_consecutive_rejected_windows_; }
      /// assignment not allowed
      block_status_monitor& operator=( const block_status_monitor& ) = delete;
      block_status_monitor& operator=( block_status_monitor&& ) = delete;
   };

   class connection : public std::enable_shared_from_this<connection> {
   public:
      explicit connection( const string& endpoint );
      connection();

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

      std::optional<peer_sync_state> peer_requested;  // this peer is requesting info from us

      std::atomic<bool>                         socket_open{false};

      const string            peer_addr;
      enum connection_types : char {
         both,
         transactions_only,
         blocks_only
      };

      std::atomic<connection_types>             connection_type{both};

   public:
      boost::asio::io_context::strand           strand;
      std::shared_ptr<tcp::socket>              socket; // only accessed through strand after construction

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
      // kept in sync with last_handshake_recv.last_irreversible_block_num, only accessed from connection strand
      uint32_t                peer_lib_num = 0;

      std::atomic<uint32_t>   trx_in_progress_size{0};
      fc::time_point          last_dropped_trx_msg_time;
      const uint32_t          connection_id;
      int16_t                 sent_handshake_count = 0;
      std::atomic<bool>       connecting{true};
      std::atomic<bool>       syncing{false};

      std::atomic<uint16_t>   protocol_version = 0;
      uint16_t                net_version = net_version_max;
      std::atomic<uint16_t>   consecutive_immediate_connection_close = 0;
      std::atomic<bool>       is_bp_connection = false;
      block_status_monitor    block_status_monitor_;

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

      connection_status get_status()const;

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
      static void _close( connection* self, bool reconnect, bool shutdown ); // for easy capture

      bool process_next_block_message(uint32_t message_length);
      bool process_next_trx_message(uint32_t message_length);
   public:

      bool populate_handshake( handshake_message& hello );

      bool resolve_and_connect();
      void connect( const std::shared_ptr<tcp::resolver>& resolver, tcp::resolver::results_type endpoints );
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

      void process_signed_block( const block_id_type& id, signed_block_ptr msg, block_state_ptr bsp );

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

      bool incoming() const { return peer_address().empty(); } // thread safe becuase of peer_address
      bool incoming_and_handshake_received() const {
         if (!incoming()) return false;
         std::lock_guard<std::mutex> g_conn( conn_mtx );
         return last_handshake_recv.p2p_address.size();
      }
   };

   const string connection::unknown = "<unknown>";

   // called from connection strand
   struct msg_handler : public fc::visitor<void> {
      connection_ptr c;
      explicit msg_handler( const connection_ptr& conn) : c(conn) {}

      template<typename T>
      void operator()( const T& ) const {
         EOS_ASSERT( false, plugin_config_exception, "Not implemented, call handle_message directly instead" );
      }

      void operator()( const handshake_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( c, "handle handshake_message" );
         c->handle_message( msg );
      }

      void operator()( const chain_size_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( c, "handle chain_size_message" );
         c->handle_message( msg );
      }

      void operator()( const go_away_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( c, "handle go_away_message" );
         c->handle_message( msg );
      }

      void operator()( const time_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( c, "handle time_message" );
         c->handle_message( msg );
      }

      void operator()( const notice_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( c, "handle notice_message" );
         c->handle_message( msg );
      }

      void operator()( const request_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( c, "handle request_message" );
         c->handle_message( msg );
      }

      void operator()( const sync_request_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( c, "handle sync_request_message" );
         c->handle_message( msg );
      }
   };


   template<typename Function>
   void net_plugin_impl::for_each_connection( Function&& f ) const {
      std::shared_lock<std::shared_mutex> g( connections_mtx );
      for( auto& c :connections ) {
         if( !f( c ) ) return;
      }
   }

   template<typename Function>
   void for_each_connection( Function&& f ) {
      my_impl->for_each_connection(std::forward<Function>(f));
   }

   template<typename Function>
   void for_each_block_connection( Function f ) {
      std::shared_lock<std::shared_mutex> g( my_impl->connections_mtx );
      for( auto& c : my_impl->connections ) {
         if( c->is_transactions_only_connection() ) continue;
         if( !f( c ) ) return;
      }
   }

   //---------------------------------------------------------------------------

   connection::connection( const string& endpoint )
      : peer_addr( endpoint ),
        strand( my_impl->thread_pool.get_executor() ),
        socket( new tcp::socket( my_impl->thread_pool.get_executor() ) ),
        log_p2p_address( endpoint ),
        connection_id( ++my_impl->current_connection_id ),
        response_expected_timer( my_impl->thread_pool.get_executor() ),
        last_handshake_recv(),
        last_handshake_sent()
   {
      my_impl->mark_bp_connection(this);
      fc_ilog( logger, "created connection ${c} to ${n}", ("c", connection_id)("n", endpoint) );
   }

   connection::connection()
      : peer_addr(),
        strand( my_impl->thread_pool.get_executor() ),
        socket( new tcp::socket( my_impl->thread_pool.get_executor() ) ),
        connection_id( ++my_impl->current_connection_id ),
        response_expected_timer( my_impl->thread_pool.get_executor() ),
        last_handshake_recv(),
        last_handshake_sent()
   {
      fc_dlog( logger, "new connection object created" );
   }

   // called from connection strand
   void connection::update_endpoints() {
      boost::system::error_code ec;
      boost::system::error_code ec2;
      auto rep = socket->remote_endpoint(ec);
      auto lep = socket->local_endpoint(ec2);
      log_remote_endpoint_ip = ec ? unknown : rep.address().to_string();
      log_remote_endpoint_port = ec ? unknown : std::to_string(rep.port());
      local_endpoint_ip = ec2 ? unknown : lep.address().to_string();
      local_endpoint_port = ec2 ? unknown : std::to_string(lep.port());
      std::lock_guard<std::mutex> g_conn( conn_mtx );
      remote_endpoint_ip = log_remote_endpoint_ip;
   }

   // called from connection strand
   void connection::set_connection_type( const string& peer_add ) {
      // host:port:[<trx>|<blk>]
      string::size_type colon = peer_add.find(':');
      string::size_type colon2 = peer_add.find(':', colon + 1);
      string::size_type end = colon2 == string::npos
            ? string::npos : peer_add.find_first_of( " :+=.,<>!$%^&(*)|-#@\t", colon2 + 1 ); // future proof by including most symbols without using regex
      string host = peer_add.substr( 0, colon );
      string port = peer_add.substr( colon + 1, colon2 == string::npos ? string::npos : colon2 - (colon + 1));
      string type = colon2 == string::npos ? "" : end == string::npos ?
            peer_add.substr( colon2 + 1 ) : peer_add.substr( colon2 + 1, end - (colon2 + 1) );

      if( type.empty() ) {
         fc_dlog( logger, "Setting connection ${c} type for: ${peer} to both transactions and blocks", ("c", connection_id)("peer", peer_add) );
         connection_type = both;
      } else if( type == "trx" ) {
         fc_dlog( logger, "Setting connection ${c} type for: ${peer} to transactions only", ("c", connection_id)("peer", peer_add) );
         connection_type = transactions_only;
      } else if( type == "blk" ) {
         fc_dlog( logger, "Setting connection ${c} type for: ${peer} to blocks only", ("c", connection_id)("peer", peer_add) );
         connection_type = blocks_only;
      } else {
         fc_wlog( logger, "Unknown connection ${c} type: ${t}, for ${peer}", ("c", connection_id)("t", type)("peer", peer_add) );
      }
   }

   connection_status connection::get_status()const {
      connection_status stat;
      stat.peer = peer_addr;
      stat.connecting = connecting;
      stat.syncing = syncing;
      stat.is_bp_peer = is_bp_connection;
      std::lock_guard<std::mutex> g( conn_mtx );
      stat.last_handshake = last_handshake_recv;
      return stat;
   }

   // called from connection stand
   bool connection::start_session() {
      verify_strand_in_this_thread( strand, __func__, __LINE__ );

      update_endpoints();
      boost::asio::ip::tcp::no_delay nodelay( true );
      boost::system::error_code ec;
      socket->set_option( nodelay, ec );
      if( ec ) {
         peer_elog( this, "connection failed (set_option): ${e1}", ( "e1", ec.message() ) );
         close();
         return false;
      } else {
         peer_dlog( this, "connected" );
         socket_open = true;
         start_read_message();
         return true;
      }
   }

   bool connection::connected() {
      return socket_is_open() && !connecting;
   }

   bool connection::current() {
      return (connected() && !syncing);
   }

   void connection::flush_queues() {
      buffer_queue.clear_write_queue();
   }

   void connection::close( bool reconnect, bool shutdown ) {
      strand.post( [self = shared_from_this(), reconnect, shutdown]() {
         connection::_close( self.get(), reconnect, shutdown );
      });
   }

   // called from connection strand
   void connection::_close( connection* self, bool reconnect, bool shutdown ) {
      self->socket_open = false;
      boost::system::error_code ec;
      if( self->socket->is_open() ) {
         self->socket->shutdown( tcp::socket::shutdown_both, ec );
         self->socket->close( ec );
      }
      self->socket.reset( new tcp::socket( my_impl->thread_pool.get_executor() ) );
      self->flush_queues();
      self->connecting = false;
      self->syncing = false;
      self->block_status_monitor_.reset();
      ++self->consecutive_immediate_connection_close;
      bool has_last_req = false;
      {
         std::lock_guard<std::mutex> g_conn( self->conn_mtx );
         has_last_req = self->last_req.has_value();
         self->last_handshake_recv = handshake_message();
         self->last_handshake_sent = handshake_message();
         self->last_close = fc::time_point::now();
         self->conn_node_id = fc::sha256();
      }
      if( has_last_req && !shutdown ) {
         my_impl->dispatcher->retry_fetch( self->shared_from_this() );
      }
      self->peer_lib_num = 0;
      self->peer_requested.reset();
      self->sent_handshake_count = 0;
      if( !shutdown) my_impl->sync_master->sync_reset_lib_num( self->shared_from_this(), true );
      peer_ilog( self, "closing" );
      self->cancel_wait();

      if( reconnect && !shutdown ) {
         my_impl->start_conn_timer( std::chrono::milliseconds( 100 ), connection_wptr() );
      }
   }

   // called from connection strand
   void connection::blk_send_branch( const block_id_type& msg_head_id ) {
      uint32_t head_num = my_impl->get_chain_head_num();

      peer_dlog(this, "head_num = ${h}",("h",head_num));
      if(head_num == 0) {
         notice_message note;
         note.known_blocks.mode = normal;
         note.known_blocks.pending = 0;
         enqueue(note);
         return;
      }

      if( logger.is_enabled( fc::log_level::debug ) ) {
         std::unique_lock<std::mutex> g_conn( conn_mtx );
         if( last_handshake_recv.generation >= 1 ) {
            peer_dlog( this, "maybe truncating branch at = ${h}:${id}",
                       ("h", block_header::num_from_id(last_handshake_recv.head_id))("id", last_handshake_recv.head_id) );
         }
      }
      const auto lib_num = peer_lib_num;
      if( lib_num == 0 ) return; // if last_irreversible_block_id is null (we have not received handshake or reset)

      auto msg_head_num = block_header::num_from_id(msg_head_id);
      bool on_fork = msg_head_num == 0;
      bool unknown_block = false;
      if( !on_fork ) {
         try {
            const controller& cc = my_impl->chain_plug->chain();
            block_id_type my_id = cc.get_block_id_for_num( msg_head_num ); // thread-safe
            on_fork = my_id != msg_head_id;
         } catch( const unknown_block_exception& ) {
            unknown_block = true;
         } catch( ... ) {
            on_fork = true;
         }
      }
      if( unknown_block ) {
         peer_ilog( this, "Peer asked for unknown block ${mn}, sending: benign_other go away", ("mn", msg_head_num) );
         no_retry = benign_other;
         enqueue( go_away_message( benign_other ) );
      } else {
         if( on_fork ) msg_head_num = 0;
         // if peer on fork, start at their last lib, otherwise we can start at msg_head+1
         blk_send_branch_impl( msg_head_num, lib_num, head_num );
      }
   }

   // called from connection strand
   void connection::blk_send_branch_impl( uint32_t msg_head_num, uint32_t lib_num, uint32_t head_num ) {
      if( !peer_requested ) {
         auto last = msg_head_num != 0 ? msg_head_num : lib_num;
         peer_requested = peer_sync_state( last+1, head_num, last );
      } else {
         auto last = msg_head_num != 0 ? msg_head_num : std::min( peer_requested->last, lib_num );
         uint32_t end   = std::max( peer_requested->end_block, head_num );
         peer_requested = peer_sync_state( last+1, end, last );
      }
      if( peer_requested->start_block <= peer_requested->end_block ) {
         peer_ilog( this, "enqueue ${s} - ${e}", ("s", peer_requested->start_block)("e", peer_requested->end_block) );
         enqueue_sync_block();
      } else {
         peer_ilog( this, "nothing to enqueue" );
         peer_requested.reset();
      }
   }

   // called from connection strand
   void connection::blk_send( const block_id_type& blkid ) {
      try {
         controller& cc = my_impl->chain_plug->chain();
         signed_block_ptr b = cc.fetch_block_by_id( blkid ); // thread-safe
         if( b ) {
            peer_dlog( this, "fetch_block_by_id num ${n}", ("n", b->block_num()) );
            enqueue_block( b );
         } else {
            peer_ilog( this, "fetch block by id returned null, id ${id}", ("id", blkid) );
         }
      } catch( const assert_exception& ex ) {
         peer_elog( this, "caught assert on fetch_block_by_id, ${ex}, id ${id}", ("ex", ex.to_string())("id", blkid) );
      } catch( ... ) {
         peer_elog( this, "caught other exception fetching block id ${id}", ("id", blkid) );
      }
   }

   void connection::stop_send() {
      syncing = false;
   }

   void connection::send_handshake() {
      strand.post( [c = shared_from_this()]() {
         std::unique_lock<std::mutex> g_conn( c->conn_mtx );
         if( c->populate_handshake( c->last_handshake_sent ) ) {
            static_assert( std::is_same_v<decltype( c->sent_handshake_count ), int16_t>, "INT16_MAX based on int16_t" );
            if( c->sent_handshake_count == INT16_MAX ) c->sent_handshake_count = 1; // do not wrap
            c->last_handshake_sent.generation = ++c->sent_handshake_count;
            auto last_handshake_sent = c->last_handshake_sent;
            g_conn.unlock();
            peer_ilog( c, "Sending handshake generation ${g}, lib ${lib}, head ${head}, id ${id}",
                       ("g", last_handshake_sent.generation)
                       ("lib", last_handshake_sent.last_irreversible_block_num)
                       ("head", last_handshake_sent.head_num)("id", last_handshake_sent.head_id.str().substr(8,16)) );
            c->enqueue( last_handshake_sent );
         }
      });
   }

   // called from connection strand
   void connection::check_heartbeat( tstamp current_time ) {
      if( latest_msg_time > 0 ) {
         if( current_time > latest_msg_time + hb_timeout ) {
            no_retry = benign_other;
            if( !peer_address().empty() ) {
               peer_wlog(this, "heartbeat timed out for peer address");
               close(true);
            } else {
               peer_wlog(this, "heartbeat timed out");
               close(false);
            }
            return;
         } else {
            const tstamp timeout = std::max(hb_timeout/2, 2*std::chrono::milliseconds(config::block_interval_ms).count());
            if ( current_time > latest_blk_time + timeout ) {
               send_handshake();
               return;
            }
         }
      }

      send_time();
   }

   // called from connection strand
   void connection::send_time() {
      time_message xpkt;
      xpkt.org = rec;
      xpkt.rec = dst;
      xpkt.xmt = get_time();
      org = xpkt.xmt;
      enqueue(xpkt);
   }

   // called from connection strand
   void connection::send_time(const time_message& msg) {
      time_message xpkt;
      xpkt.org = msg.xmt;
      xpkt.rec = msg.dst;
      xpkt.xmt = get_time();
      enqueue(xpkt);
   }

   // called from connection strand
   void connection::queue_write(const std::shared_ptr<vector<char>>& buff,
                                std::function<void(boost::system::error_code, std::size_t)> callback,
                                bool to_sync_queue) {
      if( !buffer_queue.add_write_queue( buff, callback, to_sync_queue )) {
         peer_wlog( this, "write_queue full ${s} bytes, giving up on connection", ("s", buffer_queue.write_queue_size()) );
         close();
         return;
      }
      do_queue_write();
   }

   // called from connection strand
   void connection::do_queue_write() {
      if( !buffer_queue.ready_to_send() )
         return;
      connection_ptr c(shared_from_this());

      std::vector<boost::asio::const_buffer> bufs;
      buffer_queue.fill_out_buffer( bufs );

      strand.post( [c{std::move(c)}, bufs{std::move(bufs)}]() {
         boost::asio::async_write( *c->socket, bufs,
            boost::asio::bind_executor( c->strand, [c, socket=c->socket]( boost::system::error_code ec, std::size_t w ) {
            try {
               c->buffer_queue.clear_out_queue();
               // May have closed connection and cleared buffer_queue
               if( !c->socket_is_open() || socket != c->socket ) {
                  peer_ilog( c, "async write socket ${r} before callback", ("r", c->socket_is_open() ? "changed" : "closed") );
                  c->close();
                  return;
               }

               if( ec ) {
                  if( ec.value() != boost::asio::error::eof ) {
                     peer_elog( c, "Error sending to peer: ${i}", ( "i", ec.message() ) );
                  } else {
                     peer_wlog( c, "connection closure detected on write" );
                  }
                  c->close();
                  return;
               }

               c->buffer_queue.out_callback( ec, w );

               c->enqueue_sync_block();
               c->do_queue_write();
            } catch ( const std::bad_alloc& ) {
              throw;
            } catch ( const boost::interprocess::bad_alloc& ) {
              throw;
            } catch( const fc::exception& ex ) {
               peer_elog( c, "fc::exception in do_queue_write: ${s}", ("s", ex.to_string()) );
            } catch( const std::exception& ex ) {
               peer_elog( c, "std::exception in do_queue_write: ${s}", ("s", ex.what()) );
            } catch( ... ) {
               peer_elog( c, "Unknown exception in do_queue_write" );
            }
         }));
      });
   }

   // called from connection strand
   void connection::cancel_sync(go_away_reason reason) {
      peer_dlog( this, "cancel sync reason = ${m}, write queue size ${o} bytes",
                 ("m", reason_str( reason ))("o", buffer_queue.write_queue_size()) );
      cancel_wait();
      flush_queues();
      switch (reason) {
      case validation :
      case fatal_other : {
         no_retry = reason;
         enqueue( go_away_message( reason ));
         break;
      }
      default:
         peer_ilog(this, "sending empty request but not calling sync wait");
         enqueue( ( sync_request_message ) {0,0} );
      }
   }

   // called from connection strand
   bool connection::enqueue_sync_block() {
      if( !peer_requested ) {
         return false;
      } else {
         peer_dlog( this, "enqueue sync block ${num}", ("num", peer_requested->last + 1) );
      }
      uint32_t num = ++peer_requested->last;
      if(num == peer_requested->end_block) {
         peer_requested.reset();
         peer_dlog( this, "completing enqueue_sync_block ${num}", ("num", num) );
      }

      controller& cc = my_impl->chain_plug->chain();
      signed_block_ptr sb;
      try {
         sb = cc.fetch_block_by_number( num ); // thread-safe
      } FC_LOG_AND_DROP();
      if( sb ) {
         enqueue_block( sb, true );
      } else {
         peer_ilog( this, "enqueue sync, unable to fetch block ${num}, sending benign_other go away", ("num", num) );
         peer_requested.reset(); // unable to provide requested blocks
         no_retry = benign_other;
         enqueue( go_away_message( benign_other ) );
      }

      return true;
   }

   //------------------------------------------------------------------------

   using send_buffer_type = std::shared_ptr<std::vector<char>>;

   struct buffer_factory {

      /// caches result for subsequent calls, only provide same net_message instance for each invocation
      const send_buffer_type& get_send_buffer( const net_message& m ) {
         if( !send_buffer ) {
            send_buffer = create_send_buffer( m );
         }
         return send_buffer;
      }

   protected:
      send_buffer_type send_buffer;

   protected:
      static send_buffer_type create_send_buffer( const net_message& m ) {
         const uint32_t payload_size = fc::raw::pack_size( m );

         const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
         const size_t buffer_size = message_header_size + payload_size;

         auto send_buffer = std::make_shared<vector<char>>(buffer_size);
         fc::datastream<char*> ds( send_buffer->data(), buffer_size);
         ds.write( header, message_header_size );
         fc::raw::pack( ds, m );

         return send_buffer;
      }

      template< typename T>
      static send_buffer_type create_send_buffer( uint32_t which, const T& v ) {
         // match net_message static_variant pack
         const uint32_t which_size = fc::raw::pack_size( unsigned_int( which ) );
         const uint32_t payload_size = which_size + fc::raw::pack_size( v );

         const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
         const size_t buffer_size = message_header_size + payload_size;

         auto send_buffer = std::make_shared<vector<char>>( buffer_size );
         fc::datastream<char*> ds( send_buffer->data(), buffer_size );
         ds.write( header, message_header_size );
         fc::raw::pack( ds, unsigned_int( which ) );
         fc::raw::pack( ds, v );

         return send_buffer;
      }

   };

   struct block_buffer_factory : public buffer_factory {

      /// caches result for subsequent calls, only provide same signed_block_ptr instance for each invocation.
      const send_buffer_type& get_send_buffer( const signed_block_ptr& sb ) {
         if( !send_buffer ) {
            send_buffer = create_send_buffer( sb );
         }
         return send_buffer;
      }

   private:

      static std::shared_ptr<std::vector<char>> create_send_buffer( const signed_block_ptr& sb ) {
         static_assert( signed_block_which == fc::get_index<net_message, signed_block>() );
         // this implementation is to avoid copy of signed_block to net_message
         // matches which of net_message for signed_block
         fc_dlog( logger, "sending block ${bn}", ("bn", sb->block_num()) );
         return buffer_factory::create_send_buffer( signed_block_which, *sb );
      }
   };

   struct trx_buffer_factory : public buffer_factory {

      /// caches result for subsequent calls, only provide same packed_transaction_ptr instance for each invocation.
      const send_buffer_type& get_send_buffer( const packed_transaction_ptr& trx ) {
         if( !send_buffer ) {
            send_buffer = create_send_buffer( trx );
         }
         return send_buffer;
      }

   private:

      static std::shared_ptr<std::vector<char>> create_send_buffer( const packed_transaction_ptr& trx ) {
         static_assert( packed_transaction_which == fc::get_index<net_message, packed_transaction>() );
         // this implementation is to avoid copy of packed_transaction to net_message
         // matches which of net_message for packed_transaction
         return buffer_factory::create_send_buffer( packed_transaction_which, *trx );
      }
   };

   //------------------------------------------------------------------------

   // called from connection strand
   void connection::enqueue( const net_message& m ) {
      verify_strand_in_this_thread( strand, __func__, __LINE__ );
      go_away_reason close_after_send = no_reason;
      if (std::holds_alternative<go_away_message>(m)) {
         close_after_send = std::get<go_away_message>(m).reason;
      }

      buffer_factory buff_factory;
      auto send_buffer = buff_factory.get_send_buffer( m );
      enqueue_buffer( send_buffer, close_after_send );
   }

   // called from connection strand
   void connection::enqueue_block( const signed_block_ptr& b, bool to_sync_queue) {
      peer_dlog( this, "enqueue block ${num}", ("num", b->block_num()) );
      verify_strand_in_this_thread( strand, __func__, __LINE__ );

      block_buffer_factory buff_factory;
      auto sb = buff_factory.get_send_buffer( b );
      latest_blk_time = get_time();
      enqueue_buffer( sb, no_reason, to_sync_queue);
   }

   // called from connection strand
   void connection::enqueue_buffer( const std::shared_ptr<std::vector<char>>& send_buffer,
                                    go_away_reason close_after_send,
                                    bool to_sync_queue)
   {
      connection_ptr self = shared_from_this();
      queue_write(send_buffer,
            [conn{std::move(self)}, close_after_send](boost::system::error_code ec, std::size_t ) {
                        if (ec) return;
                        if (close_after_send != no_reason) {
                           fc_ilog( logger, "sent a go away message: ${r}, closing connection ${cid}",
                                    ("r", reason_str(close_after_send))("cid", conn->connection_id) );
                           conn->close();
                           return;
                        }
                  },
                  to_sync_queue);
   }

   // thread safe
   void connection::cancel_wait() {
      std::lock_guard<std::mutex> g( response_expected_timer_mtx );
      response_expected_timer.cancel();
   }

   // thread safe
   void connection::sync_wait() {
      connection_ptr c(shared_from_this());
      std::lock_guard<std::mutex> g( response_expected_timer_mtx );
      response_expected_timer.expires_from_now( my_impl->resp_expected_period );
      response_expected_timer.async_wait(
            boost::asio::bind_executor( c->strand, [c]( boost::system::error_code ec ) {
               c->sync_timeout( ec );
            } ) );
   }

   // thread safe
   void connection::fetch_wait() {
      connection_ptr c( shared_from_this() );
      std::lock_guard<std::mutex> g( response_expected_timer_mtx );
      response_expected_timer.expires_from_now( my_impl->resp_expected_period );
      response_expected_timer.async_wait(
            boost::asio::bind_executor( c->strand, [c]( boost::system::error_code ec ) {
               c->fetch_timeout(ec);
            } ) );
   }

   // called from connection strand
   void connection::sync_timeout( boost::system::error_code ec ) {
      if( !ec ) {
         my_impl->sync_master->sync_reassign_fetch( shared_from_this(), benign_other );
         close(true);
      } else if( ec != boost::asio::error::operation_aborted ) { // don't log on operation_aborted, called on destroy
         peer_elog( this, "setting timer for sync request got error ${ec}", ("ec", ec.message()) );
      }
   }

   // called from connection strand
   void connection::fetch_timeout( boost::system::error_code ec ) {
      if( !ec ) {
         my_impl->dispatcher->retry_fetch( shared_from_this() );
      } else if( ec != boost::asio::error::operation_aborted ) { // don't log on operation_aborted, called on destroy
         peer_elog( this, "setting timer for fetch request got error ${ec}", ("ec", ec.message() ) );
      }
   }

   // called from connection strand
   void connection::request_sync_blocks(uint32_t start, uint32_t end) {
      sync_request_message srm = {start,end};
      enqueue( net_message(srm) );
      sync_wait();
   }

   //-----------------------------------------------------------
   void block_status_monitor::reset() {
      in_accepted_state_ = true;
      events_ = 0;
   }

   void block_status_monitor::rejected() {
      const auto now = fc::time_point::now();

      // in rejected state
      if(!in_accepted_state_) {
         const auto elapsed = now - window_start_;
         if( elapsed < window_size_ ) {
            return;
         }
         ++events_;
         window_start_ = now;
         return;
      }

      // switching to rejected state
      in_accepted_state_ = false;
      window_start_ = now;
      events_ = 0;
   }
   //-----------------------------------------------------------

    sync_manager::sync_manager( uint32_t req_span )
      :sync_known_lib_num( 0 )
      ,sync_last_requested_num( 0 )
      ,sync_next_expected_num( 1 )
      ,sync_req_span( req_span )
      ,sync_source()
      ,sync_state(in_sync)
   {
   }

   constexpr auto sync_manager::stage_str(stages s) {
    switch (s) {
    case in_sync : return "in sync";
    case lib_catchup: return "lib catchup";
    case head_catchup : return "head catchup";
    default : return "unkown";
    }
  }

   bool sync_manager::set_state(stages newstate) {
      if( sync_state == newstate ) {
         return false;
      }
      fc_ilog( logger, "old state ${os} becoming ${ns}", ("os", stage_str( sync_state ))( "ns", stage_str( newstate ) ) );
      sync_state = newstate;
      return true;
   }

   // called from c's connection strand
   void sync_manager::sync_reset_lib_num(const connection_ptr& c, bool closing) {
      std::unique_lock<std::mutex> g( sync_mtx );
      if( sync_state == in_sync ) {
         sync_source.reset();
      }
      if( !c ) return;
      if( !closing ) {
         if( c->peer_lib_num > sync_known_lib_num ) {
            sync_known_lib_num = c->peer_lib_num;
         }
      } else {
         // Closing connection, therefore its view of LIB can no longer be considered as we will no longer be connected.
         // Determine current LIB of remaining peers as our sync_known_lib_num.
         uint32_t highest_lib_num = 0;
         for_each_block_connection( [&highest_lib_num]( const auto& cc ) {
            std::lock_guard<std::mutex> g_conn( cc->conn_mtx );
            if( cc->current() && cc->last_handshake_recv.last_irreversible_block_num > highest_lib_num ) {
               highest_lib_num = cc->last_handshake_recv.last_irreversible_block_num;
            }
            return true;
         } );
         sync_known_lib_num = highest_lib_num;

         // if closing the connection we are currently syncing from, then reset our last requested and next expected.
         if( c == sync_source ) {
            reset_last_requested_num(g);
            // if starting to sync need to always start from lib as we might be on our own fork
            uint32_t lib_num = my_impl->get_chain_lib_num();
            sync_next_expected_num = lib_num + 1;
            request_next_chunk( std::move(g) );
         }
      }
   }

   // call with g_sync locked, called from conn's connection strand
   void sync_manager::request_next_chunk( std::unique_lock<std::mutex> g_sync, const connection_ptr& conn ) {
      auto chain_info = my_impl->get_chain_info();

      fc_dlog( logger, "sync_last_requested_num: ${r}, sync_next_expected_num: ${e}, sync_known_lib_num: ${k}, sync_req_span: ${s}",
               ("r", sync_last_requested_num)("e", sync_next_expected_num)("k", sync_known_lib_num)("s", sync_req_span) );

      if( chain_info.head_num < sync_last_requested_num && sync_source && sync_source->current() ) {
         fc_ilog( logger, "ignoring request, head is ${h} last req = ${r}, sync_next_expected_num: ${e}, sync_known_lib_num: ${k}, sync_req_span: ${s}, source connection ${c}",
                  ("h", chain_info.head_num)("r", sync_last_requested_num)("e", sync_next_expected_num)
                  ("k", sync_known_lib_num)("s", sync_req_span)("c", sync_source->connection_id) );
         return;
      }

      /* ----------
       * next chunk provider selection criteria
       * a provider is supplied and able to be used, use it.
       * otherwise select the next available from the list, round-robin style.
       */

      connection_ptr new_sync_source = sync_source;
      if (conn && conn->current() ) {
         new_sync_source = conn;
      } else {
         std::shared_lock<std::shared_mutex> g( my_impl->connections_mtx );
         if( my_impl->connections.size() == 0 ) {
            new_sync_source.reset();
         } else if( my_impl->connections.size() == 1 ) {
            if (!new_sync_source) {
               new_sync_source = *my_impl->connections.begin();
            }
         } else {
            // init to a linear array search
            auto cptr = my_impl->connections.begin();
            auto cend = my_impl->connections.end();
            // do we remember the previous source?
            if (new_sync_source) {
               //try to find it in the list
               cptr = my_impl->connections.find( new_sync_source );
               cend = cptr;
               if( cptr == my_impl->connections.end() ) {
                  //not there - must have been closed! cend is now connections.end, so just flatten the ring.
                  new_sync_source.reset();
                  cptr = my_impl->connections.begin();
               } else {
                  //was found - advance the start to the next. cend is the old source.
                  if( ++cptr == my_impl->connections.end() && cend != my_impl->connections.end() ) {
                     cptr = my_impl->connections.begin();
                  }
               }
            }

            //scan the list of peers looking for another able to provide sync blocks.
            if( cptr != my_impl->connections.end() ) {
               auto cstart_it = cptr;
               do {
                  //select the first one which is current and has valid lib and break out.
                  if( !(*cptr)->is_transactions_only_connection() && (*cptr)->current() ) {
                     std::lock_guard<std::mutex> g_conn( (*cptr)->conn_mtx );
                     if( (*cptr)->last_handshake_recv.last_irreversible_block_num >= sync_known_lib_num ) {
                        new_sync_source = *cptr;
                        break;
                     }
                  }
                  if( ++cptr == my_impl->connections.end() )
                     cptr = my_impl->connections.begin();
               } while( cptr != cstart_it );
            }
            // no need to check the result, either source advanced or the whole list was checked and the old source is reused.
         }
      }

      // verify there is an available source
      if( !new_sync_source || !new_sync_source->current() || new_sync_source->is_transactions_only_connection() ) {
         fc_elog( logger, "Unable to continue syncing at this time");
         if( !new_sync_source ) sync_source.reset();
         sync_known_lib_num = chain_info.lib_num;
         reset_last_requested_num(g_sync);
         set_state( in_sync ); // probably not, but we can't do anything else
         return;
      }

      bool request_sent = false;
      if( sync_last_requested_num != sync_known_lib_num ) {
         uint32_t start = sync_next_expected_num;
         uint32_t end = start + sync_req_span - 1;
         if( end > sync_known_lib_num )
            end = sync_known_lib_num;
         if( end > 0 && end >= start ) {
            sync_last_requested_num = end;
            sync_source = new_sync_source;
            g_sync.unlock();
            request_sent = true;
            new_sync_source->strand.post( [new_sync_source, start, end]() {
               peer_ilog( new_sync_source, "requesting range ${s} to ${e}", ("s", start)("e", end) );
               new_sync_source->request_sync_blocks( start, end );
            } );
         }
      }
      if( !request_sent ) {
         g_sync.unlock();
         send_handshakes();
      }
   }

   // static, thread safe
   void sync_manager::send_handshakes() {
      for_each_connection( []( auto& ci ) {
         if( ci->current() ) {
            ci->send_handshake();
         }
         return true;
      } );
   }

   bool sync_manager::is_sync_required( uint32_t fork_head_block_num ) {
      fc_dlog( logger, "last req = ${req}, last recv = ${recv} known = ${known} our head = ${head}",
               ("req", sync_last_requested_num)( "recv", sync_next_expected_num )( "known", sync_known_lib_num )
               ("head", fork_head_block_num ) );

      return( sync_last_requested_num < sync_known_lib_num ||
              fork_head_block_num < sync_last_requested_num );
   }

   // called from c's connection strand
   void sync_manager::start_sync(const connection_ptr& c, uint32_t target) {
      std::unique_lock<std::mutex> g_sync( sync_mtx );
      if( target > sync_known_lib_num) {
         sync_known_lib_num = target;
      }

      auto chain_info = my_impl->get_chain_info();
      if( !is_sync_required( chain_info.head_num ) || target <= chain_info.lib_num ) {
         peer_dlog( c, "We are already caught up, my irr = ${b}, head = ${h}, target = ${t}",
                  ("b", chain_info.lib_num)( "h", chain_info.head_num )( "t", target ) );
         c->send_handshake();
         return;
      }

      if( sync_state == in_sync ) {
         set_state( lib_catchup );
      }
      sync_next_expected_num = std::max( chain_info.lib_num + 1, sync_next_expected_num );

      // p2p_high_latency_test.py test depends on this exact log statement.
      peer_ilog( c, "Catching up with chain, our last req is ${cc}, theirs is ${t}, next expected ${n}",
                 ("cc", sync_last_requested_num)("t", target)("n", sync_next_expected_num) );

      request_next_chunk( std::move( g_sync ), c );
   }

   // called from connection strand
   void sync_manager::sync_reassign_fetch(const connection_ptr& c, go_away_reason reason) {
      std::unique_lock<std::mutex> g( sync_mtx );
      peer_ilog( c, "reassign_fetch, our last req is ${cc}, next expected is ${ne}",
               ("cc", sync_last_requested_num)("ne", sync_next_expected_num) );

      if( c == sync_source ) {
         c->cancel_sync(reason);
         reset_last_requested_num(g);
         request_next_chunk( std::move(g) );
      }
   }

   // called from c's connection strand
   void sync_manager::recv_handshake( const connection_ptr& c, const handshake_message& msg ) {

      if( c->is_transactions_only_connection() ) return;

      auto chain_info = my_impl->get_chain_info();

      sync_reset_lib_num(c, false);

      auto current_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      int64_t network_latency_ns = current_time_ns - msg.time; // net latency in nanoseconds
      if( network_latency_ns < 0 ) {
         peer_wlog(c, "Peer sent a handshake with a timestamp skewed by at least ${t}ms", ("t", network_latency_ns/1000000));
         network_latency_ns = 0;
      }
      // number of blocks syncing node is behind from a peer node, round up
      uint32_t nblk_behind_by_net_latency = std::lround( static_cast<double>(network_latency_ns) / static_cast<double>(block_interval_ns) );
      // 2x for time it takes for message to reach back to peer node
      uint32_t nblk_combined_latency = 2 * nblk_behind_by_net_latency;
      // message in the log below is used in p2p_high_latency_test.py test
      peer_dlog(c, "Network latency is ${lat}ms, ${num} blocks discrepancy by network latency, ${tot_num} blocks discrepancy expected once message received",
                ("lat", network_latency_ns/1000000)("num", nblk_behind_by_net_latency)("tot_num", nblk_combined_latency));

      //--------------------------------
      // sync need checks; (lib == last irreversible block)
      //
      // 0. my head block id == peer head id means we are all caught up block wise
      // 1. my head block num < peer lib - start sync locally
      // 2. my lib > peer head num + nblk_combined_latency - send last_irr_catch_up notice if not the first generation
      //
      // 3  my head block num + nblk_combined_latency < peer head block num - update sync state and send a catchup request
      // 4  my head block num >= peer block num + nblk_combined_latency send a notice catchup if this is not the first generation
      //    4.1 if peer appears to be on a different fork ( our_id_for( msg.head_num ) != msg.head_id )
      //        then request peer's blocks
      //
      //-----------------------------

      if (chain_info.head_id == msg.head_id) {
         peer_ilog( c, "handshake lib ${lib}, head ${head}, head id ${id}.. sync 0, lib ${l}",
                    ("lib", msg.last_irreversible_block_num)("head", msg.head_num)("id", msg.head_id.str().substr(8,16))("l", chain_info.lib_num) );
         c->syncing = false;
         notice_message note;
         note.known_blocks.mode = none;
         note.known_trx.mode = catch_up;
         note.known_trx.pending = 0;
         c->enqueue( note );
         return;
      }
      if (chain_info.head_num < msg.last_irreversible_block_num) {
         peer_ilog( c, "handshake lib ${lib}, head ${head}, head id ${id}.. sync 1, head ${h}, lib ${l}",
                    ("lib", msg.last_irreversible_block_num)("head", msg.head_num)("id", msg.head_id.str().substr(8,16))
                    ("h", chain_info.head_num)("l", chain_info.lib_num) );
         c->syncing = false;
         if (c->sent_handshake_count > 0) {
            c->send_handshake();
         }
         return;
      }
      if (chain_info.lib_num > msg.head_num + nblk_combined_latency) {
         peer_ilog( c, "handshake lib ${lib}, head ${head}, head id ${id}.. sync 2, head ${h}, lib ${l}",
                    ("lib", msg.last_irreversible_block_num)("head", msg.head_num)("id", msg.head_id.str().substr(8,16))
                    ("h", chain_info.head_num)("l", chain_info.lib_num) );
         if (msg.generation > 1 || c->protocol_version > proto_base) {
            notice_message note;
            note.known_trx.pending = chain_info.lib_num;
            note.known_trx.mode = last_irr_catch_up;
            note.known_blocks.mode = last_irr_catch_up;
            note.known_blocks.pending = chain_info.head_num;
            c->enqueue( note );
         }
         c->syncing = true;
         return;
      }

      if (chain_info.head_num + nblk_combined_latency < msg.head_num ) {
         peer_ilog( c, "handshake lib ${lib}, head ${head}, head id ${id}.. sync 3, head ${h}, lib ${l}",
                    ("lib", msg.last_irreversible_block_num)("head", msg.head_num)("id", msg.head_id.str().substr(8,16))
                    ("h", chain_info.head_num)("l", chain_info.lib_num) );
         c->syncing = false;
         verify_catchup(c, msg.head_num, msg.head_id);
         return;
      } else if(chain_info.head_num >= msg.head_num + nblk_combined_latency) {
         peer_ilog( c, "handshake lib ${lib}, head ${head}, head id ${id}.. sync 4, head ${h}, lib ${l}",
                    ("lib", msg.last_irreversible_block_num)("head", msg.head_num)("id", msg.head_id.str().substr(8,16))
                    ("h", chain_info.head_num)("l", chain_info.lib_num) );
         if (msg.generation > 1 ||  c->protocol_version > proto_base) {
            notice_message note;
            note.known_trx.mode = none;
            note.known_blocks.mode = catch_up;
            note.known_blocks.pending = chain_info.head_num;
            note.known_blocks.ids.push_back(chain_info.head_id);
            c->enqueue( note );
         }
         c->syncing = false;
         bool on_fork = true;
         try {
            controller& cc = my_impl->chain_plug->chain();
            on_fork = cc.get_block_id_for_num( msg.head_num ) != msg.head_id; // thread-safe
         } catch( ... ) {}
         if( on_fork ) {
            request_message req;
            req.req_blocks.mode = catch_up;
            req.req_trx.mode = none;
            c->enqueue( req );
         }
         return;
      } else {
         peer_dlog( c, "Block discrepancy is within network latency range.");
      }
   }

   // called from c's connection strand
   bool sync_manager::verify_catchup(const connection_ptr& c, uint32_t num, const block_id_type& id) {
      request_message req;
      req.req_blocks.mode = catch_up;
      for_each_block_connection( [num, &id, &req]( const auto& cc ) {
         std::lock_guard<std::mutex> g_conn( cc->conn_mtx );
         if( cc->fork_head_num > num || cc->fork_head == id ) {
            req.req_blocks.mode = none;
            return false;
         }
         return true;
      } );
      if( req.req_blocks.mode == catch_up ) {
         {
            std::lock_guard<std::mutex> g( sync_mtx );
            peer_ilog( c, "catch_up while in ${s}, fork head num = ${fhn} "
                          "target LIB = ${lib} next_expected = ${ne}, id ${id}...",
                     ("s", stage_str( sync_state ))("fhn", num)("lib", sync_known_lib_num)
                     ("ne", sync_next_expected_num)("id", id.str().substr( 8, 16 )) );
         }
         auto chain_info = my_impl->get_chain_info();
         if( sync_state == lib_catchup || num < chain_info.lib_num )
            return false;
         set_state( head_catchup );
         {
            std::lock_guard<std::mutex> g_conn( c->conn_mtx );
            c->fork_head = id;
            c->fork_head_num = num;
         }

         req.req_blocks.ids.emplace_back( chain_info.head_id );
      } else {
         peer_ilog( c, "none notice while in ${s}, fork head num = ${fhn}, id ${id}...",
                  ("s", stage_str( sync_state ))("fhn", num)("id", id.str().substr(8,16)) );
         std::lock_guard<std::mutex> g_conn( c->conn_mtx );
         c->fork_head = block_id_type();
         c->fork_head_num = 0;
      }
      req.req_trx.mode = none;
      c->enqueue( req );
      return true;
   }

   // called from c's connection strand
   void sync_manager::sync_recv_notice( const connection_ptr& c, const notice_message& msg) {
      peer_dlog( c, "sync_manager got ${m} block notice", ("m", modes_str( msg.known_blocks.mode )) );
      EOS_ASSERT( msg.known_blocks.mode == catch_up || msg.known_blocks.mode == last_irr_catch_up, plugin_exception,
                  "sync_recv_notice only called on catch_up" );
      if (msg.known_blocks.mode == catch_up) {
         if (msg.known_blocks.ids.size() == 0) {
            peer_elog( c, "got a catch up with ids size = 0" );
         } else {
            const block_id_type& id = msg.known_blocks.ids.back();
            peer_ilog( c, "notice_message, pending ${p}, blk_num ${n}, id ${id}...",
                     ("p", msg.known_blocks.pending)("n", block_header::num_from_id(id))("id",id.str().substr(8,16)) );
            if( !my_impl->dispatcher->have_block( id ) ) {
               verify_catchup( c, msg.known_blocks.pending, id );
            } else {
               // we already have the block, so update peer with our view of the world
               c->send_handshake();
            }
         }
      } else if (msg.known_blocks.mode == last_irr_catch_up) {
         {
            c->peer_lib_num = msg.known_trx.pending;
            std::lock_guard<std::mutex> g_conn( c->conn_mtx );
            c->last_handshake_recv.last_irreversible_block_num = msg.known_trx.pending;
         }
         sync_reset_lib_num(c, false);
         start_sync(c, msg.known_trx.pending);
      }
   }

   // called from connection strand
   void sync_manager::rejected_block( const connection_ptr& c, uint32_t blk_num ) {
      c->block_status_monitor_.rejected();
      std::unique_lock<std::mutex> g( sync_mtx );
      reset_last_requested_num(g);
      if( c->block_status_monitor_.max_events_violated()) {
         peer_wlog( c, "block ${bn} not accepted, closing connection", ("bn", blk_num) );
         sync_source.reset();
         g.unlock();
         c->close();
      } else {
         g.unlock();
         c->send_handshake();
      }
   }

   // called from connection strand
   void sync_manager::sync_update_expected( const connection_ptr& c, const block_id_type& blk_id, uint32_t blk_num, bool blk_applied ) {
      std::unique_lock<std::mutex> g_sync( sync_mtx );
      if( blk_num <= sync_last_requested_num ) {
         peer_dlog( c, "sync_last_requested_num: ${r}, sync_next_expected_num: ${e}, sync_known_lib_num: ${k}, sync_req_span: ${s}",
                  ("r", sync_last_requested_num)("e", sync_next_expected_num)("k", sync_known_lib_num)("s", sync_req_span) );
         if (blk_num != sync_next_expected_num && !blk_applied) {
            auto sync_next_expected = sync_next_expected_num;
            g_sync.unlock();
            peer_dlog( c, "expected block ${ne} but got ${bn}", ("ne", sync_next_expected)("bn", blk_num) );
            return;
         }
         sync_next_expected_num = blk_num + 1;
      }
   }

   // called from c's connection strand
   void sync_manager::sync_recv_block(const connection_ptr& c, const block_id_type& blk_id, uint32_t blk_num, bool blk_applied) {
      peer_dlog( c, "got block ${bn}", ("bn", blk_num) );
      if( app().is_quiting() ) {
         c->close( false, true );
         return;
      }
      c->block_status_monitor_.accepted();
      sync_update_expected( c, blk_id, blk_num, blk_applied );
      std::unique_lock<std::mutex> g_sync( sync_mtx );
      stages state = sync_state;
      peer_dlog( c, "state ${s}", ("s", stage_str( state )) );
      if( state == head_catchup ) {
         peer_dlog( c, "sync_manager in head_catchup state" );
         sync_source.reset();
         g_sync.unlock();

         block_id_type null_id;
         bool set_state_to_head_catchup = false;
         for_each_block_connection( [&null_id, blk_num, &blk_id, &c, &set_state_to_head_catchup]( const auto& cp ) {
            std::unique_lock<std::mutex> g_cp_conn( cp->conn_mtx );
            uint32_t fork_head_num = cp->fork_head_num;
            block_id_type fork_head_id = cp->fork_head;
            g_cp_conn.unlock();
            if( fork_head_id == null_id ) {
               // continue
            } else if( fork_head_num < blk_num || fork_head_id == blk_id ) {
               std::lock_guard<std::mutex> g_conn( c->conn_mtx );
               c->fork_head = null_id;
               c->fork_head_num = 0;
            } else {
               set_state_to_head_catchup = true;
            }
            return true;
         } );

         if( set_state_to_head_catchup ) {
            if( set_state( head_catchup ) ) {
                send_handshakes();
            }
         } else {
            set_state( in_sync );
            send_handshakes();
         }
      } else if( state == lib_catchup ) {
         if( blk_num >= sync_known_lib_num ) {
            peer_dlog( c, "All caught up with last known last irreversible block resending handshake" );
            set_state( in_sync );
            g_sync.unlock();
            send_handshakes();
         } else if( blk_num >= sync_last_requested_num ) {
            request_next_chunk( std::move( g_sync) );
         } else {
            g_sync.unlock();
            peer_dlog( c, "calling sync_wait" );
            c->sync_wait();
         }
      }
   }

   //------------------------------------------------------------------------

   // thread safe
   bool dispatch_manager::add_peer_block( const block_id_type& blkid, uint32_t connection_id) {
      uint32_t block_num = block_header::num_from_id(blkid);
      std::lock_guard<std::mutex> g( blk_state_mtx );
      auto bptr = blk_state.get<by_connection_id>().find( std::make_tuple(block_num, std::ref(blkid), connection_id) );
      bool added = (bptr == blk_state.end());
      if( added ) {
         blk_state.insert( {blkid, connection_id} );
      }
      return added;
   }

   bool dispatch_manager::peer_has_block( const block_id_type& blkid, uint32_t connection_id ) const {
      uint32_t block_num = block_header::num_from_id(blkid);
      std::lock_guard<std::mutex> g(blk_state_mtx);
      const auto blk_itr = blk_state.get<by_connection_id>().find( std::make_tuple(block_num, std::ref(blkid), connection_id) );
      return blk_itr != blk_state.end();
   }

   bool dispatch_manager::have_block( const block_id_type& blkid ) const {
      uint32_t block_num = block_header::num_from_id(blkid);
      std::lock_guard<std::mutex> g(blk_state_mtx);
      const auto& index = blk_state.get<by_connection_id>();
      auto blk_itr = index.find( std::make_tuple(block_num, std::ref(blkid)) );
      return blk_itr != index.end();
   }

   void dispatch_manager::rm_block( const block_id_type& blkid ) {
      uint32_t block_num = block_header::num_from_id(blkid);
      fc_dlog( logger, "rm_block ${n}, id: ${id}", ("n", block_num)("id", blkid));
      std::lock_guard<std::mutex> g(blk_state_mtx);
      auto& index = blk_state.get<by_connection_id>();
      auto p = index.equal_range( std::make_tuple(block_num, std::ref(blkid)) );
      index.erase(p.first, p.second);
   }

   bool dispatch_manager::add_peer_txn( const transaction_id_type id, const time_point_sec& trx_expires,
                                        uint32_t connection_id, const time_point_sec& now ) {
      std::lock_guard<std::mutex> g( local_txns_mtx );
      auto tptr = local_txns.get<by_id>().find( std::make_tuple( std::ref( id ), connection_id ) );
      bool added = (tptr == local_txns.end());
      if( added ) {
         // expire at either transaction expiration or configured max expire time whichever is less
         time_point_sec expires = now + my_impl->p2p_dedup_cache_expire_time_us;
         expires = std::min( trx_expires, expires );
         local_txns.insert( node_transaction_state{
            .id = id,
            .expires = expires,
            .connection_id = connection_id} );
      }
      return added;
   }

   bool dispatch_manager::have_txn( const transaction_id_type& tid ) const {
      std::lock_guard<std::mutex> g( local_txns_mtx );
      const auto tptr = local_txns.get<by_id>().find( tid );
      return tptr != local_txns.end();
   }

   void dispatch_manager::expire_txns() {
      size_t start_size = 0, end_size = 0;

      std::unique_lock<std::mutex> g( local_txns_mtx );
      start_size = local_txns.size();
      auto& old = local_txns.get<by_expiry>();
      auto ex_lo = old.lower_bound( fc::time_point_sec( 0 ) );
      auto ex_up = old.upper_bound( time_point::now() );
      old.erase( ex_lo, ex_up );
      g.unlock();

      fc_dlog( logger, "expire_local_txns size ${s} removed ${r}", ("s", start_size)( "r", start_size - end_size ) );
   }

   void dispatch_manager::expire_blocks( uint32_t lib_num ) {
      std::lock_guard<std::mutex> g(blk_state_mtx);
      auto& stale_blk = blk_state.get<by_connection_id>();
      stale_blk.erase( stale_blk.lower_bound(1), stale_blk.upper_bound(lib_num) );
   }

   // thread safe
   void dispatch_manager::bcast_block(const signed_block_ptr& b, const block_id_type& id) {
      fc_dlog( logger, "bcast block ${b}", ("b", b->block_num()) );

      if( my_impl->sync_master->syncing_with_peer() ) return;

      block_buffer_factory buff_factory;
      const auto bnum = b->block_num();
      for_each_block_connection( [this, &id, &bnum, &b, &buff_factory]( auto& cp ) {
         fc_dlog( logger, "socket_is_open ${s}, connecting ${c}, syncing ${ss}, connection ${cid}",
                  ("s", cp->socket_is_open())("c", cp->connecting.load())("ss", cp->syncing.load())("cid", cp->connection_id) );
         if( !cp->current() ) return true;

         if( !add_peer_block( id, cp->connection_id ) ) {
            fc_dlog( logger, "not bcast block ${b} to connection ${cid}", ("b", bnum)("cid", cp->connection_id) );
            return true;
         }

         send_buffer_type sb = buff_factory.get_send_buffer( b );

         cp->strand.post( [cp, bnum, sb{std::move(sb)}]() {
            cp->latest_blk_time = cp->get_time();
            bool has_block = cp->peer_lib_num >= bnum;
            if( !has_block ) {
               peer_dlog( cp, "bcast block ${b}", ("b", bnum) );
               cp->enqueue_buffer( sb, no_reason );
            }
         });
         return true;
      } );
   }

   // called from c's connection strand
   void dispatch_manager::recv_block(const connection_ptr& c, const block_id_type& id, uint32_t bnum) {
      std::unique_lock<std::mutex> g( c->conn_mtx );
      if (c &&
          c->last_req &&
          c->last_req->req_blocks.mode != none &&
          !c->last_req->req_blocks.ids.empty() &&
          c->last_req->req_blocks.ids.back() == id) {
         peer_dlog( c, "resetting last_req" );
         c->last_req.reset();
      }
      g.unlock();

      peer_dlog(c, "canceling wait");
      c->cancel_wait();
   }

   void dispatch_manager::rejected_block(const block_id_type& id) {
      fc_dlog( logger, "rejected block ${id}", ("id", id) );
   }

   // called from any thread
   void dispatch_manager::bcast_transaction(const packed_transaction_ptr& trx) {
      trx_buffer_factory buff_factory;
      const auto now = fc::time_point::now();
      for_each_connection( [this, &trx, &now, &buff_factory]( auto& cp ) {
         if( cp->is_blocks_only_connection() || !cp->current() ) {
            return true;
         }
         if( !add_peer_txn(trx->id(), trx->expiration(), cp->connection_id, now) ) {
            return true;
         }

         send_buffer_type sb = buff_factory.get_send_buffer( trx );
         fc_dlog( logger, "sending trx: ${id}, to connection ${cid}", ("id", trx->id())("cid", cp->connection_id) );
         cp->strand.post( [cp, sb{std::move(sb)}]() {
            cp->enqueue_buffer( sb, no_reason );
         } );
         return true;
      } );
   }

   // called from any thread
   void dispatch_manager::rejected_transaction(const packed_transaction_ptr& trx) {
      fc_dlog( logger, "not sending rejected transaction ${tid}", ("tid", trx->id()) );
      // keep rejected transaction around for awhile so we don't broadcast it, don't remove from local_txns
   }

   // called from c's connection strand
   void dispatch_manager::recv_notice(const connection_ptr& c, const notice_message& msg, bool generated) {
      if (msg.known_trx.mode == normal) {
      } else if (msg.known_trx.mode != none) {
         peer_elog( c, "passed a notice_message with something other than a normal on none known_trx" );
         return;
      }
      if (msg.known_blocks.mode == normal) {
         // known_blocks.ids is never > 1
         if( !msg.known_blocks.ids.empty() ) {
            if( msg.known_blocks.pending == 1 ) { // block id notify of 2.0.0, ignore
               return;
            }
         }
      } else if (msg.known_blocks.mode != none) {
         peer_elog( c, "passed a notice_message with something other than a normal on none known_blocks" );
         return;
      }
   }

   // called from c's connection strand
   void dispatch_manager::retry_fetch(const connection_ptr& c) {
      peer_dlog( c, "retry fetch" );
      request_message last_req;
      block_id_type bid;
      {
         std::lock_guard<std::mutex> g_c_conn( c->conn_mtx );
         if( !c->last_req ) {
            return;
         }
         peer_wlog( c, "failed to fetch from peer" );
         if( c->last_req->req_blocks.mode == normal && !c->last_req->req_blocks.ids.empty() ) {
            bid = c->last_req->req_blocks.ids.back();
         } else {
            peer_wlog( c, "no retry, block mpde = ${b} trx mode = ${t}",
                       ("b", modes_str( c->last_req->req_blocks.mode ))( "t", modes_str( c->last_req->req_trx.mode ) ) );
            return;
         }
         last_req = *c->last_req;
      }
      for_each_block_connection( [this, &c, &last_req, &bid]( auto& conn ) {
         if( conn == c )
            return true;

         {
            std::lock_guard<std::mutex> guard( conn->conn_mtx );
            if( conn->last_req ) {
               return true;
            }
         }

         bool sendit = peer_has_block( bid, conn->connection_id );
         if( sendit ) {
            conn->strand.post( [conn, last_req{std::move(last_req)}]() {
               conn->enqueue( last_req );
               conn->fetch_wait();
               std::lock_guard<std::mutex> g_conn_conn( conn->conn_mtx );
               conn->last_req = last_req;
            } );
            return false;
         }
         return true;
      } );

      // at this point no other peer has it, re-request or do nothing?
      peer_wlog( c, "no peer has last_req" );
      if( c->connected() ) {
         c->enqueue( last_req );
         c->fetch_wait();
      }
   }

   //------------------------------------------------------------------------

   // called from any thread
   bool connection::resolve_and_connect() {
      switch ( no_retry ) {
         case no_reason:
         case wrong_version:
         case benign_other:
         case duplicate: // attempt reconnect in case connection has been dropped, should quickly disconnect if duplicate
            break;
         default:
            fc_dlog( logger, "Skipping connect due to go_away reason ${r}",("r", reason_str( no_retry )));
            return false;
      }

      string::size_type colon = peer_address().find(':');
      if (colon == std::string::npos || colon == 0) {
         fc_elog( logger, "Invalid peer address. must be \"host:port[:<blk>|<trx>]\": ${p}", ("p", peer_address()) );
         return false;
      }

      connection_ptr c = shared_from_this();

      if( consecutive_immediate_connection_close > def_max_consecutive_immediate_connection_close || no_retry == benign_other ) {
         auto connector_period_us = std::chrono::duration_cast<std::chrono::microseconds>( my_impl->connector_period );
         std::lock_guard<std::mutex> g( c->conn_mtx );
         if( last_close == fc::time_point() || last_close > fc::time_point::now() - fc::microseconds( connector_period_us.count() ) ) {
            return true; // true so doesn't remove from valid connections
         }
      }

      strand.post([c]() {
         string::size_type colon = c->peer_address().find(':');
         string::size_type colon2 = c->peer_address().find(':', colon + 1);
         string host = c->peer_address().substr( 0, colon );
         string port = c->peer_address().substr( colon + 1, colon2 == string::npos ? string::npos : colon2 - (colon + 1));
         c->set_connection_type( c->peer_address() );

         auto resolver = std::make_shared<tcp::resolver>( my_impl->thread_pool.get_executor() );
         connection_wptr weak_conn = c;
         // Note: need to add support for IPv6 too
         resolver->async_resolve( tcp::v4(), host, port, boost::asio::bind_executor( c->strand,
            [resolver, weak_conn, host, port]( const boost::system::error_code& err, tcp::resolver::results_type endpoints ) {
               auto c = weak_conn.lock();
               if( !c ) return;
               if( !err ) {
                  c->connect( resolver, endpoints );
               } else {
                  fc_elog( logger, "Unable to resolve ${host}:${port} ${error}",
                           ("host", host)("port", port)( "error", err.message() ) );
                  c->connecting = false;
                  ++c->consecutive_immediate_connection_close;
               }
         } ) );
      } );
      return true;
   }

   // called from connection strand
   void connection::connect( const std::shared_ptr<tcp::resolver>& resolver, tcp::resolver::results_type endpoints ) {
      connecting = true;
      pending_message_buffer.reset();
      buffer_queue.clear_out_queue();
      boost::asio::async_connect( *socket, endpoints,
         boost::asio::bind_executor( strand,
               [resolver, c = shared_from_this(), socket=socket]( const boost::system::error_code& err, const tcp::endpoint& endpoint ) {
            if( !err && socket->is_open() && socket == c->socket ) {
               if( c->start_session() ) {
                  c->send_handshake();
               }
            } else {
               fc_elog( logger, "connection failed to ${host}:${port} ${error}",
                        ("host", endpoint.address().to_string())("port", endpoint.port())( "error", err.message()));
               c->close( false );
            }
      } ) );
   }

   void net_plugin_impl::start_listen_loop() {
      connection_ptr new_connection = std::make_shared<connection>();
      new_connection->connecting = true;
      new_connection->strand.post( [this, new_connection = std::move( new_connection )](){
         acceptor->async_accept( *new_connection->socket,
            boost::asio::bind_executor( new_connection->strand, [new_connection, socket=new_connection->socket, this]( boost::system::error_code ec ) {
            if( !ec ) {
               uint32_t visitors = 0;
               uint32_t from_addr = 0;
               boost::system::error_code rec;
               const auto& paddr_add = socket->remote_endpoint( rec ).address();
               string paddr_str;
               if( rec ) {
                  fc_elog( logger, "Error getting remote endpoint: ${m}", ("m", rec.message()));
               } else {
                  paddr_str = paddr_add.to_string();
                  for_each_connection( [&visitors, &from_addr, &paddr_str]( auto& conn ) {
                     if( conn->socket_is_open()) {
                        if( conn->peer_address().empty()) {
                           ++visitors;
                           std::lock_guard<std::mutex> g_conn( conn->conn_mtx );
                           if( paddr_str == conn->remote_endpoint_ip ) {
                              ++from_addr;
                           }
                        }
                     }
                     return true;
                  } );
                  if( from_addr < max_nodes_per_host && (auto_bp_peering_enabled() || max_client_count == 0 || visitors < max_client_count)) {
                     fc_ilog( logger, "Accepted new connection: " + paddr_str );
                     new_connection->set_heartbeat_timeout( heartbeat_timeout );
                     if( new_connection->start_session()) {
                        std::lock_guard<std::shared_mutex> g_unique( connections_mtx );
                        connections.insert( new_connection );
                     }

                  } else {
                     if( from_addr >= max_nodes_per_host ) {
                        fc_dlog( logger, "Number of connections (${n}) from ${ra} exceeds limit ${l}",
                                 ("n", from_addr + 1)( "ra", paddr_str )( "l", max_nodes_per_host ));
                     } else {
                        fc_dlog( logger, "max_client_count ${m} exceeded", ("m", max_client_count));
                     }
                     // new_connection never added to connections and start_session not called, lifetime will end
                     boost::system::error_code ec;
                     socket->shutdown( tcp::socket::shutdown_both, ec );
                     socket->close( ec );
                  }
               }
            } else {
               fc_elog( logger, "Error accepting connection: ${m}", ("m", ec.message()));
               // For the listed error codes below, recall start_listen_loop()
               switch (ec.value()) {
                  case EMFILE: // same as boost::system::errc::too_many_files_open
                  {
                     // no file descriptors available to accept the connection. Wait on async_timer
                     // and retry listening using shorter 100ms timer than SHiP or http_plugin
                     // as net_pluging is more critical
                     accept_error_timer.expires_from_now(boost::posix_time::milliseconds(100));
                     accept_error_timer.async_wait([this]( const boost::system::error_code &ec) {
                        if (!ec)
                           start_listen_loop();
                     });
                     return; // wait for timer!!
                  }
                  case ECONNABORTED:
                  case ENFILE:
                  case ENOBUFS:
                  case ENOMEM:
                  case EPROTO:
                     break;
                  default:
                     return;
               }
            }
            start_listen_loop();
         }));
      } );
   }

   // only called from strand thread
   void connection::start_read_message() {
      try {
         std::size_t minimum_read =
               std::atomic_exchange<decltype(outstanding_read_bytes.load())>( &outstanding_read_bytes, 0 );
         minimum_read = minimum_read != 0 ? minimum_read : message_header_size;

         if (my_impl->use_socket_read_watermark) {
            const size_t max_socket_read_watermark = 4096;
            std::size_t socket_read_watermark = std::min<std::size_t>(minimum_read, max_socket_read_watermark);
            boost::asio::socket_base::receive_low_watermark read_watermark_opt(socket_read_watermark);
            boost::system::error_code ec;
            socket->set_option( read_watermark_opt, ec );
            if( ec ) {
               peer_elog( this, "unable to set read watermark: ${e1}", ("e1", ec.message()) );
            }
         }

         auto completion_handler = [minimum_read](boost::system::error_code ec, std::size_t bytes_transferred) -> std::size_t {
            if (ec || bytes_transferred >= minimum_read ) {
               return 0;
            } else {
               return minimum_read - bytes_transferred;
            }
         };

         uint32_t write_queue_size = buffer_queue.write_queue_size();
         if( write_queue_size > def_max_write_queue_size ) {
            peer_elog( this, "write queue full ${s} bytes, giving up on connection, closing", ("s", write_queue_size) );
            close( false );
            return;
         }

         boost::asio::async_read( *socket,
            pending_message_buffer.get_buffer_sequence_for_boost_async_read(), completion_handler,
            boost::asio::bind_executor( strand,
              [conn = shared_from_this(), socket=socket]( boost::system::error_code ec, std::size_t bytes_transferred ) {
               // may have closed connection and cleared pending_message_buffer
               if( !conn->socket_is_open() || socket != conn->socket ) return;

               bool close_connection = false;
               try {
                  if( !ec ) {
                     if (bytes_transferred > conn->pending_message_buffer.bytes_to_write()) {
                        peer_elog( conn, "async_read_some callback: bytes_transfered = ${bt}, buffer.bytes_to_write = ${btw}",
                                   ("bt",bytes_transferred)("btw",conn->pending_message_buffer.bytes_to_write()) );
                     }
                     EOS_ASSERT(bytes_transferred <= conn->pending_message_buffer.bytes_to_write(), plugin_exception, "");
                     conn->pending_message_buffer.advance_write_ptr(bytes_transferred);
                     while (conn->pending_message_buffer.bytes_to_read() > 0) {
                        uint32_t bytes_in_buffer = conn->pending_message_buffer.bytes_to_read();

                        if (bytes_in_buffer < message_header_size) {
                           conn->outstanding_read_bytes = message_header_size - bytes_in_buffer;
                           break;
                        } else {
                           uint32_t message_length;
                           auto index = conn->pending_message_buffer.read_index();
                           conn->pending_message_buffer.peek(&message_length, sizeof(message_length), index);
                           if(message_length > def_send_buffer_size*2 || message_length == 0) {
                              peer_elog( conn, "incoming message length unexpected (${i})", ("i", message_length) );
                              close_connection = true;
                              break;
                           }

                           auto total_message_bytes = message_length + message_header_size;

                           if (bytes_in_buffer >= total_message_bytes) {
                              conn->pending_message_buffer.advance_read_ptr(message_header_size);
                              conn->consecutive_immediate_connection_close = 0;
                              if (!conn->process_next_message(message_length)) {
                                 return;
                              }
                           } else {
                              auto outstanding_message_bytes = total_message_bytes - bytes_in_buffer;
                              auto available_buffer_bytes = conn->pending_message_buffer.bytes_to_write();
                              if (outstanding_message_bytes > available_buffer_bytes) {
                                 conn->pending_message_buffer.add_space( outstanding_message_bytes - available_buffer_bytes );
                              }

                              conn->outstanding_read_bytes = outstanding_message_bytes;
                              break;
                           }
                        }
                     }
                     if( !close_connection ) conn->start_read_message();
                  } else {
                     if (ec.value() != boost::asio::error::eof) {
                        peer_elog( conn, "Error reading message: ${m}", ( "m", ec.message() ) );
                     } else {
                        peer_ilog( conn, "Peer closed connection" );
                     }
                     close_connection = true;
                  }
               }
               catch ( const std::bad_alloc& )
               {
                 throw;
               }
               catch ( const boost::interprocess::bad_alloc& )
               {
                 throw;
               }
               catch(const fc::exception &ex)
               {
                  peer_elog( conn, "Exception in handling read data ${s}", ("s",ex.to_string()) );
                  close_connection = true;
               }
               catch(const std::exception &ex) {
                  peer_elog( conn, "Exception in handling read data: ${s}", ("s",ex.what()) );
                  close_connection = true;
               }
               catch (...) {
                  peer_elog( conn, "Undefined exception handling read data" );
                  close_connection = true;
               }

               if( close_connection ) {
                  peer_elog( conn, "Closing connection" );
                  conn->close();
               }
         }));
      } catch (...) {
         peer_elog( this, "Undefined exception in start_read_message, closing connection" );
         close();
      }
   }

   // called from connection strand
   bool connection::process_next_message( uint32_t message_length ) {
      try {
         latest_msg_time = get_time();

         // if next message is a block we already have, exit early
         auto peek_ds = pending_message_buffer.create_peek_datastream();
         unsigned_int which{};
         fc::raw::unpack( peek_ds, which );
         if( which == signed_block_which ) {
            latest_blk_time = get_time();
            return process_next_block_message( message_length );

         } else if( which == packed_transaction_which ) {
            return process_next_trx_message( message_length );

         } else {
            auto ds = pending_message_buffer.create_datastream();
            net_message msg;
            fc::raw::unpack( ds, msg );
            msg_handler m( shared_from_this() );
            std::visit( m, msg );
         }

      } catch( const fc::exception& e ) {
         peer_elog( this, "Exception in handling message: ${s}", ("s", e.to_detail_string()) );
         close();
         return false;
      }
      return true;
   }

   // called from connection strand
   bool connection::process_next_block_message(uint32_t message_length) {
      auto peek_ds = pending_message_buffer.create_peek_datastream();
      unsigned_int which{};
      fc::raw::unpack( peek_ds, which ); // throw away
      block_header bh;
      fc::raw::unpack( peek_ds, bh );

      const block_id_type blk_id = bh.calculate_id();
      const uint32_t blk_num = block_header::num_from_id(blk_id);
      // don't add_peer_block because we have not validated this block header yet
      if( my_impl->dispatcher->have_block( blk_id ) ) {
         peer_dlog( this, "canceling wait, already received block ${num}, id ${id}...",
                    ("num", blk_num)("id", blk_id.str().substr(8,16)) );
         my_impl->sync_master->sync_recv_block( shared_from_this(), blk_id, blk_num, false );
         cancel_wait();

         pending_message_buffer.advance_read_ptr( message_length );
         return true;
      }
      peer_dlog( this, "received block ${num}, id ${id}..., latency: ${latency}",
                 ("num", bh.block_num())("id", blk_id.str().substr(8,16))
                 ("latency", (fc::time_point::now() - bh.timestamp).count()/1000) );
      if( !my_impl->sync_master->syncing_with_peer() ) { // guard against peer thinking it needs to send us old blocks
         uint32_t lib_num = my_impl->get_chain_lib_num();
         if( blk_num < lib_num ) {
            std::unique_lock<std::mutex> g( conn_mtx );
            const auto last_sent_lib = last_handshake_sent.last_irreversible_block_num;
            g.unlock();
            peer_ilog( this, "received block ${n} less than ${which}lib ${lib}",
                       ("n", blk_num)("which", blk_num < last_sent_lib ? "sent " : "")
                       ("lib", blk_num < last_sent_lib ? last_sent_lib : lib_num) );
            my_impl->sync_master->reset_last_requested_num(my_impl->sync_master->locked_sync_mutex());
            enqueue( (sync_request_message) {0, 0} );
            send_handshake();
            cancel_wait();

            pending_message_buffer.advance_read_ptr( message_length );
            return true;
         }
      }

      auto ds = pending_message_buffer.create_datastream();
      fc::raw::unpack( ds, which );
      shared_ptr<signed_block> ptr = std::make_shared<signed_block>();
      fc::raw::unpack( ds, *ptr );

      auto is_webauthn_sig = []( const fc::crypto::signature& s ) {
         return s.which() == fc::get_index<fc::crypto::signature::storage_type, fc::crypto::webauthn::signature>();
      };
      bool has_webauthn_sig = is_webauthn_sig( ptr->producer_signature );

      constexpr auto additional_sigs_eid = additional_block_signatures_extension::extension_id();
      auto exts = ptr->validate_and_extract_extensions();
      if( exts.count( additional_sigs_eid ) ) {
         const auto &additional_sigs = std::get<additional_block_signatures_extension>(exts.lower_bound( additional_sigs_eid )->second).signatures;
         has_webauthn_sig |= std::any_of( additional_sigs.begin(), additional_sigs.end(), is_webauthn_sig );
      }

      if( has_webauthn_sig ) {
         peer_dlog( this, "WebAuthn signed block received, closing connection" );
         close();
         return false;
      }

      handle_message( blk_id, std::move( ptr ) );
      return true;
   }

   // called from connection strand
   bool connection::process_next_trx_message(uint32_t message_length) {
      if( !my_impl->p2p_accept_transactions ) {
         peer_dlog( this, "p2p-accept-transaction=false - dropping txn" );
         pending_message_buffer.advance_read_ptr( message_length );
         return true;
      }

      const unsigned long trx_in_progress_sz = this->trx_in_progress_size.load();

      auto ds = pending_message_buffer.create_datastream();
      unsigned_int which{};
      fc::raw::unpack( ds, which );
      shared_ptr<packed_transaction> ptr = std::make_shared<packed_transaction>();
      fc::raw::unpack( ds, *ptr );
      if( trx_in_progress_sz > def_max_trx_in_progress_size) {
         ++my_impl->metrics.dropped_trxs.value;
         char reason[72];
         snprintf(reason, 72, "Dropping trx, too many trx in progress %lu bytes", trx_in_progress_sz);
         my_impl->producer_plug->log_failed_transaction(ptr->id(), ptr, reason);
         if (fc::time_point::now() - fc::seconds(1) >= last_dropped_trx_msg_time) {
            last_dropped_trx_msg_time = fc::time_point::now();
            my_impl->metrics.post_metrics();
            peer_wlog(this, reason);
         }
         return true;
      }
      bool have_trx = my_impl->dispatcher->have_txn( ptr->id() );
      my_impl->dispatcher->add_peer_txn( ptr->id(), ptr->expiration(), connection_id );

      if( have_trx ) {
         peer_dlog( this, "got a duplicate transaction - dropping" );
         return true;
      }

      handle_message( std::move( ptr ) );
      return true;
   }

   void net_plugin_impl::plugin_shutdown() {
         in_shutdown = true;
         {
            std::lock_guard<std::mutex> g( connector_check_timer_mtx );
            if( connector_check_timer )
               connector_check_timer->cancel();
         }
         {
            std::lock_guard<std::mutex> g( expire_timer_mtx );
            if( expire_timer )
               expire_timer->cancel();
         }
         {
            std::lock_guard<std::mutex> g( keepalive_timer_mtx );
            if( keepalive_timer )
               keepalive_timer->cancel();
         }

         {
            fc_ilog( logger, "close ${s} connections", ("s", connections.size()) );
            std::lock_guard<std::shared_mutex> g( connections_mtx );
            for( auto& con : connections ) {
               fc_dlog( logger, "close: ${cid}", ("cid", con->connection_id) );
               con->close( false, true );
            }
            connections.clear();
         }

         thread_pool.stop();

         if( acceptor ) {
            boost::system::error_code ec;
            acceptor->cancel( ec );
            acceptor->close( ec );
         }
   }

   // call only from main application thread
   void net_plugin_impl::update_chain_info() {
      controller& cc = chain_plug->chain();
      uint32_t lib_num = 0, head_num = 0;
      {
         std::lock_guard<std::mutex> g( chain_info_mtx );
         chain_info.lib_num = lib_num = cc.last_irreversible_block_num();
         chain_info.lib_id = cc.last_irreversible_block_id();
         chain_info.head_num = head_num = cc.fork_db_head_block_num();
         chain_info.head_id = cc.fork_db_head_block_id();
      }
      fc_dlog( logger, "updating chain info lib ${lib}, fork ${fork}", ("lib", lib_num)("fork", head_num) );
   }

   net_plugin_impl::chain_info_t net_plugin_impl::get_chain_info() const {
      std::lock_guard<std::mutex> g( chain_info_mtx );
      return chain_info;
   }

   uint32_t net_plugin_impl::get_chain_lib_num() const {
      std::lock_guard<std::mutex> g( chain_info_mtx );
      return chain_info.lib_num;
   }

   uint32_t net_plugin_impl::get_chain_head_num() const {
      std::lock_guard<std::mutex> g( chain_info_mtx );
      return chain_info.head_num;
   }

   bool connection::is_valid( const handshake_message& msg ) const {
      // Do some basic validation of an incoming handshake_message, so things
      // that really aren't handshake messages can be quickly discarded without
      // affecting state.
      bool valid = true;
      if (msg.last_irreversible_block_num > msg.head_num) {
         peer_wlog( this, "Handshake message validation: last irreversible block (${i}) is greater than head block (${h})",
                  ("i", msg.last_irreversible_block_num)("h", msg.head_num) );
         valid = false;
      }
      if (msg.p2p_address.empty()) {
         peer_wlog( this, "Handshake message validation: p2p_address is null string" );
         valid = false;
      } else if( msg.p2p_address.length() > max_handshake_str_length ) {
         // see max_handshake_str_length comment in protocol.hpp
         peer_wlog( this, "Handshake message validation: p2p_address too large: ${p}",
                    ("p", msg.p2p_address.substr(0, max_handshake_str_length) + "...") );
         valid = false;
      }
      if (msg.os.empty()) {
         peer_wlog( this, "Handshake message validation: os field is null string" );
         valid = false;
      } else if( msg.os.length() > max_handshake_str_length ) {
         peer_wlog( this, "Handshake message validation: os field too large: ${p}",
                    ("p", msg.os.substr(0, max_handshake_str_length) + "...") );
         valid = false;
      }
      if( msg.agent.length() > max_handshake_str_length ) {
         peer_wlog( this, "Handshake message validation: agent field too large: ${p}",
                  ("p", msg.agent.substr(0, max_handshake_str_length) + "...") );
         valid = false;
      }
      if ((msg.sig != chain::signature_type() || msg.token != sha256()) && (msg.token != fc::sha256::hash(msg.time))) {
         peer_wlog( this, "Handshake message validation: token field invalid" );
         valid = false;
      }
      return valid;
   }

   void connection::handle_message( const chain_size_message& msg ) {
      peer_dlog(this, "received chain_size_message");
   }

   void connection::handle_message( const handshake_message& msg ) {
      peer_dlog( this, "received handshake_message" );
      if( !is_valid( msg ) ) {
         peer_elog( this, "bad handshake message");
         no_retry = go_away_reason::fatal_other;
         enqueue( go_away_message( fatal_other ) );
         return;
      }
      peer_dlog( this, "received handshake gen ${g}, lib ${lib}, head ${head}",
                 ("g", msg.generation)("lib", msg.last_irreversible_block_num)("head", msg.head_num) );

      peer_lib_num = msg.last_irreversible_block_num;
      std::unique_lock<std::mutex> g_conn( conn_mtx );
      last_handshake_recv = msg;
      g_conn.unlock();

      connecting = false;
      if (msg.generation == 1) {
         if( msg.node_id == my_impl->node_id) {
            peer_elog( this, "Self connection detected node_id ${id}. Closing connection", ("id", msg.node_id) );
            no_retry = go_away_reason::self;
            enqueue( go_away_message( go_away_reason::self ) );
            return;
         }

         log_p2p_address = msg.p2p_address;

         my_impl->mark_bp_connection(this);
         if (my_impl->exceeding_connection_limit(this)) {
            // When auto bp peering is enabled, the start_listen_loop check doesn't have enough information to determine
            // if a client is a BP peer. In start_listen_loop, it only has the peer address which a node is connecting
            // from, but it would be different from the address it is listening. The only way to make sure is when the
            // first handshake message is received with the p2p_address information in the message. Thus the connection
            // limit checking has to be here when auto bp peering is enabled.
            fc_dlog(logger, "max_client_count ${m} exceeded", ("m", my_impl->max_client_count));
            my_impl->disconnect(peer_address());
            return;
         }

         if( peer_address().empty() ) {
            set_connection_type( msg.p2p_address );
         }

         std::unique_lock<std::mutex> g_conn( conn_mtx );
         if( peer_address().empty() || last_handshake_recv.node_id == fc::sha256()) {
            auto c_time = last_handshake_sent.time;
            g_conn.unlock();
            peer_dlog( this, "checking for duplicate" );
            std::shared_lock<std::shared_mutex> g_cnts( my_impl->connections_mtx );
            for(const auto& check : my_impl->connections) {
               if(check.get() == this)
                  continue;
               std::unique_lock<std::mutex> g_check_conn( check->conn_mtx );
               fc_dlog( logger, "dup check: connected ${c}, ${l} =? ${r}",
                        ("c", check->connected())("l", check->last_handshake_recv.node_id)("r", msg.node_id) );
               if(check->connected() && check->last_handshake_recv.node_id == msg.node_id) {
                  if (net_version < proto_dup_goaway_resolution || msg.network_version < proto_dup_goaway_resolution) {
                     // It's possible that both peers could arrive here at relatively the same time, so
                     // we need to avoid the case where they would both tell a different connection to go away.
                     // Using the sum of the initial handshake times of the two connections, we will
                     // arbitrarily (but consistently between the two peers) keep one of them.

                     auto check_time = check->last_handshake_sent.time + check->last_handshake_recv.time;
                     g_check_conn.unlock();
                     if (msg.time + c_time <= check_time)
                        continue;
                  } else if (net_version < proto_dup_node_id_goaway || msg.network_version < proto_dup_node_id_goaway) {
                     if (my_impl->p2p_address < msg.p2p_address) {
                        fc_dlog( logger, "my_impl->p2p_address '${lhs}' < msg.p2p_address '${rhs}'",
                                 ("lhs", my_impl->p2p_address)( "rhs", msg.p2p_address ) );
                        // only the connection from lower p2p_address to higher p2p_address will be considered as a duplicate,
                        // so there is no chance for both connections to be closed
                        continue;
                     }
                  } else if (my_impl->node_id < msg.node_id) {
                     fc_dlog( logger, "not duplicate, my_impl->node_id '${lhs}' < msg.node_id '${rhs}'",
                              ("lhs", my_impl->node_id)("rhs", msg.node_id) );
                     // only the connection from lower node_id to higher node_id will be considered as a duplicate,
                     // so there is no chance for both connections to be closed
                     continue;
                  }

                  g_cnts.unlock();
                  peer_dlog( this, "sending go_away duplicate, msg.p2p_address: ${add}", ("add", msg.p2p_address) );
                  go_away_message gam(duplicate);
                  gam.node_id = conn_node_id;
                  enqueue(gam);
                  no_retry = duplicate;
                  return;
               }
            }
         } else {
            peer_dlog( this, "skipping duplicate check, addr == ${pa}, id = ${ni}",
                       ("pa", peer_address())( "ni", last_handshake_recv.node_id ) );
            g_conn.unlock();
         }

         if( msg.chain_id != my_impl->chain_id ) {
            peer_elog( this, "Peer on a different chain. Closing connection" );
            no_retry = go_away_reason::wrong_chain;
            enqueue( go_away_message(go_away_reason::wrong_chain) );
            return;
         }
         protocol_version = my_impl->to_protocol_version(msg.network_version);
         if( protocol_version != net_version ) {
            peer_ilog( this, "Local network version different: ${nv} Remote version: ${mnv}",
                       ("nv", net_version)("mnv", protocol_version.load()) );
         } else {
            peer_ilog( this, "Local network version: ${nv}", ("nv", net_version) );
         }

         conn_node_id = msg.node_id;
         short_conn_node_id = conn_node_id.str().substr( 0, 7 );

         if( !my_impl->authenticate_peer( msg ) ) {
            peer_elog( this, "Peer not authenticated.  Closing connection." );
            no_retry = go_away_reason::authentication;
            enqueue( go_away_message( go_away_reason::authentication ) );
            return;
         }

         uint32_t peer_lib = msg.last_irreversible_block_num;
         uint32_t lib_num = my_impl->get_chain_lib_num();

         peer_dlog( this, "handshake check for fork lib_num = ${ln}, peer_lib = ${pl}", ("ln", lib_num)("pl", peer_lib) );

         if( peer_lib <= lib_num && peer_lib > 0 ) {
            bool on_fork = false;
            try {
               controller& cc = my_impl->chain_plug->chain();
               block_id_type peer_lib_id = cc.get_block_id_for_num( peer_lib ); // thread-safe
               on_fork = (msg.last_irreversible_block_id != peer_lib_id);
            } catch( const unknown_block_exception& ) {
               // allow this for now, will be checked on sync
               peer_dlog( this, "peer last irreversible block ${pl} is unknown", ("pl", peer_lib) );
            } catch( ... ) {
               peer_wlog( this, "caught an exception getting block id for ${pl}", ("pl", peer_lib) );
               on_fork = true;
            }
            if( on_fork ) {
                  peer_elog( this, "Peer chain is forked, sending: forked go away" );
                  no_retry = go_away_reason::forked;
                  enqueue( go_away_message( go_away_reason::forked ) );
            }
         }

         // we don't support the 2.1 packed_transaction & signed_block, so tell 2.1 clients we are 2.0
         if( protocol_version >= proto_pruned_types && protocol_version < proto_leap_initial ) {
            sent_handshake_count = 0;
            net_version = proto_explicit_sync;
            send_handshake();
            return;
         }

         if( sent_handshake_count == 0 ) {
            send_handshake();
         }
      }

      my_impl->sync_master->recv_handshake( shared_from_this(), msg );
   }

   void connection::handle_message( const go_away_message& msg ) {
      peer_wlog( this, "received go_away_message, reason = ${r}", ("r", reason_str( msg.reason )) );

      bool retry = no_retry == no_reason; // if no previous go away message
      no_retry = msg.reason;
      if( msg.reason == duplicate ) {
         conn_node_id = msg.node_id;
      }
      if( msg.reason == wrong_version ) {
         if( !retry ) no_retry = fatal_other; // only retry once on wrong version
      }
      else if ( msg.reason == benign_other ) {
         if ( retry ) peer_dlog( this, "received benign_other reason, retrying to connect");
      }
      else {
         retry = false;
      }
      flush_queues();

      close( retry ); // reconnect if wrong_version
   }

   void connection::handle_message( const time_message& msg ) {
      peer_ilog( this, "received time_message" );

      /* We've already lost however many microseconds it took to dispatch
       * the message, but it can't be helped.
       */
      msg.dst = get_time();

      // If the transmit timestamp is zero, the peer is horribly broken.
      if(msg.xmt == 0)
         return;                 /* invalid timestamp */

      if(msg.xmt == xmt)
         return;                 /* duplicate packet */

      xmt = msg.xmt;
      rec = msg.rec;
      dst = msg.dst;

      if( msg.org == 0 ) {
         send_time( msg );
         return;  // We don't have enough data to perform the calculation yet.
      }

      double offset = (double(rec - org) + double(msg.xmt - dst)) / 2;
      double NsecPerUsec{1000};

      if( logger.is_enabled( fc::log_level::all ) )
         logger.log( FC_LOG_MESSAGE( all, "Clock offset is ${o}ns (${us}us)",
                                     ("o", offset)( "us", offset / NsecPerUsec ) ) );
      org = 0;
      rec = 0;

      std::unique_lock<std::mutex> g_conn( conn_mtx );
      if( last_handshake_recv.generation == 0 ) {
         g_conn.unlock();
         send_handshake();
      }
   }

   void connection::handle_message( const notice_message& msg ) {
      // peer tells us about one or more blocks or txns. When done syncing, forward on
      // notices of previously unknown blocks or txns,
      //
      peer_dlog( this, "received notice_message" );
      connecting = false;
      if( msg.known_blocks.ids.size() > 1 ) {
         peer_elog( this, "Invalid notice_message, known_blocks.ids.size ${s}, closing connection",
                    ("s", msg.known_blocks.ids.size()) );
         close( false );
         return;
      }
      if( msg.known_trx.mode != none ) {
         if( logger.is_enabled( fc::log_level::debug ) ) {
            const block_id_type& blkid = msg.known_blocks.ids.empty() ? block_id_type{} : msg.known_blocks.ids.back();
            peer_dlog( this, "this is a ${m} notice with ${n} pending blocks: ${num} ${id}...",
                       ("m", modes_str( msg.known_blocks.mode ))("n", msg.known_blocks.pending)
                       ("num", block_header::num_from_id( blkid ))("id", blkid.str().substr( 8, 16 )) );
         }
      }
      switch (msg.known_trx.mode) {
      case none:
         break;
      case last_irr_catch_up: {
         std::unique_lock<std::mutex> g_conn( conn_mtx );
         last_handshake_recv.head_num = msg.known_blocks.pending;
         g_conn.unlock();
         break;
      }
      case catch_up : {
         break;
      }
      case normal: {
         my_impl->dispatcher->recv_notice( shared_from_this(), msg, false );
      }
      }

      if( msg.known_blocks.mode != none ) {
         peer_dlog( this, "this is a ${m} notice with ${n} blocks",
                    ("m", modes_str( msg.known_blocks.mode ))( "n", msg.known_blocks.pending ) );
      }
      switch (msg.known_blocks.mode) {
      case none : {
         break;
      }
      case last_irr_catch_up:
      case catch_up: {
         my_impl->sync_master->sync_recv_notice( shared_from_this(), msg );
         break;
      }
      case normal : {
         my_impl->dispatcher->recv_notice( shared_from_this(), msg, false );
         break;
      }
      default: {
         peer_elog( this, "bad notice_message : invalid known_blocks.mode ${m}",
                    ("m", static_cast<uint32_t>(msg.known_blocks.mode)) );
      }
      }
   }

   void connection::handle_message( const request_message& msg ) {
      if( msg.req_blocks.ids.size() > 1 ) {
         peer_elog( this, "Invalid request_message, req_blocks.ids.size ${s}, closing",
                    ("s", msg.req_blocks.ids.size()) );
         close();
         return;
      }

      switch (msg.req_blocks.mode) {
      case catch_up :
         peer_dlog( this, "received request_message:catch_up" );
         blk_send_branch( msg.req_blocks.ids.empty() ? block_id_type() : msg.req_blocks.ids.back() );
         break;
      case normal :
         peer_dlog( this, "received request_message:normal" );
         if( !msg.req_blocks.ids.empty() ) {
            blk_send( msg.req_blocks.ids.back() );
         }
         break;
      default:;
      }


      switch (msg.req_trx.mode) {
      case catch_up :
         break;
      case none :
         if( msg.req_blocks.mode == none ) {
            stop_send();
         }
         // no break
      case normal :
         if( !msg.req_trx.ids.empty() ) {
            peer_elog( this, "Invalid request_message, req_trx.ids.size ${s}", ("s", msg.req_trx.ids.size()) );
            close();
            return;
         }
         break;
      default:;
      }
   }

   void connection::handle_message( const sync_request_message& msg ) {
      peer_dlog( this, "peer requested ${start} to ${end}", ("start", msg.start_block)("end", msg.end_block) );
      if( msg.end_block == 0 ) {
         peer_requested.reset();
         flush_queues();
      } else {
         if (peer_requested) {
            // This happens when peer already requested some range and sync is still in progress
            // It could be higher in case of peer requested head catchup and current request is lib catchup
            // So to make sure peer will receive all requested blocks we assign end_block to highest value
            peer_requested->end_block = std::max(msg.end_block, peer_requested->end_block);
         }
         else {
            peer_requested = peer_sync_state( msg.start_block, msg.end_block, msg.start_block-1);
         }
         enqueue_sync_block();
      }
   }

   size_t calc_trx_size( const packed_transaction_ptr& trx ) {
      return trx->get_estimated_size();
   }

   // called from connection strand
   void connection::handle_message( packed_transaction_ptr trx ) {
      const auto& tid = trx->id();
      peer_dlog( this, "received packed_transaction ${id}", ("id", tid) );

      trx_in_progress_size += calc_trx_size( trx );
      my_impl->chain_plug->accept_transaction( trx,
         [weak = weak_from_this(), trx](const std::variant<fc::exception_ptr, transaction_trace_ptr>& result) mutable {
         // next (this lambda) called from application thread
         if (std::holds_alternative<fc::exception_ptr>(result)) {
            fc_dlog( logger, "bad packed_transaction : ${m}", ("m", std::get<fc::exception_ptr>(result)->what()) );
         } else {
            const transaction_trace_ptr& trace = std::get<transaction_trace_ptr>(result);
            if( !trace->except ) {
               fc_dlog( logger, "chain accepted transaction, bcast ${id}", ("id", trace->id) );
            } else {
               fc_elog( logger, "bad packed_transaction : ${m}", ("m", trace->except->what()));
            }
         }
         connection_ptr conn = weak.lock();
         if( conn ) {
            conn->trx_in_progress_size -= calc_trx_size( trx );
         }
      });
   }

   // called from connection strand
   void connection::handle_message( const block_id_type& id, signed_block_ptr ptr ) {
      peer_dlog( this, "received signed_block ${num}, id ${id}", ("num", block_header::num_from_id(id))("id", id) );

      // post to dispatcher strand so that we don't have multiple threads validating the block header
      // the dispatcher strand will sync the add_peer_block and rm_block calls
      my_impl->dispatcher->strand.post([id, c{shared_from_this()}, ptr{std::move(ptr)}, cid=connection_id]() mutable {
         controller& cc = my_impl->chain_plug->chain();

         // may have come in on a different connection and posted into dispatcher strand before this one
         if( my_impl->dispatcher->have_block( id ) || cc.fetch_block_state_by_id( id ) ) { // thread-safe
            my_impl->dispatcher->add_peer_block( id, c->connection_id );
            c->strand.post( [c, id]() {
               my_impl->sync_master->sync_recv_block( c, id, block_header::num_from_id(id), false );
            });
            return;
         }

         block_state_ptr bsp;
         bool exception = false;
         try {
            // this may return null if block is not immediately ready to be processed
            bsp = cc.create_block_state( id, ptr );
         } catch( const fc::exception& ex ) {
            exception = true;
            fc_elog( logger, "bad block exception connection ${cid}: #${n} ${id}...: ${m}",
                     ("cid", cid)("n", ptr->block_num())("id", id.str().substr(8,16))("m",ex.to_string()));
         } catch( ... ) {
            exception = true;
            fc_elog( logger, "bad block connection ${cid}: #${n} ${id}...: unknown exception",
                     ("cid", cid)("n", ptr->block_num())("id", id.str().substr(8,16)));
         }
         if( exception ) {
            c->strand.post( [c, id, blk_num=ptr->block_num()]() {
               my_impl->sync_master->rejected_block( c, blk_num );
               my_impl->dispatcher->rejected_block( id );
            });
            return;
         }


         uint32_t block_num = bsp ? bsp->block_num : 0;

         if( block_num != 0 ) {
            fc_dlog( logger, "validated block header, broadcasting immediately, connection ${cid}, blk num = ${num}, id = ${id}",
                     ("cid", cid)("num", block_num)("id", bsp->id) );
            my_impl->dispatcher->add_peer_block( bsp->id, cid ); // no need to send back to sender
            my_impl->dispatcher->bcast_block( bsp->block, bsp->id );
         }

         app().executor().post(priority::medium, exec_queue::read_write, [ptr{std::move(ptr)}, bsp{std::move(bsp)}, id, c{std::move(c)}]() mutable {
            c->process_signed_block( id, std::move(ptr), std::move(bsp) );
         });

         if( block_num != 0 ) {
            // ready to process immediately, so signal producer to interrupt start_block
            my_impl->producer_plug->received_block(block_num);
         }
      });
   }

   // called from application thread
   void connection::process_signed_block( const block_id_type& blk_id, signed_block_ptr msg, block_state_ptr bsp ) {
      controller& cc = my_impl->chain_plug->chain();
      uint32_t blk_num = block_header::num_from_id(blk_id);
      // use c in this method instead of this to highlight that all methods called on c-> must be thread safe
      connection_ptr c = shared_from_this();

      // if we have closed connection then stop processing
      if( !c->socket_is_open() ) {
         if( bsp ) {
            // valid bsp means add_peer_block already called, need to remove it since we are not going to process the block
            // call on dispatch strand to serialize with the add_peer_block calls
            my_impl->dispatcher->strand.post( [blk_id]() {
               my_impl->dispatcher->rm_block( blk_id );
            } );
         }
         return;
      }

      try {
         if( cc.fetch_block_by_id(blk_id) ) {
            c->strand.post( [sync_master = my_impl->sync_master.get(),
                             dispatcher = my_impl->dispatcher.get(), c, blk_id, blk_num]() {
               dispatcher->add_peer_block( blk_id, c->connection_id );
               sync_master->sync_recv_block( c, blk_id, blk_num, false );
            });
            return;
         }
      } catch(...) {
         // should this even be caught?
         fc_elog( logger, "Caught an unknown exception trying to recall block ID" );
      }

      fc::microseconds age( fc::time_point::now() - msg->timestamp);
      fc_dlog( logger, "received signed_block: #${n} block age in secs = ${age}, connection ${cid}, ${v}",
               ("n", blk_num)("age", age.to_seconds())("cid", c->connection_id)("v", bsp ? "pre-validated" : "validation pending") );

      go_away_reason reason = no_reason;
      bool accepted = false;
      try {
         accepted = my_impl->chain_plug->accept_block(msg, blk_id, bsp);
         my_impl->update_chain_info();
      } catch( const unlinkable_block_exception &ex) {
         fc_elog(logger, "unlinkable_block_exception connection ${cid}: #${n} ${id}...: ${m}",
                 ("cid", c->connection_id)("n", blk_num)("id", blk_id.str().substr(8,16))("m",ex.to_string()));
         reason = unlinkable;
      } catch( const block_validate_exception &ex ) {
         fc_elog(logger, "block_validate_exception connection ${cid}: #${n} ${id}...: ${m}",
                 ("cid", c->connection_id)("n", blk_num)("id", blk_id.str().substr(8,16))("m",ex.to_string()));
         reason = validation;
      } catch( const assert_exception &ex ) {
         fc_elog(logger, "block assert_exception connection ${cid}: #${n} ${id}...: ${m}",
                 ("cid", c->connection_id)("n", blk_num)("id", blk_id.str().substr(8,16))("m",ex.to_string()));
         reason = fatal_other;
      } catch( const fc::exception &ex ) {
         fc_elog(logger, "bad block exception connection ${cid}: #${n} ${id}...: ${m}",
                 ("cid", c->connection_id)("n", blk_num)("id", blk_id.str().substr(8,16))("m",ex.to_string()));
         reason = fatal_other;
      } catch( ... ) {
         fc_elog(logger, "bad block connection ${cid}: #${n} ${id}...: unknown exception",
                 ("cid", c->connection_id)("n", blk_num)("id", blk_id.str().substr(8,16)));
         reason = fatal_other;
      }

      if( accepted ) {
         boost::asio::post( my_impl->thread_pool.get_executor(), [dispatcher = my_impl->dispatcher.get(), cid=c->connection_id, blk_id, msg]() {
            fc_dlog( logger, "accepted signed_block : #${n} ${id}...", ("n", msg->block_num())("id", blk_id.str().substr(8,16)) );
            dispatcher->add_peer_block( blk_id, cid );
         });
         c->strand.post( [sync_master = my_impl->sync_master.get(), dispatcher = my_impl->dispatcher.get(), c, blk_id, blk_num]() {
            dispatcher->recv_block( c, blk_id, blk_num );
            sync_master->sync_recv_block( c, blk_id, blk_num, true );
         });
      } else {
         c->strand.post( [sync_master = my_impl->sync_master.get(), dispatcher = my_impl->dispatcher.get(), c, blk_id, blk_num, reason]() {
            if( reason == unlinkable || reason == no_reason ) {
               // unlinkable may be linkable in the future, so indicate we have not received it
               // call on dispatch strand to serialize with the add_peer_block calls
               my_impl->dispatcher->strand.post( [blk_id]() {
                  my_impl->dispatcher->rm_block( blk_id );
               } );
            }
            // reason==no_reason means accept_block() return false because we are producing, don't call rejected_block which sends handshake
            if( reason != no_reason ) {
               sync_master->rejected_block( c, blk_num );
            }
            dispatcher->rejected_block( blk_id );
         });
      }
   }

   // called from any thread
   void net_plugin_impl::start_conn_timer(boost::asio::steady_timer::duration du, std::weak_ptr<connection> from_connection) {
      if( in_shutdown ) return;
      std::lock_guard<std::mutex> g( connector_check_timer_mtx );
      ++connector_checks_in_flight;
      connector_check_timer->expires_from_now( du );
      connector_check_timer->async_wait( [my = shared_from_this(), from_connection](boost::system::error_code ec) {
            std::unique_lock<std::mutex> g( my->connector_check_timer_mtx );
            int num_in_flight = --my->connector_checks_in_flight;
            g.unlock();
            if( !ec ) {
               my->connection_monitor(from_connection, num_in_flight == 0 );
            } else {
               if( num_in_flight == 0 ) {
                  if( my->in_shutdown ) return;
                  fc_elog( logger, "Error from connection check monitor: ${m}", ("m", ec.message()));
                  my->start_conn_timer( my->connector_period, std::weak_ptr<connection>() );
               }
            }
      });
   }

   // thread safe
   void net_plugin_impl::start_expire_timer() {
      if( in_shutdown ) return;
      std::lock_guard<std::mutex> g( expire_timer_mtx );
      expire_timer->expires_from_now( txn_exp_period);
      expire_timer->async_wait( [my = shared_from_this()]( boost::system::error_code ec ) {
         if( !ec ) {
            my->expire();
         } else {
            if( my->in_shutdown ) return;
            fc_elog( logger, "Error from transaction check monitor: ${m}", ("m", ec.message()) );
            my->start_expire_timer();
         }
      } );
   }

   // thread safe
   void net_plugin_impl::ticker() {
      if( in_shutdown ) return;
      std::lock_guard<std::mutex> g( keepalive_timer_mtx );
      keepalive_timer->expires_from_now(keepalive_interval);
      keepalive_timer->async_wait( [my = shared_from_this()]( boost::system::error_code ec ) {
            my->ticker();
            if( ec ) {
               if( my->in_shutdown ) return;
               fc_wlog( logger, "Peer keepalive ticked sooner than expected: ${m}", ("m", ec.message()) );
            }

            tstamp current_time = connection::get_time();
            my->for_each_connection( [current_time]( auto& c ) {
               if( c->socket_is_open() ) {
                  c->strand.post([c, current_time]() {
                     c->check_heartbeat(current_time);
                  } );
               }
               return true;
            } );
         } );
   }

   void net_plugin_impl::start_monitors() {
      {
         std::lock_guard<std::mutex> g( connector_check_timer_mtx );
         connector_check_timer.reset(new boost::asio::steady_timer( my_impl->thread_pool.get_executor() ));
      }
      {
         std::lock_guard<std::mutex> g( expire_timer_mtx );
         expire_timer.reset( new boost::asio::steady_timer( my_impl->thread_pool.get_executor() ) );
      }
      start_conn_timer(connector_period, std::weak_ptr<connection>());
      start_expire_timer();
   }

   void net_plugin_impl::expire() {
      auto now = time_point::now();
      uint32_t lib_num = get_chain_lib_num();
      dispatcher->expire_blocks( lib_num );
      dispatcher->expire_txns();
      fc_dlog( logger, "expire_txns ${n}us", ("n", time_point::now() - now) );

      start_expire_timer();
   }

   // called from any thread
   void net_plugin_impl::connection_monitor(std::weak_ptr<connection> from_connection, bool reschedule ) {
      auto max_time = fc::time_point::now();
      max_time += fc::milliseconds(max_cleanup_time_ms);
      auto from = from_connection.lock();
      std::unique_lock<std::shared_mutex> g( connections_mtx );
      auto it = (from ? connections.find(from) : connections.begin());
      if (it == connections.end()) it = connections.begin();
      size_t num_rm = 0, num_clients = 0, num_peers = 0, num_bp_peers = 0;
      while (it != connections.end()) {
         if (fc::time_point::now() >= max_time) {
            connection_wptr wit = *it;
            g.unlock();
            fc_dlog( logger, "Exiting connection monitor early, ran out of time: ${t}", ("t", max_time - fc::time_point::now()) );
            fc_ilog( logger, "p2p client connections: ${num}/${max}, peer connections: ${pnum}/${pmax}",
                     ("num", num_clients)("max", max_client_count)("pnum", num_peers)("pmax", supplied_peers.size()) );
            if( reschedule ) {
               start_conn_timer( std::chrono::milliseconds( 1 ), wit ); // avoid exhausting
            }
            return;
         }
         if ((*it)->is_bp_connection)
            ++num_bp_peers;
         else if ((*it)->incoming())
            ++num_clients;
         else
            ++num_peers;

         if( !(*it)->socket_is_open() && !(*it)->connecting) {
            if( !(*it)->incoming() ) {
               if( !(*it)->resolve_and_connect() ) {
                  it = connections.erase(it);
                  --num_peers; ++num_rm;
                  continue;
               }
            } else {
               --num_clients; ++num_rm;
               it = connections.erase(it);
               continue;
            }
         }
         ++it;
      }
      g.unlock();

      metrics.num_clients.value = num_clients;
      metrics.num_peers.value = num_peers;
      metrics.post_metrics();

      if( num_clients > 0 || num_peers > 0 )
         fc_ilog( logger, "p2p client connections: ${num}/${max}, peer connections: ${pnum}/${pmax}, block producer peers: ${num_bp_peers}",
                  ("num", num_clients)("max", max_client_count)("pnum", num_peers)("pmax", supplied_peers.size())("num_bp_peers", num_bp_peers) );
      fc_dlog( logger, "connection monitor, removed ${n} connections", ("n", num_rm) );
      if( reschedule ) {
         start_conn_timer( connector_period, std::weak_ptr<connection>());
      }
   }

   // called from application thread
   void net_plugin_impl::on_accepted_block_header(const block_state_ptr& bs) {
      update_chain_info();
      dispatcher->strand.post( [bs]() {
         fc_dlog( logger, "signaled accepted_block_header, blk num = ${num}, id = ${id}", ("num", bs->block_num)("id", bs->id) );
         my_impl->dispatcher->bcast_block( bs->block, bs->id );
      });
   }

   void net_plugin_impl::on_accepted_block(const block_state_ptr& ) {
      on_pending_schedule(chain_plug->chain().pending_producers());
      on_active_schedule(chain_plug->chain().active_producers());
   }

   // called from application thread
   void net_plugin_impl::on_irreversible_block( const block_state_ptr& block) {
      fc_dlog( logger, "on_irreversible_block, blk num = ${num}, id = ${id}", ("num", block->block_num)("id", block->id) );
      update_chain_info();
   }

   // called from application thread
   void net_plugin_impl::transaction_ack(const std::pair<fc::exception_ptr, packed_transaction_ptr>& results) {
      boost::asio::post( my_impl->thread_pool.get_executor(), [dispatcher = my_impl->dispatcher.get(), results]() {
         const auto& id = results.second->id();
         if (results.first) {
            fc_dlog( logger, "signaled NACK, trx-id = ${id} : ${why}", ("id", id)( "why", results.first->to_detail_string() ) );
            dispatcher->rejected_transaction(results.second);
         } else {
            fc_dlog( logger, "signaled ACK, trx-id = ${id}", ("id", id) );
            dispatcher->bcast_transaction(results.second);
         }
      });
   }

   bool net_plugin_impl::authenticate_peer(const handshake_message& msg) const {
      if(allowed_connections == None)
         return false;

      if(allowed_connections == Any)
         return true;

      if(allowed_connections & (Producers | Specified)) {
         auto allowed_it = std::find(allowed_peers.begin(), allowed_peers.end(), msg.key);
         auto private_it = private_keys.find(msg.key);
         bool found_producer_key = false;
         if(producer_plug != nullptr)
            found_producer_key = producer_plug->is_producer_key(msg.key);
         if( allowed_it == allowed_peers.end() && private_it == private_keys.end() && !found_producer_key) {
            fc_elog( logger, "Peer ${peer} sent a handshake with an unauthorized key: ${key}.",
                     ("peer", msg.p2p_address)("key", msg.key) );
            return false;
         }
      }

      if(msg.sig != chain::signature_type() && msg.token != sha256()) {
         sha256 hash = fc::sha256::hash(msg.time);
         if(hash != msg.token) {
            fc_elog( logger, "Peer ${peer} sent a handshake with an invalid token.", ("peer", msg.p2p_address) );
            return false;
         }
         chain::public_key_type peer_key;
         try {
            peer_key = crypto::public_key(msg.sig, msg.token, true);
         }
         catch (const std::exception& /*e*/) {
            fc_elog( logger, "Peer ${peer} sent a handshake with an unrecoverable key.", ("peer", msg.p2p_address) );
            return false;
         }
         if((allowed_connections & (Producers | Specified)) && peer_key != msg.key) {
            fc_elog( logger, "Peer ${peer} sent a handshake with an unauthenticated key.", ("peer", msg.p2p_address) );
            return false;
         }
      }
      else if(allowed_connections & (Producers | Specified)) {
         fc_dlog( logger, "Peer sent a handshake with blank signature and token, but this node accepts only authenticated connections." );
         return false;
      }
      return true;
   }

   chain::public_key_type net_plugin_impl::get_authentication_key() const {
      if(!private_keys.empty())
         return private_keys.begin()->first;
      /*producer_plugin* pp = app().find_plugin<producer_plugin>();
      if(pp != nullptr && pp->get_state() == abstract_plugin::started)
         return pp->first_producer_public_key();*/
      return chain::public_key_type();
   }

   chain::signature_type net_plugin_impl::sign_compact(const chain::public_key_type& signer, const fc::sha256& digest) const
   {
      auto private_key_itr = private_keys.find(signer);
      if(private_key_itr != private_keys.end())
         return private_key_itr->second.sign(digest);
      if(producer_plug != nullptr && producer_plug->get_state() == abstract_plugin::started)
         return producer_plug->sign_compact(signer, digest);
      return chain::signature_type();
   }

   // call from connection strand
   bool connection::populate_handshake( handshake_message& hello ) {
      namespace sc = std::chrono;
      hello.network_version = net_version_base + net_version;
      auto chain_info = my_impl->get_chain_info();
      hello.last_irreversible_block_num = chain_info.lib_num;
      hello.last_irreversible_block_id = chain_info.lib_id;
      hello.head_num = chain_info.head_num;
      hello.head_id = chain_info.head_id;
      hello.chain_id = my_impl->chain_id;
      hello.node_id = my_impl->node_id;
      hello.key = my_impl->get_authentication_key();
      hello.time = sc::duration_cast<sc::nanoseconds>(sc::system_clock::now().time_since_epoch()).count();
      hello.token = fc::sha256::hash(hello.time);
      hello.sig = my_impl->sign_compact(hello.key, hello.token);
      // If we couldn't sign, don't send a token.
      if(hello.sig == chain::signature_type())
         hello.token = sha256();
      hello.p2p_address = my_impl->p2p_address;
      if( is_transactions_only_connection() ) hello.p2p_address += ":trx";
      if( is_blocks_only_connection() ) hello.p2p_address += ":blk";
      hello.p2p_address += " - " + hello.node_id.str().substr(0,7);
#if defined( __APPLE__ )
      hello.os = "osx";
#elif defined( __linux__ )
      hello.os = "linux";
#elif defined( _WIN32 )
      hello.os = "win32";
#else
      hello.os = "other";
#endif
      hello.agent = my_impl->user_agent_name;

      return true;
   }

   net_plugin::net_plugin()
      :my( new net_plugin_impl ) {
      my_impl = my.get();
   }

   net_plugin::~net_plugin() {
   }

   void net_plugin::set_program_options( options_description& /*cli*/, options_description& cfg )
   {
      cfg.add_options()
         ( "p2p-listen-endpoint", bpo::value<string>()->default_value( "0.0.0.0:9876" ), "The actual host:port used to listen for incoming p2p connections.")
         ( "p2p-server-address", bpo::value<string>(), "An externally accessible host:port for identifying this node. Defaults to p2p-listen-endpoint.")
         ( "p2p-peer-address", bpo::value< vector<string> >()->composing(),
           "The public endpoint of a peer node to connect to. Use multiple p2p-peer-address options as needed to compose a network.\n"
           "  Syntax: host:port[:<trx>|<blk>]\n"
           "  The optional 'trx' and 'blk' indicates to node that only transactions 'trx' or blocks 'blk' should be sent."
           "  Examples:\n"
           "    p2p.eos.io:9876\n"
           "    p2p.trx.eos.io:9876:trx\n"
           "    p2p.blk.eos.io:9876:blk\n")
         ( "p2p-max-nodes-per-host", bpo::value<int>()->default_value(def_max_nodes_per_host), "Maximum number of client nodes from any single IP address")
         ( "p2p-accept-transactions", bpo::value<bool>()->default_value(true), "Allow transactions received over p2p network to be evaluated and relayed if valid.")
         ( "p2p-auto-bp-peer", bpo::value< vector<string> >()->composing(),
           "The account and public p2p endpoint of a block producer node to automatically connect to when the it is in producer schedule proximity\n."
           "   Syntax: account,host:port\n"
           "   Example,\n"
           "     eosproducer1,p2p.eos.io:9876\n"
           "     eosproducer2,p2p.trx.eos.io:9876:trx\n"
           "     eosproducer3,p2p.blk.eos.io:9876:blk\n")
         ( "agent-name", bpo::value<string>()->default_value("EOS Test Agent"), "The name supplied to identify this node amongst the peers.")
         ( "allowed-connection", bpo::value<vector<string>>()->multitoken()->default_value({"any"}, "any"), "Can be 'any' or 'producers' or 'specified' or 'none'. If 'specified', peer-key must be specified at least once. If only 'producers', peer-key is not required. 'producers' and 'specified' may be combined.")
         ( "peer-key", bpo::value<vector<string>>()->composing()->multitoken(), "Optional public key of peer allowed to connect.  May be used multiple times.")
         ( "peer-private-key", boost::program_options::value<vector<string>>()->composing()->multitoken(),
           "Tuple of [PublicKey, WIF private key] (may specify multiple times)")
         ( "max-clients", bpo::value<int>()->default_value(def_max_clients), "Maximum number of clients from which connections are accepted, use 0 for no limit")
         ( "connection-cleanup-period", bpo::value<int>()->default_value(def_conn_retry_wait), "number of seconds to wait before cleaning up dead connections")
         ( "max-cleanup-time-msec", bpo::value<int>()->default_value(10), "max connection cleanup time per cleanup call in milliseconds")
         ( "p2p-dedup-cache-expire-time-sec", bpo::value<uint32_t>()->default_value(10), "Maximum time to track transaction for duplicate optimization")
         ( "net-threads", bpo::value<uint16_t>()->default_value(my->thread_pool_size),
           "Number of worker threads in net_plugin thread pool" )
         ( "sync-fetch-span", bpo::value<uint32_t>()->default_value(def_sync_fetch_span), "number of blocks to retrieve in a chunk from any individual peer during synchronization")
         ( "use-socket-read-watermark", bpo::value<bool>()->default_value(false), "Enable experimental socket read watermark optimization")
         ( "peer-log-format", bpo::value<string>()->default_value( "[\"${_name}\" - ${_cid} ${_ip}:${_port}] " ),
           "The string used to format peers when logging messages about them.  Variables are escaped with ${<variable name>}.\n"
           "Available Variables:\n"
           "   _name  \tself-reported name\n\n"
           "   _cid   \tassigned connection id\n\n"
           "   _id    \tself-reported ID (64 hex characters)\n\n"
           "   _sid   \tfirst 8 characters of _peer.id\n\n"
           "   _ip    \tremote IP address of peer\n\n"
           "   _port  \tremote port number of peer\n\n"
           "   _lip   \tlocal IP address connected to peer\n\n"
           "   _lport \tlocal port number connected to peer\n\n")
         ( "p2p-keepalive-interval-ms", bpo::value<int>()->default_value(def_keepalive_interval), "peer heartbeat keepalive message interval in milliseconds")

        ;
   }

   template<typename T>
   T dejsonify(const string& s) {
      return fc::json::from_string(s).as<T>();
   }

   void net_plugin::plugin_initialize( const variables_map& options ) {
      try {
         handle_sighup();
         fc_ilog( logger, "Initialize net plugin" );

         peer_log_format = options.at( "peer-log-format" ).as<string>();

         my->sync_master.reset( new sync_manager( options.at( "sync-fetch-span" ).as<uint32_t>()));

         my->connector_period = std::chrono::seconds( options.at( "connection-cleanup-period" ).as<int>());
         my->max_cleanup_time_ms = options.at("max-cleanup-time-msec").as<int>();
         my->txn_exp_period = def_txn_expire_wait;
         my->p2p_dedup_cache_expire_time_us = fc::seconds( options.at( "p2p-dedup-cache-expire-time-sec" ).as<uint32_t>() );
         my->resp_expected_period = def_resp_expected_wait;
         my->max_client_count = options.at( "max-clients" ).as<int>();
         my->max_nodes_per_host = options.at( "p2p-max-nodes-per-host" ).as<int>();
         my->p2p_accept_transactions = options.at( "p2p-accept-transactions" ).as<bool>();

         my->use_socket_read_watermark = options.at( "use-socket-read-watermark" ).as<bool>();
         my->keepalive_interval = std::chrono::milliseconds( options.at( "p2p-keepalive-interval-ms" ).as<int>() );
         EOS_ASSERT( my->keepalive_interval.count() > 0, chain::plugin_config_exception,
                     "p2p-keepalive_interval-ms must be greater than 0" );

         if( options.count( "p2p-keepalive-interval-ms" )) {
            my->heartbeat_timeout = std::chrono::milliseconds( options.at( "p2p-keepalive-interval-ms" ).as<int>() * 2 );
         }

         if( options.count( "p2p-listen-endpoint" ) && options.at("p2p-listen-endpoint").as<string>().length()) {
            my->p2p_address = options.at( "p2p-listen-endpoint" ).as<string>();
            EOS_ASSERT( my->p2p_address.length() <= max_p2p_address_length, chain::plugin_config_exception,
                        "p2p-listen-endpoint too long, must be less than ${m}", ("m", max_p2p_address_length) );
         }
         if( options.count( "p2p-server-address" ) ) {
            my->p2p_server_address = options.at( "p2p-server-address" ).as<string>();
            EOS_ASSERT( my->p2p_server_address.length() <= max_p2p_address_length, chain::plugin_config_exception,
                        "p2p_server_address too long, must be less than ${m}", ("m", max_p2p_address_length) );
         }

         my->thread_pool_size = options.at( "net-threads" ).as<uint16_t>();
         EOS_ASSERT( my->thread_pool_size > 0, chain::plugin_config_exception,
                     "net-threads ${num} must be greater than 0", ("num", my->thread_pool_size) );

         if( options.count( "p2p-peer-address" )) {
            auto v = options.at( "p2p-peer-address" ).as<vector<string> >();
            my->supplied_peers.insert(v.begin(), v.end());
         }
         if( options.count( "agent-name" )) {
            my->user_agent_name = options.at( "agent-name" ).as<string>();
            EOS_ASSERT( my->user_agent_name.length() <= max_handshake_str_length, chain::plugin_config_exception,
                        "agent-name too long, must be less than ${m}", ("m", max_handshake_str_length) );
         }

         if ( options.count( "p2p-auto-bp-peer")) {
            my->set_bp_peers(options.at( "p2p-auto-bp-peer" ).as<vector<string>>());
            my->for_each_bp_peer_address([this](const auto& addr) {
               EOS_ASSERT(my->supplied_peers.count(addr) == 0, chain::plugin_config_exception,
                          "\"${addr}\" should only appear in either p2p-peer-address or p2p-auto-bp-peer option, not both.",
                          ("addr",addr));
            });
         }

         if( options.count( "allowed-connection" )) {
            const std::vector<std::string> allowed_remotes = options["allowed-connection"].as<std::vector<std::string>>();
            for( const std::string& allowed_remote : allowed_remotes ) {
               if( allowed_remote == "any" )
                  my->allowed_connections |= net_plugin_impl::Any;
               else if( allowed_remote == "producers" )
                  my->allowed_connections |= net_plugin_impl::Producers;
               else if( allowed_remote == "specified" )
                  my->allowed_connections |= net_plugin_impl::Specified;
               else if( allowed_remote == "none" )
                  my->allowed_connections = net_plugin_impl::None;
            }
         }

         if( my->allowed_connections & net_plugin_impl::Specified )
            EOS_ASSERT( options.count( "peer-key" ),
                        plugin_config_exception,
                       "At least one peer-key must accompany 'allowed-connection=specified'" );

         if( options.count( "peer-key" )) {
            const std::vector<std::string> key_strings = options["peer-key"].as<std::vector<std::string>>();
            for( const std::string& key_string : key_strings ) {
               my->allowed_peers.push_back( dejsonify<chain::public_key_type>( key_string ));
            }
         }

         if( options.count( "peer-private-key" )) {
            const std::vector<std::string> key_id_to_wif_pair_strings = options["peer-private-key"].as<std::vector<std::string>>();
            for( const std::string& key_id_to_wif_pair_string : key_id_to_wif_pair_strings ) {
               auto key_id_to_wif_pair = dejsonify<std::pair<chain::public_key_type, std::string>>(
                     key_id_to_wif_pair_string );
               my->private_keys[key_id_to_wif_pair.first] = fc::crypto::private_key( key_id_to_wif_pair.second );
            }
         }

         my->chain_plug = app().find_plugin<chain_plugin>();
         EOS_ASSERT( my->chain_plug, chain::missing_chain_plugin_exception, ""  );
         my->chain_id = my->chain_plug->get_chain_id();
         fc::rand_pseudo_bytes( my->node_id.data(), my->node_id.data_size());
         const controller& cc = my->chain_plug->chain();

         if( cc.get_read_mode() == db_read_mode::IRREVERSIBLE ) {
            if( my->p2p_accept_transactions ) {
               my->p2p_accept_transactions = false;
               fc_wlog( logger, "p2p-accept-transactions set to false due to read-mode: irreversible" );
            }
         }
         if( my->p2p_accept_transactions ) {
            my->chain_plug->enable_accept_transactions();
         }

      } FC_LOG_AND_RETHROW()
   }

   void net_plugin::plugin_startup() {
      try {

      fc_ilog( logger, "my node_id is ${id}", ("id", my->node_id ));

      my->producer_plug = app().find_plugin<producer_plugin>();
      my->set_producer_accounts(my->producer_plug->producer_accounts());

      my->thread_pool.start( my->thread_pool_size, []( const fc::exception& e ) {
         fc_elog( logger, "Exception in net plugin thread pool, exiting: ${e}", ("e", e.to_detail_string()) );
         app().quit();
      } );

      my->dispatcher.reset( new dispatch_manager( my_impl->thread_pool.get_executor() ) );

      if( !my->p2p_accept_transactions && my->p2p_address.size() ) {
         fc_ilog( logger, "\n"
               "***********************************\n"
               "* p2p-accept-transactions = false *\n"
               "*    Transactions not forwarded   *\n"
               "***********************************\n" );
      }

      tcp::endpoint listen_endpoint;
      if( my->p2p_address.size() > 0 ) {
         auto host = my->p2p_address.substr( 0, my->p2p_address.find( ':' ));
         auto port = my->p2p_address.substr( host.size() + 1, my->p2p_address.size());
         tcp::resolver resolver( my->thread_pool.get_executor() );
         // Note: need to add support for IPv6 too?
         listen_endpoint = *resolver.resolve( tcp::v4(), host, port );

         my->acceptor.reset( new tcp::acceptor( my_impl->thread_pool.get_executor() ) );

         if( !my->p2p_server_address.empty() ) {
            my->p2p_address = my->p2p_server_address;
         } else {
            if( listen_endpoint.address().to_v4() == address_v4::any()) {
               boost::system::error_code ec;
               auto host = host_name( ec );
               if( ec.value() != boost::system::errc::success ) {

                  FC_THROW_EXCEPTION( fc::invalid_arg_exception,
                                      "Unable to retrieve host_name. ${msg}", ("msg", ec.message()));

               }
               auto port = my->p2p_address.substr( my->p2p_address.find( ':' ), my->p2p_address.size());
               my->p2p_address = host + port;
            }
         }
      }

      {
         chain::controller& cc = my->chain_plug->chain();
         cc.accepted_block_header.connect( [my = my]( const block_state_ptr& s ) {
            my->on_accepted_block_header( s );
         } );

         cc.accepted_block.connect( [my = my]( const block_state_ptr& s ) {
            my->on_accepted_block( s );
         } );
         cc.irreversible_block.connect( [my = my]( const block_state_ptr& s ) {
            my->on_irreversible_block( s );
         } );
      }

      {
         std::lock_guard<std::mutex> g( my->keepalive_timer_mtx );
         my->keepalive_timer.reset( new boost::asio::steady_timer( my->thread_pool.get_executor() ) );
      }

      my->incoming_transaction_ack_subscription = app().get_channel<compat::channels::transaction_ack>().subscribe(
            std::bind(&net_plugin_impl::transaction_ack, my.get(), std::placeholders::_1));

      app().executor().post(priority::highest, [my=my, listen_endpoint](){
         if( my->acceptor ) {
            try {
               my->acceptor->open(listen_endpoint.protocol());
               my->acceptor->set_option(tcp::acceptor::reuse_address(true));
               my->acceptor->bind(listen_endpoint);
               my->acceptor->listen();
            } catch (const std::exception& e) {
               fc_elog( logger, "net_plugin::plugin_startup failed to bind to port ${port}, ${what}",
                     ("port", listen_endpoint.port())("what", e.what()) );
               app().quit();
               return;
            }
            fc_ilog( logger, "starting listener, max clients is ${mc}",("mc",my->max_client_count) );
            my->start_listen_loop();
         }

         my->ticker();
         my->start_monitors();
         my->update_chain_info();
         for( const auto& seed_node : my->supplied_peers ) {
            my->connect( seed_node );
         }
      });

      } catch( ... ) {
         // always want plugin_shutdown even on exception
         plugin_shutdown();
         throw;
      }
   }

   void net_plugin::handle_sighup() {
      fc::logger::update( logger_name, logger );
   }

   void net_plugin::plugin_shutdown() {
      try {
         fc_ilog( logger, "shutdown.." );

         my->plugin_shutdown();   
         app().executor().post( 0, [me = my](){} ); // keep my pointer alive until queue is drained
         fc_ilog( logger, "exit shutdown" );
      }
      FC_CAPTURE_AND_RETHROW()
   }

   void net_plugin::register_metrics_listener(metrics_listener listener) {
      my->metrics.register_listener(listener);
   }

   /**
    *  Used to trigger a new connection from RPC API
    */
   string net_plugin::connect( const string& host ) {
      return my->connect( host );
   }

   string net_plugin_impl::connect( const string& host ) {
      std::lock_guard<std::shared_mutex> g( connections_mtx );
      if( find_connection( host ) )
         return "already connected";

      connection_ptr c = std::make_shared<connection>( host );
      fc_dlog( logger, "calling active connector: ${h}", ("h", host) );
      if( c->resolve_and_connect() ) {
         fc_dlog( logger, "adding new connection to the list: ${host} ${cid}", ("host", host)("cid", c->connection_id) );
         c->set_heartbeat_timeout( heartbeat_timeout );
         connections.insert( c );
      }
      return "added connection";
   }

   string net_plugin_impl::disconnect( const string& host ) {
      std::lock_guard<std::shared_mutex> g( connections_mtx );
      for( auto itr = connections.begin(); itr != connections.end(); ++itr ) {
         if( (*itr)->peer_address() == host ) {
            fc_ilog( logger, "disconnecting: ${cid}", ("cid", (*itr)->connection_id) );
            (*itr)->close();
            connections.erase(itr);
            return "connection removed";
         }
      }
      return "no known connection for host";
   }

   string net_plugin::disconnect( const string& host ) {
      return my->disconnect(host);
   }

   std::optional<connection_status> net_plugin::status( const string& host )const {
      std::shared_lock<std::shared_mutex> g( my->connections_mtx );
      auto con = my->find_connection( host );
      if( con )
         return con->get_status();
      return std::optional<connection_status>();
   }

   vector<connection_status> net_plugin::connections()const {
      vector<connection_status> result;
      std::shared_lock<std::shared_mutex> g( my->connections_mtx );
      result.reserve( my->connections.size() );
      for( const auto& c : my->connections ) {
         result.push_back( c->get_status() );
      }
      return result;
   }

   // call with connections_mtx
   connection_ptr net_plugin_impl::find_connection( const string& host )const {
      for( const auto& c : connections )
         if( c->peer_address() == host ) return c;
      return connection_ptr();
   }

   constexpr uint16_t net_plugin_impl::to_protocol_version(uint16_t v) {
      if (v >= net_version_base) {
         v -= net_version_base;
         return (v > net_version_range) ? 0 : v;
      }
      return 0;
   }

   bool net_plugin_impl::in_sync() const {
      return sync_master->is_in_sync();
   }

}
