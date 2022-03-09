/*
    Original source: https://github.com/khalladay/hooking-by-example

    MIT License

    Copyright (c) 2020 Kyle Halladay

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "pch.h"
#include <cstdlib>
#include <x86.h>
#include <capstone.h>
#include <vector>
#include <Windows.h>
#include <Psapi.h>

void* AllocatePageNearAddress(void* targetAddr)
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    const uint64_t PAGE_SIZE = sysInfo.dwPageSize;

    uint64_t startAddr = (uint64_t(targetAddr) & ~(PAGE_SIZE - 1)); //round down to nearest page boundary
    uint64_t minAddr = min(startAddr - 0x7FFFFF00, (uint64_t)sysInfo.lpMinimumApplicationAddress);
    uint64_t maxAddr = max(startAddr + 0x7FFFFF00, (uint64_t)sysInfo.lpMaximumApplicationAddress);

    uint64_t startPage = (startAddr - (startAddr % PAGE_SIZE));

    uint64_t pageOffset = 1;
    while (1)
    {
        uint64_t byteOffset = pageOffset * PAGE_SIZE;
        uint64_t highAddr = startPage + byteOffset;
        uint64_t lowAddr = (startPage > byteOffset) ? startPage - byteOffset : 0;

        bool needsExit = highAddr > maxAddr && lowAddr < minAddr;

        if (highAddr < maxAddr)
        {
            void* outAddr = VirtualAlloc((void*)highAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (outAddr)
                return outAddr;
        }

        if (lowAddr > minAddr)
        {
            void* outAddr = VirtualAlloc((void*)lowAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (outAddr != nullptr)
                return outAddr;
        }

        pageOffset++;

        if (needsExit)
        {
            break;
        }
    }

    return nullptr;
}

void WriteAbsoluteJump64(void* absJumpMemory, void* addrToJumpTo)
{
    uint8_t absJumpInstructions[] = { 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x41, 0xFF, 0xE2 };

    uint64_t addrToJumpTo64 = (uint64_t)addrToJumpTo;
    memcpy(&absJumpInstructions[2], &addrToJumpTo64, sizeof(addrToJumpTo64));
    memcpy(absJumpMemory, absJumpInstructions, sizeof(absJumpInstructions));
}

typedef struct _X64Instructions
{
    cs_insn* instructions;
    uint32_t numInstructions;
    uint32_t numBytes;
} X64Instructions;

X64Instructions StealBytes(void* function)
{
    // Disassemble stolen bytes
    csh handle;
    cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON); // we need details enabled for relocating RIP relative instrs

    size_t count;
    cs_insn* disassembledInstructions; //allocated by cs_disasm, needs to be manually freed later
    count = cs_disasm(handle, (uint8_t*)function, 20, (uint64_t)function, 20, &disassembledInstructions);

    //get the instructions covered by the first 5 bytes of the original function
    uint32_t byteCount = 0;
    uint32_t stolenInstrCount = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        cs_insn& inst = disassembledInstructions[i];
        byteCount += inst.size;
        stolenInstrCount++;
        if (byteCount >= 5) break;
    }

    //replace instructions in target func with NOPs
    memset(function, 0x90, byteCount);

    cs_close(&handle);
    return { disassembledInstructions, stolenInstrCount, byteCount };
}

bool IsRelativeJump(cs_insn& inst)
{
    bool isAnyJumpInstruction = inst.id >= X86_INS_JAE && inst.id <= X86_INS_JS;
    bool isJmp = inst.id == X86_INS_JMP;
    bool startsWithEBorE9 = inst.bytes[0] == 0xEB || inst.bytes[0] == 0xE9;
    return isJmp ? startsWithEBorE9 : isAnyJumpInstruction;
}

bool IsRelativeCall(cs_insn& inst)
{
    bool isCall = inst.id == X86_INS_CALL;
    bool startsWithE8 = inst.bytes[0] == 0xE8;
    return isCall && startsWithE8;
}

void RewriteJumpInstruction(cs_insn* instr, uint8_t* instrPtr, uint8_t* absTableEntry)
{
    uint8_t distToJumpTable = uint8_t(absTableEntry - (instrPtr + instr->size));

    //jmp instructions can have a 1 or 2 byte opcode, and need a 1-4 byte operand
    //rewrite the operand for the jump to go to the jump table
    uint8_t instrByteSize = instr->bytes[0] == 0x0F ? 2 : 1;
    uint8_t operandSize = instr->size - instrByteSize;

    switch (operandSize)
    {
    case 1: instr->bytes[instrByteSize] = distToJumpTable; break;
    case 2: {uint16_t dist16 = distToJumpTable; memcpy(&instr->bytes[instrByteSize], &dist16, 2); } break;
    case 4: {uint32_t dist32 = distToJumpTable; memcpy(&instr->bytes[instrByteSize], &dist32, 4); } break;
    }
}


void RewriteCallInstruction(cs_insn* instr, uint8_t* instrPtr, uint8_t* absTableEntry)
{
    uint8_t distToJumpTable = uint8_t(absTableEntry - (instrPtr + instr->size));

    //calls need to be rewritten as relative jumps to the abs table
    //but we want to preserve the length of the instruction, so pad with NOPs
    uint8_t jmpBytes[2] = { 0xEB, distToJumpTable };
    memset(instr->bytes, 0x90, instr->size);
    memcpy(instr->bytes, jmpBytes, sizeof(jmpBytes));
}

uint32_t AddJmpToAbsTable(cs_insn& jmp, uint8_t* absTableMem)
{
    char* targetAddrStr = jmp.op_str; //where the instruction intended to go
    uint64_t targetAddr = _strtoui64(targetAddrStr, NULL, 0);
    WriteAbsoluteJump64(absTableMem, (void*)targetAddr);
    return 13; //size of mov/jmp instrs for absolute jump
}

uint32_t AddCallToAbsTable(cs_insn& call, uint8_t* absTableMem, uint8_t* jumpBackToHookedFunc)
{
    char* targetAddrStr = call.op_str; //where the instruction intended to go
    uint64_t targetAddr = _strtoui64(targetAddrStr, NULL, 0);

    uint8_t* dstMem = absTableMem;

    uint8_t callAsmBytes[] =
    {
      0x49, 0xBA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, //movabs 64 bit value into r10
      0x41, 0xFF, 0xD2, //call r10
    };
    memcpy(&callAsmBytes[2], &targetAddr, sizeof(void*));
    memcpy(dstMem, &callAsmBytes, sizeof(callAsmBytes));
    dstMem += sizeof(callAsmBytes);

    //after the call, we need to add a second 2 byte jump, which will jump back to the 
      //final jump of the stolen bytes
    uint8_t jmpBytes[2] = { 0xEB, uint8_t(jumpBackToHookedFunc - (absTableMem + sizeof(jmpBytes))) };
    memcpy(dstMem, jmpBytes, sizeof(jmpBytes));

    return sizeof(callAsmBytes) + sizeof(jmpBytes); //15
}

bool IsRIPRelativeInstr(cs_insn& inst)
{
    cs_x86* x86 = &(inst.detail->x86);

    for (uint32_t i = 0; i < inst.detail->x86.op_count; i++)
    {
        cs_x86_op* op = &(x86->operands[i]);

        //mem type is rip relative, like lea rcx,[rip+0xbeef]
        if (op->type == X86_OP_MEM)
        {
            //if we're relative to rip
            return op->mem.base == X86_REG_RIP;
        }
    }

    return false;
}

template<class T>
T GetDisplacement(cs_insn* inst, uint8_t offset)
{
    T disp;
    memcpy(&disp, &inst->bytes[offset], sizeof(T));
    return disp;
}

//rewrite instruction bytes so that any RIP-relative displacement operands
//make sense with wherever we're relocating to
void RelocateInstruction(cs_insn* inst, void* dstLocation)
{
    cs_x86* x86 = &(inst->detail->x86);
    uint8_t offset = x86->encoding.disp_offset;

    uint64_t displacement = inst->bytes[x86->encoding.disp_offset];
    switch (x86->encoding.disp_size)
    {
    case 1:
    {
        int8_t disp = GetDisplacement<uint8_t>(inst, offset);
        disp -= int8_t(uint64_t(dstLocation) - inst->address);
        memcpy(&inst->bytes[offset], &disp, 1);
    }break;

    case 2:
    {
        int16_t disp = GetDisplacement<uint16_t>(inst, offset);
        disp -= int16_t(uint64_t(dstLocation) - inst->address);
        memcpy(&inst->bytes[offset], &disp, 2);
    }break;

    case 4:
    {
        int32_t disp = GetDisplacement<int32_t>(inst, offset);
        disp -= int32_t(uint64_t(dstLocation) - inst->address);
        memcpy(&inst->bytes[offset], &disp, 4);
    }break;
    }
}

uint32_t BuildTrampoline(void* func2hook, void* dstMemForTrampoline)
{
    X64Instructions stolenInstrs = StealBytes(func2hook);

    uint8_t* stolenByteMem = (uint8_t*)dstMemForTrampoline;
    uint8_t* jumpBackMem = stolenByteMem + stolenInstrs.numBytes;
    uint8_t* absTableMem = jumpBackMem + 13; //13 is the size of a 64 bit mov/jmp instruction pair

    for (uint32_t i = 0; i < stolenInstrs.numInstructions; ++i)
    {
        cs_insn& inst = stolenInstrs.instructions[i];
        if (inst.id >= X86_INS_LOOP && inst.id <= X86_INS_LOOPNE)
        {
            return 0; //bail out on loop instructions, I don't have a good way of handling them 
        }

        if (IsRIPRelativeInstr(inst))
        {
            RelocateInstruction(&inst, stolenByteMem);
        }
        else if (IsRelativeJump(inst))
        {
            uint32_t aitSize = AddJmpToAbsTable(inst, absTableMem);
            RewriteJumpInstruction(&inst, stolenByteMem, absTableMem);
            absTableMem += aitSize;
        }
        else if (inst.id == X86_INS_CALL)
        {
            uint32_t aitSize = AddCallToAbsTable(inst, absTableMem, jumpBackMem);
            RewriteCallInstruction(&inst, stolenByteMem, absTableMem);
            absTableMem += aitSize;
        }
        memcpy(stolenByteMem, inst.bytes, inst.size);
        stolenByteMem += inst.size;
    }

    WriteAbsoluteJump64(jumpBackMem, (uint8_t*)func2hook + 5);
    free(stolenInstrs.instructions);

    return uint32_t(absTableMem - (uint8_t*)dstMemForTrampoline);
}


void InstallHook(void* func2hook, void* payloadFunc, void** trampolinePtr)
{
    DWORD oldProtect;
    VirtualProtect(func2hook, 1024, PAGE_EXECUTE_READWRITE, &oldProtect);

    void* hookMemory = AllocatePageNearAddress(func2hook);
    uint32_t trampolineSize = BuildTrampoline(func2hook, hookMemory);
    *trampolinePtr = hookMemory;

    //create the relay function
    void* relayFuncMemory = (char*)hookMemory + trampolineSize;
    WriteAbsoluteJump64(relayFuncMemory, payloadFunc); //write relay func instructions

    //install the hook
    uint8_t jmpInstruction[5] = { 0xE9, 0x0, 0x0, 0x0, 0x0 };
    const int32_t relAddr = (int32_t)relayFuncMemory - ((int32_t)func2hook + sizeof(jmpInstruction));
    memcpy(jmpInstruction + 1, &relAddr, 4);
    memcpy(func2hook, jmpInstruction, sizeof(jmpInstruction));
}
