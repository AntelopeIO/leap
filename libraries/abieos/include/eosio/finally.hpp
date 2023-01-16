#pragma once

namespace eosio
{
   template <typename F>
   struct finally
   {
      F f;
      ~finally() { f(); }
   };

   template <typename F>
   finally(F) -> finally<F>;
}  // namespace eosio
