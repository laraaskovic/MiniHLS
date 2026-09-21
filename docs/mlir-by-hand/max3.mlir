func.func @max3(%a: i16, %b: i16, %c: i16) -> i16 {
  %ab = arith.cmpi sgt, %a, %b : i16
  %m1 = arith.select %ab, %a, %b : i16
  %mc = arith.cmpi sgt, %m1, %c : i16
  %m2 = arith.select %mc, %m1, %c : i16
  func.return %m2 : i16
}