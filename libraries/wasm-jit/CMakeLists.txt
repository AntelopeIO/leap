set(WAVM_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/Include)
include_directories(${WAVM_INCLUDE_DIR})

set(CMAKE_CXX_STANDARD 11)

option(WAVM_METRICS_OUTPUT "controls printing the timings of some operations to stdout" OFF)
if(WAVM_METRICS_OUTPUT)
	add_definitions("-DWAVM_METRICS_OUTPUT=1")
else()
	add_definitions("-DWAVM_METRICS_OUTPUT=0")
endif()

option(PRETEND_32BIT_ADDRESS_SPACE "forces 64-bit WAVM to reserve address-space as it would in a 32-bit process" OFF)
if(PRETEND_32BIT_ADDRESS_SPACE)
	add_definitions("-DPRETEND_32BIT_ADDRESS_SPACE=1")
else()
	add_definitions("-DPRETEND_32BIT_ADDRESS_SPACE=0")
endif()

add_subdirectory(Include/Inline)

add_subdirectory(Source/IR)
add_subdirectory(Source/Logging)
add_subdirectory(Source/Runtime)
add_subdirectory(Source/WASM)
add_subdirectory(Source/WAST)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Include/ DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/wasm-jit COMPONENT dev EXCLUDE_FROM_ALL)
