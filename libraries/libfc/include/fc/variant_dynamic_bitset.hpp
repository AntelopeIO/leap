#pragma once

#include <fc/variant.hpp>
#include <boost/dynamic_bitset.hpp>

namespace fc
{
   template<typename T> void to_variant( const boost::dynamic_bitset<T>& bs, fc::variant& v ) {
      auto num_blocks = bs.num_blocks();
      if ( num_blocks > MAX_NUM_ARRAY_ELEMENTS ) throw std::range_error( "number of blocks of dynamic_bitset cannot be greather than MAX_NUM_ARRAY_ELEMENTS" );

      std::vector<T> blocks(num_blocks);
      boost::to_block_range(bs, blocks.begin());

      v = fc::variant(blocks);
   }

   template<typename T> void from_variant( const fc::variant& v, boost::dynamic_bitset<T>& bs ) {
      const std::vector<fc::variant>& vars = v.get_array();
      auto num_vars = vars.size();
      if( num_vars > MAX_NUM_ARRAY_ELEMENTS ) throw std::range_error( "number of variants for dynamic_bitset cannot be greather than MAX_NUM_ARRAY_ELEMENTS" );

      std::vector<T> blocks;
      blocks.reserve(num_vars);
      for( const auto& var: vars ) {
         blocks.push_back( var.as<T>() );
      }

      bs = { blocks.cbegin(), blocks.cend() };
   }
} // namespace fc
