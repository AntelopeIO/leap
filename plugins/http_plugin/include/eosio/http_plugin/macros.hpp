#pragma once

struct async_result_visitor : public fc::visitor<fc::variant> {
   template<typename T>
   fc::variant operator()(const T& v) const {
      return fc::variant(v);
   }
};

#define CALL_ASYNC_WITH_400(api_name, api_handle, api_namespace, call_name, call_result, http_response_code, params_type) \
{std::string("/v1/" #api_name "/" #call_name), \
   [api_handle](string&&, string&& body, url_response_callback&& cb) mutable { \
      auto deadline = api_handle.start(); \
      try { \
         auto params = parse_params<api_namespace::call_name ## _params, params_type>(body);\
         FC_CHECK_DEADLINE(deadline);\
         api_handle.call_name( std::move(params), \
               [cb=std::move(cb), body=std::move(body)](const std::variant<fc::exception_ptr, call_result>& result){ \
               if (std::holds_alternative<fc::exception_ptr>(result)) {\
                  try {\
                     std::get<fc::exception_ptr>(result)->dynamic_rethrow_exception();\
                  } catch (...) {\
                     http_plugin::handle_exception(#api_name, #call_name, body, cb);\
                  }\
               } else {\
                  cb(http_response_code, fc::time_point::maximum(), std::visit(async_result_visitor(), result));\
               }\
            });\
      } catch (...) { \
         http_plugin::handle_exception(#api_name, #call_name, body, cb); \
      } \
   }\
}
