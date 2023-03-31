#include <fc/exception/exception.hpp>
#include <fc/log/gelf_appender.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/variant.hpp>
#include <fc/io/json.hpp>
#include <fc/crypto/city.hpp>
#include <fc/compress/zlib.hpp>
#include <fc/log/logger_config.hpp>

#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <iostream>

namespace fc
{
  const std::vector<std::string> gelf_appender::config::reserved_field_names  = {
      "_id",            // per GELF specification
      "_timestamp_ns",  // Remaining names all populated by appender
      "_log_id",
      "_line",
      "_file",
      "_method_name",
      "_thread_name",
      "_task_name"
  };

  const std::regex gelf_appender::config::user_field_name_pattern{"^_[\\w\\.\\-]*$"}; // per GELF specification

  class gelf_appender::impl
  {
  public:
    using work_guard_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    config                                         cfg;
    std::thread                                    thread;
    boost::asio::io_context                        io_context;
    work_guard_t work_guard =                      boost::asio::make_work_guard(io_context);
    boost::asio::ip::udp::socket                   gelf_socket;

    impl(const variant& c) : gelf_socket(io_context)
    {
      mutable_variant_object mvo;
      from_variant(c, mvo);

      cfg.endpoint = mvo["endpoint"].as<std::string>();
      mvo.erase("endpoint");
      cfg.host = mvo["host"].as<std::string>();
      mvo.erase("host");
      cfg.user_fields = mvo;

      for(auto&& field_name : config::reserved_field_names) {
         if (cfg.user_fields.contains(field_name.c_str())) {
            FC_THROW_EXCEPTION(invalid_arg_exception, "Field name '${field_name}' is reserved",
                               ("field_name", field_name));
         }
      }
      for(auto&& field : cfg.user_fields) {
         if (!std::regex_match(field.key(), config::user_field_name_pattern)) {
            FC_THROW_EXCEPTION(invalid_arg_exception, "Field name '${field_name} must begin with an underscore and contain only letters, numbers, underscores, dashes, and dots.",
                               ("field_name", field.key()));
         }
      }
    }

    ~impl()
    {
       if (thread.joinable()) {
          work_guard.reset();
          thread.join();
       }
    }

    static std::shared_ptr<std::vector<char>> make_new_bufer(boost::asio::const_buffer buf) {
      const char* p = static_cast<const char*>(buf.data());
      return std::make_shared<std::vector<char>>(p, p+buf.size());
    }

    static std::shared_ptr<std::vector<char>> make_new_bufer(const std::array<boost::asio::const_buffer, 2>& bufs) {
      auto new_buf = std::make_shared<std::vector<char>>();
      new_buf->reserve(bufs[0].size() + bufs[1].size());
      for (int i = 0; i < 2; ++i) {
          const char* p = static_cast<const char*>(bufs[i].data());
          new_buf->insert(new_buf->end(), p, p + bufs[i].size());
      }
      return new_buf;
    }

    template <typename Buffers>
    void send(Buffers&& bufs) {
      boost::system::error_code ec;
      gelf_socket.send(std::forward<Buffers>(bufs), 0, ec);
      if (ec == boost::asio::error::would_block) {
          auto new_buf = make_new_bufer(std::forward<Buffers>(bufs));
          gelf_socket.async_send(boost::asio::buffer(*new_buf),
                                    [new_buf](const boost::system::error_code& /*ec*/, std::size_t /*bytes_transferred*/) {
                                        // Swallow errors.  Currently only used for GELF logging, so depend on local
                                        // log to catch anything that doesn't make it across the network.
                                    });
      }
    }
  };

  gelf_appender::gelf_appender(const variant& args) :
    my(new impl(args))
  {
  }

  void gelf_appender::initialize()
  {
    try
    {
      if (my->cfg.endpoint.empty()) {
        fprintf(stderr, "The logging destination is not specified\n");
        return;
      }

      std::string_view endpoint = my->cfg.endpoint;
      string::size_type colon_pos = endpoint.rfind(':');
      FC_ASSERT(colon_pos != std::string::npos, "The logging destination port is not specified");
      auto port = endpoint.substr(colon_pos + 1);

      auto hostname = (endpoint[0] == '[' && colon_pos >= 2)  ? endpoint.substr( 1, colon_pos-2 ) : endpoint.substr( 0, colon_pos );

      boost::asio::ip::udp::resolver resolver{ my->io_context };
      auto endpoints = resolver.resolve(hostname, port);

      if (endpoints.empty())
          FC_THROW_EXCEPTION(unknown_host_exception, "The logging destination host name can not be resolved: ${hostname}",
                              ("hostname", std::string(hostname)));

      my->gelf_socket.connect(*endpoints.begin());
      std::cerr << "opened GELF socket to endpoint " << my->cfg.endpoint << "\n";
      my->gelf_socket.non_blocking(true);

      my->thread = std::thread([this] {
        try {
          fc::set_os_thread_name("gelf");
          my->io_context.run();
        } catch (std::exception& ex) {
          fprintf(stderr, "GELF logger caught exception at %s:%d : %s\n", __FILE__, __LINE__, ex.what());
        } catch (...) {
          fprintf(stderr, "GELF logger caught exception unknown exception %s:%d\n", __FILE__, __LINE__);
        }
      });

    }
    catch (...)
    {
      std::cerr << "error opening GELF socket to endpoint " << my->cfg.endpoint << "\n";
    }
  }

  gelf_appender::~gelf_appender()
  {}

  void do_log(gelf_appender::impl* my, uint64_t time_ns, const log_message& message)
  {
    log_context context = message.get_context();

    mutable_variant_object gelf_message;
    gelf_message["version"] = "1.1";
    gelf_message["host"] = my->cfg.host;
    gelf_message["short_message"] = format_string(message.get_format(), message.get_data(), true);

    gelf_message["timestamp"] = time_ns / 1000000.;
    gelf_message["_timestamp_ns"] = time_ns;

    static uint64_t gelf_log_counter;
    gelf_message["_log_id"] = fc::to_string(++gelf_log_counter);

    switch (context.get_log_level())
    {
    case log_level::debug:
      gelf_message["level"] = 7; // debug
      break;
    case log_level::info:
      gelf_message["level"] = 6; // info
      break;
    case log_level::warn:
      gelf_message["level"] = 4; // warning
      break;
    case log_level::error:
      gelf_message["level"] = 3; // error
      break;
    case log_level::all:
    case log_level::off:
      // these shouldn't be used in log messages, but do something deterministic just in case
      gelf_message["level"] = 6; // info
      break;
    }

    if (!context.get_context().empty())
      gelf_message["context"] = context.get_context();
    gelf_message["_line"] = context.get_line_number();
    gelf_message["_file"] = context.get_file();
    gelf_message["_method_name"] = context.get_method();
    gelf_message["_thread_name"] = context.get_thread_name();
    if (!context.get_task_name().empty())
      gelf_message["_task_name"] = context.get_task_name();

    for(auto&& field : my->cfg.user_fields) {
      gelf_message[field.key()] = field.value();
    }

    string gelf_message_as_string = json::to_string(gelf_message,
          fc::time_point::now() + fc::exception::format_time_limit,
          json::output_formatting::legacy_generator); // GELF 1.1 specifies unstringified numbers
    gelf_message_as_string = zlib_compress(gelf_message_as_string);

    // packets are sent by UDP, and they tend to disappear if they
    // get too large.  It's hard to find any solid numbers on how
    // large they can be before they get dropped -- datagrams can
    // be up to 64k, but anything over 512 is not guaranteed.
    // You can play with this number, intermediate values like
    // 1400 and 8100 are likely to work on most intranets.
    const unsigned max_payload_size = 512;

    if (gelf_message_as_string.size() <= max_payload_size)
    {
      my->send(boost::asio::buffer(gelf_message_as_string));
    }
    else
    {
      // split the message
      struct gelf_header {
        uint8_t magic[2] = { 0x1e, 0x0f};
        uint64_t message_id;
        uint8_t seq = 0;
        uint8_t count = 0;
      } header;

      // we need to generate an 8-byte ID for this message.
      // city hash should do
      header.message_id = city_hash64(gelf_message_as_string.c_str(), gelf_message_as_string.size());
      const unsigned body_length = max_payload_size - sizeof(header);
      header.count = (gelf_message_as_string.size() + body_length - 1) / body_length;
      unsigned bytes_sent = 0;

      while (bytes_sent < gelf_message_as_string.size())
      {
        unsigned bytes_to_send = std::min((unsigned)gelf_message_as_string.size() - bytes_sent,
                                          body_length);

        std::array<boost::asio::const_buffer,2> bufs = {
          boost::asio::const_buffer(&header, sizeof(header)),
          boost::asio::const_buffer(gelf_message_as_string.c_str() + bytes_sent, bytes_to_send)
        };
        my->send(bufs);
        ++header.seq;
        bytes_sent += bytes_to_send;
      }
      FC_ASSERT(header.seq == header.count);
    }
  }

  void gelf_appender::log(const log_message& message) {
    if (!my->thread.joinable())
      return;

    // use now() instead of context.get_timestamp() because log_message construction can include user provided long running calls
    boost::asio::post(my->io_context, [impl = my.get(), time_ns = time_point::now().time_since_epoch().count(), message] () {
      try {
        do_log(impl, time_ns, message);
      } catch (std::exception& ex) {
        fprintf(stderr, "GELF logger caught exception at %s:%d : %s\n", __FILE__, __LINE__, ex.what());
      } catch (...) {
        fprintf(stderr, "GELF logger caught exception unknown exception %s:%d\n", __FILE__, __LINE__);
      }
    });
  }
} // fc
