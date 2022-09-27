#include <fc/compress/zlib.hpp>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filter/zlib.hpp>

namespace bio = boost::iostreams;

namespace fc
{
  string zlib_compress(const string& in)
  {
    string out;
    bio::filtering_ostream comp;
    comp.push(bio::zlib_compressor(bio::zlib::default_compression));
    comp.push(bio::back_inserter(out));
    bio::write(comp, in.data(), in.size());
    bio::close(comp);
    return out;
  }
}
