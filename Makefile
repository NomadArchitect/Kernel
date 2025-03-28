# Config file
include ../Makefile.conf

KERNEL_FILENAME = fennix.elf

CC = ../$(COMPILER_PATH)/$(COMPILER_ARCH)gcc
CPP = ../$(COMPILER_PATH)/$(COMPILER_ARCH)g++
LD = ../$(COMPILER_PATH)/$(COMPILER_ARCH)ld
AS = ../$(COMPILER_PATH)/$(COMPILER_ARCH)as
NM = ../$(COMPILER_PATH)/$(COMPILER_ARCH)nm
OBJCOPY = ../$(COMPILER_PATH)/$(COMPILER_ARCH)objcopy
OBJDUMP = ../$(COMPILER_PATH)/$(COMPILER_ARCH)objdump
GDB = ../$(COMPILER_PATH)/$(COMPILER_ARCH)gdb

RUST_TARGET_PATH = arch/$(OSARCH)/rust-target.json

GIT_COMMIT = $(shell git rev-parse HEAD)
GIT_COMMIT_SHORT = $(shell git rev-parse --short HEAD)

HEADERS = $(sort $(dir $(wildcard ./include/*))) $(sort $(dir $(wildcard ./include_std/*)))
INCLUDE_DIR = -I./include -I./include_std

BMP_SOURCES = $(shell find ./ -type f -name '*.bmp')
PSF_SOURCES = $(shell find ./ -type f -name '*.psf')
ifeq ($(OSARCH), amd64)
S_SOURCES = $(shell find ./ -type f -name '*.S' -not -path "./arch/i386/*" -not -path "./arch/aarch64/*")
s_SOURCES = $(shell find ./ -type f -name '*.s' -not -path "./arch/i386/*" -not -path "./arch/aarch64/*")
C_SOURCES = $(shell find ./ -type f -name '*.c' -not -path "./arch/i386/*" -not -path "./arch/aarch64/*")
CPP_SOURCES = $(shell find ./ -type f -name '*.cpp' -not -path "./arch/i386/*" -not -path "./arch/aarch64/*")
HEADERS += $(sort $(dir $(wildcard ./arch/amd64/include/*)))
INCLUDE_DIR += -I./arch/amd64/include
else ifeq ($(OSARCH), i386)
S_SOURCES = $(shell find ./ -type f -name '*.S' -not -path "./arch/amd64/*" -not -path "./arch/aarch64/*")
s_SOURCES = $(shell find ./ -type f -name '*.s' -not -path "./arch/amd64/*" -not -path "./arch/aarch64/*")
C_SOURCES = $(shell find ./ -type f -name '*.c' -not -path "./arch/amd64/*" -not -path "./arch/aarch64/*")
CPP_SOURCES = $(shell find ./ -type f -name '*.cpp' -not -path "./arch/amd64/*" -not -path "./arch/aarch64/*")
HEADERS += $(sort $(dir $(wildcard ./arch/i386/include/*)))
INCLUDE_DIR += -I./arch/i386/include
else ifeq ($(OSARCH), aarch64)
S_SOURCES = $(shell find ./ -type f -name '*.S' -not -path "./arch/amd64/*" -not -path "./arch/i386/*")
s_SOURCES = $(shell find ./ -type f -name '*.s' -not -path "./arch/amd64/*" -not -path "./arch/i386/*")
C_SOURCES = $(shell find ./ -type f -name '*.c' -not -path "./arch/amd64/*" -not -path "./arch/i386/*")
CPP_SOURCES = $(shell find ./ -type f -name '*.cpp' -not -path "./arch/amd64/*" -not -path "./arch/i386/*")
HEADERS += $(sort $(dir $(wildcard ./arch/aarch64/include/*)))
INCLUDE_DIR += -I./arch/aarch64/include
endif
OBJ = $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o) $(ASM_SOURCES:.asm=.o) $(S_SOURCES:.S=.o) $(s_SOURCES:.s=.o) $(PSF_SOURCES:.psf=.o) $(BMP_SOURCES:.bmp=.o)
STACK_USAGE_OBJ = $(C_SOURCES:.c=.su) $(CPP_SOURCES:.cpp=.su)
GCNO_OBJ = $(C_SOURCES:.c=.gcno) $(CPP_SOURCES:.cpp=.gcno)

LDFLAGS := -Wl,-Map kernel.map -static -nostdlib -nodefaultlibs -nolibc

# Disable all warnings by adding "-w" in WARNCFLAG and if you want to treat the warnings as errors, add "-Werror"
# -Wconversion this may be re-added later
WARNCFLAG = -Wall -Wextra \
			-Wfloat-equal -Wpointer-arith -Wcast-align \
			-Wredundant-decls -Winit-self -Wswitch-default \
			-Wstrict-overflow=5 -Wno-error=cpp -Werror \
			-Wno-unused-parameter

# https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html
CFLAGS :=										\
	$(INCLUDE_DIR)								\
	-D__kernel__='1'							\
	-DKERNEL_NAME='"$(OSNAME)"' 				\
	-DKERNEL_ARCH='"$(OSARCH)"' 				\
	-DKERNEL_VERSION='"$(KERNEL_VERSION)"'		\
	-DGIT_COMMIT='"$(GIT_COMMIT)"'				\
	-DGIT_COMMIT_SHORT='"$(GIT_COMMIT_SHORT)"'

ifeq ($(OSARCH), amd64)

CFLAGS += -fno-pic -fno-pie -mno-red-zone -march=core2	\
		  -mcmodel=kernel -fno-builtin -Da64 -Da86 -m64
CFLAG_STACK_PROTECTOR := -fstack-protector-all
LDFLAGS += -Tarch/amd64/linker.ld 			\
	-fno-pic -fno-pie 						\
	-Wl,-static,--no-dynamic-linker,-ztext 	\
	-zmax-page-size=0x1000					\
	-Wl,-Map kernel.map

else ifeq ($(OSARCH), i386)

CFLAGS += -fno-pic -fno-pie -mno-red-zone -march=pentium \
		  -fno-builtin -Da32 -Da86 -m32
CFLAG_STACK_PROTECTOR := -fstack-protector-all
LDFLAGS += -Tarch/i386/linker.ld 			\
	-fno-pic -fno-pie 						\
	-Wl,-static,--no-dynamic-linker,-ztext 	\
	-zmax-page-size=0x1000					\
	-Wl,-Map kernel.map

else ifeq ($(OSARCH), aarch64)

CFLAGS += -fno-builtin -Wstack-protector -Daa64 -fPIC -mno-outline-atomics
CFLAG_STACK_PROTECTOR := -fstack-protector-all
LDFLAGS += -Tarch/aarch64/linker.ld -fPIC -pie 	\
	-Wl,-static,--no-dynamic-linker,-ztext 		\
	-zmax-page-size=0x1000 						\
	-Wl,-Map kernel.map

endif

# -finstrument-functions for __cyg_profile_func_enter & __cyg_profile_func_exit. Used for profiling and debugging.
ifeq ($(DEBUG), 1)
#	CFLAGS += --coverage
#	CFLAGS += -pg
#	CFLAGS += -finstrument-functions
	CFLAGS += -DDEBUG -ggdb3 -O0 -fdiagnostics-color=always -fstack-usage -fsanitize=undefined
ifeq ($(OSARCH), amd64)
	CFLAGS += -fverbose-asm
endif
ifneq ($(OSARCH), aarch64)
	CFLAGS += -fstack-check
endif
	LDFLAGS += -ggdb3 -O0
	ASFLAGS += -g --gstabs+ --gdwarf-5 -D
	WARNCFLAG += -Wno-unused-function -Wno-maybe-uninitialized -Wno-builtin-declaration-mismatch -Wno-unknown-pragmas -Wno-unused-parameter -Wno-unused-variable
endif

default:
	$(error Please specify a target)

prepare:
	$(info Nothing to prepare)

build: $(KERNEL_FILENAME)

dump:
ifeq (,$(wildcard $(KERNEL_FILENAME)))
	$(error $(KERNEL_FILENAME) does not exist)
endif
	$(info Dumping $(KERNEL_FILENAME) in AT T syntax...)
	$(OBJDUMP) -D -g -s -d $(KERNEL_FILENAME) > kernel_dump.map
	$(info Dumping $(KERNEL_FILENAME) in Intel syntax...)
	$(OBJDUMP) -M intel -D -g -s -d $(KERNEL_FILENAME) > kernel_dump_intel.map

$(KERNEL_FILENAME): $(OBJ)
	$(info Linking $@)
	$(CC) $(LDFLAGS) $(OBJ) -o $@
#	$(CC) $(LDFLAGS) $(OBJ) -mno-red-zone -lgcc -o $@

%.o: %.c $(HEADERS)
	$(info Compiling $<)
	$(CC) $(CFLAGS) $(CFLAG_STACK_PROTECTOR) $(WARNCFLAG) -std=c17 -c $< -o $@

# https://gcc.gnu.org/projects/cxx-status.html
%.o: %.cpp $(HEADERS)
	$(info Compiling $<)
	$(CPP) $(CFLAGS) -fcoroutines $(CFLAG_STACK_PROTECTOR) $(WARNCFLAG) -std=c++20 -c $< -o $@ -fno-rtti

%.o: %.S
	$(info Compiling $<)
	$(AS) $(ASFLAGS) -c $< -o $@

%.o: %.s
	$(info Compiling $<)
	$(AS) $(ASFLAGS) -c $< -o $@

%.o: %.psf
ifeq ($(OSARCH), amd64)
	$(OBJCOPY) -O elf64-x86-64 -I binary $< $@
else ifeq ($(OSARCH), i386)
	$(OBJCOPY) -O elf32-i386 -I binary $< $@
else ifeq ($(OSARCH), aarch64)
	$(OBJCOPY) -O elf64-littleaarch64 -I binary $< $@
endif
	$(NM) $@

%.o: %.bmp
ifeq ($(OSARCH), amd64)
	$(OBJCOPY) -O elf64-x86-64 -I binary $< $@
else ifeq ($(OSARCH), i386)
	$(OBJCOPY) -O elf32-i386 -I binary $< $@
else ifeq ($(OSARCH), aarch64)
	$(OBJCOPY) -O elf64-littlearch64 -I binary $< $@
endif
	$(NM) $@

clean:
	rm -f kernel.map kernel_dump.map kernel_dump_intel.map $(OBJ) $(STACK_USAGE_OBJ) $(GCNO_OBJ) $(KERNEL_FILENAME)
