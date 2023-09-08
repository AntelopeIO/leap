#pragma once
#include <boost/smart_ptr/detail/spinlock.hpp>
#include <memory>

namespace fc {

   /**
    * Based off boost::atomic_shared_ptr, but uses std::shared_ptr instead of boost::shared_ptr.
    * Needed as our C++20 libs do not provide std::atomic<std::shared_ptr> yet.
    */
   template<class T>
   class atomic_shared_ptr
   {
   private:
      std::shared_ptr<T> p_;

      mutable boost::detail::spinlock l_;

      atomic_shared_ptr(const atomic_shared_ptr&) = delete;
      atomic_shared_ptr& operator=(const atomic_shared_ptr&) = delete;

   public:
      constexpr atomic_shared_ptr() noexcept
      : l_{{0}}
      {
      }

      atomic_shared_ptr( std::shared_ptr<T> p ) noexcept
      : p_( std::move( p ) ), l_{{0}}
      {
      }


      atomic_shared_ptr& operator=( std::shared_ptr<T> r ) noexcept {
         boost::detail::spinlock::scoped_lock lock( l_ );
         p_.swap( r );

         return *this;
      }

      constexpr bool is_lock_free() const noexcept {
         return false;
      }

      std::shared_ptr<T> load() const noexcept {
         boost::detail::spinlock::scoped_lock lock( l_ );
         return p_;
      }

      template<class M> std::shared_ptr<T> load( M ) const noexcept {
         boost::detail::spinlock::scoped_lock lock( l_ );
         return p_;
      }

      void store( std::shared_ptr<T> r ) noexcept {
         boost::detail::spinlock::scoped_lock lock( l_ );
         p_.swap( r );
      }

      template<class M> void store( std::shared_ptr<T> r, M ) noexcept {
         boost::detail::spinlock::scoped_lock lock( l_ );
         p_.swap( r );
      }

      std::shared_ptr<T> exchange( std::shared_ptr<T> r ) noexcept {
         {
            boost::detail::spinlock::scoped_lock lock( l_ );
            p_.swap( r );
         }

         return std::move( r );
      }

      template<class M> std::shared_ptr<T> exchange( std::shared_ptr<T> r, M ) noexcept {
         {
            boost::detail::spinlock::scoped_lock lock( l_ );
            p_.swap( r );
         }

         return std::move( r );
      }
   };

} // namespace fc
