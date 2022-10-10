#include <interrupts.hpp>

#if defined(__amd64__)
#include "../Architecture/amd64/cpu/gdt.hpp"
#include "../Architecture/amd64/cpu/idt.hpp"
#elif defined(__i386__)
#include "../Architecture/i686/cpu/gdt.hpp"
#include "../Architecture/i686/cpu/idt.hpp"
#elif defined(__aarch64__)
#endif

namespace Interrupts
{
    void Initialize()
    {
#if defined(__amd64__)
        GlobalDescriptorTable::Init(0);
        InterruptDescriptorTable::Init(0);
#elif defined(__i386__)
#elif defined(__aarch64__)
#endif
    }

    void Enable()
    {
        
    }
}
