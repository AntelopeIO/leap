#pragma once
#include <boost/asio.hpp>

#include <condition_variable>
#include <mutex>
#include <queue>

namespace appbase {
// adapted from: https://www.boost.org/doc/libs/1_69_0/doc/html/boost_asio/example/cpp11/invocation/prioritised_handlers.cpp

// Locking has to be coordinated by caller, use with care.
class exec_pri_queue : public boost::asio::execution_context
{
public:

   void enable_locking(uint32_t num_threads, std::function<bool()> should_exit) {
      assert(num_threads > 0 && num_waiting_ == 0);
      lock_enabled_ = true;
      max_waiting_ = num_threads;
      should_exit_ = std::move(should_exit);
      exiting_blocking_ = false;
   }

   void disable_locking() {
      lock_enabled_ = false;
      should_exit_ = [](){ assert(false); return true; }; // should not be called when locking is disabled
   }

   // called from appbase::application_base::exec poll_one() or run_one()
   template <typename Function>
   void add(int priority, size_t order, Function function)
   {
      std::unique_ptr<queued_handler_base> handler(new queued_handler<Function>(priority, order, std::move(function)));
      if (lock_enabled_) {
         std::lock_guard g( mtx_ );
         handlers_.push( std::move( handler ) );
         if (num_waiting_)
            cond_.notify_one();
      } else {
         handlers_.push( std::move( handler ) );
      }
   }

   // only call when no lock required
   void clear()
   {
      handlers_ = prio_queue();
   }

   // only call when no lock required
   bool execute_highest()
   {
      if( !handlers_.empty() ) {
         handlers_.top()->execute();
         handlers_.pop();
      }

      return !handlers_.empty();
   }

private:
   // has to be defined before use, auto return type
   auto pop() {
      auto t = std::move(const_cast<std::unique_ptr<queued_handler_base>&>(handlers_.top()));
      handlers_.pop();
      return t;
   }

public:

   bool execute_highest_locked(bool should_block) {
      std::unique_lock g(mtx_);
      if (should_block) {
         ++num_waiting_;
         cond_.wait(g, [this](){
            bool exit = exiting_blocking_ || should_exit_();
            bool empty = handlers_.empty();
            if (empty || exit) {
               if (((empty && num_waiting_ == max_waiting_) || exit) && !exiting_blocking_) {
                  cond_.notify_all();
                  exiting_blocking_ = true;
               }
               return exit || exiting_blocking_; // same as calling should_exit(), but faster
            }
            return true;
         });
         --num_waiting_;
         if (exiting_blocking_ || should_exit_())
            return false;
      }
      if( handlers_.empty() )
         return false;
      auto t = pop();
      g.unlock();
      t->execute();
      return true;
   }

   // Only call when locking disabled
   size_t size() const { return handlers_.size(); }

   // Only call when locking disabled
   bool empty() const { return handlers_.empty(); }

   // Only call when locking disabled
   const auto& top() const { return handlers_.top(); }

   class executor
   {
   public:
      executor(exec_pri_queue& q, int p, size_t o)
            : context_(q), priority_(p), order_(o)
      {
      }

      exec_pri_queue& context() const noexcept
      {
         return context_;
      }

      template <typename Function, typename Allocator>
      void dispatch(Function f, const Allocator&) const
      {
         context_.add(priority_, order_, std::move(f));
      }

      template <typename Function, typename Allocator>
      void post(Function f, const Allocator&) const
      {
         context_.add(priority_, order_, std::move(f));
      }

      template <typename Function, typename Allocator>
      void defer(Function f, const Allocator&) const
      {
         context_.add(priority_, order_, std::move(f));
      }

      void on_work_started() const noexcept {}
      void on_work_finished() const noexcept {}

      bool operator==(const executor& other) const noexcept
      {
         return order_ == other.order_ && &context_ == &other.context_ && priority_ == other.priority_;
      }

      bool operator!=(const executor& other) const noexcept
      {
         return !operator==(other);
      }

   private:
      exec_pri_queue& context_;
      int priority_;
      size_t order_;
   };

   template <typename Function>
   boost::asio::executor_binder<Function, executor>
   wrap(int priority, size_t order, Function&& func)
   {
      return boost::asio::bind_executor( executor(*this, priority, order), std::forward<Function>(func) );
   }

private:
   class queued_handler_base
   {
   public:
      queued_handler_base( int p, size_t order )
            : priority_( p )
            , order_( order )
      {
      }

      virtual ~queued_handler_base() = default;

      virtual void execute() = 0;

      int priority() const { return priority_; }
      // C++20
      // friend std::weak_ordering operator<=>(const queued_handler_base&,
      //                                       const queued_handler_base&) noexcept = default;
      friend bool operator<(const queued_handler_base& a,
                            const queued_handler_base& b) noexcept
      {
         return std::tie( a.priority_, a.order_ ) < std::tie( b.priority_, b.order_ );
      }

   private:
      int priority_;
      size_t order_;
   };

   template <typename Function>
   class queued_handler : public queued_handler_base
   {
   public:
      queued_handler(int p, size_t order, Function f)
            : queued_handler_base( p, order )
            , function_( std::move(f) )
      {
      }

      void execute() override
      {
         function_();
      }

   private:
      Function function_;
   };

   struct deref_less
   {
      template<typename Pointer>
      bool operator()(const Pointer& a, const Pointer& b) noexcept(noexcept(*a < *b))
      {
         return *a < *b;
      }
   };

   bool lock_enabled_ = false;
   std::mutex mtx_;
   std::condition_variable cond_;
   uint32_t num_waiting_{0};
   uint32_t max_waiting_{0};
   bool exiting_blocking_{false};
   std::function<bool()> should_exit_; // called holding mtx_
   using prio_queue = std::priority_queue<std::unique_ptr<queued_handler_base>, std::deque<std::unique_ptr<queued_handler_base>>, deref_less>;
   prio_queue handlers_;
};

} // appbase
