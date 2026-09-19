// A test driver for dot.mlir: fill a[i] = i and b[i] = i + 1, call @dot, and
// print the result. run.sh concatenates this file with dot.mlir, lowers both to
// the LLVM dialect, and runs them with mlir-runner.
//
// Expected: the sum of i * (i + 1) for i in 0..63, which is 87360.

// Provided by MLIR's runtime support library, libmlir_c_runner_utils.
func.func private @printI64(i64)
func.func private @printNewline()

func.func @main() {
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c64 = arith.constant 64 : index
  %a = memref.alloca() : memref<64xi16>
  %b = memref.alloca() : memref<64xi16>

  scf.for %i = %c0 to %c64 step %c1 {
    %value = arith.index_cast %i : index to i16
    %one = arith.constant 1 : i16
    %next = arith.addi %value, %one : i16
    memref.store %value, %a[%i] : memref<64xi16>
    memref.store %next, %b[%i] : memref<64xi16>
  }

  %result = func.call @dot(%a, %b) : (memref<64xi16>, memref<64xi16>) -> i32
  %wide = arith.extsi %result : i32 to i64
  func.call @printI64(%wide) : (i64) -> ()
  func.call @printNewline() : () -> ()
  return
}
