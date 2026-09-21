func.func @scratch(%a: i32, %b: i32) -> i32 {
  %zero = arith.constant 0 : i32
  %one  = arith.constant 1 : i32
  %p1 = arith.muli %a, %b : i32
  %p2 = arith.muli %a, %b : i32        // identical to %p1
  %s  = arith.addi %p1, %p2 : i32
  %t  = arith.addi %s, %zero : i32     // + 0
  %u  = arith.muli %t, %one : i32      // * 1
  %dead = arith.subi %a, %b : i32      // never used
  func.return %u : i32
}