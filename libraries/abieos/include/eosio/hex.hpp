#pragma once

#include <string>

namespace eosio
{
   template <typename SrcIt, typename DestIt>
   void hex(SrcIt begin, SrcIt end, DestIt dest)
   {
      auto nibble = [&dest](uint8_t i) {
         if (i <= 9)
            *dest++ = '0' + i;
         else
            *dest++ = 'A' + i - 10;
      };
      while (begin != end)
      {
         nibble(((uint8_t)*begin) >> 4);
         nibble(((uint8_t)*begin) & 0xf);
         ++begin;
      }
   }

   template <typename SrcIt>
   std::string hex(SrcIt begin, SrcIt end)
   {
      std::string s;
      s.reserve((end - begin) * 2);
      hex(begin, end, std::back_inserter(s));
      return s;
   }
}  // namespace eosio
