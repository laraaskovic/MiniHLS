// tests/mlirgen_test.cpp — E4.S2: the emitter, checked against IR text.
//
// `ctest -R mlir.max3` only proves the emitted module survives the verifier.
// These tests pin down *which* operations come out, which is the actual claim
// of S2: max3 lowers to two comparators and two muxes, a name read emits
// nothing, and a declaration emits nothing at all.
#include <gtest/gtest.h>
#include "driver/compile.hpp"
#include "mlirgen/mlirgen.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace minihls;

namespace {

// Front end + emitter, printed. `ok` is false when either refused, and the
// first diagnostic is kept so the refusal tests can look at it.
struct Emitted {
  bool ok = false;
  std::string ir;
  std::string firstError;
  unsigned bodyOps = 0;      // operations in the entry block, terminator included
};

Emitted emit(const std::string& source) {
  Emitted result;
  Compilation compilation = compileText("<test>", source);
  if (!compilation.ok) {
    result.firstError = compilation.diags->all().front().message;
    return result;
  }

  mlir::MLIRContext context;
  auto module = emitModule(context, compilation);
  if (!module) {
    result.firstError = compilation.diags->all().front().message;
    return result;
  }
  if (mlir::failed(mlir::verify(*module))) {
    result.firstError = "the emitted IR failed the verifier";
    return result;
  }

  for (auto function : module->getOps<mlir::func::FuncOp>())
    result.bodyOps = static_cast<unsigned>(
        std::distance(function.getBody().front().begin(),
                      function.getBody().front().end()));

  llvm::raw_string_ostream stream(result.ir);
  module->print(stream);
  result.ok = true;
  return result;
}

} // namespace

// The whole point of S2: this text, operation for operation. It is the file
// hand-written in S1, modulo the block-argument names MLIR chooses itself.
TEST(MLIRGen, Max3MatchesTheHandWrittenModule) {
  auto emitted = emit("i16 max3(i16 a, i16 b, i16 c) {\n"
                      "  i16 largest = a > b ? a : b;\n"
                      "  return largest > c ? largest : c;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_EQ(emitted.ir,
            "module {\n"
            "  func.func @max3(%arg0: i16, %arg1: i16, %arg2: i16) -> i16 {\n"
            "    %0 = arith.cmpi sgt, %arg0, %arg1 : i16\n"
            "    %1 = arith.select %0, %arg0, %arg1 : i16\n"
            "    %2 = arith.cmpi sgt, %1, %arg2 : i16\n"
            "    %3 = arith.select %2, %1, %arg2 : i16\n"
            "    return %3 : i16\n"
            "  }\n"
            "}\n");
}

// Four lines of source, four operations. `largest` does not appear at all —
// no alloca, no store, no load. It named %1 and the name evaporated.
TEST(MLIRGen, TheDeclarationOfLargestEmitsNothing) {
  auto emitted = emit("i16 max3(i16 a, i16 b, i16 c) {\n"
                      "  i16 largest = a > b ? a : b;\n"
                      "  return largest > c ? largest : c;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_EQ(emitted.bodyOps, 5u);                 // 2 cmpi, 2 select, 1 return
  EXPECT_EQ(emitted.ir.find("largest"), std::string::npos);
  EXPECT_EQ(emitted.ir.find("memref"), std::string::npos);
}

// Reading a parameter is a DenseMap lookup, so the body is just its
// terminator: the value it returns already existed as a block argument.
TEST(MLIRGen, ReadingAParameterEmitsNoOperation) {
  auto emitted = emit("i16 ident(i16 a) { return a; }\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_EQ(emitted.bodyOps, 1u);
  EXPECT_NE(emitted.ir.find("return %arg0 : i16"), std::string::npos);
}

TEST(MLIRGen, AnIntegerLiteralBecomesAConstantOfItsCheckedType) {
  auto emitted = emit("i16 five() { return 5; }\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("arith.constant 5 : i16"), std::string::npos);
}

// MLIR integers are signless, so `u8` prints as `i8`. The signedness the type
// system tracked is not lost — it moved into the choice of predicate.
TEST(MLIRGen, SignednessSurvivesInThePredicateNotTheType) {
  auto unsignedMax = emit("u8 umax(u8 a, u8 b) { return a > b ? a : b; }\n");
  ASSERT_TRUE(unsignedMax.ok) << unsignedMax.firstError;
  EXPECT_NE(unsignedMax.ir.find("arith.cmpi ugt, %arg0, %arg1 : i8"), std::string::npos);
  EXPECT_EQ(unsignedMax.ir.find("u8"), std::string::npos);

  auto signedMax = emit("i8 smax(i8 a, i8 b) { return a > b ? a : b; }\n");
  ASSERT_TRUE(signedMax.ok) << signedMax.firstError;
  EXPECT_NE(signedMax.ir.find("arith.cmpi sgt, %arg0, %arg1 : i8"), std::string::npos);
}

TEST(MLIRGen, EqualityAndInequalityNeedNoSignedness) {
  auto emitted = emit("i16 f(i16 a, i16 b, i16 c) {\n"
                      "  u1 e = a == b;\n"
                      "  return e != 0 ? c : a;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("arith.cmpi eq, %arg0, %arg1 : i16"), std::string::npos);
  EXPECT_NE(emitted.ir.find("arith.cmpi ne,"), std::string::npos);
}

// Assignment replaces the entry in `values_` — it does not write to storage.
// So the second assignment simply makes `m` mean %arg1, and the function
// returns that. This is SSA construction for straight-line code, entire.
TEST(MLIRGen, AssignmentRebindsTheNameInsteadOfStoring) {
  auto emitted = emit("i16 pick(i16 a, i16 b) {\n"
                      "  i16 m = a;\n"
                      "  m = b;\n"
                      "  return m;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_EQ(emitted.bodyOps, 1u);
  EXPECT_NE(emitted.ir.find("return %arg1 : i16"), std::string::npos);
}

// The inner `m` is a different Symbol*, so rebinding it cannot disturb the
// outer one — the map is keyed on symbols, never on spelling.
TEST(MLIRGen, ShadowedNamesAreDistinctSymbolsAndDistinctValues) {
  auto emitted = emit("i16 nested(i16 a, i16 b) {\n"
                      "  i16 m = a;\n"
                      "  {\n"
                      "    i16 m = b;\n"
                      "    m = a;\n"
                      "  }\n"
                      "  return m;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("return %arg0 : i16"), std::string::npos);
}

// A file-scope const has no SSA value until something reads it; each read
// materialises a constant in place.
TEST(MLIRGen, AFileScopeConstantIsMaterialisedWhereItIsRead) {
  auto emitted = emit("const i16 THRESH = 100;\n"
                      "u1 over(i16 a) { return a > THRESH ? 1 : 0; }\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("arith.constant 100 : i16"), std::string::npos);
  EXPECT_NE(emitted.ir.find("arith.cmpi sgt,"), std::string::npos);
}

// Deliberately NOT cached in values_. A cached constant would be created at
// whichever insertion point read it first, and from S5 that point can be
// inside an scf region — where it would not dominate a later use outside.
// Emitting one per read is always correct; `cse` collapses them in E5.
TEST(MLIRGen, EachReadOfAConstantGetsItsOwnOperation) {
  auto emitted = emit("const i16 K = 7;\n"
                      "i16 twice(i16 a) {\n"
                      "  i16 lo = a > K ? a : K;\n"
                      "  return lo > K ? lo : K;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  unsigned constants = 0;
  for (size_t at = emitted.ir.find("arith.constant"); at != std::string::npos;
       at = emitted.ir.find("arith.constant", at + 1))
    ++constants;
  EXPECT_EQ(constants, 4u);           // K is read four times
}

// The symptom side of the sema fix in TypeChecker.ContextPinsEveryNodeOf...:
// both arms are untyped literals, so their width comes from the return type.
// An i1 constant here would be caught by the return-width check, but with a
// message blaming S3 for what is a type-checker gap.
TEST(MLIRGen, LiteralArmsTakeTheirWidthFromTheContext) {
  auto emitted = emit("i16 flag(i16 a, i16 b) { return a > b ? 1 : 0; }\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("arith.constant 1 : i16"), std::string::npos);
  EXPECT_NE(emitted.ir.find("arith.constant 0 : i16"), std::string::npos);
  EXPECT_NE(emitted.ir.find("arith.select %0, %c1_i16, %c0_i16 : i16"),
            std::string::npos);
}

// ---- what S2 deliberately refuses ----
//
// Each of these must produce a diagnostic and NO module. Emitting half a
// function and letting the verifier find it is the failure mode to avoid.

// S3 replaced the refusal: the operands now extend to the result width. The
// width table itself is covered case by case in tests/mlirgen/*.hc.
TEST(MLIRGen, ArithmeticExtendsItsOperandsToTheResultWidth) {
  auto emitted = emit("i17 add(i16 a, i16 b) { return a + b; }\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("arith.extsi %arg0 : i16 to i17"), std::string::npos);
  EXPECT_NE(emitted.ir.find("arith.addi"), std::string::npos);
  EXPECT_EQ(emitted.ir.find("i16 to i16"), std::string::npos);   // no null cast
}

// The guard protects the divisor, not the result. If a zero reaches
// arith.divsi the operation is undefined, and an optimiser is then entitled
// to delete the correction that follows it.
TEST(MLIRGen, DivisionGuardsTheDivisorRatherThanTheResult) {
  auto emitted = emit("i17 dvs(i16 a, i16 b) { return a / b; }\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  const size_t guard = emitted.ir.find("arith.cmpi eq");
  const size_t div = emitted.ir.find("arith.divsi");
  ASSERT_NE(guard, std::string::npos);
  ASSERT_NE(div, std::string::npos);
  EXPECT_LT(guard, div) << "the zero test must come before the division";
  EXPECT_NE(emitted.ir.find("arith.select"), std::string::npos);
}

TEST(MLIRGen, ArrayParametersAreRefused) {
  auto emitted = emit("i16 first(i16 x[4]) { return x[0]; }\n");
  EXPECT_FALSE(emitted.ok);
  EXPECT_NE(emitted.firstError.find("scalar"), std::string::npos) << emitted.firstError;
}

// The induction variable's update is exempt from the width-growth rules
// (LANGUAGE.md), so `i = i + 1` on a u4 is legal and this program really does
// reach MLIRGen — which is the point. Asserting only `!ok` would also pass for
// a program that never got past the type checker.
TEST(MLIRGen, ForLoopsAreStillRefused) {
  auto forLoop = emit("i16 f(i16 a, i16 b) {\n"
                      "  i16 m = a;\n"
                      "  for (u4 i = 0; i < 4; i = i + 1) { m = b; }\n"
                      "  return m;\n"
                      "}\n");
  EXPECT_FALSE(forLoop.ok);
  EXPECT_NE(forLoop.firstError.find("E4.S6"), std::string::npos) << forLoop.firstError;
}

// An early exit cannot be one of the values an scf.if yields, so it is
// refused rather than mis-emitted. Lifting this needs more than S5 has.
TEST(MLIRGen, AReturnInsideAnIfIsRefused) {
  auto emitted = emit("i16 f(u1 c, i16 a, i16 b) {\n"
                      "  if (c) { return a; }\n"
                      "  return b;\n"
                      "}\n");
  EXPECT_FALSE(emitted.ok);
  EXPECT_NE(emitted.firstError.find("return"), std::string::npos) << emitted.firstError;
}

// ---- if/else becomes a value (E4.S5) ----

// The merge point is where the two straight-line rules stop being enough:
// after the `if`, "the value m holds" has two answers, and a DenseMap cannot
// store both. scf.if results are MLIR's structured answer to phi nodes.
TEST(MLIRGen, AnIfBecomesAnScfIfWithOneResultPerAssignedSymbol) {
  auto emitted = emit("i16 f(u1 c, i16 a, i16 b) {\n"
                      "  i16 m = a;\n"
                      "  if (c) { m = b; } else { m = a; }\n"
                      "  return m;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("scf.if %arg0 -> (i16)"), std::string::npos);
  EXPECT_NE(emitted.ir.find("scf.yield %arg2 : i16"), std::string::npos);
  EXPECT_NE(emitted.ir.find("scf.yield %arg1 : i16"), std::string::npos);
}

// A symbol assigned in only one branch still needs a result; the branch that
// leaves it alone yields the value it had before the `if`.
TEST(MLIRGen, AOneSidedIfStillYieldsFromBothBranches) {
  auto emitted = emit("i16 f(u1 c, i16 a, i16 b) {\n"
                      "  i16 m = a;\n"
                      "  if (c) { m = b; }\n"
                      "  return m;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("scf.if %arg0 -> (i16)"), std::string::npos);
  EXPECT_NE(emitted.ir.find("scf.yield %arg2 : i16"), std::string::npos);
  EXPECT_NE(emitted.ir.find("scf.yield %arg1 : i16"), std::string::npos);
}

// Nothing assigned in either branch means no results at all — an scf.if that
// is pure control flow. `values_` is untouched across it.
TEST(MLIRGen, AnIfThatAssignsNothingHasNoResults) {
  auto emitted = emit("i16 f(u1 c, i16 a) {\n"
                      "  if (c) { } else { }\n"
                      "  return a;\n"
                      "}\n");
  ASSERT_TRUE(emitted.ok) << emitted.firstError;
  EXPECT_NE(emitted.ir.find("scf.if"), std::string::npos);
  EXPECT_EQ(emitted.ir.find("-> ("), std::string::npos);
}
