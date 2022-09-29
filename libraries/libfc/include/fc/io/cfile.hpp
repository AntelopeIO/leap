#pragma once
#include <fc/filesystem.hpp>
#include <fc/io/datastream.hpp>
#include <cstdio>
#include <ios>
#include <fcntl.h>
#include <sys/stat.h>

#include <boost/interprocess/file_mapping.hpp>

#ifndef _WIN32
#define FC_FOPEN(p, m) fopen(p, m)
#else
#define FC_CAT(s1, s2) s1 ## s2
#define FC_PREL(s) FC_CAT(L, s)
#define FC_FOPEN(p, m) _wfopen(p, FC_PREL(m))
#endif

namespace fc {

namespace detail {
   using unique_file = std::unique_ptr<FILE, decltype( &fclose )>;
}

class cfile_datastream;

/**
 * Wrapper for c-file access that provides a similar interface as fstream without all the overhead of std streams.
 * std::ios_base::failure exception thrown for errors.
 */
class cfile {
public:
   cfile()
     : _file(nullptr, &fclose)
   {}

   void set_file_path( fc::path file_path ) {
      _file_path = std::move( file_path );
   }

   fc::path get_file_path() const {
      return _file_path;
   }

   bool is_open() const { return _open; }

   auto fileno() const {
      int fd = ::fileno(_file.get());
      if( -1 == fd ) {
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to convert file pointer to file descriptor, error: " +
                                       std::to_string( errno ) );
      }
      return fd;
   }

   static constexpr const char* create_or_update_rw_mode = "ab+";
   static constexpr const char* update_rw_mode = "rb+";
   static constexpr const char* truncate_rw_mode = "wb+";

   /// @param mode is any mode supported by fopen
   ///        Tested with:
   ///         "ab+" - open for binary update - create if does not exist
   ///         "rb+" - open for binary update - file must exist
   void open( const char* mode ) {
      _file.reset( FC_FOPEN( _file_path.generic_string().c_str(), mode ) );
      if( !_file ) {
         throw std::ios_base::failure( "cfile unable to open: " +  _file_path.generic_string() + " in mode: " + std::string( mode ) );
      }
#ifndef _WIN32
      struct stat st;
      _file_blk_size = 4096;
      if( fstat(fileno(), &st) == 0 )
         _file_blk_size = st.st_blksize;
#endif
      _open = true;
   }

   size_t tellp() const {
      long result = ftell( _file.get() );
      if (result == -1)
         throw std::ios_base::failure("cfile: " + get_file_path().generic_string() +
                                      " unable to get the current position of the file, error: " + std::to_string( errno ));
      return static_cast<size_t>(result);
   }

   void seek( long loc ) {
      if( 0 != fseek( _file.get(), loc, SEEK_SET ) ) {
         int err = ferror(_file.get());
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to SEEK_SET to: " + std::to_string(loc) +
                                       ", ferror: " + std::to_string(err) );
      }
   }

   void seek_end( long loc ) {
      if( 0 != fseek( _file.get(), loc, SEEK_END ) ) {
         int err = ferror(_file.get());
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to SEEK_END to: " + std::to_string(loc) +
                                       ", ferror: " + std::to_string(err) );
      }
   }

   void skip( long loc) {
      if( 0 != fseek( _file.get(), loc, SEEK_CUR ) ) {
         int err = ferror(_file.get());
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to SEEK_CUR to: " + std::to_string(loc) +
                                       ", ferror: " + std::to_string(err) );
      }
   }

   void read( char* d, size_t n ) {
      size_t result = fread( d, 1, n, _file.get() );
      if( result != n ) {
         int err = ferror(_file.get());
         int eof = feof(_file.get());
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to read " + std::to_string( n ) + " bytes;"
                                       " only read " + std::to_string( result ) +
                                       ", eof: " + (eof == 0 ? "false" : "true") +
                                       ", ferror: " + std::to_string(err) );
      }
   }

   void write( const char* d, size_t n ) {
      size_t result = fwrite( d, 1, n, _file.get() );
      if( result != n ) {
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to write " + std::to_string( n ) + " bytes; only wrote " + std::to_string( result ) );
      }
   }

   void flush() {
      if( 0 != fflush( _file.get() ) ) {
         int err = ferror( _file.get() );
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to flush file, ferror: " + std::to_string( err ) );
      }
   }

   void sync() {
      const int fd = fileno();
      if( -1 == fsync( fd ) ) {
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to sync file, error: " + std::to_string( errno ) );
      }
#ifdef __APPLE__
      if( -1 == fcntl( fd, F_FULLFSYNC ) ) {
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to F_FULLFSYNC file, error: " + std::to_string( errno ) );
      }
#endif
   }

   //rounds to filesystem block boundaries; e.g. punch_hole(5000, 14000) when blocksz=4096 punches from 8192 to 12288
   //end is not inclusive; eg punch_hole(4096, 8192) will punch 4096 bytes (assuming blocksz=4096)
   void punch_hole(size_t begin, size_t end) {
      if(begin % _file_blk_size) {
         begin &= ~(_file_blk_size-1);
         begin += _file_blk_size;
      }
      end &= ~(_file_blk_size-1);

      if(begin >= end)
         return;

      int ret = 0;
#if defined(__linux__)
      ret = fallocate(fileno(), FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, begin, end-begin);
#elif defined(__APPLE__)
      struct fpunchhole puncher = {0, 0, static_cast<off_t>(begin), static_cast<off_t>(end-begin)};
      ret = fcntl(fileno(), F_PUNCHHOLE, &puncher);
#endif
      if(ret == -1)
         wlog("Failed to punch hole in file ${f}: ${e}", ("f", _file_path)("e", strerror(errno)));

      flush();
   }

   static bool supports_hole_punching() {
#if defined(__linux__) || defined(__APPLE__)
      return true;
#endif
      return false;
   }

   size_t filesystem_block_size() const { return _file_blk_size; }

   bool eof() const { return feof(_file.get()) != 0; }

   int getc() { 
      int ret = fgetc(_file.get());  
      if (ret == EOF) {
         throw std::ios_base::failure( "cfile: " + _file_path.generic_string() +
                                       " unable to read 1 byte");
      }
      return ret;
   }

   void close() {
      _file.reset();
      _open = false;
   }

   boost::interprocess::mapping_handle_t get_mapping_handle() const {
      return {fileno(), false};
   }

   cfile_datastream create_datastream();

private:
   bool                  _open = false;
   fc::path              _file_path;
   size_t                _file_blk_size = 4096;
   detail::unique_file   _file;
};

/*
 *  @brief datastream adapter that adapts cfile for use with fc unpack
 *
 *  This class supports unpack functionality but not pack.
 */
class cfile_datastream {
public:
   explicit cfile_datastream( cfile& cf ) : cf(cf) {}

   void skip( size_t s ) {
      std::vector<char> d( s );
      read( &d[0], s );
   }

   bool read( char* d, size_t s ) {
      cf.read( d, s );
      return true;
   }

   bool get( unsigned char& c ) { return get( *(char*)&c ); }

   bool get( char& c ) { return read(&c, 1); }

   size_t tellp() const { return cf.tellp(); }

 private:
   cfile& cf;
};

inline cfile_datastream cfile::create_datastream() {
   return cfile_datastream(*this);
}

template <>
class datastream<fc::cfile, void> : public fc::cfile {
 public:
   using fc::cfile::cfile;

   bool seekp(size_t pos) { return this->seek(pos), true; }

   bool get(char& c) {
      c = this->getc();
      return true;
   }

   fc::cfile&       storage() { return *this; }
   const fc::cfile& storage() const { return *this; }
};


} // namespace fc

#ifndef _WIN32
#undef FC_FOPEN
#else
#undef FC_CAT
#undef FC_PREL
#undef FC_FOPEN
#endif
