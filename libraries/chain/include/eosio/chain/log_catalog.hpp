#pragma once
#include <fc/io/cfile.hpp>
#include <fc/io/datastream.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <regex>
#include <map>

namespace eosio {
namespace chain {

namespace bfs = boost::filesystem;

template <typename Lambda>
void for_each_file_in_dir_matches(const bfs::path& dir, std::string_view pattern, Lambda&& lambda) {
   const std::regex        my_filter(pattern.begin(), pattern.size());
   std::smatch             what;
   bfs::directory_iterator end_itr; // Default ctor yields past-the-end
   for (bfs::directory_iterator p(dir); p != end_itr; ++p) {
      // Skip if not a file
      if (!bfs::is_regular_file(p->status()))
         continue;
      // skip if it does not match the pattern
      if (!std::regex_match(p->path().filename().string(), what, my_filter))
         continue;
      lambda(p->path());
   }
}

struct null_verifier {
   template <typename LogData>
   void verify(const LogData&, const bfs::path&) {}
};

template <typename LogData, typename LogIndex, typename LogVerifier = null_verifier>
struct log_catalog {
   using block_num_t = uint32_t;

   struct mapped_type {
      block_num_t last_block_num = 0;
      bfs::path   filename_base;
   };
   using collection_t              = std::map<block_num_t, mapped_type>;
   using size_type                 = typename collection_t::size_type;
   static constexpr size_type npos = std::numeric_limits<size_type>::max();

   bfs::path    retained_dir;
   bfs::path    archive_dir;
   size_type    max_retained_files = std::numeric_limits<size_type>::max();
   collection_t collection;
   size_type    active_index = npos;
   LogData      log_data;
   LogIndex     log_index;
   LogVerifier  verifier;

   bool empty() const { return collection.empty(); }

   block_num_t first_block_num() const {
      if (empty())
         return std::numeric_limits<block_num_t>::max();
      return collection.begin()->first;
   }

   block_num_t last_block_num() const {
      if (empty())
         return std::numeric_limits<block_num_t>::min();
      return collection.rbegin()->second.last_block_num;
   }

   static bfs::path make_absolute_dir(const bfs::path& base_dir, bfs::path new_dir) {
      if (new_dir.is_relative())
         new_dir = base_dir / new_dir;

      if (!bfs::is_directory(new_dir))
         bfs::create_directories(new_dir);

      return new_dir;
   }

   void open(const bfs::path& log_dir, const bfs::path& retained_path, const bfs::path& archive_path, const char* name,
             const char* suffix_pattern = R"(-\d+-\d+\.log)") {

      retained_dir = make_absolute_dir(log_dir, retained_path.empty() ? log_dir : retained_path);
      if (!archive_path.empty()) {
         archive_dir = make_absolute_dir(log_dir, archive_path);
      }

      std::string pattern = std::string(name) + suffix_pattern;
      for_each_file_in_dir_matches(retained_dir, pattern, [this](bfs::path path) {
         auto log_path               = path;
         const auto& index_path      = path.replace_extension("index");
         auto path_without_extension = log_path.parent_path() / log_path.stem().string();

         LogData log(log_path);

         verifier.verify(log, log_path);

         // check if index file matches the log file
         if (!index_matches_data(index_path, log)) {
            ilog("Recreating index for: ${i}", ("i", index_path.string()));
            log.construct_index( index_path );
         }

         auto existing_itr = collection.find(log.first_block_num());
         if (existing_itr != collection.end()) {
            if (log.last_block_num() <= existing_itr->second.last_block_num) {
               wlog("${log_path} contains the overlapping range with ${existing_path}.log, dropping ${log_path} "
                    "from catalog",
                    ("log_path", log_path.string())("existing_path", existing_itr->second.filename_base.string()));
               return;
            } else {
               wlog(
                   "${log_path} contains the overlapping range with ${existing_path}.log, droping ${existing_path}.log "
                   "from catelog",
                   ("log_path", log_path.string())("existing_path", existing_itr->second.filename_base.string()));
            }
         }

         collection.insert_or_assign(log.first_block_num(), mapped_type{log.last_block_num(), std::move(path_without_extension)});
      });
   }

   bool index_matches_data(const bfs::path& index_path, LogData& log) const {
      if (!bfs::exists(index_path))
         return false;

      auto num_blocks_in_index = bfs::file_size(index_path) / sizeof(uint64_t);
      if (num_blocks_in_index != log.num_blocks())
         return false;

      // make sure the last 8 bytes of index and log matches
      fc::cfile index_file;
      index_file.set_file_path(index_path);
      index_file.open("r");
      index_file.seek_end(-sizeof(uint64_t));
      uint64_t pos;
      index_file.read(reinterpret_cast<char*>(&pos), sizeof(pos));
      return pos == log.last_block_position();
   }

   std::optional<uint64_t> get_block_position(uint32_t block_num) {
      try {
         if (active_index != npos) {
            auto active_item = std::next(collection.begin(), active_index);
            if (active_item->first <= block_num && block_num <= active_item->second.last_block_num) {
               return log_index.nth_block_position(block_num - log_data.first_block_num());
            }
         }
         if (block_num < first_block_num())
            return {};

         auto it = --collection.upper_bound(block_num);

         if (block_num <= it->second.last_block_num) {
            auto name = it->second.filename_base;
            log_data.open(name.replace_extension("log"));
            log_index.open(name.replace_extension("index"));
            active_index = std::distance(collection.begin(), it); //collection.index_of(it);
            return log_index.nth_block_position(block_num - log_data.first_block_num());
         }
         return {};
      } catch (...) {
         active_index = npos;
         return {};
      }
   }

   fc::datastream<fc::cfile>* ro_stream_for_block(uint32_t block_num) {
      auto pos = get_block_position(block_num);
      if (pos) {
         return &log_data.ro_stream_at(*pos);
      }
      return nullptr;
   }

   template <typename ...Rest>
   auto ro_stream_for_block(uint32_t block_num, Rest&& ...rest) -> std::optional<decltype( std::declval<LogData>().ro_stream_at(0, std::forward<Rest&&>(rest)...))> {
      auto pos = get_block_position(block_num);
      if (pos) {
         return log_data.ro_stream_at(*pos, std::forward<Rest&&>(rest)...);
      }
      return {};
   }

   std::optional<block_id_type> id_for_block(uint32_t block_num) {
      auto pos = get_block_position(block_num);
      if (pos) {
         return log_data.block_id_at(*pos);
      }
      return {};
   }

   static void rename_if_not_exists(bfs::path old_name, bfs::path new_name) {
      if (!bfs::exists(new_name)) {
         bfs::rename(old_name, new_name);
      } else {
         bfs::remove(old_name);
         wlog("${new_name} already exists, just removing ${old_name}",
              ("old_name", old_name.string())("new_name", new_name.string()));
      }
   }

   static void rename_bundle(bfs::path orig_path, bfs::path new_path) {
      rename_if_not_exists(orig_path.replace_extension(".log"), new_path.replace_extension(".log"));
      rename_if_not_exists(orig_path.replace_extension(".index"), new_path.replace_extension(".index"));
   }

   /// Add a new entry into the catalog.
   ///
   /// Notice that \c start_block_num must be monotonically increasing between the invocations of this function
   /// so that the new entry would be inserted at the 'end' of the map; otherwise, \c active_index would be
   /// invalidated and the mapping between the log data their block range would be wrong. This function is only used
   /// during the splitting of block log. Using this function for other purpose should make sure if the monotonically
   /// increasing block num guarantee can be met.
   void add(uint32_t start_block_num, uint32_t end_block_num, const bfs::path& dir, const char* name) {

      const int bufsize = 64;
      char      buf[bufsize];
      snprintf(buf, bufsize, "%s-%u-%u", name, start_block_num, end_block_num);
      bfs::path new_path = retained_dir / buf;
      rename_bundle(dir / name, new_path);
      size_type items_to_erase = 0;
      collection.emplace(start_block_num, mapped_type{end_block_num, std::move(new_path)});
      if (collection.size() >= max_retained_files) {
         items_to_erase =
            max_retained_files > 0 ? collection.size() - max_retained_files : collection.size();
         auto end = std::next( collection.begin(), items_to_erase);

         for (auto it = collection.begin(); it != end; ++it) {
            auto orig_name = it->second.filename_base;
            if (archive_dir.empty()) {
               // delete the old files when no backup dir is specified
               bfs::remove(orig_name.replace_extension("log"));
               bfs::remove(orig_name.replace_extension("index"));
            } else {
               // move the archive dir
               rename_bundle(orig_name, archive_dir / orig_name.filename());
            }
         }
         collection.erase(collection.begin(), end);
         active_index = active_index == npos || active_index < items_to_erase
                        ? npos
                        : active_index - items_to_erase;
      }
   }

   /// Truncate the catalog so that the log/index bundle containing the block with \c block_num
   /// would be rename to \c new_name; the log/index bundles with blocks strictly higher
   /// than \c block_num would be deleted, and all the renamed/removed entries would be erased
   /// from the catalog.
   ///
   /// \return if nonzero, it's the starting block number for the log/index bundle being renamed.
   uint32_t truncate(uint32_t block_num, bfs::path new_name) {
      if (collection.empty())
         return 0;

      auto remove_files = [](typename collection_t::const_reference v) {
         auto name = v.second.filename_base;
         bfs::remove(name.replace_extension("log"));
         bfs::remove(name.replace_extension("index"));
      };

      active_index = npos;
      auto it = collection.upper_bound(block_num);

      if (it == collection.begin() || block_num > std::prev(it)->second.last_block_num) {
         std::for_each(it, collection.end(), remove_files);
         collection.erase(it, collection.end());
         return 0;
      } else {
         auto truncate_it = --it;
         auto name        = truncate_it->second.filename_base;
         bfs::rename(name.replace_extension("log"), new_name.replace_extension("log"));
         bfs::rename(name.replace_extension("index"), new_name.replace_extension("index"));
         std::for_each(std::next(truncate_it), collection.end(), remove_files);
         auto result = truncate_it->first;
         collection.erase(truncate_it, collection.end());
         return result;
      }
   }
};

} // namespace chain
} // namespace eosio
