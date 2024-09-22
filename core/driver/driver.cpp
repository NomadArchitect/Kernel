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

#include <driver.hpp>

#include <interface/driver.h>
#include <interface/input.h>
#include <memory.hpp>
#include <ints.hpp>
#include <task.hpp>
#include <printf.h>
#include <exec.hpp>
#include <rand.hpp>
#include <cwalk.h>
#include <md5.h>

#include "../../kernel.h"

using namespace vfs;

namespace Driver
{
	void Manager::PreloadDrivers()
	{
		debug("Initializing driver manager");
		const char *DriverDirectory = Config.DriverDirectory;
		FileNode *drvDirNode = fs->GetByPath(DriverDirectory, nullptr);
		if (!drvDirNode)
		{
			error("Failed to open driver directory %s", DriverDirectory);
			KPrint("Failed to open driver directory %s", DriverDirectory);
			return;
		}

		foreach (const auto &drvNode in drvDirNode->Children)
		{
			debug("Checking driver %s", drvNode->Path.c_str());
			if (!drvNode->IsRegularFile())
				continue;

			if (Execute::GetBinaryType(drvNode->Path) != Execute::BinTypeELF)
			{
				error("Driver %s is not an ELF binary", drvNode->Path.c_str());
				continue;
			}

			DriverObject drvObj = {.BaseAddress = 0,
								   .EntryPoint = 0,
								   .vma = new Memory::VirtualMemoryArea(thisProcess->PageTable),
								   .Path = drvNode->Path,
								   .InterruptHandlers = new std::unordered_map<uint8_t, void *>(),
								   .DeviceOperations = new std::unordered_map<dev_t, DriverHandlers>(),
								   .ID = DriverIDCounter};

			int err = this->LoadDriverFile(drvObj, drvNode);
			debug("err = %d (%s)", err, strerror(err));
			if (err != 0)
			{
				error("Failed to load driver %s: %s",
					  drvNode->Path.c_str(), strerror(err));

				delete drvObj.vma;
				delete drvObj.InterruptHandlers;
				delete drvObj.DeviceOperations;
				continue;
			}

			debug("gdb: \"0x%lX\" %s", drvObj.BaseAddress, drvObj.Name);

			Drivers.insert({DriverIDCounter++, drvObj});
		}
	}

	void Manager::LoadAllDrivers()
	{
		if (Drivers.empty())
		{
			KPrint("\x1b[1;31;41mNo drivers to load");
			return;
		}

		foreach (auto &var in Drivers)
		{
			DriverObject &Drv = var.second;

			debug("Calling driver %s at %#lx", Drv.Path.c_str(), Drv.EntryPoint);
			int (*DrvInit)(dev_t) = (int (*)(dev_t))Drv.EntryPoint;
			Drv.ErrorCode = DrvInit(Drv.ID);
			if (Drv.ErrorCode < 0)
			{
				KPrint("FATAL: _start() failed for %s: %s",
					   Drv.Name, strerror(Drv.ErrorCode));
				error("Failed to load driver %s: %s",
					  Drv.Path.c_str(), strerror(Drv.ErrorCode));

				Drv.vma->FreeAllPages();
				continue;
			}

			KPrint("Loading driver %s", Drv.Name);

			debug("Calling Probe()=%#lx on driver %s",
				  Drv.Probe, Drv.Path.c_str());
			Drv.ErrorCode = Drv.Probe();
			if (Drv.ErrorCode < 0)
			{
				KPrint("Probe() failed for %s: %s",
					   Drv.Name, strerror(Drv.ErrorCode));
				error("Failed to probe driver %s: %s",
					  Drv.Path.c_str(), strerror(Drv.ErrorCode));

				Drv.vma->FreeAllPages();
				continue;
			}

			debug("Calling driver Entry()=%#lx function on driver %s",
				  Drv.Entry, Drv.Path.c_str());
			Drv.ErrorCode = Drv.Entry();
			if (Drv.ErrorCode < 0)
			{
				KPrint("Entry() failed for %s: %s",
					   Drv.Name, strerror(Drv.ErrorCode));
				error("Failed to initialize driver %s: %s",
					  Drv.Path.c_str(), strerror(Drv.ErrorCode));

				Drv.vma->FreeAllPages();
				continue;
			}

			debug("Loaded driver %s", Drv.Path.c_str());
			Drv.Initialized = true;
		}
	}

	void Manager::UnloadAllDrivers()
	{
		foreach (auto &var in Drivers)
		{
			DriverObject *Drv = &var.second;
			if (!Drv->Initialized)
				continue;

			debug("Unloading driver %s", Drv->Name);
			int err = Drv->Final();
			if (err < 0)
			{
				warn("Failed to unload driver %s: %s",
					 Drv->Name, strerror(err));
			}

			if (!Drv->InterruptHandlers->empty())
			{
				foreach (auto &rInt in * Drv->InterruptHandlers)
				{
					Interrupts::RemoveHandler((void (*)(CPU::TrapFrame *))rInt.second);
				}
				Drv->InterruptHandlers->clear();
			}
		}
		Drivers.clear();
	}

	void Manager::Panic()
	{
		Memory::Virtual vmm;
		if (Drivers.size() == 0)
			return;

		foreach (auto Driver in Drivers)
		{
			if (!Driver.second.Initialized)
				continue;

			trace("Panic on driver %s", Driver.second.Name);
			debug("%#lx", Driver.second.Panic);

			/* Crash while probing? */
			if (Driver.second.Panic && vmm.Check((void *)Driver.second.Panic))
				Driver.second.Panic();
			else
				error("No panic function for driver %s",
					  Driver.second.Name);
		}
	}

	int Manager::LoadDriverFile(DriverObject &Drv, FileNode *File)
	{
		trace("Loading driver %s in memory", File->Name.c_str());

		Elf_Ehdr ELFHeader{};
		File->Read(&ELFHeader, sizeof(Elf_Ehdr), 0);

		AssertReturnError(ELFHeader.e_ident[EI_CLASS] == ELFCLASS64, -ENOEXEC);
		AssertReturnError(ELFHeader.e_ident[EI_DATA] == ELFDATA2LSB, -ENOEXEC);
		AssertReturnError(ELFHeader.e_ident[EI_OSABI] == ELFOSABI_SYSV, -ENOEXEC);
		AssertReturnError(ELFHeader.e_ident[EI_ABIVERSION] == 0, -ENOEXEC);
		AssertReturnError(ELFHeader.e_type == ET_DYN, -ENOEXEC);
		AssertReturnError(ELFHeader.e_machine == EM_X86_64, -ENOEXEC);
		AssertReturnError(ELFHeader.e_version == EV_CURRENT, -ENOEXEC);
		AssertReturnError(ELFHeader.e_entry != 0x0, -ENOEXEC);
		AssertReturnError(ELFHeader.e_shstrndx != SHN_UNDEF, -ENOEXEC);
		Drv.EntryPoint = ELFHeader.e_entry;

		size_t segSize = 0;
		Elf_Phdr phdr{};
		for (Elf_Half i = 0; i < ELFHeader.e_phnum; i++)
		{
			File->Read(&phdr, sizeof(Elf_Phdr), ELFHeader.e_phoff + (i * sizeof(Elf_Phdr)));
			if (phdr.p_type == PT_LOAD || phdr.p_type == PT_DYNAMIC)
			{
				if (segSize < phdr.p_vaddr + phdr.p_memsz)
					segSize = phdr.p_vaddr + phdr.p_memsz;
				continue;
			}

			if (phdr.p_type == PT_INTERP)
			{
				char interp[17];
				File->Read(interp, sizeof(interp), phdr.p_offset);
				if (strncmp(interp, "/boot/fennix.elf", sizeof(interp)) != 0)
				{
					error("Interpreter is not /boot/fennix.elf");
					return -ENOEXEC;
				}
			}
		}
		debug("segSize: %ld", segSize);

		Drv.BaseAddress = (uintptr_t)Drv.vma->RequestPages(TO_PAGES(segSize) + 1);
		Drv.EntryPoint += Drv.BaseAddress;
		debug("Driver %s has entry point %#lx and base %#lx",
			  File->Name.c_str(), Drv.EntryPoint, Drv.BaseAddress);

		Elf64_Shdr sht_strtab{};
		Elf64_Shdr sht_symtab{};
		Elf_Shdr shstrtab{};
		Elf_Shdr shdr{};
		__DriverInfo driverInfo{};
		File->Read(&shstrtab, sizeof(Elf_Shdr), ELFHeader.e_shoff + (ELFHeader.e_shstrndx * ELFHeader.e_shentsize));
		for (Elf_Half i = 0; i < ELFHeader.e_shnum; i++)
		{
			if (i == ELFHeader.e_shstrndx)
				continue;

			File->Read(&shdr, ELFHeader.e_shentsize, ELFHeader.e_shoff + (i * ELFHeader.e_shentsize));

			switch (shdr.sh_type)
			{
			case SHT_PROGBITS:
				break;
			case SHT_SYMTAB:
				sht_symtab = shdr;
				continue;
			case SHT_STRTAB:
				sht_strtab = shdr;
				continue;
			case SHT_NULL:
			default:
				continue;
			}

			char symName[16];
			File->Read(symName, sizeof(symName), shstrtab.sh_offset + shdr.sh_name);
			if (strcmp(symName, ".driver.info") != 0)
				continue;

			File->Read(&driverInfo, sizeof(__DriverInfo), shdr.sh_offset);

			/* Perform relocations */
			driverInfo.Name = (const char *)(Drv.BaseAddress + (uintptr_t)driverInfo.Name);
			driverInfo.Description = (const char *)(Drv.BaseAddress + (uintptr_t)driverInfo.Description);
			driverInfo.Author = (const char *)(Drv.BaseAddress + (uintptr_t)driverInfo.Author);
			driverInfo.License = (const char *)(Drv.BaseAddress + (uintptr_t)driverInfo.License);
		}

		for (size_t h = 0; h < (sht_symtab.sh_size / sizeof(Elf64_Sym)); h++)
		{
			Elf64_Sym symEntry{};
			uintptr_t symOffset = sht_symtab.sh_offset + (h * sizeof(Elf64_Sym));
			File->Read(&symEntry, sizeof(Elf64_Sym), symOffset);

			if (symEntry.st_name == 0)
				continue;

			char symName[16];
			File->Read(symName, sizeof(symName), sht_strtab.sh_offset + symEntry.st_name);

			switch (symEntry.st_shndx)
			{
			case SHN_UNDEF:
			case SHN_ABS:
			case SHN_LOPROC /* , SHN_LORESERVE and SHN_BEFORE */:
			case SHN_AFTER:
			case SHN_HIPROC:
			case SHN_COMMON:
			case SHN_HIRESERVE:
				break;
			default:
			{
				debug("shndx: %d", symEntry.st_shndx);
				if (strcmp(symName, "DriverEntry") == 0)
					Drv.Entry = (int (*)())(Drv.BaseAddress + symEntry.st_value);
				else if (strcmp(symName, "DriverFinal") == 0)
					Drv.Final = (int (*)())(Drv.BaseAddress + symEntry.st_value);
				else if (strcmp(symName, "DriverPanic") == 0)
					Drv.Panic = (int (*)())(Drv.BaseAddress + symEntry.st_value);
				else if (strcmp(symName, "DriverProbe") == 0)
					Drv.Probe = (int (*)())(Drv.BaseAddress + symEntry.st_value);

				debug("Found %s at %#lx", symName, symEntry.st_value);
				break;
			}
			}
		}

		for (Elf_Half i = 0; i < ELFHeader.e_phnum; i++)
		{
			File->Read(&phdr, sizeof(Elf_Phdr), ELFHeader.e_phoff + (i * sizeof(Elf_Phdr)));

			switch (phdr.p_type)
			{
			case PT_LOAD:
			case PT_DYNAMIC:
			{
				if (phdr.p_memsz == 0)
					continue;

				uintptr_t dest = Drv.BaseAddress + phdr.p_vaddr;
				debug("Copying PHDR %#lx to %#lx-%#lx (%ld file bytes, %ld mem bytes)",
					  phdr.p_type, dest, dest + phdr.p_memsz,
					  phdr.p_filesz, phdr.p_memsz);

				if (phdr.p_filesz > 0)
					File->Read(dest, phdr.p_filesz, phdr.p_offset);

				if (phdr.p_memsz - phdr.p_filesz > 0)
				{
					void *zero = (void *)(dest + phdr.p_filesz);
					memset(zero, 0, phdr.p_memsz - phdr.p_filesz);
				}

				if (phdr.p_type != PT_DYNAMIC)
					break;

				Elf64_Dyn *dyn = (Elf64_Dyn *)(Drv.BaseAddress + phdr.p_vaddr);
				Elf64_Dyn *relaSize = nullptr;
				Elf64_Dyn *pltrelSize = nullptr;

				while (dyn->d_tag != DT_NULL)
				{
					switch (dyn->d_tag)
					{
					case DT_PLTRELSZ:
					{
						pltrelSize = dyn;
						break;
					}
					case DT_PLTGOT:
					{
						Elf_Addr *got = (Elf_Addr *)(Drv.BaseAddress + dyn->d_un.d_ptr);
						got[1] = 0;
						got[2] = 0;
						break;
					}
					case DT_RELASZ:
					{
						relaSize = dyn;
						break;
					}
					case DT_PLTREL:
					{
						AssertReturnError(dyn->d_un.d_val == DT_RELA, -ENOEXEC);
						break;
					}
					default:
						break;
					}
					dyn++;
				}

				dyn = (Elf64_Dyn *)(Drv.BaseAddress + phdr.p_vaddr);
				while (dyn->d_tag != DT_NULL)
				{
					switch (dyn->d_tag)
					{
					case DT_RELA: /* .rela.dyn */
					{
						AssertReturnError(relaSize != nullptr, -ENOEXEC);

						Elf64_Rela *rela = (Elf64_Rela *)(Drv.BaseAddress + dyn->d_un.d_ptr);
						for (size_t i = 0; i < (relaSize->d_un.d_val / sizeof(Elf64_Rela)); i++)
						{
							Elf64_Rela *r = &rela[i];
							uintptr_t *reloc = (uintptr_t *)(Drv.BaseAddress + r->r_offset);
							uintptr_t relocTarget = 0;

							switch (ELF64_R_TYPE(r->r_info))
							{
							case R_X86_64_GLOB_DAT:
							case R_X86_64_JUMP_SLOT:
							{
								relocTarget = Drv.BaseAddress;
								break;
							}
							case R_X86_64_RELATIVE:
							case R_X86_64_64:
							{
								relocTarget = Drv.BaseAddress + r->r_addend;
								break;
							}
							default:
							{
								fixme("Unhandled relocation type: %#lx",
									  ELF64_R_TYPE(r->r_info));
								break;
							}
							}

							*reloc = relocTarget;

							debug("Relocated %#lx to %#lx",
								  r->r_offset, *reloc);
						}
						break;
					}
					case DT_JMPREL: /* .rela.plt */
					{
						AssertReturnError(pltrelSize != nullptr, -ENOEXEC);

						std::vector<Elf64_Dyn> symtab = Execute::ELFGetDynamicTag_x86_64(File, DT_SYMTAB);
						Elf64_Sym *symbols = (Elf64_Sym *)((uintptr_t)Drv.BaseAddress + symtab[0].d_un.d_ptr);

						std::vector<Elf64_Dyn> StrTab = Execute::ELFGetDynamicTag_x86_64(File, DT_STRTAB);
						char *DynStr = (char *)((uintptr_t)Drv.BaseAddress + StrTab[0].d_un.d_ptr);

						Elf64_Rela *rela = (Elf64_Rela *)(Drv.BaseAddress + dyn->d_un.d_ptr);
						for (size_t i = 0; i < (pltrelSize->d_un.d_val / sizeof(Elf64_Rela)); i++)
						{
							Elf64_Rela *r = &rela[i];
							uintptr_t *reloc = (uintptr_t *)(Drv.BaseAddress + r->r_offset);

							switch (ELF64_R_TYPE(r->r_info))
							{
							case R_X86_64_JUMP_SLOT:
							{
								Elf64_Xword symIndex = ELF64_R_SYM(r->r_info);
								Elf64_Sym *sym = symbols + symIndex;

								const char *symName = DynStr + sym->st_name;
								debug("Resolving symbol %s", symName);

								*reloc = (uintptr_t)GetSymbolByName(symName, driverInfo.Version.APIVersion);
								break;
							}
							default:
							{
								fixme("Unhandled relocation type: %#lx",
									  ELF64_R_TYPE(r->r_info));
								break;
							}
							}
						}
						break;
					}
					case DT_PLTGOT:
					case DT_PLTRELSZ:
					case DT_RELASZ:
					case DT_PLTREL:
						break;
					default:
					{
						fixme("Unhandled dynamic tag: %#lx", dyn->d_tag);
						break;
					}
					}
					dyn++;
				}
				break;
			}
			case PT_PHDR:
			case PT_INTERP:
				break;
			default:
			{
				fixme("Unhandled program header type: %#lx", phdr.p_type);
				break;
			}
			}
		}

		AssertReturnError(driverInfo.Name != nullptr, -EFAULT);
		strncpy(Drv.Name, driverInfo.Name, sizeof(Drv.Name));
		strncpy(Drv.Description, driverInfo.Description, sizeof(Drv.Description));
		strncpy(Drv.Author, driverInfo.Author, sizeof(Drv.Author));
		Drv.Version.Major = driverInfo.Version.Major;
		Drv.Version.Minor = driverInfo.Version.Minor;
		Drv.Version.Patch = driverInfo.Version.Patch;
		strncpy(Drv.License, driverInfo.License, sizeof(Drv.License));

		return 0;
	}

	Manager::Manager() { this->InitializeDaemonFS(); }

	Manager::~Manager()
	{
		debug("Unloading drivers");
		UnloadAllDrivers();
	}
}
