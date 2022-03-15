#pragma once
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/filesystem.hpp>
#include <fc/exception/exception.hpp>
#include <fc/io/cfile.hpp>
#include <fc/io/datastream.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/raw.hpp>
#include <fstream>

namespace eosio::chain_apis {
   namespace bfs = boost::filesystem;

   /**
    * @class tracked_storage
    * @brief tracks the size of storage allocated to its underlying multi_index
    *
    * This class wraps a multi_index container and tracks the memory allocated as
    * the container creates, modifies, and deletes.
    */

   template <typename ContainerType, typename Value, typename PrimaryTag>
   class tracked_storage {
   private:
      uint64_t _size = 0;
      ContainerType _index;
   public:
      typedef PrimaryTag primary_tag;
      typedef typename ContainerType::template index<primary_tag>::type primary_index_type;

      tracked_storage() {

      }

      void read(fc::cfile_datastream& ds, uint64_t max_memory) {
         auto container_size = _index.size();
         fc::raw::unpack(ds, container_size);
         for (uint64_t i = 0; i < container_size && size() < max_memory; ++i) {
            Value v;
            fc::raw::unpack(ds, v);
            insert(std::move(v));
         }
      }

      void write(fc::cfile& dat_content) const {
         const auto container_size = _index.size();
         dat_content.write( reinterpret_cast<const char*>(&container_size), sizeof(container_size) );
         const primary_index_type& primary_idx = _index.template get<primary_tag>();
         
         for (auto itr = primary_idx.cbegin(); itr != primary_idx.cend(); ++itr) {
            auto data = fc::raw::pack(*itr);
            dat_content.write(data.data(), data.size());
         }
      }

      static fc::cfile read_from_file(const bfs::path& dir, const std::string& filename, const uint32_t magic_number,
         const uint32_t min_supported_version, const uint32_t max_supported_version) {
         if (!fc::is_directory(dir))
            fc::create_directories(dir);
         
         auto dat_file = dir / filename;
         fc::cfile dat_content;
         dat_content.set_file_path(dat_file);
         dat_content.open(fc::cfile::update_rw_mode);
         auto ds = dat_content.create_datastream();

         // validate totem
         uint32_t totem = 0;
         fc::raw::unpack( ds, totem );
         if( totem != magic_number) {
            FC_THROW_EXCEPTION(fc::parse_error_exception,
                               "File '${filename}' has unexpected magic number: ${actual_totem}. Expected ${expected_totem}",
                               ("filename", dat_file.generic_string())
                               ("actual_totem", totem)
                               ("expected_totem", magic_number));
         }

         // validate version
         uint32_t version = 0;
         fc::raw::unpack( ds, version );
         if( version < min_supported_version || version > max_supported_version) {
            FC_THROW_EXCEPTION(fc::parse_error_exception,
                               "Unsupported version of file '${filename}'. "
                               "Version is ${version} while code supports version(s) [${min},${max}]",
                               ("filename", dat_file.generic_string())
                               ("version", version)
                               ("min", min_supported_version)
                               ("max", max_supported_version));
         }
         return dat_content;
      }

      static fc::cfile write_to_file(const bfs::path& dir, const std::string& filename, const uint32_t magic_number, const uint32_t current_version) {
         if (!fc::is_directory(dir))
            fc::create_directories(dir);

         auto dat_file = dir / filename;
         fc::cfile dat_content;
         dat_content.set_file_path(dat_file.generic_string().c_str());
         dat_content.open( fc::cfile::truncate_rw_mode );
         dat_content.write( reinterpret_cast<const char*>(&magic_number), sizeof(magic_number) );
         dat_content.write( reinterpret_cast<const char*>(&current_version), sizeof(current_version) );
         return dat_content;
      }

      void insert(Value&& obj) {
         _index.insert(obj);
         _size += obj.size();
      }

      template<typename Key>
      typename primary_index_type::iterator find(const Key& key) {
         primary_index_type& primary_idx = _index.template get<primary_tag>();
         return primary_idx.find(key);
      }

      template<typename Key>
      typename primary_index_type::const_iterator find(const Key& key) const {
         const primary_index_type& primary_idx = _index.template get<primary_tag>();
         return primary_idx.find(key);
      }

      template<typename Lam>
      void modify(typename primary_index_type::iterator itr, Lam&& lam) {
         const auto orig_size = itr->size();
         _index.modify( itr, std::move(lam));
         _size += itr->size() - orig_size;
      }

      template<typename Key>
      void erase(const Key& key) {
         auto itr = _index.find(key);
         if (itr == _index.end())
            return;

         _size -= itr->size();
         _index.erase(itr);
      }

      uint64_t size() const {
         return _size;
      }

      const ContainerType& index() const {
         return _index;
      }
   };
}
