# tests/RunVerilator.cmake — generate SystemVerilog, then lint it.
#
# Two steps run separately rather than as a shell pipeline, for the same reason
# RunFileCheck.cmake does: a pipeline reports only the last command's status, so
# minihls could crash and the test would still pass.
#
# `--lint-only` parses and elaborates the module, which is what we want to know
# at this stage: that ExportVerilog produced a legal, self-consistent module
# with the ports and widths we think it has. `-Wno-fatal` keeps style warnings
# from failing the build while leaving real errors fatal — generated RTL is
# allowed to be unidiomatic, not to be wrong. Checking that it computes the
# right answer is co-simulation, which is E8.S6.

execute_process(
  COMMAND ${MINIHLS} emit-verilog ${CASE}
  OUTPUT_VARIABLE sv
  ERROR_VARIABLE diagnostics
  RESULT_VARIABLE emit_status)

if(NOT emit_status EQUAL 0)
  message(FATAL_ERROR "emit-verilog failed on ${CASE} (status ${emit_status}):\n${diagnostics}")
endif()

if(sv STREQUAL "")
  message(FATAL_ERROR "emit-verilog produced nothing for ${CASE}")
endif()

set(out ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.sv)
file(WRITE ${out} "${sv}")

execute_process(
  COMMAND ${VERILATOR} --lint-only -Wno-fatal ${out}
  ERROR_VARIABLE lint
  RESULT_VARIABLE lint_status)

if(NOT lint_status EQUAL 0)
  message(FATAL_ERROR "verilator rejected the generated ${NAME}.sv:\n${lint}\n--- generated ---\n${sv}")
endif()
