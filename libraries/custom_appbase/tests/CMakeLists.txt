file(GLOB UNIT_TESTS "*.cpp")
add_executable( custom_appbase_test ${UNIT_TESTS} )
target_link_libraries( custom_appbase_test appbase ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )
target_include_directories( custom_appbase_test PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include" )

add_test( custom_appbase_test custom_appbase_test )
