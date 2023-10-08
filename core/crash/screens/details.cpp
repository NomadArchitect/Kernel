/*
	This file is part of Fennix Kernel.

	Fennix Kernel is free software: you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation, either version 3 of
	the License, or (at your option) any later version.

	Fennix Kernel is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Fennix Kernel. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../../crashhandler.hpp"
#include "../chfcts.hpp"

#include <display.hpp>
#include <printf.h>
#include <debug.h>
#include <smp.hpp>
#include <cpu.hpp>

#if defined(a64)
#include "../../../arch/amd64/cpu/gdt.hpp"
#elif defined(a32)
#elif defined(aa64)
#endif

#include "../../../kernel.h"

namespace CrashHandler
{
    SafeFunction void DisplayDetailsScreen(CRData data)
    {
        if (data.Process)
            EHPrint("\e7981FCCurrent Process: %s(%ld)\n",
                    data.Process->Name,
                    data.Process->ID);
        if (data.Thread)
            EHPrint("\e7981FCCurrent Thread: %s(%ld)\n",
                    data.Thread->Name,
                    data.Thread->ID);
        EHPrint("\e7981FCTechnical Informations on CPU %lld:\n", data.ID);
        uintptr_t ds;
#if defined(a64)

        CPUData *cpu = (CPUData *)data.CPUData;
        if (cpu)
        {
            EHPrint("\eE46CEBCPU Data Address: %#lx\n", cpu);
            EHPrint("Core Stack: %#lx, Core ID: %ld, Error Code: %ld\n",
                    cpu->Stack, cpu->ID, cpu->ErrorCode);
            EHPrint("Is Active: %s\n", cpu->IsActive ? "true" : "false");
            EHPrint("Current Process: %#lx, Current Thread: %#lx\n",
                    cpu->CurrentProcess.load(), cpu->CurrentThread.load());
            EHPrint("Arch Specific Data: %#lx\n", cpu->Data);
            EHPrint("Checksum: 0x%X\n", cpu->Checksum);
        }

        asmv("mov %%ds, %0"
             : "=r"(ds));
#elif defined(a32)
        asmv("mov %%ds, %0"
             : "=r"(ds));
#elif defined(aa64)
#endif

#if defined(a64)
        EHPrint("\e7981FCFS=%#lx  GS=%#lx  SS=%#lx  CS=%#lx  DS=%#lx\n",
                CPU::x64::rdmsr(CPU::x64::MSR_FS_BASE), CPU::x64::rdmsr(CPU::x64::MSR_GS_BASE),
                data.Frame->ss, data.Frame->cs, ds);
        EHPrint("R8=%#lx  R9=%#lx  R10=%#lx  R11=%#lx\n", data.Frame->r8, data.Frame->r9, data.Frame->r10, data.Frame->r11);
        EHPrint("R12=%#lx  R13=%#lx  R14=%#lx  R15=%#lx\n", data.Frame->r12, data.Frame->r13, data.Frame->r14, data.Frame->r15);
        EHPrint("RAX=%#lx  RBX=%#lx  RCX=%#lx  RDX=%#lx\n", data.Frame->rax, data.Frame->rbx, data.Frame->rcx, data.Frame->rdx);
        EHPrint("RSI=%#lx  RDI=%#lx  RBP=%#lx  RSP=%#lx\n", data.Frame->rsi, data.Frame->rdi, data.Frame->rbp, data.Frame->rsp);
        EHPrint("RIP=%#lx  RFL=%#lx  INT=%#lx  ERR=%#lx  EFER=%#lx\n", data.Frame->rip, data.Frame->rflags.raw, data.Frame->InterruptNumber, data.Frame->ErrorCode, data.efer.raw);
#elif defined(a32)
        EHPrint("\e7981FCFS=%#x  GS=%#x  CS=%#x  DS=%#x\n",
                CPU::x32::rdmsr(CPU::x32::MSR_FS_BASE), CPU::x32::rdmsr(CPU::x32::MSR_GS_BASE),
                data.Frame->cs, ds);
        EHPrint("EAX=%#x  EBX=%#x  ECX=%#x  EDX=%#x\n", data.Frame->eax, data.Frame->ebx, data.Frame->ecx, data.Frame->edx);
        EHPrint("ESI=%#x  EDI=%#x  EBP=%#x  ESP=%#x\n", data.Frame->esi, data.Frame->edi, data.Frame->ebp, data.Frame->esp);
        EHPrint("EIP=%#x  EFL=%#x  INT=%#x  ERR=%#x\n", data.Frame->eip, data.Frame->eflags.raw, data.Frame->InterruptNumber, data.Frame->ErrorCode);
#elif defined(aa64)
#endif

#if defined(a86)
        EHPrint("CR0=%#lx  CR2=%#lx  CR3=%#lx  CR4=%#lx  CR8=%#lx\n", data.cr0.raw, data.cr2.raw, data.cr3.raw, data.cr4.raw, data.cr8.raw);
        EHPrint("DR0=%#lx  DR1=%#lx  DR2=%#lx  DR3=%#lx  DR6=%#lx  DR7=%#lx\n", data.dr0, data.dr1, data.dr2, data.dr3, data.dr6, data.dr7.raw);

        EHPrint("\eFC797BCR0: PE:%s     MP:%s     EM:%s     TS:%s\n     ET:%s     NE:%s     WP:%s     AM:%s\n     NW:%s     CD:%s     PG:%s\n",
                data.cr0.PE ? "True " : "False", data.cr0.MP ? "True " : "False", data.cr0.EM ? "True " : "False", data.cr0.TS ? "True " : "False",
                data.cr0.ET ? "True " : "False", data.cr0.NE ? "True " : "False", data.cr0.WP ? "True " : "False", data.cr0.AM ? "True " : "False",
                data.cr0.NW ? "True " : "False", data.cr0.CD ? "True " : "False", data.cr0.PG ? "True " : "False");

        EHPrint("\eFCBD79CR2: PFLA: %#lx\n",
                data.cr2.PFLA);

        EHPrint("\e79FC84CR3: PWT:%s     PCD:%s    PDBR:%#lx\n",
                data.cr3.PWT ? "True " : "False", data.cr3.PCD ? "True " : "False", data.cr3.PDBR);

        EHPrint("\eBD79FCCR4: VME:%s     PVI:%s     TSD:%s      DE:%s\n     PSE:%s     PAE:%s     MCE:%s     PGE:%s\n     PCE:%s    UMIP:%s  OSFXSR:%s OSXMMEXCPT:%s\n    LA57:%s    VMXE:%s    SMXE:%s   PCIDE:%s\n OSXSAVE:%s    SMEP:%s    SMAP:%s     PKE:%s\n",
                data.cr4.VME ? "True " : "False", data.cr4.PVI ? "True " : "False", data.cr4.TSD ? "True " : "False", data.cr4.DE ? "True " : "False",
                data.cr4.PSE ? "True " : "False", data.cr4.PAE ? "True " : "False", data.cr4.MCE ? "True " : "False", data.cr4.PGE ? "True " : "False",
                data.cr4.PCE ? "True " : "False", data.cr4.UMIP ? "True " : "False", data.cr4.OSFXSR ? "True " : "False", data.cr4.OSXMMEXCPT ? "True " : "False",
                data.cr4.LA57 ? "True " : "False", data.cr4.VMXE ? "True " : "False", data.cr4.SMXE ? "True " : "False", data.cr4.PCIDE ? "True " : "False",
                data.cr4.OSXSAVE ? "True " : "False", data.cr4.SMEP ? "True " : "False", data.cr4.SMAP ? "True " : "False", data.cr4.PKE ? "True " : "False");
        EHPrint("\e79FCF5CR8: TPL:%d\n", data.cr8.TPL);
#endif // a64 || a32

#if defined(a64)
        EHPrint("\eFCFC02RFL: CF:%s     PF:%s     AF:%s     ZF:%s\n     SF:%s     TF:%s     IF:%s     DF:%s\n     OF:%s   IOPL:%s     NT:%s     RF:%s\n     VM:%s     AC:%s    VIF:%s    VIP:%s\n     ID:%s     AlwaysOne:%d\n",
                data.Frame->rflags.CF ? "True " : "False", data.Frame->rflags.PF ? "True " : "False", data.Frame->rflags.AF ? "True " : "False", data.Frame->rflags.ZF ? "True " : "False",
                data.Frame->rflags.SF ? "True " : "False", data.Frame->rflags.TF ? "True " : "False", data.Frame->rflags.IF ? "True " : "False", data.Frame->rflags.DF ? "True " : "False",
                data.Frame->rflags.OF ? "True " : "False", data.Frame->rflags.IOPL ? "True " : "False", data.Frame->rflags.NT ? "True " : "False", data.Frame->rflags.RF ? "True " : "False",
                data.Frame->rflags.VM ? "True " : "False", data.Frame->rflags.AC ? "True " : "False", data.Frame->rflags.VIF ? "True " : "False", data.Frame->rflags.VIP ? "True " : "False",
                data.Frame->rflags.ID ? "True " : "False", data.Frame->rflags.AlwaysOne);
#elif defined(a32)
        EHPrint("\eFCFC02EFL: CF:%s     PF:%s     AF:%s     ZF:%s\n     SF:%s     TF:%s     IF:%s     DF:%s\n     OF:%s   IOPL:%s     NT:%s     RF:%s\n     VM:%s     AC:%s    VIF:%s    VIP:%s\n     ID:%s     AlwaysOne:%d\n",
                data.Frame->eflags.CF ? "True " : "False", data.Frame->eflags.PF ? "True " : "False", data.Frame->eflags.AF ? "True " : "False", data.Frame->eflags.ZF ? "True " : "False",
                data.Frame->eflags.SF ? "True " : "False", data.Frame->eflags.TF ? "True " : "False", data.Frame->eflags.IF ? "True " : "False", data.Frame->eflags.DF ? "True " : "False",
                data.Frame->eflags.OF ? "True " : "False", data.Frame->eflags.IOPL ? "True " : "False", data.Frame->eflags.NT ? "True " : "False", data.Frame->eflags.RF ? "True " : "False",
                data.Frame->eflags.VM ? "True " : "False", data.Frame->eflags.AC ? "True " : "False", data.Frame->eflags.VIF ? "True " : "False", data.Frame->eflags.VIP ? "True " : "False",
                data.Frame->eflags.ID ? "True " : "False", data.Frame->eflags.AlwaysOne);
#elif defined(aa64)
#endif

#if defined(a86)
        EHPrint("\eA0A0A0DR6: B0:%s     B1:%s     B2:%s     B3:%s\n     BD:%s     BS:%s     BT:%s\n",
                data.dr6.B0 ? "True " : "False", data.dr6.B1 ? "True " : "False", data.dr6.B2 ? "True " : "False", data.dr6.B3 ? "True " : "False",
                data.dr6.BD ? "True " : "False", data.dr6.BS ? "True " : "False", data.dr6.BT ? "True " : "False");

        EHPrint("\eA0F0F0DR7: L0:%s     G0:%s     L1:%s     G1:%s\n     L2:%s     G2:%s     L3:%s     G3:%s\n     LE:%s     GE:%s     GD:%s\n     R/W0:%s LEN0:%s   R/W1:%s   LEN1:%s\n     R/W2:%s LEN2:%s   R/W3:%s   LEN3:%s\n",
                data.dr7.L0 ? "True " : "False", data.dr7.G0 ? "True " : "False", data.dr7.L1 ? "True " : "False", data.dr7.G1 ? "True " : "False",
                data.dr7.L2 ? "True " : "False", data.dr7.G2 ? "True " : "False", data.dr7.L3 ? "True " : "False", data.dr7.G3 ? "True " : "False",
                data.dr7.LE ? "True " : "False", data.dr7.GE ? "True " : "False", data.dr7.GD ? "True " : "False", data.dr7.RW0 ? "True " : "False",
                data.dr7.LEN0 ? "True " : "False", data.dr7.RW1 ? "True " : "False", data.dr7.LEN1 ? "True " : "False", data.dr7.RW2 ? "True " : "False",
                data.dr7.LEN2 ? "True " : "False", data.dr7.RW3 ? "True " : "False", data.dr7.LEN3 ? "True " : "False");

#ifdef a64
        EHPrint("\e009FF0EFER: SCE:%s      LME:%s      LMA:%s      NXE:%s\n     SVME:%s    LMSLE:%s    FFXSR:%s      TCE:%s\n\n",
                data.efer.SCE ? "True " : "False", data.efer.LME ? "True " : "False", data.efer.LMA ? "True " : "False", data.efer.NXE ? "True " : "False",
                data.efer.SVME ? "True " : "False", data.efer.LMSLE ? "True " : "False", data.efer.FFXSR ? "True " : "False", data.efer.TCE ? "True " : "False");
#endif // a64
#endif

        switch (data.Frame->InterruptNumber)
        {
        case CPU::x86::DivideByZero:
        {
            DivideByZeroExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::Debug:
        {
            DebugExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::NonMaskableInterrupt:
        {
            NonMaskableInterruptExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::Breakpoint:
        {
            BreakpointExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::Overflow:
        {
            OverflowExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::BoundRange:
        {
            BoundRangeExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::InvalidOpcode:
        {
            InvalidOpcodeExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::DeviceNotAvailable:
        {
            DeviceNotAvailableExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::DoubleFault:
        {
            DoubleFaultExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::CoprocessorSegmentOverrun:
        {
            CoprocessorSegmentOverrunExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::InvalidTSS:
        {
            InvalidTSSExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::SegmentNotPresent:
        {
            SegmentNotPresentExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::StackSegmentFault:
        {
            StackFaultExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::GeneralProtectionFault:
        {
            GeneralProtectionExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::PageFault:
        {
            PageFaultExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::x87FloatingPoint:
        {
            x87FloatingPointExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::AlignmentCheck:
        {
            AlignmentCheckExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::MachineCheck:
        {
            MachineCheckExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::SIMDFloatingPoint:
        {
            SIMDFloatingPointExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::Virtualization:
        {
            VirtualizationExceptionHandler(data.Frame);
            break;
        }
        case CPU::x86::Security:
        {
            SecurityExceptionHandler(data.Frame);
            break;
        }
        default:
        {
            UnknownExceptionHandler(data.Frame);
            break;
        }
        }
    }
}