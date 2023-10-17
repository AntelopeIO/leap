#pragma once

#include <boost/asio.hpp>

#include <eosio/chain/application.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/thread_utils.hpp>

namespace eosio::resource_monitor {
   template<typename SpaceProvider>
   class file_space_handler {
   public:
      file_space_handler(SpaceProvider&& space_provider)
      :space_provider(std::move(space_provider))
      {
      }

      void start(const std::vector<std::filesystem::path>& directories) {
         for ( auto& dir: directories ) {
            add_file_system( dir );

            // A directory like "data" contains subdirectories like
            // "block". Those subdirectories can mount on different
            // file systems. Make sure they are taken care of.
            for (std::filesystem::directory_iterator itr(dir); itr != std::filesystem::directory_iterator(); ++itr) {
               if (std::filesystem::is_directory(itr->path())) {
                  add_file_system( itr->path() );
               }
            }
         }

         thread_pool.start(thread_pool_size,
            []( const fc::exception& e ) {
              elog("Exception in resource monitor plugin thread pool, exiting: ${e}", ("e", e.to_detail_string()) );
              appbase::app().quit(); },
            [&]() { space_monitor_loop(); }
         );
      }

      // called on main thread from plugin shutdown()
      void stop() {
         // After thread pool stops, timer is not accessible within it.
         // In addition, timer's destructor will call cancel.
         // Therefore, no need to call cancel explicitly.
         thread_pool.stop();
      }

      void set_sleep_time(uint32_t sleep_time) {
         sleep_time_in_secs = sleep_time;
      }

      // warning_threshold must be less than shutdown_threshold.
      // set them together so it is simpler to check.
      void set_threshold(uint32_t new_threshold, uint32_t new_warning_threshold) {
         EOS_ASSERT(new_warning_threshold < new_threshold, chain::plugin_config_exception,
                    "warning_threshold ${new_warning_threshold} must be less than threshold ${new_threshold}", ("new_warning_threshold", new_warning_threshold) ("new_threshold", new_threshold));

         shutdown_threshold = new_threshold;
         warning_threshold = new_warning_threshold;
      }

      void set_absolute(uint64_t new_v, uint64_t new_warning_v) {
         EOS_ASSERT(new_warning_v > new_v, chain::plugin_config_exception,
                    "absolute warning value ${w} must be more than absolute threshold ${n}", ("w", new_warning_v)("n", new_v));

         shutdown_absolute = new_v;
         warning_absolute = new_warning_v;
      }

      void set_shutdown_on_exceeded(bool new_shutdown_on_exceeded) {
         shutdown_on_exceeded = new_shutdown_on_exceeded;
      }

      void set_warning_interval(uint32_t new_warning_interval) {
         warning_interval = new_warning_interval;
      }

      bool is_threshold_exceeded() {
         // Go over each monitored file system
         for (auto& fs: filesystems) {
            std::error_code ec;
            auto info = space_provider.get_space(fs.path_name, ec);
            if ( ec ) {
               // As the system is running and this plugin is not a critical
               // part of the system, we should not exit.
               // Just report the failure and continue;
               wlog( "Unable to get space info for ${path_name}: [code: ${ec}] ${message}. Ignore this failure.",
                  ("path_name", fs.path_name.string())
                  ("ec", ec.value())
                  ("message", ec.message()));

               continue;
            }

            if ( info.available < fs.shutdown_available ) {
               if (output_threshold_warning || shutdown_on_exceeded) {
                  elog("Space usage warning: ${path}'s file system exceeded threshold ${threshold_desc}, "
                       "available: ${available} GiB, Capacity: ${capacity} GiB, shutdown_available: ${shutdown_available} GiB",
                       ("path", fs.path_name.string())("threshold_desc", threshold_desc())
                       ("available", to_gib(info.available))("capacity", to_gib(info.capacity))
                       ("shutdown_available", to_gib(fs.shutdown_available)));
               }
               return true;
            } else if ( info.available < fs.warning_available && output_threshold_warning ) {
               wlog("Space usage warning: ${path}'s file system approaching threshold. available: ${available} GiB, warning_available: ${warning_available} GiB",
                    ("path", fs.path_name.string())("available", to_gib(info.available))("warning_available", to_gib(fs.warning_available)));
               if ( shutdown_on_exceeded) {
                  wlog("nodeos will shutdown when space usage exceeds threshold ${threshold_desc}", ("threshold_desc", threshold_desc()));
               }
            }
         }

         return false;
      }

      void add_file_system(const std::filesystem::path& path_name) {
         // Get detailed information of the path
         struct stat statbuf{};
         auto status = space_provider.get_stat(path_name.string().c_str(), &statbuf);
         EOS_ASSERT(status == 0, chain::plugin_config_exception,
                    "Failed to run stat on ${path} with status ${status}", ("path", path_name.string())("status", status));

         ilog("${path_name}'s file system to be monitored", ("path_name", path_name.string()));

         // If the file system containing the path is already
         // in the filesystem list, do not add it again
         for (auto& fs: filesystems) {
            if (statbuf.st_dev == fs.st_dev) { // Two files belong to the same file system if their device IDs are the same.
               dlog("${path_name}'s file system already monitored", ("path_name", path_name.string()));

               return;
            }
         }

         // For efficiency, precalculate threshold values to avoid calculating it
         // everytime we check space usage. Since std::filesystem::space returns
         // available amount, we use minimum available amount as threshold.
         std::error_code ec;
         auto info = space_provider.get_space(path_name, ec);
         EOS_ASSERT(!ec, chain::plugin_config_exception,
            "Unable to get space info for ${path_name}: [code: ${ec}] ${message}",
            ("path_name", path_name.string())
            ("ec", ec.value())
            ("message", ec.message()));

         uintmax_t shutdown_available = shutdown_absolute;
         uintmax_t warning_available = warning_absolute;
         if (shutdown_absolute == 0) {
            shutdown_available = (100 - shutdown_threshold) * (info.capacity / 100); // (100 - shutdown_threshold)/100 is the percentage of minimum number of available bytes the file system must maintain
            warning_available = (100 - warning_threshold) * (info.capacity / 100);
         }

         // Add to the list
         filesystems.emplace_back(statbuf.st_dev, shutdown_available, path_name, warning_available);

         ilog("${path_name}'s file system monitored. shutdown_available: ${shutdown_available} GiB, capacity: ${capacity} GiB, threshold: ${threshold_desc}",
              ("path_name", path_name.string())("shutdown_available", to_gib(shutdown_available)) ("capacity", to_gib(info.capacity))("threshold_desc", threshold_desc()) );
      }

   // on resmon thread
   void space_monitor_loop() {
      if ( is_threshold_exceeded() && shutdown_on_exceeded ) {
         elog("Gracefully shutting down, exceeded file system configured threshold.");
         appbase::app().quit(); // This will gracefully stop Nodeos
         return;
      }
      update_warning_interval_counter();

      timer.expires_from_now( boost::posix_time::seconds( sleep_time_in_secs ));
      timer.async_wait([this](const auto& ec) {
         if ( ec ) {
            // No need to check if ec is operation_aborted (cancelled),
            // as cancel callback will never be make it here after thread_pool
            // is stopped, even though cancel is called in the timer's
            // destructor.
            wlog("Exit due to error: ${ec}, message: ${message}",
                 ("ec", ec.value())
                 ("message", ec.message()));
            return;
         } else {
            // Loop over
            space_monitor_loop();
         }
      });
   }

   private:
      SpaceProvider space_provider;

      static constexpr size_t thread_pool_size = 1;
      eosio::chain::named_thread_pool<struct resmon> thread_pool;

      boost::asio::deadline_timer timer {thread_pool.get_executor()};

      uint32_t sleep_time_in_secs {2};
      uint32_t shutdown_threshold {90};
      uint32_t warning_threshold {85};
      uint64_t shutdown_absolute {0};
      uint64_t warning_absolute {0};
      bool     shutdown_on_exceeded {true};

      struct   filesystem_info {
         dev_t      st_dev; // device id of file system containing "file_path"
         uintmax_t  shutdown_available {0}; // minimum number of available bytes the file system must maintain
         std::filesystem::path  path_name;
         uintmax_t  warning_available {0};  // warning is issued when available number of bytes drops below warning_available

         filesystem_info(dev_t dev, uintmax_t available, const std::filesystem::path& path, uintmax_t warning)
         : st_dev(dev),
         shutdown_available(available),
         path_name(path),
         warning_available(warning)
         {
         }
      };

      // Stores file systems to be monitored. Duplicate
      // file systems are not stored.
      std::vector<filesystem_info> filesystems;

      uint32_t warning_interval {1};
      uint32_t warning_interval_counter {1};
      bool     output_threshold_warning {true};

   private:
      uint64_t to_gib(uint64_t bytes) {
         return bytes/1024/1024/1024;
      }

      std::string threshold_desc() {
         if (shutdown_absolute > 0 ) {
            return std::to_string(to_gib(shutdown_absolute)) + " GiB";
         } else {
            return std::to_string(shutdown_threshold) + "%";
         }
      }

      void update_warning_interval_counter() {
         if ( warning_interval_counter == warning_interval ) {
            output_threshold_warning = true;
            warning_interval_counter = 1;
         } else {
            output_threshold_warning = false;
            ++warning_interval_counter;
         }
      }
   };
}
