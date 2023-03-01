if(CMAKE_CXX_STANDARD EQUAL 98 OR CMAKE_CXX_STANDARD LESS 17)
   message(FATAL_ERROR "appbase requires c++17 or newer")
elseif(NOT CMAKE_CXX_STANDARD)
   set(CMAKE_CXX_STANDARD 17)
   set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

file(GLOB UNIT_TESTS "*.cpp")
add_executable( custom_appbase_test ${UNIT_TESTS} )
target_link_libraries( custom_appbase_test appbase ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )
target_include_directories( custom_appbase_test PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CURRENT_SOURCE_DIR}/../../appbase/include" )

add_test( custom_appbase_test custom_appbase_test )
