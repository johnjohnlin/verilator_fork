// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Add temporaries, such as for unroll nodes
//
// Code available from: http://www.veripool.org/verilator
//
//*************************************************************************
//
// Copyright 2003-2019 by Wilson Snyder.  This program is free software; you can
// redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
//
// Verilator is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//*************************************************************************
// V3Unroll's Transformations:
//	Note is called twice.  Once on modules for GenFor unrolling,
//	Again after V3Scope for normal for loop unrolling.
//
// Each module:
//	Look for "FOR" loops and unroll them if <= 32 loops.
//	(Eventually, a better way would be to simulate the entire loop; ala V3Table.)
//	Convert remaining FORs to WHILEs
//
//*************************************************************************

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V3Unroll.h"
#include "V3Stats.h"
#include "V3Const.h"
#include "V3Ast.h"
#include "V3Simulate.h"

#include <algorithm>
#include <cstdarg>
#include <vector>
#include <utility>
using std::vector;
using std::pair;

//######################################################################
// Unroll state, as a visitor of each AstNode

class UnrollVisitor : public AstNVisitor {
private:
    // STATE
    struct VarState {
	// Correspond to all modified variables
	// for example, in for (int i = 0; ...; i++, k = i*2), this stores i and k
	AstVar*		m_forVarp;	// Iterator variable
	AstVarScope*	m_forVscp;	// Iterator variable scope (NULL for generate pass)
	V3Number*	m_varValuep;	// Current value of loop (when m_varModeReplace)
	AstConst*	m_varValuecp;	// Same as above, but different data type
    };
    typedef vector<VarState>::iterator VarStateIt;
    vector<VarState>	m_forVarps;		// Iterator variable
    vector<AstNode*>	m_ignoreIncps;		// Increment node to ignore (used when m_varModeCheck)
    bool		m_varModeCheck;		// Just checking RHS assignments
    bool		m_varModeReplace;	// Replacing varrefs
    bool		m_varAssignHit;		// Assign var hit (used when m_varModeCheck)
    bool		m_generate;		// Expand single generate For loop
    string		m_beginName;		// What name to give begin iterations
    V3Double0		m_statLoops;		// Statistic tracking
    V3Double0		m_statIters;		// Statistic tracking

    // METHODS
    VL_DEBUG_FUNC;  // Declare debug()

    VarStateIt findVariable(AstVarRef *ref) {
	VarStateIt it;
	for (it = m_forVarps.begin(); it != m_forVarps.end(); ++it) {
	    if (ref->varp() == it->m_forVarp && ref->varScopep() == it->m_forVscp) {
		break;
	    }
	}
	return it;
    }

    // VISITORS
    bool cantUnroll(AstNode* nodep, const char* reason) {
	if (m_generate) {
	    nodep->v3error("Unsupported: Can't unroll generate for; "<<reason);
	}
	UINFO(3,"   Can't Unroll: "<<reason<<" :"<<nodep<<endl);
	//if (debug()>=9) nodep->dumpTree(cout,"-cant-");
	V3Stats::addStatSum(string("Unrolling gave up, ")+reason, 1);
	return false;
    }

    int unrollCount() {
	return m_generate ? v3Global.opt.unrollCount()*16
	    : v3Global.opt.unrollCount();
    }

    bool bodySizeOverRecurse(AstNode* nodep, int& bodySize, int bodyLimit) {
	if (!nodep) return false;
	bodySize++;
	// Exit once exceeds limits, rather than always total
	// so don't go O(n^2) when can't unroll
	if (bodySize > bodyLimit) return true;
	if (bodySizeOverRecurse(nodep->op1p(), bodySize, bodyLimit)) return true;
	if (bodySizeOverRecurse(nodep->op2p(), bodySize, bodyLimit)) return true;
	if (bodySizeOverRecurse(nodep->op3p(), bodySize, bodyLimit)) return true;
	if (bodySizeOverRecurse(nodep->op4p(), bodySize, bodyLimit)) return true;
	// Tail recurse.
	return bodySizeOverRecurse(nodep->nextp(), bodySize, bodyLimit);
    }

    bool forUnrollCheck(AstNode* nodep,
			AstNode* initp,	int ninitp, // Maybe under nodep (no nextp), or standalone (ignore nextp)
			AstNode* precondsp, AstNode* condp,
			AstNode* incp,		// Maybe under nodep or in bodysp
			AstNode* bodysp) {
	// To keep the IF levels low, we return as each test fails.
	UINFO(4, " FOR Check "<<nodep<<endl);
	if (initp)	UINFO(6, "    Init "<<initp<<endl);
	if (precondsp)	UINFO(6, "    Pcon "<<precondsp<<endl);
	if (condp)	UINFO(6, "    Cond "<<condp<<endl);
	if (incp)	UINFO(6, "    Inc  "<<incp<<endl);

	// Initial value check
	AstNode *initp_tmp = initp;
	vector<AstAssign*> initAssps;
	for (int i = 0; i < ninitp; ++i) {
	    // Is initp an assign?
	    AstAssign *initAssp = VN_CAST(initp_tmp, Assign);
	    if (!initAssp) return cantUnroll(nodep, "Invalid initial assignment");
	    if (!VN_IS(initAssp->lhsp(), VarRef)) return cantUnroll(nodep, "Not an initial assignment to simple variable");
	    initp_tmp = initp_tmp->nextp();
	    // Add these assignments for later use
	    VarState push_me_v;
	    push_me_v.m_forVarp = VN_CAST(initAssp->lhsp(), VarRef)->varp();
	    push_me_v.m_forVscp = VN_CAST(initAssp->lhsp(), VarRef)->varScopep();
	    push_me_v.m_varValuep = new V3Number(initAssp->fileline());
	    push_me_v.m_varValuecp = NULL;
	    m_forVarps.push_back(push_me_v);
	    initAssps.push_back(initAssp);
	}
	// Condition check
	if (condp->nextp()) nodep->v3fatalSrc("conditional shouldn't be a list");

	// Assignment of next value check
	for (AstNode *incp_cur = incp; incp_cur != NULL; incp_cur = incp_cur->nextp()) {
	    AstAssign* incAssp = VN_CAST(incp_cur, Assign);
	    if (!incAssp) return cantUnroll(nodep, "no increment assignment");
	    // Mark this assignment should not be checked for checking later
	    m_ignoreIncps.push_back(incp_cur);
	    // Add these assignments for later use
	    AstVarRef *var_ref = VN_CAST(incAssp->lhsp(), VarRef);
	    VarStateIt it;
	    if ((it = findVariable(var_ref)) == m_forVarps.end()) {
		// Assignment to existing variable, create a new one
		VarState push_me_v;
		push_me_v.m_forVarp = var_ref->varp();
		push_me_v.m_forVscp = var_ref->varScopep();
		push_me_v.m_varValuep = new V3Number(incAssp->fileline());
		push_me_v.m_varValuecp = NULL;
		m_forVarps.push_back(push_me_v);
	    }
	}
	// This check shouldn't be needed when using V3Simulate
	// however, for repeat loops, the loop variable is auto-generated
	// and the initp statements will reference a variable outside of the initp scope
	// alas, failing to simulate.
	// FIXME: since now we can have something like for (int i = 0, j = i+1...),
	//        we only check the first assignment
	for (int i = 0; i < int(bool(initAssps.size())); ++i) {
	    // Is rhs of initp a constant?
	    AstConst* constInitp = VN_CAST(initAssps[i]->rhsp(), Const);
	    if (!constInitp) return cantUnroll(nodep, "non-constant initializer");
	}

	// Generate should have exactly one var, so we have many [0] here
	if (VN_IS(nodep, GenFor) && !m_forVarps[0].m_forVarp->isGenVar()) {
	    nodep->v3error("Non-genvar used in generate for: "<<m_forVarps[0].m_forVarp->prettyName()<<endl);
	}
	if (m_generate) V3Const::constifyParamsEdit(initAssps[0]->rhsp());  // rhsp may change

	// Now, make sure there's no assignment to this variable in the loop
	m_varModeCheck = true;
	m_varAssignHit = false;
	iterateAndNextNull(precondsp);
	iterateAndNextNull(bodysp);
	iterateAndNextNull(incp);
	m_varModeCheck = false;
	m_ignoreIncps.resize(0);
	if (m_varAssignHit) return cantUnroll(nodep, "genvar assigned *inside* loop");

	// This is commented when for-initialization-list support is added since I don't know what is this.
	// if (m_forVscp) { UINFO(8, "   Loop Variable: "<<m_forVscp<<endl); }
	// else	       { UINFO(8, "   Loop Variable: "<<m_forVarp<<endl); }
	if (debug()>=9) nodep->dumpTree(cout,"-   for: ");


	if (!m_generate) {
	    for (AstNode *incp_tmp = incp; incp_tmp != NULL; incp_tmp = incp_tmp->nextp()) {
		AstAssign* incAssp = VN_CAST(incp_tmp, Assign);
		if (!canSimulate(incAssp->rhsp())) return cantUnroll(incp, "Unable to simulate increment");
	    }
	    if (!canSimulate(condp)) return cantUnroll(condp, "Unable to simulate condition");

	    // Check whether to we actually want to try and unroll.
	    int loops;
	    if (!countLoops(initAssps, condp, incp, unrollCount(), loops))
		return cantUnroll(nodep, "Unable to simulate loop");

	    // Less than 10 statements in the body?
	    int bodySize = 0;
	    int bodyLimit = v3Global.opt.unrollStmts();
	    if (loops>0) bodyLimit = v3Global.opt.unrollStmts() / loops;
	    if (bodySizeOverRecurse(precondsp, bodySize/*ref*/, bodyLimit)
		|| bodySizeOverRecurse(bodysp, bodySize/*ref*/, bodyLimit)
		|| bodySizeOverRecurse(incp, bodySize/*ref*/, bodyLimit)) {
		return cantUnroll(nodep, "too many statements");
	    }
	}
	// Finally, we can do it
	if (!forUnroller(nodep, initAssps, condp, precondsp, incp, bodysp)) {
	    return cantUnroll(nodep, "Unable to unroll loop");
	}
	VL_DANGLING(nodep);
	// Cleanup
	for (VarStateIt it = m_forVarps.begin(); it != m_forVarps.end(); ++it) {
	    delete it->m_varValuep;
	    if (it->m_varValuecp) {
        	pushDeletep(it->m_varValuecp); VL_DANGLING(it->m_varValuecp);
	    }
	}
	m_forVarps.resize(0);
	return true;
    }

    bool canSimulate(AstNode *nodep) {
        SimulateVisitor simvis;
        AstNode* clonep = nodep->cloneTree(true);
        simvis.mainCheckTree(clonep);
        pushDeletep(clonep); clonep = NULL;
        return simvis.optimizable();
    }

    bool simulateTree(AstNode *nodep, AstNode *dtypep, V3Number &outNum) {
	// compute the actual value of RHS
	AstNode* clone = nodep->cloneTree(true);
	if (!clone) {
	    nodep->v3fatalSrc("Failed to clone tree");
	    return false;
	}
	if (!m_forVarps.empty()) {
	    // Iteration requires a back, so put under temporary node
	    AstBegin* tempp = new AstBegin (nodep->fileline(), "[EditWrapper]", clone);
	    m_varModeReplace = true;
            iterateAndNextNull(tempp->stmtsp());
	    m_varModeReplace = false;
	    clone = tempp->stmtsp()->unlinkFrBackWithNext();
	    tempp->deleteTree();
	    tempp = NULL;
	}
	SimulateVisitor simvis;
	simvis.mainParamEmulate(clone);
	if (!simvis.optimizable()) {
	    UINFO(3, "Unable to simulate" << endl);
	    if (debug()>=9) nodep->dumpTree(cout,"- _simtree: ");
	    return false;
	}
	// Fetch the result
	V3Number* res = simvis.fetchNumberNull(clone);
	if (!res) {
	    UINFO(3, "No number returned from simulation" << endl);
	    return false;
	}
	// Patch up datatype
	if (dtypep) {
	    AstConst new_con (clone->fileline(), *res);
	    new_con.dtypeFrom(dtypep);
	    outNum = new_con.num();
	    return true;
	}
	outNum = *res;
	return true;
    }

    bool countLoops(vector<AstAssign*> initps, AstNode *condp, AstNode *incps, int max, int &outLoopsr) {
	outLoopsr = 0;
	FileLine *const FL = initps.empty() ? NULL : initps[0]->fileline();
	for (vector<AstAssign*>::iterator it = initps.begin(); it != initps.end(); ++it) {
	    m_varModeReplace = true;
	    iterateAndNextNull((*it)->rhsp());
	    m_varModeReplace = false;
	    AstVarRef *var_ref = VN_CAST((*it)->lhsp(), VarRef);
	    V3Number newLoopValue = V3Number(FL);
	    VarStateIt var_it = findVariable(var_ref); // This should not be .end()
	    if (!simulateTree((*it)->rhsp(), *it, newLoopValue)) {
		return false;
	    }
	    *(var_it->m_varValuep) = newLoopValue;
	    var_it->m_varValuecp = new AstConst(FL, *(var_it->m_varValuep));
	}
	while (1) {
	    V3Number res = V3Number(FL);
	    if (!simulateTree(condp, NULL, res)) {
		return false;
	    }
	    if (!res.isEqOne()) {
		break;
	    }

	    outLoopsr++;

	    // Evaluate all increment steps here
	    for (AstNode *cur_incp = incps; cur_incp != NULL; cur_incp = cur_incp->nextp()) {
		AstAssign *incpass = VN_CAST(cur_incp, Assign);
		V3Number newLoopValue = V3Number(FL);
		if (!simulateTree(incpass->rhsp(), incpass, newLoopValue)) {
		    return false;
		}
		VarStateIt it =  findVariable(VN_CAST(incpass->lhsp(), VarRef));
		if (it->m_varValuecp) {
		    pushDeletep(it->m_varValuecp);
		}
		it->m_varValuep->opAssign(newLoopValue);
		it->m_varValuecp = new AstConst(FL, *(it->m_varValuep));
	    }
	    if (outLoopsr > max) {
		return false;
	    }
	}
	return true;
    }

    bool forUnroller(AstNode* nodep,
		     vector<AstAssign*> &initps,
		     AstNode* condp,
		     AstNode* precondsp,
		     AstNode* incsp, AstNode* bodysp) {
	UINFO(9, "forUnroller "<<nodep<<endl);
	FileLine *FL = initps.empty() ? NULL : initps[0]->fileline();
	for (vector<AstAssign*>::iterator it = initps.begin(); it != initps.end(); ++it) {
	    m_varModeReplace = true;
	    iterateAndNextNull((*it)->rhsp());
	    m_varModeReplace = false;
	    AstVarRef *var_ref = VN_CAST((*it)->lhsp(), VarRef);
	    VarStateIt var_it = findVariable(var_ref); // This should not be .end()
	    V3Number newLoopValue = V3Number(nodep->fileline());
	    if (!simulateTree((*it)->rhsp(), *it, newLoopValue)) {
		return false;
	    }
	    if (var_it->m_varValuecp) {
		pushDeletep(var_it->m_varValuecp);
	    }
	    *(var_it->m_varValuep) = newLoopValue;
	    var_it->m_varValuecp = new AstConst(nodep->fileline(), *(var_it->m_varValuep));
	}
	AstNode* stmtsp = NULL;
	for (vector<AstAssign*>::iterator it = initps.begin(); it != initps.end(); ++it) {
	    (*it)->unlinkFrBack();
	    // Don't add to list, we do it once, and setting loop index isn't needed as we're constant propagating it
	}
	if (precondsp) {
	    precondsp->unlinkFrBackWithNext();
	    stmtsp = AstNode::addNextNull(stmtsp, precondsp);
	}
	if (bodysp) {
	    bodysp->unlinkFrBackWithNext();
	    stmtsp = AstNode::addNextNull(stmtsp, bodysp);  // Maybe null if no body
	}
        if (incsp && !VN_IS(nodep, GenFor)) {  // Generates don't need to increment loop index
	    incsp->unlinkFrBackWithNext();
	}
	// Mark variable to disable some later warnings
	for (VarStateIt it = m_forVarps.begin(); it != m_forVarps.end(); ++it) {
	    it->m_forVarp->usedLoopIdx(true);
	}

	AstNode* newbodysp = NULL;
	++m_statLoops;
	if (stmtsp) {
	    int times = 0;
	    while (1) {
		// UINFO(8,"      Looping "<<loopValue<<endl);
		V3Number res = V3Number(nodep->fileline());
		if (!simulateTree(condp, NULL, res)) {
		    nodep->v3error("Loop unrolling failed.");
		    return false;
		}
		if (!res.isEqOne()) {
		    break;  // Done with the loop
		}
		else {
		    // Replace iterator values with constant.
		    AstNode* oneloopp = stmtsp->cloneTree(true);
		    AstNode* oneloop_incsp = bool(incsp) ? incsp->cloneTree(true) : NULL;

		    // Iteration requires a back, so put under temporary node
		    if (oneloopp) {
			AstBegin* tempp = new AstBegin(oneloopp->fileline(),"[EditWrapper]",oneloopp);
			m_varModeReplace = true;
                        iterateAndNextNull(tempp->stmtsp());
			m_varModeReplace = false;
			oneloopp = tempp->stmtsp()->unlinkFrBackWithNext(); tempp->deleteTree(); VL_DANGLING(tempp);
		    }
		    if (oneloop_incsp) {
			// Evaluate all increment steps here
			for (AstNode *cur_incp = oneloop_incsp; cur_incp != NULL; cur_incp = cur_incp->nextp()) {
			    AstAssign *incpass = VN_CAST(cur_incp, Assign);
			    m_varModeReplace = true;
			    iterateChildren(incpass->rhsp());
			    m_varModeReplace = false;
			    V3Number newLoopValue = V3Number(nodep->fileline());
			    if (!simulateTree(incpass->rhsp(), incpass, newLoopValue)) {
				nodep->v3error("Loop unrolling failed");
				return false;
			    }
			    VarStateIt it = findVariable(VN_CAST(incpass->lhsp(), VarRef));
			    if (it->m_varValuecp) {
				pushDeletep(it->m_varValuecp);
			    }
			    it->m_varValuep->opAssign(newLoopValue);
			    it->m_varValuecp = new AstConst(nodep->fileline(), *(it->m_varValuep));
			}
			oneloopp = AstNode::addNextNull(oneloopp, oneloop_incsp);

		    }
		    if (m_generate) {
			string index = AstNode::encodeNumber(m_forVarps[0].m_varValuep->toSInt());
			string nname = m_beginName + "__BRA__" + index + "__KET__";
			oneloopp = new AstBegin(oneloopp->fileline(),nname,oneloopp,true);
		    }
		    // pushDeletep(m_varValuep); m_varValuep=NULL;
		    if (newbodysp) newbodysp->addNext(oneloopp);
		    else newbodysp = oneloopp;

		    ++m_statIters;
		    if (++times > unrollCount()*3) {
                        nodep->v3error("Loop unrolling took too long;"
                                       " probably this is an infinite loop, or set --unroll-count above "
                                       <<unrollCount());
			break;
		    }
		}
	    }
	}
	// Replace the FOR()
	if (newbodysp) nodep->replaceWith(newbodysp);
	else nodep->unlinkFrBack();
	if (bodysp) { pushDeletep(bodysp); VL_DANGLING(bodysp); }
	if (precondsp) { pushDeletep(precondsp); VL_DANGLING(precondsp); }
	for (vector<AstAssign*>::iterator it = initps.begin(); it != initps.end(); ++it) {
	    pushDeletep(*it); VL_DANGLING(*it);
	}
	if (incsp && !incsp->backp()) { pushDeletep(incsp); VL_DANGLING(incsp); }
	if (debug()>=9 && newbodysp) newbodysp->dumpTree(cout,"-  _new: ");
	return true;
    }

    virtual void visit(AstWhile* nodep) {
        iterateChildren(nodep);
	if (m_varModeCheck || m_varModeReplace) {
	} else {
	    // Constify before unroll call, as it may change what is underneath.
	    if (nodep->precondsp()) V3Const::constifyEdit(nodep->precondsp());  // precondsp may change
	    if (nodep->condp()) V3Const::constifyEdit(nodep->condp()); // condp may change

	    // === Grab initial value ===
	    // initp should be statements before the while. It is the first child if exists.
	    int ninitp = 0;
	    // traces backward to parent and first child
	    AstNode *initp = nodep, *parentp =  nodep->backp();
	    while (parentp->nextp() == initp) {
		// this condition means that we are still in a list
		initp = parentp;
		parentp = parentp->backp();
	    }
	    // traces forward to AstWhile
	    for (AstNode *cur = initp; cur != nodep;) {
		AstNode *nxt = cur->nextp();
		// TODO: Can we just itp = V3Count::... here?
		V3Const::constifyEdit(cur); VL_DANGLING(cur);
		cur = nxt;
		++ninitp;
	    }
	    // Finally, the initp is the first child in always body (TOOD: is this always correct?)
	    initp = parentp->op2p();

	    // === Grab assignments ===
	    AstNode* incp = NULL;
	    for (AstNode *cur = nodep->incsp(); cur != NULL;) {
		AstNode *nxt = cur->nextp();
		V3Const::constifyEdit(cur); VL_DANGLING(cur);
		cur = nxt;
	    }
	    if (nodep->incsp()) incp = nodep->incsp();
	    else {
		for (incp = nodep->bodysp(); incp && incp->nextp(); incp = incp->nextp()) {}
		if (incp) { V3Const::constifyEdit(incp); VL_DANGLING(incp); }
		for (incp = nodep->bodysp(); incp && incp->nextp(); incp = incp->nextp()) {}  // Again, as may have changed
	    }

	    // And check it
	    if (forUnrollCheck(nodep, initp, ninitp,
			       nodep->precondsp(), nodep->condp(),
			       incp, nodep->bodysp())) {
		pushDeletep(nodep); VL_DANGLING(nodep); // Did replacement
	    }
	}
    }
    virtual void visit(AstGenFor* nodep) {
	if (!m_generate || m_varModeReplace) {
            iterateChildren(nodep);
	}  // else V3Param will recursively call each for loop to be unrolled for us
	if (m_varModeCheck || m_varModeReplace) {
	} else {
	    // Constify before unroll call, as it may change what is underneath.
	    if (nodep->initsp()) V3Const::constifyEdit(nodep->initsp());  // initsp may change
	    if (nodep->condp()) V3Const::constifyEdit(nodep->condp());  // condp may change
	    if (nodep->incsp()) V3Const::constifyEdit(nodep->incsp());  // incsp may change
	    if (nodep->condp()->isZero()) {
		// We don't need to do any loops.  Remove the GenFor,
		// Genvar's don't care about any initial assignments.
		//
		// Note normal For's can't do exactly this deletion, as
		// we'd need to initialize the variable to the initial
		// condition, but they'll become while's which can be
		// deleted by V3Const.
                pushDeletep(nodep->unlinkFrBack()); VL_DANGLING(nodep);
            } else if (forUnrollCheck(nodep, nodep->initsp(), 1,
                                      NULL, nodep->condp(),
                                      nodep->incsp(), nodep->bodysp())) {
                pushDeletep(nodep); VL_DANGLING(nodep);  // Did replacement
	    } else {
		nodep->v3error("For loop doesn't have genvar index, or is malformed");
	    }
	}
    }
    virtual void visit(AstNodeFor* nodep) {
	if (m_generate) {  // Ignore for's when expanding genfor's
            iterateChildren(nodep);
	} else {
	    nodep->v3error("V3Begin should have removed standard FORs");
	}
    }

    virtual void visit(AstVarRef* nodep) {
	// In this mode, we mark a flag whenever a lvalue is a loop variable (this shouldn't happen)
	if (m_varModeCheck && nodep->lvalue() && (findVariable(nodep) != m_forVarps.end())) {
	    UINFO(8,"   Itervar assigned to: "<<nodep<<endl);
	    m_varAssignHit = true;
	}
	// The actual unrolling
	// In this mode, we replace all rvalue loop variable by a constant
	VarStateIt it;
	if (m_varModeReplace && !nodep->lvalue() &&
	    ((it = findVariable(nodep)) != m_forVarps.end()) && bool(it->m_varValuecp)) {
	    AstNode* newconstp = it->m_varValuecp->cloneTree(false);
	    nodep->replaceWith(newconstp);
	    pushDeletep(nodep);
	}
    }

    //--------------------
    // Default: Just iterate
    virtual void visit(AstNode* nodep) {
	if (m_varModeCheck && std::find(m_ignoreIncps.begin(), m_ignoreIncps.end(), nodep) != m_ignoreIncps.end()) {
	    // Ignore subtree that is the increment
	} else {
            iterateChildren(nodep);
	}
    }

public:
    // CONSTUCTORS
    UnrollVisitor() { init(false, ""); }
    virtual ~UnrollVisitor() {
        V3Stats::addStatSum("Optimizations, Unrolled Loops", m_statLoops);
        V3Stats::addStatSum("Optimizations, Unrolled Iterations", m_statIters);
    }
    // METHORS
    void init(bool generate, const string& beginName) {
	m_varModeCheck = false;
	m_varModeReplace = false;
        m_varAssignHit = false;
	m_generate = generate;
	m_beginName = beginName;
    }
    void process(AstNode* nodep, bool generate, const string& beginName) {
        init(generate, beginName);
        iterate(nodep);
    }
};

//######################################################################
// Unroll class functions

UnrollStateful::UnrollStateful() : m_unrollerp(new UnrollVisitor) { }
UnrollStateful::~UnrollStateful() { delete m_unrollerp; }

void UnrollStateful::unrollGen(AstNodeFor* nodep, const string& beginName) {
    UINFO(5,__FUNCTION__<<": "<<endl);
    m_unrollerp->process(nodep, true, beginName);
}

void UnrollStateful::unrollAll(AstNetlist* nodep) {
    m_unrollerp->process(nodep, false, "");
}

void V3Unroll::unrollAll(AstNetlist* nodep) {
    UINFO(2,__FUNCTION__<<": "<<endl);
    {
        UnrollStateful unroller;
        unroller.unrollAll(nodep);
    }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("unroll", 0, v3Global.opt.dumpTreeLevel(__FILE__) >= 3);
}
