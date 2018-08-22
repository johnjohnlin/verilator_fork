// This file ONLY is placed into the Public Domain, for any use,
// Author: Yu-Sheng Lin johnjohnlys@media.ee.ntu.edu.tw

module GaloisLfsr(
	input  logic clk,
	input  logic rst,
	input  logic en,
	output logic [4:0] state
);
	logic [4:0] state_w;
	logic [4:0] state_array [3];
	assign state = state_array[0];

	always_comb begin
		state_w[4] = state_array[2][0];
		state_w[3] = state_array[2][4];
		state_w[2] = state_array[2][3] ^ state_array[2][0];
		state_w[1] = state_array[2][2];
		state_w[0] = state_array[2][1];
	end

	always_ff @(posedge clk or negedge rst) begin
		if (!rst) begin
			for (int i = 0; i < 3; i++)
				state_array[i] <= 'b1;
		end else if (en) begin
			for (int i = 0; i < 2; i++)
				state_array[i] <= state_array[i+1];
			state_array[2] <= state_w;
		end
	end
endmodule
