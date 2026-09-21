#include "frontend/ast_printer.hpp"

#include <algorithm>
#include <sstream>

namespace minihls {
namespace {

std::string number(u128 value) {
  if (value == 0) return "0";
  std::string out;
  while (value) {
    out.push_back(static_cast<char>('0' + value % 10));
    value /= 10;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

std::string typeName(Type type) {
  return std::string(type.isSigned ? "i" : "u") + number(type.width);
}

std::string expr(const Expr& value);

std::string binaryOperand(const Expr& value) {
  std::string printed = expr(value);
  if (value.kind == ExprKind::Unary || value.kind == ExprKind::Index ||
      value.kind == ExprKind::Cast || value.kind == ExprKind::Ternary)
    return "(" + printed + ")";
  return printed;
}

std::string expr(const Expr& value) {
  std::ostringstream out;
  switch (value.kind) {
    case ExprKind::IntLit: return number(static_cast<const IntLit&>(value).value);
    case ExprKind::NameRef: return static_cast<const NameRef&>(value).name;
    case ExprKind::Index: {
      const auto& node = static_cast<const Index&>(value);
      return node.array + "[" + expr(*node.index) + "]";
    }
    case ExprKind::Cast: {
      const auto& node = static_cast<const Cast&>(value);
      return typeName(node.target) + "(" + expr(*node.operand) + ")";
    }
    case ExprKind::Unary: {
      const auto& node = static_cast<const Unary&>(value);
      std::string operand = expr(*node.operand);
      if (node.operand->kind == ExprKind::Unary)
        return "(" + std::string(tokName(node.op)) + "(" + operand + "))";
      return std::string(tokName(node.op)) + operand;
    }
    case ExprKind::Binary: {
      const auto& node = static_cast<const Binary&>(value);
            return "(" + binaryOperand(*node.lhs) + " " + tokName(node.op) + " " +
              binaryOperand(*node.rhs) + ")";
    }
    case ExprKind::Ternary: {
      const auto& node = static_cast<const Ternary&>(value);
      return "(" + expr(*node.cond) + " ? " + expr(*node.thenE) + " : " +
             expr(*node.elseE) + ")";
    }
  }
  return {};
}

void indent(std::ostringstream& out, unsigned depth) { out << std::string(depth * 2, ' '); }

void printStmt(std::ostringstream& out, const Stmt& stmt, unsigned depth);

void printBlock(std::ostringstream& out, const Block& block, unsigned depth) {
  out << "{\n";
  for (const auto& statement : block.stmts) printStmt(out, *statement, depth + 1);
  indent(out, depth); out << "}";
}

void printRead(std::ostringstream& out, const std::string& stream) {
  out << "read(" << stream << ")";
}

void printStmt(std::ostringstream& out, const Stmt& stmt, unsigned depth) {
  indent(out, depth);
  switch (stmt.kind) {
    case StmtKind::VarDecl: {
      const auto& node = static_cast<const VarDecl&>(stmt);
      out << typeName(node.type) << " " << node.name << " = ";
      if (node.initIsRead) printRead(out, node.readStream); else out << expr(*node.init);
      out << ";\n"; return;
    }
    case StmtKind::ArrayDecl: {
      const auto& node = static_cast<const ArrayDecl&>(stmt);
      out << typeName(node.elem) << " " << node.name << "[" << expr(*node.size) << "]";
      if (!node.init.empty()) {
        out << " = { ";
        for (size_t i = 0; i < node.init.size(); ++i) { if (i) out << ", "; out << expr(*node.init[i]); }
        out << " }";
      }
      out << ";\n"; return;
    }
    case StmtKind::Assign: {
      const auto& node = static_cast<const Assign&>(stmt);
      out << node.name;
      if (node.index) out << "[" << expr(*node.index) << "]";
      out << " = ";
      if (node.valueIsRead) printRead(out, node.readStream); else out << expr(*node.value);
      out << ";\n"; return;
    }
    case StmtKind::Write: {
      const auto& node = static_cast<const Write&>(stmt);
      out << "write(" << node.stream << ", " << expr(*node.value) << ");\n"; return;
    }
    case StmtKind::If: {
      const auto& node = static_cast<const If&>(stmt);
      out << "if (" << expr(*node.cond) << ") "; printBlock(out, *node.thenB, depth);
      if (node.elseS) {
        out << " else ";
        if (node.elseS->kind == StmtKind::If) {
          out << "if (" << expr(*static_cast<const If&>(*node.elseS).cond) << ") ";
          printBlock(out, *static_cast<const If&>(*node.elseS).thenB, depth);
        } else printBlock(out, static_cast<const Block&>(*node.elseS), depth);
      }
      out << "\n"; return;
    }
    case StmtKind::For: {
      const auto& node = static_cast<const For&>(stmt);
      if (node.pragma.kind == PragmaKind::Unroll) {
        out << "#pragma unroll"; if (node.pragma.hasValue) out << " factor=" << node.pragma.value; out << "\n"; indent(out, depth);
      } else if (node.pragma.kind == PragmaKind::Pipeline) {
        out << "#pragma pipeline II=" << node.pragma.value << "\n"; indent(out, depth);
      }
      out << "for (" << typeName(node.ivType) << " " << node.iv << " = " << expr(*node.init)
          << "; " << node.iv << " " << tokName(node.relOp) << " " << expr(*node.limit)
          << "; " << node.iv << " = " << node.iv << (node.stepIsAdd ? " + " : " - ")
          << expr(*node.step) << ") "; printBlock(out, *node.body, depth); out << "\n"; return;
    }
    case StmtKind::Return: out << "return " << expr(*static_cast<const Return&>(stmt).value) << ";\n"; return;
    case StmtKind::Block: printBlock(out, static_cast<const Block&>(stmt), depth); out << "\n"; return;
  }
}

} // namespace

std::string printExpr(const Expr& value) { return expr(value); }

std::string printProgram(const Program& program) {
  std::ostringstream out;
  for (const auto& constant : program.consts) {
    out << "const " << typeName(constant.type) << " " << constant.name;
    if (constant.isArray) {
      out << "[" << expr(*constant.size) << "] = { ";
      for (size_t i = 0; i < constant.arrayInit.size(); ++i) { if (i) out << ", "; out << expr(*constant.arrayInit[i]); }
      out << " }";
    } else out << " = " << expr(*constant.init);
    out << ";\n";
  }
  out << typeName(program.fn.returnType) << " " << program.fn.name << "(";
  for (size_t i = 0; i < program.fn.params.size(); ++i) {
    if (i) out << ", ";
    const auto& param = program.fn.params[i];
    if (param.kind == ParamKind::StreamIn || param.kind == ParamKind::StreamOut)
      out << (param.kind == ParamKind::StreamIn ? "in" : "out") << " stream<" << typeName(param.type) << "> " << param.name;
    else {
      out << typeName(param.type) << " " << param.name;
      if (param.kind == ParamKind::Array) out << "[" << expr(*param.size) << "]";
    }
  }
  out << ") "; printBlock(out, *program.fn.body, 0); out << "\n";
  return out.str();
}

} // namespace minihls