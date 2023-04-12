#pragma once
#include <fc/any.hpp>
#include <fc/string.hpp>
#include <memory>

namespace boost { namespace asio { class io_context; typedef io_context io_service; } }

namespace fc {
   class appender;
   class log_message;
   class variant;

   class appender_factory {
      public:
       typedef std::shared_ptr<appender_factory> ptr;

       virtual ~appender_factory(){};
       virtual std::shared_ptr<appender> create( const variant& args ) = 0;
   };

   namespace detail {
      template<typename T>
      class appender_factory_impl : public appender_factory {
        public:
           virtual std::shared_ptr<appender> create( const variant& args ) {
              return std::shared_ptr<appender>(new T(args));
           }
      };
   }

   class appender {
      public:
         typedef std::shared_ptr<appender> ptr;

         virtual void initialize() = 0;
         virtual void log( const log_message& m ) = 0;
   };
}
