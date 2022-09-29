file(GLOB HEADERS "include/prometheus/*.hpp")
add_library( prometheus ${HEADERS} src/counter.cpp)
target_include_directories( prometheus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"  )
