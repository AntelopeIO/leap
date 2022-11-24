

#include "do_http_post.hpp"
#include <curl/curl.h>
#include <eosio/chain/exceptions.hpp>
#include <map>

namespace eosio { namespace client { namespace http {

   static size_t write_callback(void* data, size_t size, size_t nmemb, void* userp) {
      size_t       realsize = size * nmemb;
      std::string& mem      = *(std::string*)userp;
      mem.append((const char*)data, realsize);
      return realsize;
   }

   // the following two functions are directly copy from the libcurl API overview from
   // https://curl.se/libcurl/c/CURLOPT_DEBUGFUNCTION.html
   void dump(const char* text, FILE* stream, unsigned char* ptr, size_t size) {
      size_t       i;
      size_t       c;
      unsigned int width = 0x10;

      fprintf(stream, "%s, %10.10ld bytes (0x%8.8lx)\n", text, (long)size, (long)size);

      for (i = 0; i < size; i += width) {
         fprintf(stream, "%4.4lx: ", (long)i);

         /* show hex to the left */
         for (c = 0; c < width; c++) {
            if (i + c < size)
               fprintf(stream, "%02x ", ptr[i + c]);
            else
               fputs("   ", stream);
         }

         /* show data on the right */
         for (c = 0; (c < width) && (i + c < size); c++) {
            char x = (ptr[i + c] >= 0x20 && ptr[i + c] < 0x80) ? ptr[i + c] : '.';
            fputc(x, stream);
         }

         fputc('\n', stream); /* newline */
      }
   }

   int my_trace(CURL* handle, curl_infotype type, char* data, size_t size, void* userp) {
      const char* text;
      (void)handle; /* prevent compiler warning */
      (void)userp;

      switch (type) {
         case CURLINFO_TEXT: fprintf(stderr, "== Info: %s", data);
         default: /* in case a new one is introduced to shock us */ return 0;

         case CURLINFO_HEADER_OUT: text = "=> Send header"; break;
         case CURLINFO_DATA_OUT: text = "=> Send data"; break;
         case CURLINFO_SSL_DATA_OUT: text = "=> Send SSL data"; break;
         case CURLINFO_HEADER_IN: text = "<= Recv header"; break;
         case CURLINFO_DATA_IN: text = "<= Recv data"; break;
         case CURLINFO_SSL_DATA_IN: text = "<= Recv SSL data"; break;
      }

      dump(text, stderr, (unsigned char*)data, size);
      return 0;
   }

   std::tuple<unsigned int, std::string> do_http_post(const std::string& base_uri, const std::string& path,
                                                      const std::vector<std::string>& headers,
                                                      const std::string& postjson, bool verify_cert, bool verbose,
                                                      bool trace) {

      static bool initialized = false;

      if (!initialized) {
         auto res = curl_global_init(CURL_GLOBAL_DEFAULT);
         EOS_ASSERT(res == CURLE_OK, chain::http_exception, curl_easy_strerror(res));
         initialized = true;
      }

      static std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(nullptr, &curl_easy_cleanup);
      if (!handle) handle.reset(curl_easy_init());
      auto curl = handle.get();
      EOS_ASSERT(curl != 0, chain::http_exception, "curl_easy_init failed");

      std::string uri;

      const char* unix_socket_prefix     = "unix://";
      const int   unix_socket_prefix_len = strlen(unix_socket_prefix);

      if (strncmp(unix_socket_prefix, base_uri.c_str(), unix_socket_prefix_len) == 0) {
         curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, base_uri.c_str() + unix_socket_prefix_len);
         uri = "http://localhost" + path;
      } else {
         // Disable use of unix domain in case it was enabled in the previous call
         curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, nullptr);
         uri = base_uri + path;
      }

      curl_easy_setopt(curl, CURLOPT_URL, uri.c_str());
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postjson.size());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postjson.c_str());

      if (trace) {
         curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, my_trace);
      }

      if (verbose || trace)
         curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

      struct curl_slist* list = NULL;

      for (auto& h : headers) list = curl_slist_append(list, h.c_str());

      list = curl_slist_append(list, "Expect:");
      list = curl_slist_append(list, "Content-Type: application/json");

      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

      std::string response;

      curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
      if (!verify_cert)
         curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

      auto res = curl_easy_perform(curl);
      if (res == CURLE_COULDNT_CONNECT || res == CURLE_URL_MALFORMAT)
         EOS_THROW(connection_exception, curl_easy_strerror(res));
      EOS_ASSERT(res == CURLE_OK, chain::http_exception, curl_easy_strerror(res));

      long http_code = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
      return { http_code, response };
   }
}}} // namespace eosio::client::http
