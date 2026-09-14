// Project64 - A Nintendo 64 emulator
// https://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

// The RSP recompiler exists only for x86 and x64. CRSPSystem holds a CRSPRecompiler by
// value, so every architecture needs the type to be complete even when no recompiler is
// available. This stub supplies exactly the members that unguarded code refers to.
//
// Nothing here is ever reached at run time. DoRspCycles only enters the recompiler path
// under __amd64__ (cpu/RSPCpu.cpp), CRSPSystem::Reset only calls Reset() under __amd64__
// and RunRecompiler only exists under __i386__ (cpu/RspSystem.cpp), so any other target
// always runs the interpreter. The compile entry points return nullptr, which the one
// unguarded caller (Hle/HleTask.cpp) already treats as failure.
//
// This is NOT the place to start an arm64 RSP recompiler. Write a real one beside the
// x86 and x64 versions instead.

#if !defined(__i386__) && !defined(_M_IX86) && !defined(__amd64__) && !defined(_M_X64)

#include <Project64-rsp-core/Recompiler/RspCodeBlock.h>
#include <stdint.h>

class CRSPSystem;

class CRSPRecompiler
{
public:
    CRSPRecompiler(CRSPSystem & System) :
        m_System(System)
    {
    }
    ~CRSPRecompiler()
    {
    }

    void Reset()
    {
    }
    void RunCPU()
    {
    }
    void SetJumpTable(uint32_t /*End*/)
    {
    }
    void * CompileTaskEnter()
    {
        return nullptr;
    }
    void * CompileTaskLeave()
    {
        return nullptr;
    }
    void * CompileHLETask(uint32_t /*Address*/, RspCodeBlocks & /*Functions*/, const uint32_t /*DispatchAddress*/)
    {
        return nullptr;
    }

private:
    CRSPRecompiler();
    CRSPRecompiler(const CRSPRecompiler &);
    CRSPRecompiler & operator=(const CRSPRecompiler &);

    CRSPSystem & m_System;
};

#endif
