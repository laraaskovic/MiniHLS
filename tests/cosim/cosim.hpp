// tests/cosim/cosim.hpp — the shared half of a co-simulation testbench.
//
// Co-simulation is the claim this whole project rests on: the SAME vectors go
// through the reference interpreter and through the generated hardware, and
// they have to agree bit for bit. The vectors are the ones in
// examples/<name>.tests, worked out on paper. Nothing here runs the
// interpreter — the .tests file IS the interpreter's answer, already checked
// against it by `minihls test`, so comparing against the file compares against
// the interpreter without needing both in one process.
//
// Verilator turns the generated .sv into a C++ class with one member per port
// and an eval() method. For a COMBINATIONAL module that is the whole protocol:
// write the inputs, call eval(), read the output. No clock to drive, no reset
// sequence, no waiting — the wires settle inside the call. A sequential module
// needs a clock loop and a reset, which is why reaching the combinational rung
// first made this file short.
//
// Each example supplies two small functions, because only it knows its own
// ports and its own signedness. Everything else is here.
#pragma once
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace minihls::cosim {

struct Case {
  std::vector<long long> inputs;
  long long expected;
  unsigned line;
};

// The .tests format, for the scalar subset: `#` to end of line is a comment,
// blank lines are ignored, and a case is `<inputs> -> <output>`. A bracketed
// list means an array or a stream, which needs E4.S7 and E6 — refused here
// with a message that says so rather than mis-parsed.
inline bool readCases(const std::string& path, std::vector<Case>& out) {
  std::ifstream in(path);
  if (!in) { std::cerr << "cosim: cannot open " << path << "\n"; return false; }

  std::string raw;
  unsigned lineNo = 0;
  while (std::getline(in, raw)) {
    ++lineNo;
    if (const auto hash = raw.find('#'); hash != std::string::npos) raw.erase(hash);
    if (raw.find_first_not_of(" \t\r") == std::string::npos) continue;

    if (raw.find('[') != std::string::npos) {
      std::cerr << "cosim: " << path << ":" << lineNo
                << ": arrays and streams are not co-simulated yet "
                   "(arrays are E4.S7, streams are E6)\n";
      return false;
    }

    const auto arrow = raw.find("->");
    if (arrow == std::string::npos) {
      std::cerr << "cosim: " << path << ":" << lineNo << ": no `->` on this line\n";
      return false;
    }

    Case c{};
    c.line = lineNo;
    std::istringstream lhs(raw.substr(0, arrow));
    for (long long v; lhs >> v;) c.inputs.push_back(v);
    std::istringstream rhs(raw.substr(arrow + 2));
    if (!(rhs >> c.expected)) {
      std::cerr << "cosim: " << path << ":" << lineNo << ": no expected value\n";
      return false;
    }
    out.push_back(c);
  }
  if (out.empty()) { std::cerr << "cosim: " << path << ": no cases\n"; return false; }
  return true;
}

// Drive every case through the module and compare. `bind` writes one case's
// inputs onto the ports; `read` returns the result as a signed C++ integer,
// which is where each example states its own result signedness — the last
// place in the whole pipeline that fact has to be restated by hand.
template <typename Dut, typename Bind, typename Read>
int run(int argc, char** argv, Bind bind, Read read) {
  if (argc != 2) {
    std::cerr << "usage: " << (argc ? argv[0] : "cosim") << " <file.tests>\n";
    return 2;
  }
  std::vector<Case> cases;
  if (!readCases(argv[1], cases)) return 1;

  Dut dut;
  unsigned passed = 0, failed = 0;
  for (const Case& c : cases) {
    bind(dut, c.inputs);
    dut.eval();                       // the combinational wires settle here
    const long long got = read(dut);
    if (got == c.expected) {
      ++passed;
    } else {
      ++failed;
      std::cerr << argv[1] << ":" << c.line << ": inputs";
      for (long long v : c.inputs) std::cerr << " " << v;
      std::cerr << " -> expected " << c.expected << ", hardware gave " << got << "\n";
    }
  }
  dut.final();

  std::cout << argv[1] << ": " << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

} // namespace minihls::cosim
