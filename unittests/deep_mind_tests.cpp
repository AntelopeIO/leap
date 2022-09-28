#include <eosio/testing/tester.hpp>
#include <fc/log/logger_config.hpp>
#include <eosio/chain/deep_mind.hpp>
#include <regex>
#include <cctype>

#include <boost/test/unit_test.hpp>

#include <deep-mind.hpp>

using namespace fc;
using namespace eosio::testing;

extern void setup_test_logging();

struct deep_mind_log_fixture
{
   deep_mind_handler deep_mind_logger;
   fc::temp_file log_output;
   deep_mind_log_fixture()
   {
      auto cfg = fc::logging_config::default_config();

      cfg.appenders.push_back(
         appender_config( "deep-mind", "dmlog",
            mutable_variant_object()
               ( "file", log_output.path().preferred_string().c_str())
         ) );

      fc::logger_config lc;
      lc.name = "deep-mind";
      lc.level = fc::log_level::all;
      lc.appenders.push_back("deep-mind");
      cfg.loggers.push_back( lc );

      fc::configure_logging(cfg);
      setup_test_logging();

      deep_mind_logger.update_config(deep_mind_handler::deep_mind_config{.zero_elapsed = true});
      deep_mind_logger.update_logger("deep-mind");
   }
   ~deep_mind_log_fixture()
   {
      fc::configure_logging(fc::logging_config::default_config());
      setup_test_logging();
   }
};

struct deep_mind_tester : deep_mind_log_fixture, validating_tester
{
   deep_mind_tester() : validating_tester({}, &deep_mind_logger) {}
};

namespace {

bool match_pattern_line(const std::string& line, const std::string& pattern);

template<typename Iter>
bool has_prefix(Iter begin, Iter end, std::string_view prefix)
{
   return end - begin >= static_cast<std::ptrdiff_t>(prefix.size()) &&
       std::equal(prefix.begin(), prefix.end(), begin);
}

static const std::string_view escaped_chars{"\\[]*.^$+?{}()|"};

bool is_escaped(char ch)
{
   return escaped_chars.find(ch) != std::string_view::npos;
}

std::string escape_line(const std::string& line)
{
   std::string result;
   for(const auto& ch : line)
   {
      if(is_escaped(ch))
      {
         result.push_back('\\');
      }
      result.push_back(ch);
   }
   return result;
}

static constexpr unsigned resync_threshold = 3;

std::string merge_line(const std::string& line, const std::string& pattern)
{
   std::string result;
   auto line_iter = line.begin(), line_end = line.end();
   auto pattern_iter = pattern.begin(), pattern_end = pattern.end();
   // The nondeterministic data is an fc::microseconds object in
   // the traces, which is 8-bytes long.
   static constexpr int xdigit_group = 16;
   int current_xdigits = 0;
   int skip_xdigit = 0;
   while(line_iter != line_end && pattern_iter != pattern_end)
   {
      if(*pattern_iter == '\\')
      {
         ++pattern_iter;
         if(!is_escaped(*pattern_iter) || *line_iter != *pattern_iter)
         {
            // Either manually edited or does not match
            return pattern;
         }
         current_xdigits = 0;
         result.push_back('\\');
         result.push_back(*pattern_iter);
      }
      else if(has_prefix(pattern_iter, pattern_end, "[[:xdigit:]]"))
      {
         if(!std::isxdigit(*line_iter))
         {
            // Does not match
            return pattern;
         }
         if(skip_xdigit && *line_iter == '0') {
            result += '0';
         } else {
            ++current_xdigits;
            result += "[[:xdigit:]]";
         }
         ++line_iter;
         pattern_iter += 12;
         if(skip_xdigit)
         {
            --skip_xdigit;
         }
         if(current_xdigits == xdigit_group)
         {
            current_xdigits = 0;
            skip_xdigit = xdigit_group;
         }
         continue;
      }
      else if(is_escaped(*pattern_iter))
      {
         // Manually edited: cannot auto merge
         return pattern;
      }
      else if(*line_iter == *pattern_iter)
      {
         if(current_xdigits > 0 && current_xdigits < xdigit_group && *line_iter == '0')
         {
            ++current_xdigits;
            result += "[[:xdigit:]]";
         }
         else
         {
            current_xdigits = 0;
            result.push_back(*line_iter);
         }
      }
      else
      {
         if(std::isxdigit(*line_iter) && std::isxdigit(*pattern_iter))
         {
            skip_xdigit = 0;
            ++current_xdigits;
            result += "[[:xdigit:]]";
         }
         else
         {
            // Don't know how to generate pattern
            return pattern;
         }
      }
      ++line_iter;
      ++pattern_iter;
      if(skip_xdigit)
      {
         --skip_xdigit;
      }
      if(current_xdigits == xdigit_group)
      {
         current_xdigits = 0;
         skip_xdigit = xdigit_group;
      }
   }
   if(line_iter != line_end || pattern_iter != pattern_end)
   {
      // Does not match
      return pattern;
   }
   return result;
}


struct edit_distance_element {
   unsigned weight;
   unsigned short unmodified;
   unsigned short prev; // 1 = input, 2 = pattern, 3 = match
};

void merge(const std::string& input_filename, const std::string& pattern_filename)
{
   auto read_file = [](const std::string& filename){
      std::ifstream input_file(filename);
      std::string s;
      std::vector<std::string> result;
      while(std::getline(input_file, s))
      {
         result.push_back(std::move(s));
      }
      return result;
   };

   auto write_file = [](const std::string& filename, const auto& cont)
   {
      std::ofstream output_file(filename, std::ios_base::trunc);
      for(const auto& elem : cont)
      {
         output_file << elem << "\n";
      }
   };

   std::vector<std::string> input = read_file(input_filename);
   std::vector<std::string> pattern = read_file(pattern_filename);
   std::vector<std::string> all_output;

   auto line_iter = input.begin(), line_end = input.end();
   auto pattern_iter = pattern.begin(), pattern_end = pattern.end();

   while(line_iter != line_end || pattern_iter != pattern_end)
   {
      std::vector<std::vector<edit_distance_element>> tab;
      auto add_point = [&](std::size_t i, std::size_t j)
      {
         const auto& line = *(line_iter + i);
         const auto& pattern = *(pattern_iter + j);
         // check whether line matches pattern
         // -1 = does not match
         // 0 = matches exactly
         // 1 = can be merged
         int match_weight = -1;
         if(match_pattern_line(line, pattern))
         {
            match_weight = 0;
         }
         else
         {
            auto merged = merge_line(line, pattern);
            if(merged != pattern)
            {
               match_weight = 1;
            }
         }
         edit_distance_element result = { std::numeric_limits<unsigned>::max() };
         if(match_weight >= 0)
         {
            if(i == 0)
            {
               result.weight = j;
            }
            else if(j == 0)
            {
               result.weight = i;
            }
            else
            {
               result = tab[i-1][j-1];
            }
            result.weight += match_weight;
            ++result.unmodified;
            result.prev = 3;
         }
         if(i > 0)
         {
            unsigned weight = tab[i-1][j].weight + 1;
            if(weight < result.weight)
            {
               result = {weight, 0, 1};
            }
         }
         if(j > 0)
         {
            unsigned weight = tab[i][j-1].weight + 1;
            if(weight < result.weight)
            {
               result = {weight, 0, 2};
            }
         }
         if(i == 0 && j == 0)
         {
            unsigned weight = 2;
            if(weight < result.weight)
            {
               result = {weight, 0, 1};
            }
         }
         return result;
      };
      auto generate_output = [&](std::size_t i, std::size_t j){
         std::vector<std::string> result;
         while(i != 0 && j != 0)
         {
            switch(tab[i - 1][j - 1].prev)
            {
            case 1:
               --i;
               result.push_back(escape_line(*(line_iter + i)));
               break;
            case 2:
               --j;
               break;
            case 3:
               --i, --j;
               result.push_back(merge_line(*(line_iter + i), *(pattern_iter + j)));
            }
         }
         while(i-- > 0)
         {
            result.push_back(escape_line(*(line_iter + i)));
         }
         std::reverse(result.begin(), result.end());
         return result;
      };
      auto next = [&](std::ptrdiff_t i, std::ptrdiff_t j) {
         auto output = generate_output(i, j);
         line_iter += i;
         pattern_iter += j;
         all_output.insert(all_output.end(), output.begin(), output.end());
      };
      auto editdiff_loop = [&](){
         // Fill an expanding square with equal lines and patterns
         for(std::ptrdiff_t i = 0; i < std::min(line_end - line_iter, pattern_end - pattern_iter); ++i)
         {
            tab.emplace_back();
            for(std::ptrdiff_t j = 0; j < i; ++j)
            {
               tab[i].push_back(add_point(i, j));
               if(tab[i][j].unmodified >= resync_threshold)
               {
                  next(i + 1, j + 1);
                  return;
               }
            }
            for(std::ptrdiff_t j = 0; j < i; ++j)
            {
               tab[j].push_back(add_point(j, i));
               if(tab[j][i].unmodified >= resync_threshold)
               {
                  next(j + 1, i + 1);
                  return;
               }
            }
            tab[i].push_back(add_point(i, i));
            if(tab[i][i].unmodified >= resync_threshold)
            {
               next(i + 1, i + 1);
               return;
            }
         }
         // fill excess lines
         for(std::ptrdiff_t i = pattern_end - pattern_iter; i < line_end - line_iter;++i)
         {
            tab.emplace_back();
            for(std::ptrdiff_t j = 0; j < pattern_end - pattern_iter; ++j)
            {
               tab[i].push_back(add_point(i, j));
            }
         }
         // fill excess patterns
         for(std::ptrdiff_t i = line_end - line_iter; i < pattern_end - pattern_iter; ++i)
         {
            for(std::ptrdiff_t j = 0; j < line_end - line_iter; ++j)
            {
               tab[j].push_back(add_point(j, i));
            }
         }
         next(line_end - line_iter, pattern_end - pattern_iter);
      };
      editdiff_loop();
   }
   write_file(pattern_filename, all_output);
}

bool match_pattern_line(const std::string& line, const std::string& pattern)
{
   std::regex pattern_re{pattern};
   return std::regex_match(line, pattern_re);
}

void match_pattern(const std::string& filename, const std::string& pattern_filename)
{
   std::ifstream file(filename.c_str());
   std::ifstream pattern_file(pattern_filename.c_str());
   std::string line, pattern_line;
   int i = 1;
   while(std::getline(pattern_file, pattern_line))
   {
      if(!std::getline(file, line))
      {
         BOOST_TEST(false, "Unexpected end of input at line " << i);
         return;
      }
      if(!match_pattern_line(line, pattern_line))
      {
         BOOST_TEST(false, "Mismatch at line " << i << "\n+ " << line << "\n- " << pattern_line);
         return;
      }
      ++i;
   }
   if(std::getline(file, line))
   {
      BOOST_TEST(false, "Expected end of file at line " << i);
   }
}

}

BOOST_AUTO_TEST_SUITE(deep_mind_tests)

BOOST_FIXTURE_TEST_CASE(deep_mind, deep_mind_tester)
{
   produce_block();

   create_account( "alice"_n );

   push_action(config::system_account_name, "updateauth"_n, "alice"_n, fc::mutable_variant_object()
               ("account", "alice")
               ("permission", "test1")
               ("parent", "active")
               ("auth", authority{{"eosio"_n, "active"_n}}));

   produce_block();

   bool save_log = [](){
      auto argc = boost::unit_test::framework::master_test_suite().argc;
      auto argv = boost::unit_test::framework::master_test_suite().argv;
      return std::find(argv, argv + argc, std::string("--save-dmlog")) != (argv + argc);
   }();

   if(save_log)
   {
      merge(log_output.path().preferred_string(), DEEP_MIND_LOGFILE);
   }
   else
   {
      match_pattern(log_output.path().preferred_string(), DEEP_MIND_LOGFILE);
   }
}

BOOST_AUTO_TEST_SUITE_END()
