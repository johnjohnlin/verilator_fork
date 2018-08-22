// -*- mode: C++; c-file-style: "cc-mode" -*-
//=============================================================================
//
// THIS MODULE IS PUBLICLY LICENSED
//
// Copyright 2001-2018 by Wilson Snyder
// Copyright 2018 by Yu-Sheng Lin johnjohnlys@media.ee.ntu.edu.tw
// you can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License Version 2.0.
//
// This is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.
//
//=============================================================================
///
/// \file
/// \brief C++ Tracing in LXT2 Format
///
//=============================================================================
// SPDIFF_OFF

#ifndef _VERILATED_LXT2_C_H_
#define _VERILATED_LXT2_C_H_ 1

#include "verilatedos.h"
#include "verilated.h"
#include "lxt2/lxt2_write.h"

#include <string>
#include <vector>
#include <map>

class VerilatedLxt2;
class VerilatedLxt2CallInfo;
typedef void (*VerilatedLxt2Callback_t)(VerilatedLxt2* vcdp, void* userthis, vluint32_t code);

//=============================================================================
// VerilatedLxt2
/// Base class to create a Verilator LXT2 dump
/// This is an internally used class - see VerilatedLxt2C for what to call from applications

class VerilatedLxt2 {
	typedef std::map<vluint32_t, lxt2_wr_symbol*> Code2SymbolType;
	typedef std::vector<VerilatedLxt2CallInfo*>  CallbackVec;
	private:
		lxt2_wr_trace* m_lxt2;
		VerilatedAssertOneThread m_assertOne; ///< Assert only called from single thread
		char m_scopeEscape;
		std::string m_module;
		CallbackVec m_callbacks; ///< Routines to perform dumping
		Code2SymbolType m_code2symbol;
		// CONSTRUCTORS
		VL_UNCOPYABLE(VerilatedLxt2);
	public:
		explicit VerilatedLxt2(lxt2_wr_trace* lxt2=NULL): m_lxt2(lxt2) {}
		~VerilatedLxt2() { if (m_lxt2 == NULL) { lxt2_wr_close(m_lxt2); } }
		bool isOpen() const { return m_lxt2 != NULL; }
		void open(const char* filename) VL_MT_UNSAFE;
		void flush() VL_MT_UNSAFE { lxt2_wr_flush(m_lxt2); }
		void close() VL_MT_UNSAFE {
			m_assertOne.check();
			lxt2_wr_close(m_lxt2);
			m_lxt2 = NULL;
		}
		// void set_time_unit (const char* unit); ///< Set time units (s/ms, defaults to ns)
		// void set_time_unit (const std::string& unit) { set_time_unit(unit.c_str()); }

		// void set_time_resolution (const char* unit); ///< Set time resolution (s/ms, defaults to ns)
		// void set_time_resolution (const std::string& unit) { set_time_resolution(unit.c_str()); }

		// double timescaleToDouble (const char* unitp);
		// std::string doubleToTimescale (double value);

		/// Change character that splits scopes.  Note whitespace are ALWAYS escapes.
		void scopeEscape(char flag) { m_scopeEscape = flag; }
		/// Is this an escape?
		bool isScopeEscape(char c) { return isspace(c) || c==m_scopeEscape; }
		/// Inside dumping routines, called each cycle to make the dump
		void dump (vluint64_t timeui);
		/// Inside dumping routines, declare callbacks for tracings
		void addCallback (VerilatedLxt2Callback_t init, VerilatedLxt2Callback_t full,
				VerilatedLxt2Callback_t change,
				void* userthis) VL_MT_UNSAFE_ONE;

		/// Inside dumping routines, declare a module
		void module (const std::string& name);
		/// Inside dumping routines, declare a signal
		void declBit (vluint32_t code, const char* name, int arraynum);
		void declBus (vluint32_t code, const char* name, int arraynum, int msb, int lsb);

		/// Inside dumping routines, dump one signal if it has changed
		void chgBit (vluint32_t code, const vluint32_t newval) {
			lxt2_wr_emit_value_int(m_lxt2, m_code2symbol[code], 0, newval);
		}
		void chgBus (vluint32_t code, const vluint32_t newval, int bits) {
			lxt2_wr_emit_value_int(m_lxt2, m_code2symbol[code], 0, newval);
		}
		void fullBit (vluint32_t code, const vluint32_t newval) { chgBit(code, newval); }
		void fullBus (vluint32_t code, const vluint32_t newval, int bits) { chgBus(code, newval, bits); }

		// TODO: Disabled functions
		// Even in a large module, these functions are not used?
		void declQuad     (vluint32_t code, const char* name, int arraynum, int msb, int lsb);
		void declArray    (vluint32_t code, const char* name, int arraynum, int msb, int lsb);
		void declTriBit   (vluint32_t code, const char* name, int arraynum);
		void declTriBus   (vluint32_t code, const char* name, int arraynum, int msb, int lsb);
		void declTriQuad  (vluint32_t code, const char* name, int arraynum, int msb, int lsb);
		void declTriArray (vluint32_t code, const char* name, int arraynum, int msb, int lsb);
		void declDouble   (vluint32_t code, const char* name, int arraynum);
		void declFloat    (vluint32_t code, const char* name, int arraynum);
		void fullQuad (vluint32_t code, const vluint64_t newval, int bits);
		void fullArray (vluint32_t code, const vluint32_t* newval, int bits);
		void fullTriBit (vluint32_t code, const vluint32_t newval, const vluint32_t newtri);
		void fullTriBus (vluint32_t code, const vluint32_t newval, const vluint32_t newtri, int bits);
		void fullTriQuad (vluint32_t code, const vluint64_t newval, const vluint32_t newtri, int bits);
		void fullTriArray (vluint32_t code, const vluint32_t* newvalp, const vluint32_t* newtrip, int bits);
		void fullDouble (vluint32_t code, const double newval);
		void fullFloat (vluint32_t code, const float newval);
		void fullBitX (vluint32_t code);
		void fullBusX (vluint32_t code, int bits);
		void fullQuadX (vluint32_t code, int bits);
		void fullArrayX (vluint32_t code, int bits);
		// Even in a large module, these functions are not used?
		void chgQuad (vluint32_t code, const vluint64_t newval, int bits);
		void chgArray (vluint32_t code, const vluint32_t* newval, int bits);
		void chgTriBit (vluint32_t code, const vluint32_t newval, const vluint32_t newtri);
		void chgTriBus (vluint32_t code, const vluint32_t newval, const vluint32_t newtri, int bits);
		void chgTriQuad (vluint32_t code, const vluint64_t newval, const vluint32_t newtri, int bits);
		void chgTriArray (vluint32_t code, const vluint32_t* newvalp, const vluint32_t* newtrip, int bits);
		void chgDouble (vluint32_t code, const double newval);
		void chgFloat (vluint32_t code, const float newval);
};

//=============================================================================
// VerilatedLxt2C
/// Create a LXT2 dump file in C standalone (no SystemC) simulations.
/// Also derived for use in SystemC simulations.
/// Thread safety: Unless otherwise indicated, every function is VL_MT_UNSAFE_ONE

class VerilatedLxt2C {
	VerilatedLxt2 m_sptrace; ///< Trace file being created

	// CONSTRUCTORS
	VL_UNCOPYABLE(VerilatedLxt2C);
	public:
	explicit VerilatedLxt2C(lxt2_wr_trace* filep=NULL) : m_sptrace(filep) {}
	~VerilatedLxt2C() {}
	public:
	// ACCESSORS
	/// Is file open?
	bool isOpen() const { return m_sptrace.isOpen(); }
	// METHODS
	/// Open a new LXT2 file
	void open(const char* filename) VL_MT_UNSAFE_ONE { m_sptrace.open(filename); }
	/// Close dump
	void close() VL_MT_UNSAFE_ONE { m_sptrace.close(); }
	/// Flush dump
	void flush() VL_MT_UNSAFE_ONE { m_sptrace.flush(); }
	/// Write one cycle of dump data
	void dump (vluint64_t timeui) { m_sptrace.dump(timeui); }
	/// Write one cycle of dump data - backward compatible and to reduce
	/// conversion warnings.  It's better to use a vluint64_t time instead.
	void dump (double timestamp) { dump(static_cast<vluint64_t>(timestamp)); }
	void dump (vluint32_t timestamp) { dump(static_cast<vluint64_t>(timestamp)); }
	void dump (int timestamp) { dump(static_cast<vluint64_t>(timestamp)); }
	/// Set time units (s/ms, defaults to ns)
	/// See also VL_TIME_PRECISION, and VL_TIME_MULTIPLIER in verilated.h
	void set_time_unit (const char* unit) { /* TODO */ }
	void set_time_unit (const std::string& unit) { set_time_unit(unit.c_str()); }
	/// Set time resolution (s/ms, defaults to ns)
	/// See also VL_TIME_PRECISION, and VL_TIME_MULTIPLIER in verilated.h
	void set_time_resolution (const char* unit) { /* TODO */ }
	void set_time_resolution (const std::string& unit) { set_time_resolution(unit.c_str()); }

	/// Internal class access
	inline VerilatedLxt2* spTrace () { return &m_sptrace; };
};

#endif // guard
