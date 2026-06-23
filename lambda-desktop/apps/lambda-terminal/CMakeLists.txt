include(FetchContent)

FetchContent_Declare(libvterm
  GIT_REPOSITORY https://github.com/neovim/libvterm.git
  GIT_TAG v0.3.3
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(libvterm)
if(NOT libvterm_POPULATED)
  if(POLICY CMP0169)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0169 OLD)
  endif()
  FetchContent_Populate(libvterm)
  if(POLICY CMP0169)
    cmake_policy(POP)
  endif()
endif()

set(LIBVTERM_GENERATED_HEADERS
  "${libvterm_SOURCE_DIR}/src/encoding/DECdrawing.inc"
  "${libvterm_SOURCE_DIR}/src/encoding/uk.inc")
add_custom_command(
  OUTPUT ${LIBVTERM_GENERATED_HEADERS}
  COMMAND perl -CSD "${libvterm_SOURCE_DIR}/tbl2inc_c.pl" "${libvterm_SOURCE_DIR}/src/encoding/DECdrawing.tbl" > "${libvterm_SOURCE_DIR}/src/encoding/DECdrawing.inc"
  COMMAND perl -CSD "${libvterm_SOURCE_DIR}/tbl2inc_c.pl" "${libvterm_SOURCE_DIR}/src/encoding/uk.tbl" > "${libvterm_SOURCE_DIR}/src/encoding/uk.inc"
  DEPENDS
    "${libvterm_SOURCE_DIR}/tbl2inc_c.pl"
    "${libvterm_SOURCE_DIR}/src/encoding/DECdrawing.tbl"
    "${libvterm_SOURCE_DIR}/src/encoding/uk.tbl"
  VERBATIM)
add_custom_target(libvterm_generated_headers DEPENDS ${LIBVTERM_GENERATED_HEADERS})
add_library(libvterm_static STATIC
  "${libvterm_SOURCE_DIR}/src/encoding.c"
  "${libvterm_SOURCE_DIR}/src/keyboard.c"
  "${libvterm_SOURCE_DIR}/src/mouse.c"
  "${libvterm_SOURCE_DIR}/src/parser.c"
  "${libvterm_SOURCE_DIR}/src/pen.c"
  "${libvterm_SOURCE_DIR}/src/screen.c"
  "${libvterm_SOURCE_DIR}/src/state.c"
  "${libvterm_SOURCE_DIR}/src/unicode.c"
  "${libvterm_SOURCE_DIR}/src/vterm.c")
add_dependencies(libvterm_static libvterm_generated_headers)
target_include_directories(libvterm_static PUBLIC "${libvterm_SOURCE_DIR}/include")
target_compile_features(libvterm_static PRIVATE c_std_99)
target_compile_options(libvterm_static PRIVATE
  -Wall
  -Wpedantic
  $<$<C_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>)
if(APPLE)
  set_target_properties(libvterm_static PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
endif()

lambdaui_add_app(lambda-terminal
  APP_NAME "Lambda Terminal"
  BUNDLE_IDENTIFIER "io.lambda.terminal"
  COMMENT "Terminal emulator for Lambda Desktop"
  SOURCES
    main.cpp
    TerminalCore.cpp
    TerminalSession.cpp
    ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell/Shell/ShellAppRegistry.cpp)

target_include_directories(lambda-terminal PRIVATE
  ${LAMBDA_DESKTOP_SOURCE_DIR}/apps/lambda-shell)
target_link_libraries(lambda-terminal PRIVATE libvterm_static tomlplusplus::tomlplusplus util)
