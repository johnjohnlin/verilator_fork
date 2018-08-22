#!/usr/bin/perl
# This file ONLY is placed into the Public Domain, for any use,
# Author: Yu-Sheng Lin johnjohnlys@media.ee.ntu.edu.tw

if (!$::Driver) { use FindBin; exec("$FindBin::Bin/bootstrap.pl", @ARGV, $0); die; }
scenarios(vlt_all => 1);

top_filename("t/t_trace_lxt2.v");
compile(
	make_top_shell => 0,
	make_main => 0,
	v_flags2 => ["--trace-lxt2 --exe $Self->{t_dir}/$Self->{name}.cpp -LDFLAGS '-lz'"],
);
execute(
	check_finished => 1,
);

run(cmd => ["lxt2vcd", "$Self->{obj_dir}/simx.lxt2", "-o", "$Self->{obj_dir}/simx.vcd"]);
vcd_identical("$Self->{obj_dir}/simx.lxt2", "t/$Self->{name}.out");

ok(1);
1;

