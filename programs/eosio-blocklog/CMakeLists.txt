add_executable( eosio-blocklog main.cpp )

find_package(progressbar 2.0 QUIET)

if(progressbar_FOUND)
  message(STATUS "Enabling progressbar in eosio-blocklog")
endif()

if( UNIX AND NOT APPLE )
  set(rt_library rt )
endif()

configure_file(config.hpp.in config.hpp ESCAPE_QUOTES)

target_include_directories(eosio-blocklog PUBLIC ${LIBPROGRESSBAR_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries( eosio-blocklog
        PRIVATE appbase
        PRIVATE eosio_chain fc ${LIBPROGRESSBAR_LIBRARIES} ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )

copy_bin( eosio-blocklog )
install( TARGETS
   eosio-blocklog

   COMPONENT base

   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
)
