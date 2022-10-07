add_library(prometheus-core
        prometheus-cpp/core/src/check_names.cc
        prometheus-cpp/core/src/counter.cc
        prometheus-cpp/core/src/detail/builder.cc
        prometheus-cpp/core/src/detail/ckms_quantiles.cc
        prometheus-cpp/core/src/detail/time_window_quantiles.cc
        prometheus-cpp/core/src/detail/utils.cc
        prometheus-cpp/core/src/family.cc
        prometheus-cpp/core/src/gauge.cc
        prometheus-cpp/core/src/histogram.cc
        prometheus-cpp/core/src/info.cc
        prometheus-cpp/core/src/registry.cc
        prometheus-cpp/core/src/serializer.cc
        prometheus-cpp/core/src/summary.cc
        prometheus-cpp/core/src/text_serializer.cc
        )

target_include_directories( prometheus-core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/prometheus-cpp/core/include)
