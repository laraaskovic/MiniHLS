// src/main.cpp
#include <cstring>
#include <iostream>
#include "version.hpp"

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::cout << "minihls " << minihls::version() << "\n";
    return 0;
  }
  std::cerr << "usage: minihls --version\n";
  return 1;
}