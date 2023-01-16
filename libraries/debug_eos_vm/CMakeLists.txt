add_library(debug_eos_vm dwarf.cpp)
target_include_directories(debug_eos_vm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(debug_eos_vm PUBLIC abieos chain)
