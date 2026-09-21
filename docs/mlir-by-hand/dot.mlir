func.func @dot(%x: memref<8xi16>, %y: memref<8xi16>) -> i32 {
  %c0   = arith.constant 0 : index
  %c1   = arith.constant 1 : index
  %c8   = arith.constant 8 : index
  %zero = arith.constant 0 : i32

  %total = scf.for %i = %c0 to %c8 step %c1 iter_args(%sum = %zero) -> (i32) {
    %xi = memref.load %x[%i] : memref<8xi16>
    %yi = memref.load %y[%i] : memref<8xi16>
    %xe = arith.extsi %xi : i16 to i32
    %ye = arith.extsi %yi : i16 to i32
    %p  = arith.muli %xe, %ye : i32
    %s  = arith.addi %sum, %p : i32
    scf.yield %s : i32
  }

  func.return %total : i32
}