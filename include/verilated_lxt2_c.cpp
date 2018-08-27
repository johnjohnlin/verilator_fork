// -*- mode: C++; c-file-style: "cc-mode" -*-
//=============================================================================
//
// THIS MODULE IS PUBLICLY LICENSED
//
// Copyright 2001-2018 by Wilson Snyder
// Copyright 2018 by Yu-Sheng Lin johnjohnlys@media.ee.ntu.edu.tw
// This program is free software;
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

#include "verilatedos.h"
#include "verilated.h"
#include "verilated_lxt2_c.h"
// Include the GTKWave implementation directly
#include "lxt2/lxt2_write.cpp"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <ctime>
#include <algorithm>
#include <sstream>

#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__CYGWIN__)
# include <io.h>
#else
# include <unistd.h>
#endif

// SPDIFF_ON

#ifndef O_LARGEFILE // For example on WIN32
# define O_LARGEFILE 0
#endif
#ifndef O_NONBLOCK
# define O_NONBLOCK 0
#endif

class VerilatedLxt2CallInfo {
protected:
    friend class VerilatedLxt2;
    VerilatedLxt2Callback_t	m_initcb;	///< Initialization Callback function
    VerilatedLxt2Callback_t	m_fullcb;	///< Full Dumping Callback function
    VerilatedLxt2Callback_t	m_changecb;	///< Incremental Dumping Callback function
    void*		m_userthis;	///< Fake "this" for caller
    vluint32_t		m_code;		///< Starting code number
    // CONSTRUCTORS
    VerilatedLxt2CallInfo (VerilatedLxt2Callback_t icb, VerilatedLxt2Callback_t fcb,
            VerilatedLxt2Callback_t changecb,
            void* ut, vluint32_t code)
        : m_initcb(icb), m_fullcb(fcb), m_changecb(changecb), m_userthis(ut), m_code(code) {};
    ~VerilatedLxt2CallInfo() {}
};

using namespace std;

void VerilatedLxt2::open(const char* filename) VL_MT_UNSAFE {
    m_assertOne.check();
    m_lxt2 = lxt2_wr_init(filename);
    for (vluint32_t ent = 0; ent< m_callbacks.size(); ent++) {
        VerilatedLxt2CallInfo *cip = m_callbacks[ent];
        cip->m_code = 1;
        (cip->m_initcb) (this, cip->m_userthis, cip->m_code);
    }
}

void VerilatedLxt2::module(const std::string& name)
{
    m_module = name;
}

//=============================================================================
// Decl
void VerilatedLxt2::declBit(vluint32_t code, const char* name, int arraynum)
{
    this->declBus(code, name, arraynum, 0, 0);
}

void VerilatedLxt2::declBus(vluint32_t code, const char* name, int arraynum, int msb, int lsb)
{
    pair<Code2SymbolType::iterator, bool> p = m_code2symbol.insert(make_pair(code, (lxt2_wr_symbol*)(NULL)));
    stringstream name_ss;
    name_ss << m_module << "." << name;
    if (arraynum >= 0) {
        name_ss << "(" << arraynum << ")";
    }
    string name_s = name_ss.str();
    for (string::iterator it = name_s.begin(); it != name_s.end(); ++it) {
        if (isScopeEscape(*it)) {
            *it = '.';
        }
    }
    if (p.second) {
        // new
        p.first->second = lxt2_wr_symbol_add(m_lxt2, name_s.c_str(), 0, msb, lsb, LXT2_WR_SYM_F_BITS);
        assert(p.first->second);
    } else {
        // alias
        lxt2_wr_symbol_alias(m_lxt2, p.first->second->name, name_s.c_str(), msb, lsb);
    }
}

//=============================================================================
// Callbacks

void VerilatedLxt2::addCallback VL_MT_UNSAFE_ONE (
        VerilatedLxt2Callback_t initcb, VerilatedLxt2Callback_t fullcb, VerilatedLxt2Callback_t changecb,
        void* userthis)
{
    m_assertOne.check();
    if (VL_UNLIKELY(isOpen())) {
        std::string msg = std::string("Internal: ")+__FILE__+"::"+__FUNCTION__+" called with already open file";
        VL_FATAL_MT(__FILE__,__LINE__,"",msg.c_str());
    }
    VerilatedLxt2CallInfo* vci = new VerilatedLxt2CallInfo(initcb, fullcb, changecb, userthis, 1);
    m_callbacks.push_back(vci);
}

//=============================================================================
// Dumping

void VerilatedLxt2::dump (vluint64_t timeui) {
    if (!isOpen()) return;
    lxt2_wr_set_time64(m_lxt2, timeui);
    for (vluint32_t ent = 0; ent< m_callbacks.size(); ent++) {
        VerilatedLxt2CallInfo *cip = m_callbacks[ent];
        (cip->m_changecb)(this, cip->m_userthis, cip->m_code);
    }
}

//********************************************************************
// Local Variables:
// End:
