# LambdaWaylandProtocols.cmake — generate Wayland protocol sources for lambda (client) and
# lambda-window-manager (server) from XML at build time.

function(lambda_init_wayland_protocol_dir)
  if(NOT LAMBDA_WAYLAND_PROTOCOL_DIR)
    set(LAMBDA_WAYLAND_PROTOCOL_DIR ${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols CACHE INTERNAL "")
    file(MAKE_DIRECTORY ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  endif()
  set(LAMBDA_WAYLAND_PROTOCOL_DIR ${LAMBDA_WAYLAND_PROTOCOL_DIR} PARENT_SCOPE)
endfunction()

function(lambda_setup_wayland_protocol_paths WAYLAND_PROTOCOLS_DIR)
  if(DEFINED LAMBDA_FRAMEWORK_PROTOCOL_DIR)
    set(_lambda_protocol_local "${LAMBDA_FRAMEWORK_PROTOCOL_DIR}/wayland")
  else()
    get_filename_component(_lambda_framework_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(_lambda_protocol_local "${_lambda_framework_dir}/protocols/wayland")
  endif()
  set(LAMBDA_XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml" PARENT_SCOPE)
  set(LAMBDA_XDG_DECORATION_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml" PARENT_SCOPE)
  set(LAMBDA_XDG_OUTPUT_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-output/xdg-output-unstable-v1.xml" PARENT_SCOPE)
  set(LAMBDA_XDG_ACTIVATION_XML "${WAYLAND_PROTOCOLS_DIR}/staging/xdg-activation/xdg-activation-v1.xml" PARENT_SCOPE)
  set(LAMBDA_VIEWPORTER_XML "${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml" PARENT_SCOPE)
  set(LAMBDA_PRESENTATION_TIME_XML "${WAYLAND_PROTOCOLS_DIR}/stable/presentation-time/presentation-time.xml" PARENT_SCOPE)
  set(LAMBDA_LINUX_DMABUF_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml" PARENT_SCOPE)
  set(LAMBDA_CURSOR_SHAPE_XML "${WAYLAND_PROTOCOLS_DIR}/staging/cursor-shape/cursor-shape-v1.xml" PARENT_SCOPE)
  set(LAMBDA_IDLE_INHIBIT_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml" PARENT_SCOPE)
  set(LAMBDA_POINTER_CONSTRAINTS_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml" PARENT_SCOPE)
  set(LAMBDA_RELATIVE_POINTER_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/relative-pointer/relative-pointer-unstable-v1.xml" PARENT_SCOPE)
  set(LAMBDA_PRIMARY_SELECTION_XML "${WAYLAND_PROTOCOLS_DIR}/unstable/primary-selection/primary-selection-unstable-v1.xml" PARENT_SCOPE)
  set(LAMBDA_FRACTIONAL_SCALE_XML "${_lambda_protocol_local}/fractional-scale-v1.xml" PARENT_SCOPE)
  set(LAMBDA_XX_CUTOUTS_XML "${_lambda_protocol_local}/xx-cutouts-v1.xml" PARENT_SCOPE)
  set(LAMBDA_WLR_LAYER_SHELL_XML "${_lambda_protocol_local}/wlr-layer-shell-unstable-v1.xml" PARENT_SCOPE)
  set(LAMBDA_EXT_BACKGROUND_EFFECT_XML "${_lambda_protocol_local}/ext-background-effect-v1.xml" PARENT_SCOPE)
endfunction()

function(lambda_wayland_protocols)
  cmake_parse_arguments(_lambda_wp
    ""
    "TARGET;MODE;XML;BASENAME"
    ""
    ${ARGN})

  if(NOT _lambda_wp_TARGET OR NOT _lambda_wp_MODE OR NOT _lambda_wp_XML OR NOT _lambda_wp_BASENAME)
    message(FATAL_ERROR
      "lambda_wayland_protocols requires TARGET, MODE (CLIENT|SERVER|BOTH), XML, and BASENAME")
  endif()

  if(NOT EXISTS "${_lambda_wp_XML}")
    message(FATAL_ERROR "lambda_wayland_protocols: XML not found: ${_lambda_wp_XML}")
  endif()

  if(NOT WAYLAND_SCANNER)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
  endif()

  lambda_init_wayland_protocol_dir()
  set(_lambda_wp_out_dir ${LAMBDA_WAYLAND_PROTOCOL_DIR})
  set(_lambda_wp_outputs)
  set(_lambda_wp_commands)

  if(_lambda_wp_MODE STREQUAL "CLIENT" OR _lambda_wp_MODE STREQUAL "BOTH")
    set(_lambda_wp_client_h ${_lambda_wp_out_dir}/${_lambda_wp_BASENAME}-client-protocol.h)
    set(_lambda_wp_client_c ${_lambda_wp_out_dir}/${_lambda_wp_BASENAME}-protocol.c)
    list(APPEND _lambda_wp_outputs ${_lambda_wp_client_h} ${_lambda_wp_client_c})
    list(APPEND _lambda_wp_commands
      COMMAND ${WAYLAND_SCANNER} client-header ${_lambda_wp_XML} ${_lambda_wp_client_h}
      COMMAND ${WAYLAND_SCANNER} private-code ${_lambda_wp_XML} ${_lambda_wp_client_c})
  endif()

  if(_lambda_wp_MODE STREQUAL "SERVER" OR _lambda_wp_MODE STREQUAL "BOTH")
    set(_lambda_wp_server_h ${_lambda_wp_out_dir}/${_lambda_wp_BASENAME}-server-protocol.h)
    set(_lambda_wp_server_c ${_lambda_wp_out_dir}/${_lambda_wp_BASENAME}-server-code.c)
    list(APPEND _lambda_wp_outputs ${_lambda_wp_server_h} ${_lambda_wp_server_c})
    list(APPEND _lambda_wp_commands
      COMMAND ${WAYLAND_SCANNER} server-header ${_lambda_wp_XML} ${_lambda_wp_server_h}
      COMMAND ${WAYLAND_SCANNER} private-code ${_lambda_wp_XML} ${_lambda_wp_server_c})
  endif()

  set(_lambda_wp_stamp ${_lambda_wp_out_dir}/${_lambda_wp_BASENAME}-${_lambda_wp_MODE}.stamp)
  add_custom_command(
    OUTPUT ${_lambda_wp_outputs} ${_lambda_wp_stamp}
    ${_lambda_wp_commands}
    COMMAND ${CMAKE_COMMAND} -E touch ${_lambda_wp_stamp}
    DEPENDS ${_lambda_wp_XML}
    COMMENT "Generating Wayland protocol ${_lambda_wp_BASENAME} (${_lambda_wp_MODE})"
    VERBATIM
  )

  target_sources(${_lambda_wp_TARGET} PRIVATE ${_lambda_wp_outputs})
  set_source_files_properties(${_lambda_wp_outputs} PROPERTIES GENERATED TRUE)
endfunction()

function(lambda_wayland_client_protocols TARGET)
  foreach(_lambda_wp IN ITEMS ${ARGN})
    if(_lambda_wp MATCHES "^([^|]+)\\|(.+)$")
      lambda_wayland_protocols(
        TARGET ${TARGET}
        MODE CLIENT
        XML "${CMAKE_MATCH_1}"
        BASENAME "${CMAKE_MATCH_2}")
    else()
      message(FATAL_ERROR "lambda_wayland_client_protocols entry must be XML|BASENAME, got: ${_lambda_wp}")
    endif()
  endforeach()
endfunction()

function(lambda_wayland_server_protocols TARGET)
  foreach(_lambda_wp IN ITEMS ${ARGN})
    if(_lambda_wp MATCHES "^([^|]+)\\|(.+)$")
      lambda_wayland_protocols(
        TARGET ${TARGET}
        MODE SERVER
        XML "${CMAKE_MATCH_1}"
        BASENAME "${CMAKE_MATCH_2}")
    else()
      message(FATAL_ERROR "lambda_wayland_server_protocols entry must be XML|BASENAME, got: ${_lambda_wp}")
    endif()
  endforeach()
endfunction()
