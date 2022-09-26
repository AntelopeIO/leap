#pragma once
#include <fc/exception/exception.hpp>
#include <fc/io/cfile.hpp>
#include <fc/io/datastream.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/raw.hpp>
#include <fstream>

namespace fc {

   /**
    * Specialize tracked::memory_size() if obj does not provide a memory_size() method that represents memory size.
    */
   namespace tracked {
      template<typename T>
      size_t memory_size( const T& obj ) {
         return obj.memory_size();
      }
   }

   /**
    * @class tracked_storage
    * @brief tracks the size of storage allocated to its underlying multi_index
    *
    * This class wraps a multi_index container and tracks the memory allocated as
    * the container creates, modifies, and deletes. It also provides read and write
    * methods for persistence.
    * 
    * Requires ContainerType::value_type to have a size() method that represents the
    * memory used for that object or specialized tracked::size() and is required to
    * be a pack/unpack-able type.
    */

   template <typename ContainerType>
   class tracked_storage {
   private:
      size_t _memory_size = 0;
      ContainerType _index;
   public:
      typedef typename ContainerType::template nth_index<0>::type primary_index_type;

      tracked_storage() = default;

      // read in the contents of a persisted tracked_storage and prevent reading in
      // more stored objects once max_memory is reached.
      // returns true if entire persisted tracked_storage was read
      bool read(fc::cfile_datastream& ds, size_t max_memory) {
         auto container_size = _index.size();
         fc::raw::unpack(ds, container_size);
         for (size_t i = 0; i < container_size; ++i) {
            if (memory_size() >= max_memory) {
               return false;
            }
            typename ContainerType::value_type v;
            fc::raw::unpack(ds, v);
            insert(std::move(v));
         }

         return true;
      }

      void write(fc::cfile& dat_content) const {
         const auto container_size = _index.size();
         dat_content.write( reinterpret_cast<const char*>(&container_size), sizeof(container_size) );
         
         for (const auto& item : _index) {
            auto data = fc::raw::pack(item);
            dat_content.write(data.data(), data.size());
         }
      }

      std::pair<typename primary_index_type::iterator, bool> insert(typename ContainerType::value_type obj) {
         const auto size = tracked::memory_size(obj);
         auto result = _index.insert(std::move(obj));
         if (result.second) {
            _memory_size += size;
         }
         return result;
      }

      template<typename Key>
      typename primary_index_type::iterator find(const Key& key) {
         primary_index_type& primary_idx = _index.template get<0>();
         return primary_idx.find(key);
      }

      template<typename Key>
      typename primary_index_type::const_iterator find(const Key& key) const {
         const primary_index_type& primary_idx = _index.template get<0>();
         return primary_idx.find(key);
      }

      template<typename Lam>
      void modify(typename primary_index_type::iterator itr, Lam lam) {
         _memory_size -= tracked::memory_size(*itr);
         if (_index.modify( itr, std::move(lam))) {
            _memory_size += tracked::memory_size(*itr);
         }
      }

      template<typename Key>
      void erase(const Key& key) {
         auto itr = _index.find(key);
         if (itr == _index.end())
            return;

         _memory_size -= tracked::memory_size(*itr);
         _index.erase(itr);
      }

      void erase(typename primary_index_type::iterator itr) {
         _memory_size -= tracked::memory_size(*itr);
         _index.erase(itr);
      }

      size_t memory_size() const {
         return _memory_size;
      }

      const ContainerType& index() const {
         return _index;
      }
   };
}
