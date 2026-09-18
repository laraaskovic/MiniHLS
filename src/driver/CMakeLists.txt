add_executable(minihls main.cpp)

target_link_libraries(minihls
  PRIVATE minihls_frontend minihls_warnings)
