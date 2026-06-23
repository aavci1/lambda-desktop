add_executable(paragraph_cache_bench paragraph_cache/bench.cpp)
target_include_directories(paragraph_cache_bench PRIVATE
  ${LAMBDAUI_FRAMEWORK_INCLUDE_DIR}
  ${LAMBDAUI_FRAMEWORK_SOURCE_DIR})
target_link_libraries(paragraph_cache_bench PRIVATE lambdaui)
set_target_properties(paragraph_cache_bench PROPERTIES CXX_STANDARD 23)
