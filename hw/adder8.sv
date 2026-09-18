`default_nettype none

// Width-8 instance of minihls_adder.
//
// A Verilated model has exactly one top module, so each width under test gets a
// thin named wrapper rather than being parameterized from the testbench.
module adder8 (
    input  wire signed [7:0] a,
    input  wire signed [7:0] b,
    output wire signed [8:0] sum
);

  minihls_adder #(.WIDTH(8)) u_adder (
      .a  (a),
      .b  (b),
      .sum(sum)
  );

endmodule

`default_nettype wire
