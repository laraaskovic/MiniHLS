# tests/RunFileCheck.cmake — run `minihls emit-mlir <case> | FileCheck <case>`.
#
# Not `sh -c "a | b"`: a shell pipeline reports only the LAST command's status,
# so minihls could crash or refuse the program and the test would still pass as
# long as FileCheck found nothing to complain about. Running the two steps
# separately means each one's failure is a failure.

execute_process(
  COMMAND ${MINIHLS} emit-mlir ${CASE}
  OUTPUT_VARIABLE ir
  ERROR_VARIABLE diagnostics
  RESULT_VARIABLE emit_status)

if(NOT emit_status EQUAL 0)
  message(FATAL_ERROR "emit-mlir failed on ${CASE} (status ${emit_status}):\n${diagnostics}")
endif()

# Named after the case, not a fixed name: `ctest -j` runs these concurrently
# and a shared scratch file would have them overwrite each other's input.
get_filename_component(case_name ${CASE} NAME_WE)
set(scratch ${CMAKE_CURRENT_BINARY_DIR}/filecheck-${case_name}.mlir)
file(WRITE ${scratch} "${ir}")

execute_process(
  COMMAND ${FILECHECK} ${CASE} --input-file=${scratch}
  RESULT_VARIABLE check_status)

if(NOT check_status EQUAL 0)
  message(FATAL_ERROR "FileCheck failed on ${CASE}:\n${ir}")
endif()
