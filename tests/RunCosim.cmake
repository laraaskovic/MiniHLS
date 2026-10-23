# tests/RunCosim.cmake — the full co-simulation, in three steps.
#
#   1. minihls emit-verilog   <name>.hc        ->  <name>.sv
#   2. verilator --cc --exe --build            ->  V<name>, a native binary
#   3. V<name> examples/<name>.tests           ->  pass/fail per vector
#
# Separate execute_process calls rather than a shell pipeline, for the reason
# RunFileCheck.cmake gives: a pipeline reports only the last command's status.
#
# Step 2's paths must be ABSOLUTE. Verilator writes a makefile and runs make
# inside the output directory, passing the testbench path through unchanged, so
# a path relative to the repo root resolves to nothing from in there and make
# reports "No rule to make target".

execute_process(
  COMMAND ${MINIHLS} emit-verilog ${CASE}
  OUTPUT_VARIABLE sv
  ERROR_VARIABLE diagnostics
  RESULT_VARIABLE emit_status)

if(NOT emit_status EQUAL 0)
  message(FATAL_ERROR "emit-verilog failed on ${CASE} (status ${emit_status}):\n${diagnostics}")
endif()

set(work ${CMAKE_CURRENT_BINARY_DIR}/cosim-${NAME})
set(sv_path ${work}/${NAME}.sv)
file(MAKE_DIRECTORY ${work})
file(WRITE ${sv_path} "${sv}")

# -Wno-fatal: the generated RTL is allowed to be unidiomatic, not to be wrong.
# -Wall on the C++ side only, so a mistake in the testbench is not silent.
execute_process(
  COMMAND ${VERILATOR} --cc --exe --build -Wno-fatal
          -Mdir ${work}/obj
          -CFLAGS "-I${COSIM_DIR} -std=c++20 -Wall"
          ${sv_path} ${COSIM_DIR}/tb_${NAME}.cpp
  OUTPUT_VARIABLE build_out
  ERROR_VARIABLE build_err
  RESULT_VARIABLE build_status)

if(NOT build_status EQUAL 0)
  message(FATAL_ERROR "verilator failed to build the ${NAME} testbench:\n"
                      "${build_err}\n${build_out}\n--- generated ---\n${sv}")
endif()

execute_process(
  COMMAND ${work}/obj/V${NAME} ${TESTS}
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE run_err
  RESULT_VARIABLE run_status)

message("${run_out}")

if(NOT run_status EQUAL 0)
  message(FATAL_ERROR
    "the generated hardware disagrees with the reference vectors:\n${run_err}\n"
    "--- generated ---\n${sv}")
endif()
