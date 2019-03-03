// DESCRIPTION: Verilator: Verilog Test module
//
// This file ONLY is placed into the Public Domain, for any use,
// without warranty, 2009 by Wilson Snyder.

module t (/*AUTOARG*/
   // Inputs
   clk
   );
   input clk;   

   /*AUTOWIRE*/
   wire [79:0] out0 = Test0();

   // Test loop
   always @ (posedge clk) begin
`ifdef TEST_VERBOSE
      $write("[%0t] result=%x\n",$time, out0);
`endif
   if (out0 !== 80'b0000000000_0000000010_0000001000_0000011000_0001000000_0010100000_0110000000_1110000000) $stop;
   $write("*-* All Finished *-*\n");
   $finish;
   end

    function [79:0] Test0;
	for (int i=0, j = (i<<i), k = 79; i < 8; i++, j = (i<<i), k-=10) begin
	    /* verilator lint_off WIDTH */
	    Test0[k-:10] = j;
	end
    endfunction
endmodule
