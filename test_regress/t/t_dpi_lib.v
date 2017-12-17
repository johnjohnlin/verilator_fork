// DESCRIPTION: Verilator: Verilog Test module
//
// Copyright 2017 by Wilson Snyder. This program is free software; you can
// redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.

module t (/*AUTOARG*/);

   import "DPI-C" function int dpii_failure();
   import "DPI-C" function void dpii_check();

   initial begin
      dpii_check();

      if (dpii_failure()!=0) begin
         $write("%%Error: Failure in DPI tests\n");
         $stop;
      end

      $write("*-* All Finished *-*\n");
      $finish;
   end

endmodule