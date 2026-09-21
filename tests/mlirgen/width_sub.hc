// Subtraction is ALWAYS signed, even from two unsigned operands — but each
// operand still extends by its own signedness, so these are extui, not extsi.
// That is what makes u8(0) - u8(1) into i9(-1) rather than 511.
// CHECK-LABEL: func.func @sub
// CHECK: %[[A:.*]] = arith.extui %arg0 : i8 to i9
// CHECK: %[[B:.*]] = arith.extui %arg1 : i8 to i9
// CHECK: arith.subi %[[A]], %[[B]] : i9
i9 sub(u8 a, u8 b) { return a - b; }
