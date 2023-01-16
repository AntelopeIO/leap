#include <eosio/name.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

bool handle_one(const std::string& s, bool reverse)
{
   try
   {
      if (reverse)
      {
         std::cout << eosio::name(s).value << std::endl;
         return true;
      }
      else
      {
         std::size_t pos;
         std::uint64_t value = std::stoull(s, &pos, 0);
         if (pos == s.size())
         {
            std::cout << eosio::name(value).to_string() << std::endl;
         }
         else
         {
            std::cerr << "Invalid name value: " << s << std::endl;
            return false;
         }
      }
   }
   catch (std::exception& e)
   {
      std::cerr << e.what() << std::endl;
      return false;
   }
   return true;
}

int usage(bool reverse)
{
   if (reverse)
   {
      std::cerr << "Usage: name2num [-x|--hex] [-d|--dec] [-r|--reverse] [names...]" << std::endl;
   }
   else
   {
      std::cerr << "Usage: num2name [-r|--reverse] [values...]" << std::endl;
   }
   return 2;
}

int main(int argc, const char** argv)
{
   if (argc == 0)
      return 1;
   bool reverse = false;
   bool hex = true;
   int result = 0;
   if (std::string_view(argv[0]).find("name2num") != std::string::npos)
   {
      reverse = true;
   }
   std::vector<std::string> args;
   for (int i = 1; i < argc; ++i)
   {
      std::string_view a(argv[i]);
      if (a[0] == '-')
      {
         if (a == "--help")
         {
            return usage(reverse);
         }
         else if (a == "--reverse")
         {
            reverse = !reverse;
         }
         else if (a == "--hex")
         {
            hex = true;
         }
         else if (a == "--dec")
         {
            hex = false;
         }
         else
         {
            for (auto ch : a.substr(1))
            {
               switch (ch)
               {
                  case 'x':
                     hex = true;
                     break;
                  case 'd':
                     hex = false;
                     break;
                  case 'r':
                     reverse = !reverse;
                     break;
                  case 'h':
                     return usage(reverse);
                  default:
                     std::cerr << "Unknown argument: " << a << std::endl;
                     return 2;
               }
            }
         }
      }
      else
      {
         args.emplace_back(a);
      }
   }
   if (reverse)
   {
      std::cout << std::showbase;
      if (hex)
         std::cout << std::hex;
   }
   if (args.empty())
   {
      std::string s;
      while (std::getline(std::cin, s))
      {
         result |= !handle_one(s, reverse);
      }
   }
   else
   {
      for (const std::string& s : args)
      {
         result |= !handle_one(s, reverse);
      }
   }
   return result;
}
