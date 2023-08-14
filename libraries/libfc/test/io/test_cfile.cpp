#include <boost/test/unit_test.hpp>

#include <fc/io/cfile.hpp>

using namespace fc;

BOOST_AUTO_TEST_SUITE(cfile_test_suite)
   BOOST_AUTO_TEST_CASE(test_simple)
   {
      fc::temp_directory tempdir;

      cfile t;
      t.set_file_path( tempdir.path() / "test" );
      t.open( "ab+" );
      BOOST_CHECK( t.is_open() );
      BOOST_CHECK( std::filesystem::exists( tempdir.path() / "test") );

      t.open( "rb+" );
      BOOST_CHECK( t.is_open() );
      t.write( "abc", 3 );
      BOOST_CHECK_EQUAL( t.tellp(), 3u );
      std::vector<char> v(3);
      t.seek( 0 );
      BOOST_CHECK_EQUAL( t.tellp(), 0u );
      t.read( &v[0], 3 );

      BOOST_CHECK_EQUAL( v[0], 'a' );
      BOOST_CHECK_EQUAL( v[1], 'b' );
      BOOST_CHECK_EQUAL( v[2], 'c' );

      t.seek_end( -2 );
      BOOST_CHECK_EQUAL( t.tellp(), 1u );
      t.read( &v[0], 1 );
      BOOST_CHECK_EQUAL( v[0], 'b' );

      int x = 42, y = 0;
      t.seek( 1 );
      t.write( reinterpret_cast<char*>( &x ), sizeof( x ) );
      t.seek( 1 );
      t.read( reinterpret_cast<char*>( &y ), sizeof( y ) );
      BOOST_CHECK_EQUAL( x, y );

      t.close();
      BOOST_CHECK( !t.is_open() );

      // re-open and read again
      t.open( "rb+" );
      BOOST_CHECK( t.is_open() );

      y = 0;
      t.seek( 1 );
      t.read( reinterpret_cast<char*>( &y ), sizeof( y ) );
      BOOST_CHECK_EQUAL( x, y );

      t.close();
      std::filesystem::remove_all( t.get_file_path() );
      BOOST_CHECK( !std::filesystem::exists( tempdir.path() / "test") );
   }

   BOOST_AUTO_TEST_CASE(test_hole_punching)
   {
      if(!cfile::supports_hole_punching())
         return;


      temp_cfile tmp("a+b");
      cfile& file = tmp.file();
      file.close();
      file.open("w+b");

      std::vector<char> a, b, c, d, e, f, g, h, i, j;
      a.assign(file.filesystem_block_size(), 'A');   //0
      b.assign(file.filesystem_block_size(), 'B');   //1
      c.assign(file.filesystem_block_size()/4, 'C'); //2
      d.assign(file.filesystem_block_size()/4, 'D');
      e.assign(file.filesystem_block_size()/4, 'E');
      f.assign(file.filesystem_block_size()/4, 'F');
      g.assign(file.filesystem_block_size()/2, 'G'); //3
      h.assign(file.filesystem_block_size()/2, 'H');
      i.assign(file.filesystem_block_size(), 'I');   //4
      j.assign(file.filesystem_block_size(), 'J');   //5

      std::vector<char> nom, nom2, nom4;
      nom.resize(file.filesystem_block_size());
      nom2.resize(file.filesystem_block_size()/2);
      nom4.resize(file.filesystem_block_size()/4);

      file.write(a.data(), a.size());
      file.write(b.data(), b.size());
      file.write(c.data(), c.size());
      file.write(d.data(), d.size());
      file.write(e.data(), e.size());
      file.write(f.data(), f.size());
      file.write(g.data(), g.size());
      file.write(h.data(), h.size());

      //should do nothing
      file.punch_hole(4, 8);
      file.seek(0);
      file.read(nom.data(), nom.size());
      BOOST_TEST_REQUIRE(nom == a);

      //should also do nothing
      file.punch_hole(file.filesystem_block_size(), file.filesystem_block_size()+file.filesystem_block_size()/2);
      file.seek(file.filesystem_block_size());
      file.read(nom.data(), nom.size());
      BOOST_TEST_REQUIRE(nom == b);

      //should only wipe out B
      file.punch_hole(file.filesystem_block_size(), file.filesystem_block_size()*2+file.filesystem_block_size()/2);
      file.seek(0);
      file.read(nom.data(), nom.size());
      BOOST_TEST_REQUIRE(nom == a);
      file.read(nom.data(), nom.size());
      BOOST_TEST_REQUIRE(nom != b);
      file.read(nom4.data(), nom4.size());
      BOOST_TEST_REQUIRE(nom4 == c);

      //write some stuff at the end after we had punched
      file.seek_end(0);
      file.write(i.data(), i.size());
      file.write(j.data(), j.size());

      //check C is intact
      file.seek(file.filesystem_block_size()*2);
      file.read(nom4.data(), nom4.size());
      BOOST_TEST_REQUIRE(nom4 == c);

      //should wipe out C,D,E,F
      file.punch_hole(file.filesystem_block_size()*2, file.filesystem_block_size()*3+file.filesystem_block_size()/2);
      file.seek(file.filesystem_block_size()*2);
      file.read(nom4.data(), nom4.size());
      BOOST_TEST_REQUIRE(nom4 != c);

      //so check that G,H,I are still intact
      file.seek(file.filesystem_block_size()*3);
      file.read(nom2.data(), nom2.size());
      BOOST_TEST_REQUIRE(nom2 == g);
      file.read(nom2.data(), nom2.size());
      BOOST_TEST_REQUIRE(nom2 == h);
      file.read(nom.data(), nom.size());
      BOOST_TEST_REQUIRE(nom == i);

      //check I is intact
      file.seek(file.filesystem_block_size()*4);
      file.read(nom.data(), nom.size());
      BOOST_TEST_REQUIRE(nom == i);

      //should only wipe out I
      file.punch_hole(file.filesystem_block_size()*4, file.filesystem_block_size()*5);
      file.seek(file.filesystem_block_size()*4);
      file.read(nom.data(), nom.size());
      BOOST_TEST_REQUIRE(nom != i);
      file.read(nom.data(), nom.size());
      BOOST_TEST_REQUIRE(nom == j);
   }

BOOST_AUTO_TEST_SUITE_END()
