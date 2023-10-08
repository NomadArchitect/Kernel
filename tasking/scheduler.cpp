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

#include <task.hpp>

#include <dumper.hpp>
#include <convert.h>
#include <lock.hpp>
#include <printf.h>
#include <smp.hpp>
#include <io.h>

#include "../kernel.h"

#if defined(a64)
#include "../arch/amd64/cpu/apic.hpp"
#include "../arch/amd64/cpu/gdt.hpp"
#elif defined(a32)
#include "../arch/i386/cpu/apic.hpp"
#include "../arch/i386/cpu/gdt.hpp"
#elif defined(aa64)
#endif

/* FIXME: On screen task manager is corrupting the stack... */
// #define ON_SCREEN_SCHEDULER_TASK_MANAGER 1

// #define DEBUG_SCHEDULER 1
// #define DEBUG_GET_NEXT_AVAILABLE_PROCESS 1
// #define DEBUG_GET_NEXT_AVAILABLE_THREAD 1
// #define DEBUG_FIND_NEW_PROCESS 1
// #define DEBUG_SCHEDULER_SEARCH_PROCESS_THREAD 1
// #define DEBUG_WAKE_UP_THREADS 1

/* Global */
#ifdef DEBUG_SCHEDULER

#define DEBUG_GET_NEXT_AVAILABLE_PROCESS 1
#define DEBUG_GET_NEXT_AVAILABLE_THREAD 1
#define DEBUG_FIND_NEW_PROCESS 1
#define DEBUG_SCHEDULER_SEARCH_PROCESS_THREAD 1
#define DEBUG_WAKE_UP_THREADS 1

#define schedbg(m, ...)      \
	debug(m, ##__VA_ARGS__); \
	__sync
#else
#define schedbg(m, ...)
#endif

/* GetNextAvailableThread */
#ifdef DEBUG_GET_NEXT_AVAILABLE_PROCESS
#define gnap_schedbg(m, ...) \
	debug(m, ##__VA_ARGS__); \
	__sync
#else
#define gnap_schedbg(m, ...)
#endif

/* GetNextAvailableProcess */
#ifdef DEBUG_GET_NEXT_AVAILABLE_THREAD
#define gnat_schedbg(m, ...) \
	debug(m, ##__VA_ARGS__); \
	__sync
#else
#define gnat_schedbg(m, ...)
#endif

/* FindNewProcess */
#ifdef DEBUG_FIND_NEW_PROCESS
#define fnp_schedbg(m, ...)  \
	debug(m, ##__VA_ARGS__); \
	__sync
#else
#define fnp_schedbg(m, ...)
#endif

/* SchedulerSearchProcessThread */
#ifdef DEBUG_SCHEDULER_SEARCH_PROCESS_THREAD
#define sspt_schedbg(m, ...) \
	debug(m, ##__VA_ARGS__); \
	__sync
#else
#define sspt_schedbg(m, ...)
#endif

/* WakeUpThreads */
#ifdef DEBUG_WAKE_UP_THREADS
#define wut_schedbg(m, ...)  \
	debug(m, ##__VA_ARGS__); \
	__sync
#else
#define wut_schedbg(m, ...)
#endif

extern "C" SafeFunction NIF void TaskingScheduler_OneShot(int TimeSlice)
{
	if (TimeSlice == 0)
		TimeSlice = Tasking::TaskPriority::Normal;
#if defined(a86)
	((APIC::Timer *)Interrupts::apicTimer[GetCurrentCPU()->ID])->OneShot(CPU::x86::IRQ16, TimeSlice);
#elif defined(aa64)
#endif
}

namespace Tasking
{
#if defined(a86)
	SafeFunction NIF bool Task::FindNewProcess(void *CPUDataPointer)
	{
		CPUData *CurrentCPU = (CPUData *)CPUDataPointer;
		fnp_schedbg("%d processes", ProcessList.size());
#ifdef DEBUG_FIND_NEW_PROCESS
		foreach (auto process in ProcessList)
			fnp_schedbg("Process %d %s", process->ID,
						process->Name);
#endif
		foreach (auto process in ProcessList)
		{
			if (unlikely(InvalidPCB(process)))
				continue;

			switch (process->State.load())
			{
			case TaskState::Ready:
				fnp_schedbg("Ready process (%s)%d",
							process->Name, process->ID);
				break;
			default:
				fnp_schedbg("Process \"%s\"(%d) status %d",
							process->Name, process->ID,
							process->State);

				/* We don't actually remove the process. RemoveProcess
				   firstly checks if it's terminated, if not, it will
				   loop through Threads and call RemoveThread on
				   terminated threads. */
				RemoveProcess(process);
				continue;
			}

			foreach (auto thread in process->Threads)
			{
				if (unlikely(InvalidTCB(thread)))
					continue;

				if (thread->State.load() != TaskState::Ready)
					continue;

				if (thread->Info.Affinity[CurrentCPU->ID] == false)
					continue;

				CurrentCPU->CurrentProcess = process;
				CurrentCPU->CurrentThread = thread;
				return true;
			}
		}
		fnp_schedbg("No process to run.");
		return false;
	}

	SafeFunction NIF bool Task::GetNextAvailableThread(void *CPUDataPointer)
	{
		CPUData *CurrentCPU = (CPUData *)CPUDataPointer;

		size_t ThreadsSize = CurrentCPU->CurrentProcess->Threads.size();

		for (size_t i = 0; i < ThreadsSize; i++)
		{
			if (CurrentCPU->CurrentProcess->Threads[i] == CurrentCPU->CurrentThread.load())
			{
				size_t TempIndex = i;
			RetryAnotherThread:
				if (TempIndex + 1 >= ThreadsSize)
					break;

				TCB *nextThread = CurrentCPU->CurrentProcess->Threads[TempIndex + 1];

				if (unlikely(InvalidTCB(nextThread)))
				{
					if (TempIndex > ThreadsSize)
						break;
					TempIndex++;

					gnat_schedbg("Thread %#lx is invalid", nextThread);
					goto RetryAnotherThread;
				}

				gnat_schedbg("\"%s\"(%d) and next thread is \"%s\"(%d)",
							 CurrentCPU->CurrentProcess->Threads[i]->Name,
							 CurrentCPU->CurrentProcess->Threads[i]->ID,
							 nextThread->Name, nextThread->ID);

				if (nextThread->State.load() != TaskState::Ready)
				{
					gnat_schedbg("Thread %d is not ready", nextThread->ID);
					TempIndex++;
					goto RetryAnotherThread;
				}

				if (nextThread->Info.Affinity[CurrentCPU->ID] == false)
					continue;

				CurrentCPU->CurrentThread = nextThread;
				gnat_schedbg("[thd 0 -> end] Scheduling thread %d parent of %s->%d Procs %d",
							 nextThread->ID, nextThread->Parent->Name,
							 ThreadsSize, ProcessList.size());
				return true;
			}
#ifdef DEBUG
			else
			{
				gnat_schedbg("Thread %d is not the current one",
							 CurrentCPU->CurrentProcess->Threads[i]->ID);
			}
#endif
		}
		return false;
	}

	SafeFunction NIF bool Task::GetNextAvailableProcess(void *CPUDataPointer)
	{
		CPUData *CurrentCPU = (CPUData *)CPUDataPointer;

		bool Skip = true;
		foreach (auto process in ProcessList)
		{
			if (process == CurrentCPU->CurrentProcess.load())
			{
				Skip = false;
				gnap_schedbg("Found current process %#lx", process);
				continue;
			}

			if (Skip)
			{
				gnap_schedbg("Skipping process %#lx", process);
				continue;
			}

			if (unlikely(InvalidPCB(process)))
			{
				gnap_schedbg("Invalid process %#lx", process);
				continue;
			}

			if (process->State.load() != TaskState::Ready)
			{
				gnap_schedbg("Process %d is not ready", process->ID);
				continue;
			}

			foreach (auto thread in process->Threads)
			{
				if (unlikely(InvalidTCB(thread)))
				{
					gnap_schedbg("Invalid thread %#lx", thread);
					continue;
				}

				if (thread->State.load() != TaskState::Ready)
				{
					gnap_schedbg("Thread %d is not ready", thread->ID);
					continue;
				}

				if (thread->Info.Affinity[CurrentCPU->ID] == false)
					continue;

				CurrentCPU->CurrentProcess = process;
				CurrentCPU->CurrentThread = thread;
				gnap_schedbg("[cur proc+1 -> first thd] Scheduling thread %d %s->%d (Total Procs %d)",
							 thread->ID, thread->Name, process->Threads.size(), ProcessList.size());
				return true;
			}
		}
		gnap_schedbg("No process to run.");
		return false;
	}

	SafeFunction NIF bool Task::SchedulerSearchProcessThread(void *CPUDataPointer)
	{
		CPUData *CurrentCPU = (CPUData *)CPUDataPointer;

		foreach (auto process in ProcessList)
		{
			if (unlikely(InvalidPCB(process)))
			{
				sspt_schedbg("Invalid process %#lx", process);
				continue;
			}

			if (process->State.load() != TaskState::Ready)
			{
				sspt_schedbg("Process %d is not ready", process->ID);
				continue;
			}

			foreach (auto thread in process->Threads)
			{
				if (unlikely(InvalidTCB(thread)))
				{
					sspt_schedbg("Invalid thread %#lx", thread);
					continue;
				}

				if (thread->State.load() != TaskState::Ready)
				{
					sspt_schedbg("Thread %d is not ready", thread->ID);
					continue;
				}

				if (thread->Info.Affinity[CurrentCPU->ID] == false)
					continue;

				CurrentCPU->CurrentProcess = process;
				CurrentCPU->CurrentThread = thread;
				sspt_schedbg("[proc 0 -> end -> first thd] Scheduling thread %d parent of %s->%d (Procs %d)",
							 thread->ID, thread->Parent->Name, process->Threads.size(), ProcessList.size());
				return true;
			}
		}
		return false;
	}

	SafeFunction NIF void Task::UpdateProcessState()
	{
		foreach (auto process in ProcessList)
		{
			if (unlikely(InvalidPCB(process)))
				continue;

			if (process->State.load() == TaskState::Terminated)
				continue;

			if (process->Threads.size() == 1)
			{
				process->State.exchange(process->Threads[0]->State.load());
				continue;
			}

			bool AllThreadsSleeping = true;
			foreach (auto thread in process->Threads)
			{
				if (thread->State.load() != TaskState::Sleeping)
				{
					AllThreadsSleeping = false;
					break;
				}
			}

			if (AllThreadsSleeping)
				process->State.store(TaskState::Sleeping);
			else if (process->State.load() == TaskState::Sleeping)
				process->State.store(TaskState::Ready);
		}
	}

	SafeFunction NIF void Task::WakeUpThreads()
	{
		foreach (auto process in ProcessList)
		{
			if (unlikely(InvalidPCB(process)))
				continue;

			Tasking::TaskState pState = process->State.load();
			if (pState != TaskState::Ready &&
				pState != TaskState::Sleeping &&
				pState != TaskState::Blocked)
				continue;

			foreach (auto thread in process->Threads)
			{
				if (unlikely(InvalidTCB(thread)))
					continue;

				if (likely(thread->State.load() != TaskState::Sleeping))
					continue;

				/* Check if the thread is ready to wake up. */
				if (unlikely(thread->Info.SleepUntil < TimeManager->GetCounter()))
				{
					if (pState == TaskState::Sleeping)
						process->State.store(TaskState::Ready);
					thread->State.store(TaskState::Ready);

					thread->Info.SleepUntil = 0;
					wut_schedbg("Thread \"%s\"(%d) woke up.", thread->Name, thread->ID);
				}
				else
				{
					wut_schedbg("Thread \"%s\"(%d) is not ready to wake up. (SleepUntil: %d, Counter: %d)",
								thread->Name, thread->ID, thread->Info.SleepUntil, TimeManager->GetCounter());
				}
			}
		}
	}

#ifdef ON_SCREEN_SCHEDULER_TASK_MANAGER
	int SuccessSource = 0;
	int sanity;

	const char *Statuses[] = {
		"FF0000", /* Unknown */
		"AAFF00", /* Ready */
		"00AA00", /* Running */
		"FFAA00", /* Sleeping */
		"FFAA00", /* Blocked */
		"FFAA00", /* Stopped */
		"FFAA00", /* Waiting */

		"FF0088", /* Zombie */
		"FF0000", /* Terminated */
	};

	const char *StatusesSign[] = {
		"Unknown",
		"Ready",
		"Run",
		"Sleep",
		"Block",
		"Wait",

		"Zombie",
		"Terminated",
	};

	const char *SuccessSourceStrings[] = {
		"Unknown",
		"GetNextAvailableThread",
		"GetNextAvailableProcess",
		"SchedulerSearchProcessThread",
	};

	SafeFunction NIF void OnScreenTaskManagerUpdate()
	{
		TimeManager->Sleep(10, Time::Units::Milliseconds);
		Video::ScreenBuffer *sb = Display->GetBuffer(0);
		for (short i = 0; i < 340; i++)
		{
			for (short j = 0; j < 600; j++)
			{
				uint32_t *Pixel = (uint32_t *)((uintptr_t)sb->Buffer + (j * sb->Width + i) * (bInfo.Framebuffer[0].BitsPerPixel / 8));
				*Pixel = 0x222222;
			}
		}

		uint32_t tmpX, tmpY;
		Display->GetBufferCursor(0, &tmpX, &tmpY);
		Display->SetBufferCursor(0, 0, 0);
		printf("\eF02C21Task Manager\n");
		foreach (auto Proc in TaskManager->GetProcessList())
		{
			int State = Proc->State;
			printf("\e%s-> \eAABBCC%s \e00AAAA%s\n",
				   Statuses[State], Proc->Name, StatusesSign[State]);

			foreach (auto Thd in Proc->Threads)
			{
				State = Thd->State;
				printf("  \e%s-> \eAABBCC%s \e00AAAA%s\n\eAABBCC",
					   Statuses[State], Thd->Name, StatusesSign[State]);
			}
		}
		register uintptr_t CurrentStackAddress asm("rsp");
		printf("Sanity: %d, Stack: %#lx\nSched. Source: %s", sanity++, CurrentStackAddress, SuccessSourceStrings[SuccessSource]);
		if (sanity > 1000)
			sanity = 0;
		Display->SetBufferCursor(0, tmpX, tmpY);
		Display->SetBuffer(0);
		TimeManager->Sleep(10, Time::Units::Milliseconds);
	}
#endif

#ifdef a64
	SafeFunction NIF void Task::Schedule(CPU::x64::TrapFrame *Frame)
#else
	SafeFunction NIF void Task::Schedule(CPU::x32::TrapFrame *Frame)
#endif
	{
		if (unlikely(StopScheduler))
		{
			warn("Scheduler stopped.");
			return;
		}
		bool ProcessNotChanged = false;
/* Restore kernel page table for safety reasons. */
#ifdef a64
		CPU::x64::writecr3({.raw = (uint64_t)KernelPageTable});
#else
		CPU::x32::writecr3({.raw = (uint32_t)KernelPageTable});
#endif
		uint64_t SchedTmpTicks = TimeManager->GetCounter();
		this->LastTaskTicks.store(size_t(SchedTmpTicks - this->SchedulerTicks.load()));
		CPUData *CurrentCPU = GetCurrentCPU();
		this->LastCore.store(CurrentCPU->ID);
		schedbg("Scheduler called on CPU %d.", CurrentCPU->ID);

#ifdef DEBUG_SCHEDULER
		{
			schedbg("================================================================");
			schedbg("State: 0-ukn | 1-rdy | 2-run | 3-wait | 4-term");
			schedbg("Technical Informations on regs %#lx", Frame->InterruptNumber);
			size_t ds;
			asmv("mov %%ds, %0"
				 : "=r"(ds));
			schedbg("FS=%#lx  GS=%#lx  SS=%#lx  CS=%#lx  DS=%#lx",
					CPU::x64::rdmsr(CPU::x64::MSR_FS_BASE), CPU::x64::rdmsr(CPU::x64::MSR_GS_BASE),
					Frame->ss, Frame->cs, ds);
			schedbg("R8=%#lx  R9=%#lx  R10=%#lx  R11=%#lx",
					Frame->r8, Frame->r9, Frame->r10, Frame->r11);
			schedbg("R12=%#lx  R13=%#lx  R14=%#lx  R15=%#lx",
					Frame->r12, Frame->r13, Frame->r14, Frame->r15);
			schedbg("RAX=%#lx  RBX=%#lx  RCX=%#lx  RDX=%#lx",
					Frame->rax, Frame->rbx, Frame->rcx, Frame->rdx);
			schedbg("RSI=%#lx  RDI=%#lx  RBP=%#lx  RSP=%#lx",
					Frame->rsi, Frame->rdi, Frame->rbp, Frame->rsp);
			schedbg("RIP=%#lx  RFL=%#lx  INT=%#lx  ERR=%#lx",
					Frame->rip, Frame->rflags, Frame->InterruptNumber, Frame->ErrorCode);
			schedbg("================================================================");
		}
#endif

		if (unlikely(InvalidPCB(CurrentCPU->CurrentProcess.load()) ||
					 InvalidTCB(CurrentCPU->CurrentThread.load())))
		{
			schedbg("Invalid process or thread. Finding a new one.");
			ProcessNotChanged = true;
			if (this->FindNewProcess(CurrentCPU))
				goto Success;
			else
				goto Idle;
		}
		else
		{
			CurrentCPU->CurrentThread->Registers = *Frame;
			CPU::x64::fxsave(&CurrentCPU->CurrentThread->FPU);
#ifdef a64
			CurrentCPU->CurrentThread->ShadowGSBase = CPU::x64::rdmsr(CPU::x64::MSR_SHADOW_GS_BASE);
			CurrentCPU->CurrentThread->GSBase = CPU::x64::rdmsr(CPU::x64::MSR_GS_BASE);
			CurrentCPU->CurrentThread->FSBase = CPU::x64::rdmsr(CPU::x64::MSR_FS_BASE);
#else
			CurrentCPU->CurrentThread->ShadowGSBase = uintptr_t(CPU::x32::rdmsr(CPU::x32::MSR_SHADOW_GS_BASE));
			CurrentCPU->CurrentThread->GSBase = uintptr_t(CPU::x32::rdmsr(CPU::x32::MSR_GS_BASE));
			CurrentCPU->CurrentThread->FSBase = uintptr_t(CPU::x32::rdmsr(CPU::x32::MSR_FS_BASE));
#endif

			if (CurrentCPU->CurrentProcess->State.load() == TaskState::Running)
				CurrentCPU->CurrentProcess->State.store(TaskState::Ready);
			if (CurrentCPU->CurrentThread->State.load() == TaskState::Running)
				CurrentCPU->CurrentThread->State.store(TaskState::Ready);

			this->UpdateProcessState();
			schedbg("Passed UpdateProcessState");

			this->WakeUpThreads();
			schedbg("Passed WakeUpThreads");

			if (this->SchedulerUpdateTrapFrame)
			{
				this->SchedulerUpdateTrapFrame = false;
				goto Success;
			}

			if (this->GetNextAvailableThread(CurrentCPU))
			{
#ifdef ON_SCREEN_SCHEDULER_TASK_MANAGER
				SuccessSource = 1;
#endif
				ProcessNotChanged = true;
				goto Success;
			}
			schedbg("Passed GetNextAvailableThread");

			if (this->GetNextAvailableProcess(CurrentCPU))
			{
#ifdef ON_SCREEN_SCHEDULER_TASK_MANAGER
				SuccessSource = 2;
#endif
				goto Success;
			}
			schedbg("Passed GetNextAvailableProcess");

			if (SchedulerSearchProcessThread(CurrentCPU))
			{
#ifdef ON_SCREEN_SCHEDULER_TASK_MANAGER
				SuccessSource = 3;
#endif
				schedbg("Passed SchedulerSearchProcessThread");
				goto Success;
			}
			else
			{
				schedbg("SchedulerSearchProcessThread failed. Going idle.");
				goto Idle;
			}
		}

		warn("Unwanted reach!");
		TaskingScheduler_OneShot(100);
		goto End;

	Idle:
		ProcessNotChanged = true;
		CurrentCPU->CurrentProcess = IdleProcess;
		CurrentCPU->CurrentThread = IdleThread;

	Success:
		schedbg("Process \"%s\"(%d) Thread \"%s\"(%d) is now running on CPU %d",
				CurrentCPU->CurrentProcess->Name, CurrentCPU->CurrentProcess->ID,
				CurrentCPU->CurrentThread->Name, CurrentCPU->CurrentThread->ID, CurrentCPU->ID);

		if (!ProcessNotChanged)
			UpdateUsage(&CurrentCPU->CurrentProcess->Info,
						CurrentCPU->CurrentProcess->Security.ExecutionMode,
						CurrentCPU->ID);

		UpdateUsage(&CurrentCPU->CurrentThread->Info,
					CurrentCPU->CurrentThread->Security.ExecutionMode,
					CurrentCPU->ID);

		CurrentCPU->CurrentProcess->State.store(TaskState::Running);
		CurrentCPU->CurrentThread->State.store(TaskState::Running);

		*Frame = CurrentCPU->CurrentThread->Registers;

		for (size_t i = 0; i < (sizeof(CurrentCPU->CurrentThread->IPHistory) / sizeof(CurrentCPU->CurrentThread->IPHistory[0])) - 1; i++)
			CurrentCPU->CurrentThread->IPHistory[i + 1] = CurrentCPU->CurrentThread->IPHistory[i];

#ifdef a64
		CurrentCPU->CurrentThread->IPHistory[0] = Frame->rip;

		GlobalDescriptorTable::SetKernelStack((void *)((uintptr_t)CurrentCPU->CurrentThread->Stack->GetStackTop()));
		CPU::x64::fxrstor(&CurrentCPU->CurrentThread->FPU);
		CPU::x64::wrmsr(CPU::x64::MSR_SHADOW_GS_BASE, CurrentCPU->CurrentThread->ShadowGSBase);
		CPU::x64::wrmsr(CPU::x64::MSR_GS_BASE, CurrentCPU->CurrentThread->GSBase);
		CPU::x64::wrmsr(CPU::x64::MSR_FS_BASE, CurrentCPU->CurrentThread->FSBase);
#else
		CurrentCPU->CurrentThread->IPHistory[0] = Frame->eip;

		GlobalDescriptorTable::SetKernelStack((void *)((uintptr_t)CurrentCPU->CurrentThread->Stack->GetStackTop()));
		CPU::x32::fxrstor(&CurrentCPU->CurrentThread->FPU);
		CPU::x32::wrmsr(CPU::x32::MSR_SHADOW_GS_BASE, CurrentCPU->CurrentThread->ShadowGSBase);
		CPU::x32::wrmsr(CPU::x32::MSR_GS_BASE, CurrentCPU->CurrentThread->GSBase);
		CPU::x32::wrmsr(CPU::x32::MSR_FS_BASE, CurrentCPU->CurrentThread->FSBase);
#endif

#ifdef ON_SCREEN_SCHEDULER_TASK_MANAGER
		OnScreenTaskManagerUpdate();
#endif

		switch (CurrentCPU->CurrentProcess->Security.ExecutionMode)
		{
		case TaskExecutionMode::System:
		case TaskExecutionMode::Kernel:
			// wrmsr(MSR_SHADOW_GS_BASE, (uint64_t)CurrentCPU->CurrentThread);
			break;
		case TaskExecutionMode::User:
			// wrmsr(MSR_SHADOW_GS_BASE, CurrentCPU->CurrentThread->gs);
			break;
		default:
			error("Unknown trust level %d.",
				  CurrentCPU->CurrentProcess->Security.ExecutionMode);
			break;
		}

		if (!ProcessNotChanged)
			(&CurrentCPU->CurrentProcess->Info)->LastUpdateTime = TimeManager->GetCounter();
		(&CurrentCPU->CurrentThread->Info)->LastUpdateTime = TimeManager->GetCounter();
		TaskingScheduler_OneShot(CurrentCPU->CurrentThread->Info.Priority);

		if (CurrentCPU->CurrentThread->Security.IsDebugEnabled && CurrentCPU->CurrentThread->Security.IsKernelDebugEnabled)
		{
#ifdef a64
			trace("%s[%ld]: RIP=%#lx  RBP=%#lx  RSP=%#lx",
				  CurrentCPU->CurrentThread->Name, CurrentCPU->CurrentThread->ID,
				  CurrentCPU->CurrentThread->Registers.rip,
				  CurrentCPU->CurrentThread->Registers.rbp,
				  CurrentCPU->CurrentThread->Registers.rsp);
#else
			trace("%s[%ld]: EIP=%#lx  EBP=%#lx  ESP=%#lx",
				  CurrentCPU->CurrentThread->Name, CurrentCPU->CurrentThread->ID,
				  CurrentCPU->CurrentThread->Registers.eip,
				  CurrentCPU->CurrentThread->Registers.ebp,
				  CurrentCPU->CurrentThread->Registers.esp);
#endif
		}

		schedbg("================================================================");
		schedbg("Technical Informations on Thread %s[%ld]:",
				CurrentCPU->CurrentThread->Name, CurrentCPU->CurrentThread->ID);
		uintptr_t ds;
		asmv("mov %%ds, %0"
			 : "=r"(ds));
		schedbg("FS=%#lx  GS=%#lx  SS=%#lx  CS=%#lx  DS=%#lx",
				CPU::x64::rdmsr(CPU::x64::MSR_FS_BASE), CPU::x64::rdmsr(CPU::x64::MSR_GS_BASE),
				Frame->ss, Frame->cs, ds);
		schedbg("R8=%#lx  R9=%#lx  R10=%#lx  R11=%#lx",
				Frame->r8, Frame->r9, Frame->r10, Frame->r11);
		schedbg("R12=%#lx  R13=%#lx  R14=%#lx  R15=%#lx",
				Frame->r12, Frame->r13, Frame->r14, Frame->r15);
		schedbg("RAX=%#lx  RBX=%#lx  RCX=%#lx  RDX=%#lx",
				Frame->rax, Frame->rbx, Frame->rcx, Frame->rdx);
		schedbg("RSI=%#lx  RDI=%#lx  RBP=%#lx  RSP=%#lx",
				Frame->rsi, Frame->rdi, Frame->rbp, Frame->rsp);
		schedbg("RIP=%#lx  RFL=%#lx  INT=%#lx  ERR=%#lx",
				Frame->rip, Frame->rflags, Frame->InterruptNumber, Frame->ErrorCode);
		schedbg("================================================================");

	End:
		this->SchedulerTicks.store(size_t(TimeManager->GetCounter() - SchedTmpTicks));
#ifdef a64
		CPU::x64::writecr3({.raw = (uint64_t)CurrentCPU->CurrentProcess->PageTable});
#else
		CPU::x32::writecr3({.raw = (uint64_t)CurrentCPU->CurrentProcess->PageTable});
#endif
	}

#ifdef a64
	SafeFunction NIF void Task::OnInterruptReceived(CPU::x64::TrapFrame *Frame)
#else
	SafeFunction NIF void Task::OnInterruptReceived(CPU::x32::TrapFrame *Frame)
#endif
	{
		SmartCriticalSection(SchedulerLock);
		this->Schedule(Frame);
	}
#elif defined(aa64)
	SafeFunction bool Task::FindNewProcess(void *CPUDataPointer)
	{
		fixme("unimplemented");
	}

	SafeFunction bool Task::GetNextAvailableThread(void *CPUDataPointer)
	{
		fixme("unimplemented");
	}

	SafeFunction bool Task::GetNextAvailableProcess(void *CPUDataPointer)
	{
		fixme("unimplemented");
	}

	SafeFunction bool Task::SchedulerSearchProcessThread(void *CPUDataPointer)
	{
		fixme("unimplemented");
	}

	SafeFunction void Task::Schedule(CPU::aarch64::TrapFrame *Frame)
	{
		fixme("unimplemented");
	}

	SafeFunction void Task::OnInterruptReceived(CPU::aarch64::TrapFrame *Frame) { this->Schedule(Frame); }
#endif
}