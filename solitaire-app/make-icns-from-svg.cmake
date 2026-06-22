if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED ICONSET OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "make-icns-from-svg.cmake requires INPUT, OUTPUT, ICONSET, and WORK_DIR")
endif()
if(NOT DEFINED QLMANAGE OR NOT DEFINED SIPS OR NOT DEFINED ICONUTIL)
  message(FATAL_ERROR "make-icns-from-svg.cmake requires QLMANAGE, SIPS, and ICONUTIL")
endif()

file(REMOVE_RECURSE "${ICONSET}" "${WORK_DIR}")
file(MAKE_DIRECTORY "${ICONSET}" "${WORK_DIR}")

get_filename_component(_input_name "${INPUT}" NAME)
execute_process(
  COMMAND "${QLMANAGE}" -t -s 1024 -o "${WORK_DIR}" "${INPUT}"
  RESULT_VARIABLE _ql_result
  OUTPUT_QUIET
  ERROR_QUIET)
if(NOT _ql_result EQUAL 0)
  message(FATAL_ERROR "qlmanage failed to render ${INPUT}")
endif()

set(_source_png "${WORK_DIR}/${_input_name}.png")
if(NOT EXISTS "${_source_png}")
  file(GLOB _rendered_pngs "${WORK_DIR}/*.png")
  list(LENGTH _rendered_pngs _rendered_count)
  if(NOT _rendered_count EQUAL 1)
    message(FATAL_ERROR "qlmanage did not produce a single PNG for ${INPUT}")
  endif()
  list(GET _rendered_pngs 0 _source_png)
endif()

function(_solitaire_make_icon size filename)
  execute_process(
    COMMAND "${SIPS}" -z "${size}" "${size}" "${_source_png}" --out "${ICONSET}/${filename}"
    RESULT_VARIABLE _sips_result
    OUTPUT_QUIET
    ERROR_QUIET)
  if(NOT _sips_result EQUAL 0)
    message(FATAL_ERROR "sips failed to create ${filename}")
  endif()
endfunction()

_solitaire_make_icon(16 "icon_16x16.png")
_solitaire_make_icon(32 "icon_16x16@2x.png")
_solitaire_make_icon(32 "icon_32x32.png")
_solitaire_make_icon(64 "icon_32x32@2x.png")
_solitaire_make_icon(128 "icon_128x128.png")
_solitaire_make_icon(256 "icon_128x128@2x.png")
_solitaire_make_icon(256 "icon_256x256.png")
_solitaire_make_icon(512 "icon_256x256@2x.png")
_solitaire_make_icon(512 "icon_512x512.png")
_solitaire_make_icon(1024 "icon_512x512@2x.png")

execute_process(
  COMMAND "${ICONUTIL}" -c icns -o "${OUTPUT}" "${ICONSET}"
  RESULT_VARIABLE _iconutil_result
  OUTPUT_QUIET
  ERROR_QUIET)
if(NOT _iconutil_result EQUAL 0)
  message(FATAL_ERROR "iconutil failed to create ${OUTPUT}")
endif()
