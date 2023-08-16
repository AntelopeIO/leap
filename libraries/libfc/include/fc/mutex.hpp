#pragma once

// Enable thread safety attributes only with clang.
// The attributes can be safely erased when compiling with other compilers.
#if defined(__clang__) && (!defined(SWIG))
#define THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x) // no-op
#endif

#define CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

#define SCOPED_CAPABILITY THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define PT_GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define ACQUIRED_BEFORE(...) THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define ACQUIRED_AFTER(...) THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define REQUIRES(...) THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define REQUIRES_SHARED(...) THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define ACQUIRE(...) THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define ACQUIRE_SHARED(...) THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

#define RELEASE(...) THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#define RELEASE_SHARED(...) THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

#define RELEASE_GENERIC(...) THREAD_ANNOTATION_ATTRIBUTE__(release_generic_capability(__VA_ARGS__))

#define TRY_ACQUIRE(...) THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

#define TRY_ACQUIRE_SHARED(...) THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

#define EXCLUDES(...) THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

#define ASSERT_CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

#define ASSERT_SHARED_CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

#define RETURN_CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define NO_THREAD_SAFETY_ANALYSIS THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

#include <mutex>
#include <shared_mutex>

namespace fc {

// Defines an annotated interface for mutexes.
// These methods can be implemented to use any internal mutex implementation.
class CAPABILITY("mutex") mutex {
private:
   std::mutex mutex_;

public:
   // Acquire/lock this mutex exclusively.  Only one thread can have exclusive
   // access at any one time.  Write operations to guarded data require an
   // exclusive lock.
   void lock() ACQUIRE() { mutex_.lock(); }

   // Release/unlock an exclusive mutex.
   void unlock() RELEASE() { mutex_.unlock(); }

   // Try to acquire the mutex.  Returns true on success, and false on failure.
   bool try_lock() TRY_ACQUIRE(true) { return mutex_.try_lock(); }
};

// Defines an annotated interface for mutexes.
// These methods can be implemented to use any internal mutex implementation.
class CAPABILITY("shared_mutex") shared_mutex {
private:
   std::shared_mutex mutex_;

public:
   // Acquire/lock this mutex exclusively.  Only one thread can have exclusive
   // access at any one time.  Write operations to guarded data require an
   // exclusive lock.
   void lock() ACQUIRE() { mutex_.lock(); }

   // Acquire/lock this mutex for read operations, which require only a shared
   // lock.  This assumes a multiple-reader, single writer semantics.  Multiple
   // threads may acquire the mutex simultaneously as readers, but a writer
   // must wait for all of them to release the mutex before it can acquire it
   // exclusively.
   void lock_shared() ACQUIRE_SHARED() { mutex_.lock_shared(); }

   // Release/unlock an exclusive mutex.
   void unlock() RELEASE() { mutex_.unlock(); }

   // Release/unlock a shared mutex.
   void unlock_shared() RELEASE_SHARED() { mutex_.unlock_shared(); }

   // Try to acquire the mutex.  Returns true on success, and false on failure.
   bool try_lock() TRY_ACQUIRE(true) { return mutex_.try_lock(); }

   // Try to acquire the mutex for read operations.
   bool try_lock_shared() TRY_ACQUIRE_SHARED(true) { return mutex_.try_lock_shared(); }

   // Assert that this mutex is currently held by the calling thread.
   // void AssertHeld() ASSERT_CAPABILITY(this);

   // Assert that is mutex is currently held for read operations.
   // void AssertReaderHeld() ASSERT_SHARED_CAPABILITY(this);

   // For negative capabilities.
   // const Mutex& operator!() const { return *this; }
};

// Tag types for selecting a constructor.
struct adopt_lock_t {} inline constexpr adopt_lock = {};
struct defer_lock_t {} inline constexpr defer_lock = {};
struct shared_lock_t {} inline constexpr shared_lock = {};

// LockGuard is an RAII class that acquires a mutex in its constructor, and
// releases it in its destructor.
template <typename M>
class SCOPED_CAPABILITY lock_guard {
private:
   M& mut;

public:
   // Acquire mu, implicitly acquire *this and associate it with mu.
   [[nodiscard]] lock_guard(M& mu) ACQUIRE(mu)
      : mut(mu) {
      mu.lock();
   }

   // Assume mu is held, implicitly acquire *this and associate it with mu.
   [[nodiscard]] lock_guard(M& mu, adopt_lock_t) REQUIRES(mu)
      : mut(mu) {}

   ~lock_guard() RELEASE() { mut.unlock(); }
};

// unique_lock is an RAII class that acquires a mutex in its constructor, and
// releases it in its destructor.
template <typename M>
class SCOPED_CAPABILITY unique_lock {
private:
   using mutex_type = M;

   M*   mut;
   bool locked;

public:
   [[nodiscard]] unique_lock() noexcept
      : mut(nullptr)
      , locked(false) {}

   // Acquire mu, implicitly acquire *this and associate it with mu.
   [[nodiscard]] explicit unique_lock(M& mu) ACQUIRE(mu)
      : mut(&mu)
      , locked(true) {
      mut->lock();
   }

   // Assume mu is held, implicitly acquire *this and associate it with mu.
   [[nodiscard]] unique_lock(M& mu, adopt_lock_t) REQUIRES(mu)
      : mut(&mu)
      , locked(true) {}

   // Assume mu is not held, implicitly acquire *this and associate it with mu.
   [[nodiscard]] unique_lock(M& mu, defer_lock_t) EXCLUDES(mu)
      : mut(mu)
      , locked(false) {}

   // Release *this and all associated mutexes, if they are still held.
   // There is no warning if the scope was already unlocked before.
   ~unique_lock() RELEASE() {
      if (locked)
         mut->unlock();
   }

   // Acquire all associated mutexes exclusively.
   void lock() ACQUIRE() {
      mut->lock();
      locked = true;
   }

   // Try to acquire all associated mutexes exclusively.
   bool try_lock() TRY_ACQUIRE(true) { return locked = mut->try_lock(); }

   // Release all associated mutexes. Warn on double unlock.
   void unlock() RELEASE() {
      mut->unlock();
      locked = false;
   }

   mutex_type* release() noexcept RETURN_CAPABILITY(this) {
      mutex_type* res = mut;
      mut             = nullptr;
      locked          = false;
      return res;
   }

   mutex_type* mutex() const noexcept { return mut; }

   bool owns_lock() const noexcept { return locked; }

   explicit operator bool() const noexcept { return locked; }
};

} // namespace fc
