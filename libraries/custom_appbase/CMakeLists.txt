add_library(custom_appbase INTERFACE)
target_include_directories(custom_appbase INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(custom_appbase INTERFACE appbase)

add_subdirectory(tests)
