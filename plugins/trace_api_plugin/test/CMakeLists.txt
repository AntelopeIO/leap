add_executable( test_trace_api_plugin
        test_extraction.cpp
        test_responses.cpp
        test_trace_file.cpp
        test_data_handlers.cpp
        test_configuration_utils.cpp
        test_compressed_file.cpp
        main.cpp
        )
target_link_libraries( test_trace_api_plugin trace_api_plugin )
target_include_directories( test_trace_api_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

add_test(NAME test_trace_api_plugin COMMAND plugins/trace_api_plugin/test/test_trace_api_plugin WORKING_DIRECTORY ${CMAKE_BINARY_DIR})