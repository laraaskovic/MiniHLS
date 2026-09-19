add_subdirectory(unit)

if(MINIHLS_MLIR)
  add_subdirectory(mlir)
endif()

if(MINIHLS_COSIM)
  # Verilator installs its CMake integration under <prefix>/share/verilator.
  find_package(verilator HINTS $ENV{VERILATOR_ROOT} ${VERILATOR_ROOT})
  if(verilator_FOUND)
    add_subdirectory(cosim)
  else()
    message(WARNING
      "Verilator not found; the co-simulation suite will be skipped. "
      "Install Verilator 5 or configure with -DMINIHLS_COSIM=OFF to silence this.")
  endif()
endif()
