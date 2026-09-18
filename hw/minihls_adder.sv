`default_nettype none

// Hand-written reference adder.
//
// This is the shape MiniHLS itself will have to emit, so it also pins down the
// language's width rule for addition: the sum of two signed operands is one bit
// wider than the wider operand, which makes overflow impossible rather than
// implementation-defined.
module minihls_adder #(
    parameter int unsigned WIDTH = 8
) (
    input  wire signed [WIDTH-1:0] a,
    input  wire signed [WIDTH-1:0] b,
    output wire signed [WIDTH:0]   sum
);

  // Sign-extending both operands by one bit before the add is what makes the
  // extra result bit carry real information instead of a discarded carry-out.
  assign sum = {{1{a[WIDTH-1]}}, a} + {{1{b[WIDTH-1]}}, b};

endmodule

`default_nettype wire
