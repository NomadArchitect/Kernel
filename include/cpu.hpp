#ifndef __FENNIX_KERNEL_CPU_H__
#define __FENNIX_KERNEL_CPU_H__

#include <types.h>

/**
 * @brief CPU related functions.
 */
namespace CPU
{
    /**
     * @brief Enum for CPU::Interrupts() function.
     */
    enum InterruptsType
    {
        /**
         * @brief Check if interrupts are enabled.
         */
        Check,
        /**
         * @brief Enable interrupts.
         */
        Enable,
        /**
         * @brief Disable interrupts.
         */
        Disable
    };

    /**
     * @brief Pause the CPU
     */
    void Pause();
    /**
     * @brief Halt the CPU
     */
    void Halt();
    /**
     * @brief Check if interrupts are enabled
     *
     * @return true If InterruptsType::Check and interrupts are enabled, or if other InterruptsType were executed successfully
     * @return false If InterruptsType::Check and interrupts are disabled, or if other InterruptsType failed
     */
    bool Interrupts(InterruptsType Type = Check);

    namespace MemBar
    {
        static inline void Barrier()
        {
#if defined(__amd64__) || defined(__i386__)
            asmv("" ::
                     : "memory");
#elif defined(__aarch64__)
            asmv("dmb ish" ::
                     : "memory");
#endif
        }

        static inline void Fence()
        {
#if defined(__amd64__) || defined(__i386__)
            asmv("mfence" ::
                     : "memory");
#elif defined(__aarch64__)
            asmv("dmb ish" ::
                     : "memory");
#endif
        }

        static inline void StoreFence()
        {
#if defined(__amd64__) || defined(__i386__)
            asmv("sfence" ::
                     : "memory");
#elif defined(__aarch64__)
            asmv("dmb ishst" ::
                     : "memory");
#endif
        }

        static inline void LoadFence()
        {
#if defined(__amd64__) || defined(__i386__)
            asmv("lfence" ::
                     : "memory");
#elif defined(__aarch64__)
            asmv("dmb ishld" ::
                     : "memory");
#endif
        }
    }

    namespace x86
    {
        static inline void lgdt(void *gdt)
        {
#if defined(__amd64__) || defined(__i386__)
            asmv("lgdt (%0)"
                 :
                 : "r"(gdt));
#endif
        }

        static inline void lidt(void *idt)
        {
#if defined(__amd64__) || defined(__i386__)
            asmv("lidt (%0)"
                 :
                 : "r"(idt));
#endif
        }

        static inline void ltr(uint16_t Segment)
        {
#if defined(__amd64__) || defined(__i386__)
            asmv("ltr %0"
                 :
                 : "r"(Segment));
#endif
        }

        static inline void invlpg(void *Address)
        {
#if defined(__amd64__) || defined(__i386__)
            asmv("invlpg (%0)"
                 :
                 : "r"(Address)
                 : "memory");
#endif
        }
    }
}

#endif // !__FENNIX_KERNEL_CPU_H__