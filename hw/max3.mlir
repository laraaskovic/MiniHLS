hw.module @max3(in %a : i16, in %b : i16, in %c : i16, out result : i16) {

  %gt = comb.icmp sgt %a, %b : i16
  %r  = comb.mux %gt, %a, %b : i16
  
  // r hold max of a and b now do r and c

  %mu = comb.icmp sgt %r, %c : i16
  %fin  = comb.mux %mu, %r, %c : i16

  hw.output %fin : i16
}