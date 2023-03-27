#include <kconfig.hpp>

#include <convert.h>
#include <cargs.h>
#include <printf.h>
#include <targp.h>

#include "kernel.h"

// TODO: Implement proper fprintf
EXTERNC void fprintf(FILE *stream, const char *Format, ...)
{
    va_list args;
    va_start(args, Format);
    vprintf(Format, args);
    va_end(args);
    UNUSED(stream);
}

// TODO: Implement proper fputs
EXTERNC void fputs(const char *s, FILE *stream)
{
    printf("%s", s);
    UNUSED(stream);
}

static struct cag_option ConfigOptions[] = {
    {.identifier = 'a',
     .access_letters = "aA",
     .access_name = "alloc",
     .value_name = "TYPE",
     .description = "Memory allocator to use"},

    {.identifier = 'c',
     .access_letters = "cC",
     .access_name = "cores",
     .value_name = "VALUE",
     .description = "Number of cores to use (0 = all; 1 is the first code, not 0)"},

    {.identifier = 'p',
     .access_letters = "pP",
     .access_name = "ioapicirq",
     .value_name = "VALUE",
     .description = "Which core will be used for I/O APIC interrupts"},

    {.identifier = 't',
     .access_letters = "tT",
     .access_name = "tasking",
     .value_name = "MODE",
     .description = "Tasking mode (multi, single)"},

    {.identifier = 'd',
     .access_letters = "dD",
     .access_name = "drvdir",
     .value_name = "PATH",
     .description = "Directory to load drivers from"},

    {.identifier = 'i',
     .access_letters = "iI",
     .access_name = "init",
     .value_name = "PATH",
     .description = "Path to init program"},

    {.identifier = 'l',
     .access_letters = NULL,
     .access_name = "udl",
     .value_name = "BOOL",
     .description = "Unlock the deadlock after 10 retries"},

    {.identifier = 'o',
     .access_letters = NULL,
     .access_name = "ioc",
     .value_name = "BOOL",
     .description = "Enable Interrupts On Crash. If enabled, the navigation keys will be enabled on crash"},

    {.identifier = 's',
     .access_letters = NULL,
     .access_name = "simd",
     .value_name = "BOOL",
     .description = "Enable SIMD instructions"},

    {.identifier = 'b',
     .access_letters = NULL,
     .access_name = "bootanim",
     .value_name = "BOOL",
     .description = "Enable boot animation"},

    {.identifier = 'h',
     .access_letters = "h",
     .access_name = "help",
     .value_name = NULL,
     .description = "Show help on screen and halt"}};

void ParseConfig(char *ConfigString, KernelConfig *ModConfig)
{
    if (ConfigString == NULL)
    {
        KPrint("Empty kernel parameters!");
        return;
    }
    else if (strlen(ConfigString) == 0)
    {
        KPrint("Empty kernel parameters!");
        return;
    }

    if (ModConfig == NULL)
    {
        KPrint("ModConfig is NULL!");
        return;
    }

    KPrint("Kernel parameters: %s", ConfigString);
    debug("Kernel parameters: %s", ConfigString);

    char *argv[32];
    int argc = 0;
    /* Not sure if the quotes are being parsed correctly. */
    targp_parse(ConfigString, argv, &argc);

#ifdef DEBUG
    for (int i = 0; i < argc; i++)
        debug("argv[%d] = %s", i, argv[i]);
    debug("argc = %d", argc);
#endif

    char identifier;
    const char *value;
    cag_option_context context;

    cag_option_prepare(&context, ConfigOptions, CAG_ARRAY_SIZE(ConfigOptions), argc, argv);

    while (cag_option_fetch(&context))
    {
        identifier = cag_option_get(&context);
        switch (identifier)
        {
        case 'a':
        {
            value = cag_option_get_value(&context);
            if (strcmp(value, "xallocv1") == 0)
            {
                KPrint("\eAAFFAAUsing XallocV1 as memory allocator");
                ModConfig->AllocatorType = Memory::MemoryAllocatorType::XallocV1;
            }
            else if (strcmp(value, "liballoc11") == 0)
            {
                KPrint("\eAAFFAAUsing Liballoc11 as memory allocator");
                ModConfig->AllocatorType = Memory::MemoryAllocatorType::liballoc11;
            }
            else if (strcmp(value, "pages") == 0)
            {
                KPrint("\eAAFFAAUsing Pages as memory allocator");
                ModConfig->AllocatorType = Memory::MemoryAllocatorType::Pages;
            }
            else
            {
                KPrint("\eAAFFAAUnknown memory allocator: %s", value);
                ModConfig->AllocatorType = Memory::MemoryAllocatorType::None;
            }
            break;
        }
        case 'c':
        {
            value = cag_option_get_value(&context);
            KPrint("\eAAFFAAUsing %s cores", atoi(value) ? value : "all");
            ModConfig->Cores = atoi(value);
            break;
        }
        case 'p':
        {
            value = cag_option_get_value(&context);
            KPrint("\eAAFFAARedirecting I/O APIC interrupts to %s%s", atoi(value) ? "core " : "", atoi(value) ? value : "BSP");
            ModConfig->IOAPICInterruptCore = atoi(value);
            break;
        }
        case 't':
        {
            value = cag_option_get_value(&context);
            if (strcmp(value, "multi") == 0)
            {
                KPrint("\eAAFFAAUsing Multi-Tasking Scheduler");
                ModConfig->SchedulerType = 1;
            }
            else if (strcmp(value, "single") == 0)
            {
                KPrint("\eAAFFAAUsing Mono-Tasking Scheduler");
                ModConfig->SchedulerType = 0;
            }
            else
            {
                KPrint("\eAAFFAAUnknown scheduler: %s", value);
                ModConfig->SchedulerType = 0;
            }
            break;
        }
        case 'd':
        {
            value = cag_option_get_value(&context);
            strncpy(ModConfig->DriverDirectory, value, strlen(value));
            KPrint("\eAAFFAAUsing %s as driver directory", value);
            break;
        }
        case 'i':
        {
            value = cag_option_get_value(&context);
            strncpy(ModConfig->InitPath, value, strlen(value));
            KPrint("\eAAFFAAUsing %s as init program", value);
            break;
        }
        case 'o':
        {
            value = cag_option_get_value(&context);
            strcmp(value, "true") == 0 ? ModConfig->InterruptsOnCrash = true : ModConfig->InterruptsOnCrash = false;
            KPrint("\eAAFFAAInterrupts on crash: %s", value);
            break;
        }
        case 'l':
        {
            value = cag_option_get_value(&context);
            strcmp(value, "true") == 0 ? ModConfig->UnlockDeadLock = true : ModConfig->UnlockDeadLock = false;
            KPrint("\eAAFFAAUnlocking the deadlock after 10 retries");
            break;
        }
        case 's':
        {
            value = cag_option_get_value(&context);
            strcmp(value, "true") == 0 ? ModConfig->SIMD = true : ModConfig->SIMD = false;
            KPrint("\eAAFFAASingle Instruction, Multiple Data (SIMD): %s", value);
            break;
        }
        case 'b':
        {
            value = cag_option_get_value(&context);
            strcmp(value, "true") == 0 ? ModConfig->BootAnimation = true : ModConfig->BootAnimation = false;
            KPrint("\eAAFFAABoot animation: %s", value);
            break;
        }
        case 'h':
        {
            KPrint("\n---------------------------------------------------------------------------\nUsage: kernel.fsys [OPTION]...\nKernel configuration.");
            cag_option_print(ConfigOptions, CAG_ARRAY_SIZE(ConfigOptions), nullptr);
            KPrint("\eFF2200System Halted.");
            CPU::Stop();
        }
        default:
        {
            KPrint("\eFF2200Unknown option: %c", identifier);
            break;
        }
        }
    }
    debug("Config loaded");
}
