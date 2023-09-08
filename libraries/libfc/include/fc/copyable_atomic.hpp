#pragma once
#include <fc/reflect/reflect.hpp>
#include <fc/atomic_shared_ptr.hpp>
#include <atomic>
#include <memory>

namespace fc {

   /**
    * std::atomic is not copyable. Provide a wrapper to easily allow it to be copied when appropriate.
    * Note appropriate depends on use case. This implementation does a simple load/store.
    */
   template <typename T>
   struct copyable_atomic
   {
      std::atomic<T> value{};

      copyable_atomic() noexcept = default;
      explicit copyable_atomic(T v) noexcept : value(v) {}

      copyable_atomic(const copyable_atomic& rhs) noexcept { value.store(rhs.value.load()); }
      copyable_atomic& operator=(const copyable_atomic& rhs) noexcept { value.store(rhs.value.load()); return *this; }

      T load() const noexcept { return value.load(); }
      void store(T v) noexcept { value.store(v); }
   };

   /**
    * atomic_shared_ptr is not copyable for same reason as atomic is not copyable. Provide a wrapper to easily allow
    * it to be copied when appropriate.
    */
   template <typename T>
   struct copyable_atomic_shared_ptr {
      fc::atomic_shared_ptr<T> value{};

      copyable_atomic_shared_ptr() noexcept = default;
      explicit copyable_atomic_shared_ptr(const std::shared_ptr<T>& v) noexcept : value(v) {}

      copyable_atomic_shared_ptr(const copyable_atomic_shared_ptr& rhs) noexcept { value.store(rhs.value.load()); }
      copyable_atomic_shared_ptr& operator=(const copyable_atomic_shared_ptr& rhs) noexcept { value.store(rhs.value.load()); return *this; }

      std::shared_ptr<T> load() const noexcept { return value.load(); }
      void store(const std::shared_ptr<T>& v) noexcept { value.store(v); }
   };

} // namespace fc

FC_REFLECT_TEMPLATE( (typename T), fc::copyable_atomic<T>, (value) )
FC_REFLECT_TEMPLATE( (typename T), fc::copyable_atomic_shared_ptr<T>, (value) )
