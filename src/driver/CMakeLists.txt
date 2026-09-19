add_executable(minihls main.cpp)

target_link_libraries(minihls
  PRIVATE minihls_frontend minihls_sema minihls_interp minihls_warnings)

if(MINIHLS_MLIR)
  target_link_libraries(minihls PRIVATE minihls_mlirgen minihls_transforms)
  target_compile_definitions(minihls PRIVATE MINIHLS_HAVE_MLIR)
endif()
