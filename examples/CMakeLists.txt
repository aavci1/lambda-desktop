# Sample apps (link the static `flux` library). Included when FLUX_BUILD_EXAMPLES=ON at the project root (default OFF).

# Bundled fonts (Material Symbols, etc.) live next to each example binary so Application can register them at runtime.
function(flux_copy_example_resources target_name)
  add_custom_command(
    TARGET ${target_name}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../resources/fonts/MaterialSymbolsRounded.ttf"
            "$<TARGET_FILE_DIR:${target_name}>/fonts/MaterialSymbolsRounded.ttf"
    VERBATIM
    COMMENT "Copy bundled fonts for ${target_name}")
endfunction()

function(flux_add_example target_name)
  add_executable(${target_name} ${ARGN})
  target_link_libraries(${target_name} PRIVATE flux)
  if(FLUX_ENABLE_ASAN)
    target_compile_options(${target_name} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target_name} PRIVATE -fsanitize=address)
  endif()
  flux_copy_example_resources(${target_name})
endfunction()

flux_add_example(alert-demo alert-demo/main.cpp)
flux_add_example(animation-demo animation-demo/main.cpp)
flux_add_example(blend-demo blend-demo/main.cpp)
flux_add_example(button-demo button-demo/main.cpp)
flux_add_example(card-demo card-demo/main.cpp)
flux_add_example(car-multimedia car-multimedia/main.cpp)
flux_add_example(checkbox-demo checkbox-demo/main.cpp)
flux_add_example(clock-demo clock-demo/main.cpp)
flux_add_example(cursor-demo cursor-demo/main.cpp)
flux_add_example(gradient-demo gradient-demo/main.cpp)
flux_add_example(hello-world hello-world/main.cpp)
flux_add_example(icon-demo icon-demo/main.cpp)
flux_add_example(image-demo image-demo/main.cpp)

option(FLUX_BUILD_LAMBDA_STUDIO "Build the lambda-studio example when vendor/llama.cpp is available." ON)
if(FLUX_BUILD_LAMBDA_STUDIO AND EXISTS "${CMAKE_SOURCE_DIR}/vendor/llama.cpp/CMakeLists.txt")
  set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/llama.cpp ${CMAKE_BINARY_DIR}/vendor/llama.cpp)
  flux_add_example(lambda-studio lambda-studio/main.cpp lambda-studio/AppRuntime.cpp)
  target_link_libraries(lambda-studio PRIVATE llama-common sqlite3)
elseif(FLUX_BUILD_LAMBDA_STUDIO)
  message(WARNING "Skipping lambda-studio: vendor/llama.cpp is not available.")
endif()

flux_add_example(layout-demo layout-demo/main.cpp)
flux_add_example(markdown-formatter-demo markdown-formatter-demo/main.cpp)
flux_add_example(output-demo output-demo/main.cpp)
flux_add_example(popover-demo popover-demo/main.cpp)
flux_add_example(scene-graph-demo scene-graph-demo/main.cpp)
flux_add_example(scroll-demo scroll-demo/main.cpp)
flux_add_example(segmented-demo segmented-demo/main.cpp)
flux_add_example(select-demo select-demo/main.cpp)
flux_add_example(slider-demo slider-demo/main.cpp)
flux_add_example(svg-demo svg-demo/main.cpp)
flux_add_example(table-demo table-demo/main.cpp)
flux_add_example(text-demo text-demo/main.cpp)
flux_add_example(textinput-demo textinput-demo/main.cpp)
flux_add_example(theme-demo theme-demo/main.cpp)
flux_add_example(toast-demo toast-demo/main.cpp)
flux_add_example(toggle-demo toggle-demo/main.cpp)
flux_add_example(tooltip-demo tooltip-demo/main.cpp)
flux_add_example(typography-demo typography-demo/main.cpp)
flux_add_example(window-chrome-demo window-chrome-demo/main.cpp)
