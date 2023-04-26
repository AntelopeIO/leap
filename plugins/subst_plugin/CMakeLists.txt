add_library(
    subst_plugin
    subst_plugin.cpp
    subst_plugin.hpp
)

target_link_libraries(
    subst_plugin
    chain_plugin
    eosio_chain
    appbase
    fc)

target_include_directories(
    subst_plugin
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/abieos/include"
)
