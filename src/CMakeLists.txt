# src/CMakeLists.txt
add_library(minihls_core version.cpp)
target_include_directories(minihls_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(minihls main.cpp)
target_link_libraries(minihls PRIVATE minihls_core)