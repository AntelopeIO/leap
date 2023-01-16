#include <cstdio>
#include <eosio/for_each_field.hpp>
#include <eosio/reflection.hpp>

int error_count;

void report_error(const char* assertion, const char* file, int line)
{
   if (error_count <= 20)
   {
      std::printf("%s:%d: failed %s\n", file, line, assertion);
   }
   ++error_count;
}

#define CHECK(...)                                       \
   do                                                    \
   {                                                     \
      if (__VA_ARGS__)                                   \
      {                                                  \
      }                                                  \
      else                                               \
      {                                                  \
         report_error(#__VA_ARGS__, __FILE__, __LINE__); \
      }                                                  \
   } while (0)

struct fn
{
   int test(int i) { return i * 2; }
};
EOSIO_REFLECT(fn, test);

int main()
{
   int counter = 0;
   eosio::for_each_field<fn>([&](const char* name, auto method) { ++counter; });
   CHECK(counter == 0);
   eosio::for_each_method<fn>([&](const char* name, int (fn::*m)(int)) {
      CHECK(m == &fn::test);
      ++counter;
   });
   CHECK(counter == 1);
   if (error_count)
      return 1;
}
