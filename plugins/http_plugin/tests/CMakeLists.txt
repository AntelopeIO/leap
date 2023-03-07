file(GLOB UNIT_TESTS "*.cpp")

add_executable( http_plugin_unit_tests ${UNIT_TESTS})

target_link_libraries( http_plugin_unit_tests
                       PRIVATE appbase
                       PRIVATE http_plugin        
                       ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )

target_include_directories( http_plugin_unit_tests PUBLIC
                            ${CMAKE_SOURCE_DIR}/plugins/http_plugin/include )

add_test( NAME http_plugin_unit_tests COMMAND http_plugin_unit_tests )
