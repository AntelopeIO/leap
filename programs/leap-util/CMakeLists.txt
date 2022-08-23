add_executable( leap-util main.cpp CLI11.hpp actions_generic.cpp)

if( UNIX AND NOT APPLE )
  set(rt_library rt )
endif()

target_include_directories(leap-util PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries( leap-util
        PRIVATE appbase version
        PRIVATE eosio_chain fc ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )

copy_bin( leap-util )
install( TARGETS
   leap-util

   COMPONENT base

   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
)
