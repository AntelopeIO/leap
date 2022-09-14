add_library(leap-cli11 INTERFACE)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(leap-cli11
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)