`default_nettype none

// Width-16 instance of minihls_adder. See adder8.sv for why the wrapper exists.
module adder16 (
    input  wire signed [15:0] a,
    input  wire signed [15:0] b,
    output wire signed [16:0] sum
);

  minihls_adder #(.WIDTH(16)) u_adder (
      .a  (a),
      .b  (b),
      .sum(sum)
  );

endmodule

`default_nettype wire
