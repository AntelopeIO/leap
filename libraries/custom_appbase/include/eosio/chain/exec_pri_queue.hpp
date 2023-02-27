#pragma once
#include <boost/asio.hpp>

#include <queue>

namespace appbase {
// adapted from: https://www.boost.org/doc/libs/1_69_0/doc/html/boost_asio/example/cpp11/invocation/prioritised_handlers.cpp

class exec_pri_queue : public boost::asio::execution_context
{
public:

   template <typename Function>
   void add(int priority, int order, Function function)
   {
      std::unique_ptr<queued_handler_base> handler(new queued_handler<Function>(priority, order, std::move(function)));

      handlers_.push(std::move(handler));
   }

   void clear()
   {
      handlers_ = prio_queue();
   }
   
   void execute_all()
   {
      while (!handlers_.empty()) {
         handlers_.top()->execute();
         handlers_.pop();
      }
   }

   bool execute_highest()
   {
      if( !handlers_.empty() ) {
         handlers_.top()->execute();
         handlers_.pop();
      }

      return !handlers_.empty();
   }

   size_t size() const { return handlers_.size(); }

   bool empty() const { return handlers_.empty(); }

   const auto& top() const { return handlers_.top(); }

   class executor
   {
   public:
      executor(exec_pri_queue& q, int p, int o)
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
         return &context_ == &other.context_ && priority_ == other.priority_;
      }

      bool operator!=(const executor& other) const noexcept
      {
         return !operator==(other);
      }

   private:
      exec_pri_queue& context_;
      int priority_;
      int order_;
   };

   template <typename Function>
   boost::asio::executor_binder<Function, executor>
   wrap(int priority, int order, Function&& func)
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

   using prio_queue = std::priority_queue<std::unique_ptr<queued_handler_base>, std::deque<std::unique_ptr<queued_handler_base>>, deref_less>;
   prio_queue handlers_;
};

} // appbase
