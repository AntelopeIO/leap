#pragma once
#include <boost/asio.hpp>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace appbase {
// adapted from: https://www.boost.org/doc/libs/1_69_0/doc/html/boost_asio/example/cpp11/invocation/prioritised_handlers.cpp

enum class exec_queue {
   read_only,          // the queue storing tasks which are safe to execute
                       // in parallel with other read-only & read_exclusive tasks in the read-only
                       // thread pool as well as on the main app thread.
                       // Multi-thread safe as long as nothing is executed from the read_write queue.
   read_write,         // the queue storing tasks which can be only executed
                       // on the app thread while read-only tasks are
                       // not being executed in read-only threads. Single threaded.
   read_exclusive      // the queue storing tasks which should only be executed
                       // in parallel with other read_exclusive or read_only tasks in the
                       // read-only thread pool. Should never be executed on the main thread.
                       // If no read-only thread pool is available this queue grows unbounded
                       // as tasks will never execute. User is responsible for not queueing
                       // read_exclusive tasks if no read-only thread pool is available.
};

// Locking has to be coordinated by caller, use with care.
class exec_pri_queue : public boost::asio::execution_context
{
public:

   void stop() {
      std::lock_guard g( mtx_ );
      exiting_blocking_ = true;
      cond_.notify_all();
   }

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
   void add(int priority, exec_queue q, size_t order, Function function) {
      prio_queue& que = priority_que(q);
      std::unique_ptr<queued_handler_base> handler(new queued_handler<Function>(priority, order, std::move(function)));
      if (lock_enabled_) {
         std::lock_guard g( mtx_ );
         que.push( std::move( handler ) );
         if (num_waiting_)
            cond_.notify_one();
      } else {
         que.push( std::move( handler ) );
      }
   }

   // only call when no lock required
   void clear() {
      read_only_handlers_ = prio_queue();
      read_write_handlers_ = prio_queue();
      read_exclusive_handlers_ = prio_queue();
   }

   // only call when no lock required
   bool execute_highest(exec_queue q) {
      prio_queue& que = priority_que(q);
      if( !que.empty() ) {
         que.top()->execute();
         que.pop();
      }

      return !que.empty();
   }

   // only call when no lock required
   bool execute_highest(exec_queue lhs, exec_queue rhs) {
      prio_queue& lhs_que = priority_que(lhs);
      prio_queue& rhs_que = priority_que(rhs);
      size_t size = lhs_que.size() + rhs_que.size();
      if (size == 0)
         return false;
      exec_queue q = rhs;
      if (!lhs_que.empty() && (rhs_que.empty() || *rhs_que.top() < *lhs_que.top()))
         q = lhs;
      prio_queue& que = priority_que(q);
      que.top()->execute();
      que.pop();
      --size;
      return size > 0;
   }

   bool execute_highest_locked(exec_queue q) {
      prio_queue& que = priority_que(q);
      std::unique_lock g(mtx_);
      if (que.empty())
         return false;
      auto t = pop(que);
      g.unlock();
      t->execute();
      return true;
   }

   bool execute_highest_locked(exec_queue lhs, exec_queue rhs, bool should_block) {
      prio_queue& lhs_que = priority_que(lhs);
      prio_queue& rhs_que = priority_que(rhs);
      std::unique_lock g(mtx_);
      if (should_block) {
         ++num_waiting_;
         cond_.wait(g, [&](){
            bool exit = exiting_blocking_ || should_exit_();
            bool empty = lhs_que.empty() && rhs_que.empty();
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
      if (lhs_que.empty() && rhs_que.empty())
         return false;
      exec_queue q = rhs;
      if (!lhs_que.empty() && (rhs_que.empty() || *rhs_que.top() < *lhs_que.top()))
         q = lhs;
      auto t = pop(priority_que(q));
      g.unlock();
      t->execute();
      return true;
   }

   // Only call when locking disabled
   size_t size(exec_queue q) const { return priority_que(q).size(); }
   size_t size() const { return read_only_handlers_.size() + read_write_handlers_.size() + read_exclusive_handlers_.size(); }

   // Only call when locking disabled
   bool empty(exec_queue q) const { return priority_que(q).empty(); }

   // Only call when locking disabled
   const auto& top(exec_queue q) const { return priority_que(q).top(); }

   class executor
   {
   public:
      executor(exec_pri_queue& q, int p, size_t o, exec_queue que)
            : context_(q), que_(que), priority_(p), order_(o)
      {
      }

      exec_pri_queue& context() const noexcept
      {
         return context_;
      }

      template <typename Function, typename Allocator>
      void dispatch(Function f, const Allocator&) const
      {
         context_.add(priority_, que_, order_, std::move(f));
      }

      template <typename Function, typename Allocator>
      void post(Function f, const Allocator&) const
      {
         context_.add(priority_, que_, order_, std::move(f));
      }

      template <typename Function, typename Allocator>
      void defer(Function f, const Allocator&) const
      {
         context_.add(priority_, que_, order_, std::move(f));
      }

      void on_work_started() const noexcept {}
      void on_work_finished() const noexcept {}

      bool operator==(const executor& other) const noexcept
      {
         return order_ == other.order_ && priority_ == other.priority_ && que_ == other.que_ && &context_ == &other.context_;
      }

      bool operator!=(const executor& other) const noexcept
      {
         return !operator==(other);
      }

   private:
      exec_pri_queue& context_;
      exec_queue que_;
      int priority_;
      size_t order_;
   };

   template <typename Function>
   boost::asio::executor_binder<Function, executor>
   wrap(int priority, exec_queue q, size_t order, Function&& func)
   {
      return boost::asio::bind_executor( executor(*this, priority, order, q), std::forward<Function>(func) );
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

   using prio_queue = std::priority_queue<std::unique_ptr<queued_handler_base>, std::deque<std::unique_ptr<queued_handler_base>>, deref_less>;

   prio_queue& priority_que(exec_queue q) {
      switch (q) {
         case exec_queue::read_only:
            return read_only_handlers_;
         case exec_queue::read_write:
            return read_write_handlers_;
         case exec_queue::read_exclusive:
            return read_exclusive_handlers_;
      }
   }

   const prio_queue& priority_que(exec_queue q) const {
      switch (q) {
         case exec_queue::read_only:
            return read_only_handlers_;
         case exec_queue::read_write:
            return read_write_handlers_;
         case exec_queue::read_exclusive:
            return read_exclusive_handlers_;
      }
   }

   static std::unique_ptr<exec_pri_queue::queued_handler_base> pop(prio_queue& que) {
      // work around std::priority_queue not having a pop() that returns value
      auto t = std::move(const_cast<std::unique_ptr<queued_handler_base>&>(que.top()));
      que.pop();
      return t;
   }

   bool lock_enabled_ = false;
   mutable std::mutex mtx_;
   std::condition_variable cond_;
   uint32_t num_waiting_{0};
   uint32_t max_waiting_{0};
   bool exiting_blocking_{false};
   std::function<bool()> should_exit_; // called holding mtx_
   prio_queue read_only_handlers_;
   prio_queue read_write_handlers_;
   prio_queue read_exclusive_handlers_;
};

} // appbase
