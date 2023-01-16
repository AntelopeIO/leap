add_library(
    subst_plugin
    subst_plugin.cpp
    subst_plugin.hpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/debug_eos_vm/dwarf.cpp"
)

target_link_libraries(
    subst_plugin
    chain_plugin
    eosio_chain
    appbase)

target_include_directories(
    subst_plugin
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/abieos/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/debug_eos_vm/include"
)
