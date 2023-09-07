#pragma once
#include <fc/reflect/reflect.hpp>
#include <atomic>

namespace fc {

   /**
    * std::atomic is not copyable. Provide a wrapper to easily allow it to be copied when appropriate.
    * Note appropriate depends on use case. This implementation does a simple load/store.
    */
   template <typename T>
   struct copyable_atomic
   {
      std::atomic<T> value{};

      copyable_atomic() = default;
      explicit copyable_atomic(T v) : value(v) {}

      copyable_atomic(const copyable_atomic& rhs) { value.store(rhs.value.load()); }
      copyable_atomic& operator=(const copyable_atomic& rhs) { value.store(rhs.value.load()); return *this; }

      T load() const { return value.load(); }
      void store(T v) { value.store(v); }
   };

} // namespace fc

FC_REFLECT_TEMPLATE( (typename T), fc::copyable_atomic<T>, (value) )
