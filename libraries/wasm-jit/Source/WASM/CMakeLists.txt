set(Sources WASMSerialization.cpp)
set(PublicHeaders ${WAVM_INCLUDE_DIR}/WASM/WASM.h)
include_directories(${WAVM_INCLUDE_DIR}/WASM)

add_library(WASM STATIC ${Sources} ${PublicHeaders})
add_definitions(-DWEBASSEMBLY_API=DLL_EXPORT)
target_link_libraries(WASM Logging IR)
target_include_directories(WASM PUBLIC $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/wasm-jit>)

install(TARGETS WASM
   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
   COMPONENT dev EXCLUDE_FROM_ALL)
