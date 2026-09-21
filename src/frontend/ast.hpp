#pragma once
#include "frontend/token.hpp"
#include <memory>
#include <string>
#include <vector>

namespace minihls {

struct Symbol;

struct Type {
  unsigned width = 1;
  bool isSigned = false;
  bool isPoly = false;
  u128 constValue = 0;
};

//expressions
enum class ExprKind { IntLit, NameRef, Index, Cast, Unary, Binary, Ternary };

struct Expr {
  ExprKind kind;
  Range range;
  Type type;
  bool typeKnown = false;
  virtual ~Expr() = default;
protected:
  Expr(ExprKind k, Range r) : kind(k), range(r) {}
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLit : Expr {
  u128 value;
  IntLit(Range r, u128 v) : Expr(ExprKind::IntLit, r), value(v) {}
};

struct NameRef : Expr {
  std::string name;
  Symbol* symbol = nullptr;
  NameRef(Range r, std::string n) : Expr(ExprKind::NameRef, r), name(std::move(n)) {}
};

struct Index : Expr {                       // a[i] — array name only, per the grammar
  std::string array;
  ExprPtr index;
  Symbol* symbol = nullptr;
  Index(Range r, std::string a, ExprPtr i)
      : Expr(ExprKind::Index, r), array(std::move(a)), index(std::move(i)) {}
};

struct Cast : Expr {                        // T(e)
  Type type;
  ExprPtr operand;
  Cast(Range r, Type t, ExprPtr o)
      : Expr(ExprKind::Cast, r), type(t), operand(std::move(o)) {}
};

struct Unary : Expr {                       // - + ~ !
  Tok op;
  ExprPtr operand;
  Unary(Range r, Tok o, ExprPtr e)
      : Expr(ExprKind::Unary, r), op(o), operand(std::move(e)) {}
};

struct Binary : Expr {
  Tok op;
  ExprPtr lhs, rhs;
  Binary(Range r, Tok o, ExprPtr l, ExprPtr rr)
      : Expr(ExprKind::Binary, r), op(o), lhs(std::move(l)), rhs(std::move(rr)) {}
};

struct Ternary : Expr {                     // c ? a : b
  ExprPtr cond, thenE, elseE;
  Ternary(Range r, ExprPtr c, ExprPtr t, ExprPtr e)
      : Expr(ExprKind::Ternary, r), cond(std::move(c)),
        thenE(std::move(t)), elseE(std::move(e)) {}
};

//statements

enum class StmtKind { VarDecl, ArrayDecl, Assign, Write, If, For, Return, Block };

struct Stmt {
  StmtKind kind;
  Range range;
  virtual ~Stmt() = default;
protected:
  Stmt(StmtKind k, Range r) : kind(k), range(r) {}
};
using StmtPtr = std::unique_ptr<Stmt>;

struct Block : Stmt {
  std::vector<StmtPtr> stmts;
  explicit Block(Range r) : Stmt(StmtKind::Block, r) {}
};
using BlockPtr = std::unique_ptr<Block>;

struct VarDecl : Stmt {                     // i32 acc = <expr | read(s)>;
  Type type;
  std::string name;
  ExprPtr init;                             // null when initIsRead
  bool initIsRead = false;
  std::string readStream;                   // set when initIsRead
  Symbol* readSymbol = nullptr;
  Symbol* symbol = nullptr;
  VarDecl(Range r) : Stmt(StmtKind::VarDecl, r) {}
};

struct ArrayDecl : Stmt {                   // i16 buf[8] = { ... };
  Type elem;
  std::string name;
  ExprPtr size;
  std::vector<ExprPtr> init;                // empty if uninitialised
  Symbol* symbol = nullptr;
  ArrayDecl(Range r) : Stmt(StmtKind::ArrayDecl, r) {}
};

struct Assign : Stmt {                      // x = e;  or  a[i] = e;
  std::string name;
  ExprPtr index;                            // null for a plain variable
  ExprPtr value;                            // null when valueIsRead
  bool valueIsRead = false;
  std::string readStream;
  Symbol* readSymbol = nullptr;
  Symbol* symbol = nullptr;
  Assign(Range r) : Stmt(StmtKind::Assign, r) {}
};

struct Write : Stmt {                       // write(r, e);
  std::string stream;
  ExprPtr value;
  Symbol* symbol = nullptr;
  Write(Range r) : Stmt(StmtKind::Write, r) {}
};

struct If : Stmt {
  ExprPtr cond;
  BlockPtr thenB;
  StmtPtr elseS;                            // null, a Block, or another If
  If(Range r) : Stmt(StmtKind::If, r) {}
};

enum class PragmaKind { None, Unroll, Pipeline };
struct Pragma {
  PragmaKind kind = PragmaKind::None;
  unsigned value = 0;                       // unroll factor, or II
  bool hasValue = false;
  Range range;
};

struct For : Stmt {                         // the rigid header from the grammar
  Pragma pragma;
  Type ivType;
  std::string iv;
  ExprPtr init;                             // const_expr
  Tok relOp;                                // < <= > >=
  ExprPtr limit;                            // const_expr
  bool stepIsAdd = true;                    // `i = i + k` vs `i = i - k`
  ExprPtr step;                             // const_expr
  BlockPtr body;
  Symbol* ivSymbol = nullptr;
  uint64_t tripCount = 0;
  bool tripCountKnown = false;
  For(Range r) : Stmt(StmtKind::For, r) {}
};

struct Return : Stmt {
  ExprPtr value;
  Return(Range r) : Stmt(StmtKind::Return, r) {}
};


enum class ParamKind { Scalar, Array, StreamIn, StreamOut };

struct Param {
  ParamKind kind = ParamKind::Scalar;
  Type type;                                // element type for arrays/streams
  std::string name;
  ExprPtr size;                             // arrays only
  Symbol* symbol = nullptr;
  Range range;
};

struct ConstDecl {
  Type type;
  std::string name;
  bool isArray = false;
  ExprPtr size;                             // arrays only
  ExprPtr init;                             // scalars only
  std::vector<ExprPtr> arrayInit;           // arrays only
  Symbol* symbol = nullptr;
  bool valueKnown = false;
  Bits value;
  std::vector<Bits> foldedElements;
  Range range;
};

struct Function {
  Type returnType;
  std::string name;
  std::vector<Param> params;
  BlockPtr body;
  Range range;
};

struct Program {
  std::vector<ConstDecl> consts;
  Function fn;
  std::shared_ptr<void> symbolStorage;
};

} // namespace minihls