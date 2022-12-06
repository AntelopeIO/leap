#pragma once

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/ordered_index.hpp>

namespace bmi = boost::multi_index;
using bmi::composite_key;
using bmi::composite_key_compare;
using bmi::const_mem_fun;
using bmi::indexed_by;
using bmi::member;
using bmi::ordered_non_unique;
using bmi::ordered_unique;
using bmi::tag;

struct by_id;
