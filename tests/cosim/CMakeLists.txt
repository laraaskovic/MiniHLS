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
