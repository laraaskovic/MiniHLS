# The Verilated models and the Verilator runtime are generated C++, so they live
# in their own library: holding machine-written code to this project's warning
# set produces noise nobody can act on. verilate() exports its include
# directories PUBLIC, so the testbench still sees the generated headers.
add_library(minihls_cosim_models STATIC)

# The generated headers are pulled into the testbench too, where they would trip
# -Wsign-compare and -Wconversion from inside template instantiations. Marking
# the interface include directories as system keeps those diagnostics out.
set_target_properties(minihls_cosim_models PROPERTIES SYSTEM TRUE)

# One model per width under test. A Verilated model has exactly one top module,
# which is why hw/ carries a named wrapper for each width.
verilate(minihls_cosim_models
  PREFIX Vadder8
  TOP_MODULE adder8
  SOURCES
    ${PROJECT_SOURCE_DIR}/hw/adder8.sv
    ${PROJECT_SOURCE_DIR}/hw/minihls_adder.sv
  VERILATOR_ARGS -Wall)

verilate(minihls_cosim_models
  PREFIX Vadder16
  TOP_MODULE adder16
  SOURCES
    ${PROJECT_SOURCE_DIR}/hw/adder16.sv
    ${PROJECT_SOURCE_DIR}/hw/minihls_adder.sv
  VERILATOR_ARGS -Wall)

add_executable(minihls_cosim_tests
  adder_cosim_test.cpp)

target_link_libraries(minihls_cosim_tests
  PRIVATE minihls_cosim_models minihls_support minihls_warnings GTest::gtest_main)

add_test(NAME cosim COMMAND minihls_cosim_tests)

# max3 written by hand in CIRCT's hardware dialects (milestone P0). CIRCT turns
# hw/max3.mlir into SystemVerilog at build time, and the test checks that
# hardware against the reference interpreter running examples/max3.hc -- the
# same shape every generated design will be checked in.
if(MINIHLS_MLIR)
  set(max3_sv_dir "${CMAKE_CURRENT_BINARY_DIR}/max3_sv")
  set(max3_export
    "${MINIHLS_CIRCT_OPT}" "${PROJECT_SOURCE_DIR}/hw/max3.mlir"
    "--export-split-verilog=dir-name=${max3_sv_dir}" -o /dev/null)
  # verilate() runs Verilator while configuring, so the Verilog has to exist
  # before the build starts; the custom command below keeps it up to date.
  execute_process(COMMAND ${max3_export} RESULT_VARIABLE max3_export_result)
  if(NOT max3_export_result EQUAL 0)
    message(FATAL_ERROR "circt-opt could not export hw/max3.mlir")
  endif()
  add_custom_command(
    OUTPUT "${max3_sv_dir}/max3.sv"
    COMMAND ${max3_export}
    DEPENDS "${PROJECT_SOURCE_DIR}/hw/max3.mlir"
    COMMENT "Exporting hw/max3.mlir to SystemVerilog with CIRCT"
    VERBATIM)
  add_custom_target(minihls_max3_sv DEPENDS "${max3_sv_dir}/max3.sv")

  add_library(minihls_cosim_max3 STATIC)
  set_target_properties(minihls_cosim_max3 PROPERTIES SYSTEM TRUE)
  add_dependencies(minihls_cosim_max3 minihls_max3_sv)
  verilate(minihls_cosim_max3
    PREFIX Vmax3
    TOP_MODULE max3
    SOURCES "${max3_sv_dir}/max3.sv"
    VERILATOR_ARGS -Wall)

  add_executable(minihls_cosim_max3_tests max3_cosim_test.cpp)
  target_link_libraries(minihls_cosim_max3_tests
    PRIVATE minihls_cosim_max3 minihls_interp minihls_warnings GTest::gtest_main)
  target_compile_definitions(minihls_cosim_max3_tests
    PRIVATE MINIHLS_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
  add_test(NAME cosim_max3 COMMAND minihls_cosim_max3_tests)
endif()
