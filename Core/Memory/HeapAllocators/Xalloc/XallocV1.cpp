#include "Xalloc.hpp"

Xalloc_def;

#define XALLOC_CONCAT(x, y) x##y
#define XStoP(x) (x / Xalloc_PAGE_SIZE + 1)
#define XPtoS(x) (x * Xalloc_PAGE_SIZE)
#define Xalloc_BlockChecksum 0xA110C

extern "C" void *Xalloc_REQUEST_PAGES(Xsize_t Pages);
extern "C" void Xalloc_FREE_PAGES(void *Address, Xsize_t Pages);
extern "C" void Xalloc_MAP_MEMORY(void *VirtualAddress, void *PhysicalAddress, Xsize_t Flags);
extern "C" void Xalloc_UNMAP_MEMORY(void *VirtualAddress);

// TODO: Change memcpy with an optimized version
void *Xmemcpy(void *__restrict__ Destination, const void *__restrict__ Source, Xuint64_t Length)
{
    unsigned char *dst = (unsigned char *)Destination;
    const unsigned char *src = (const unsigned char *)Source;
    for (Xuint64_t i = 0; i < Length; i++)
        dst[i] = src[i];
    return Destination;
}

// TODO: Change memset with an optimized version
void *Xmemset(void *__restrict__ Destination, int Data, Xuint64_t Length)
{
    unsigned char *Buffer = (unsigned char *)Destination;
    for (Xuint64_t i = 0; i < Length; i++)
        Buffer[i] = (unsigned char)Data;
    return Destination;
}

namespace Xalloc
{
    class Block
    {
    public:
        void *Address = nullptr;

        int Checksum = Xalloc_BlockChecksum;
        Xsize_t Size = 0;
        Block *Next = nullptr;
        Block *Last = nullptr;
        bool IsFree = true;

        bool Check()
        {
            if (this->Checksum != Xalloc_BlockChecksum)
                return false;
            return true;
        }

        Block(Xsize_t Size)
        {
            this->Size = Size;
            this->Address = Xalloc_REQUEST_PAGES(XStoP(Size));
        }

        ~Block()
        {
            Xalloc_FREE_PAGES(this->Address, XStoP(this->Size));
        }

        /**
         * @brief Overload new operator to allocate memory from the heap
         * @param Size Unused
         * @return void* Pointer to the allocated memory
         */
        void *operator new(Xsize_t Size)
        {
            void *ptr = Xalloc_REQUEST_PAGES(XStoP(sizeof(Block)));
            return ptr;
        }

        /**
         * @brief Overload delete operator to free memory from the heap
         * @param Address Pointer to the memory to free
         */
        void operator delete(void *Address)
        {
            Xalloc_FREE_PAGES(Address, XStoP(sizeof(Block)));
        }
    } __attribute__((packed, aligned((16))));

    class SmartSMAPClass
    {
    private:
        V1 *allocator = nullptr;

    public:
        SmartSMAPClass(V1 *allocator)
        {
            this->allocator = allocator;
            this->allocator->Xstac();
        }
        ~SmartSMAPClass() { this->allocator->Xclac(); }
    };
#define SmartSMAP SmartSMAPClass XALLOC_CONCAT(SmartSMAP##_, __COUNTER__)(this)

    void V1::Xstac()
    {
        if (this->SMAPUsed)
        {
#if defined(__amd64__) || defined(__i386__)
            asm volatile("stac" ::
                             : "cc");
#endif
        }
    }

    void V1::Xclac()
    {
        if (this->SMAPUsed)
        {
#if defined(__amd64__) || defined(__i386__)
            asm volatile("clac" ::
                             : "cc");
#endif
        }
    }

    void V1::Arrange()
    {
        Xalloc_err("Arrange() is not implemented yet!");
    }

    void *V1::malloc(Xsize_t Size)
    {
        if (Size == 0)
        {
            Xalloc_warn("Attempted to allocate 0 bytes!");
            return nullptr;
        }

        SmartSMAP;
        Xalloc_lock;

        if (this->FirstBlock == nullptr)
        {
            this->FirstBlock = new Block(Size);
            ((Block *)this->FirstBlock)->IsFree = false;
            ((Block *)this->FirstBlock)->Checksum = Xalloc_BlockChecksum;
            Xmemset(((Block *)this->FirstBlock)->Address, 0, Size);
            Xalloc_unlock;
            return ((Block *)this->FirstBlock)->Address;
        }

        Block *CurrentBlock = ((Block *)this->FirstBlock);
        while (CurrentBlock != nullptr)
        {
            if (CurrentBlock->IsFree && CurrentBlock->Size >= Size)
            {
                CurrentBlock->IsFree = false;
                CurrentBlock->Checksum = Xalloc_BlockChecksum;
                Xmemset(CurrentBlock->Address, 0, Size);
                Xalloc_unlock;
                return CurrentBlock->Address;
            }
            CurrentBlock = CurrentBlock->Next;
        }

        CurrentBlock = ((Block *)this->FirstBlock);
        while (CurrentBlock->Next != nullptr)
            CurrentBlock = CurrentBlock->Next;

        CurrentBlock->Next = new Block(Size);
        ((Block *)CurrentBlock->Next)->Last = CurrentBlock;
        ((Block *)CurrentBlock->Next)->IsFree = false;
        ((Block *)CurrentBlock->Next)->Checksum = Xalloc_BlockChecksum;
        Xmemset(((Block *)CurrentBlock->Next)->Address, 0, Size);
        Xalloc_unlock;
        return ((Block *)CurrentBlock->Next)->Address;
    }

    void V1::free(void *Address)
    {
        if (Address == nullptr)
        {
            Xalloc_warn("Attempted to free a null pointer!");
            return;
        }

        SmartSMAP;
        Xalloc_lock;

        Block *CurrentBlock = ((Block *)this->FirstBlock);
        while (CurrentBlock != nullptr)
        {
            if (CurrentBlock->Address == Address)
            {
                if (CurrentBlock->IsFree)
                {
                    Xalloc_warn("Attempted to free an already freed pointer!");
                    Xalloc_unlock;
                    return;
                }

                CurrentBlock->IsFree = true;
                CurrentBlock->Checksum = Xalloc_BlockChecksum;
                Xalloc_unlock;
                return;
            }
            CurrentBlock = CurrentBlock->Next;
        }

        Xalloc_err("Invalid address.");
        Xalloc_unlock;
    }

    void *V1::calloc(Xsize_t NumberOfBlocks, Xsize_t Size)
    {
        if (NumberOfBlocks == 0 || Size == 0)
        {
            Xalloc_warn("The %s%s%s is 0!",
                        NumberOfBlocks == 0 ? "NumberOfBlocks" : "",
                        NumberOfBlocks == 0 && Size == 0 ? " and " : "",
                        Size == 0 ? "Size" : "");
            return nullptr;
        }

        return this->malloc(NumberOfBlocks * Size);
    }

    void *V1::realloc(void *Address, Xsize_t Size)
    {
        if (Address == nullptr)
            return this->malloc(Size);

        if (Size == 0)
        {
            this->free(Address);
            return nullptr;
        }

        // SmartSMAP;
        // Xalloc_lock;
        // ...
        // Xalloc_unlock;

        // TODO: Implement realloc
        this->free(Address);
        return this->malloc(Size);
    }

    V1::V1(void *BaseVirtualAddress, bool UserMode, bool SMAPEnabled)
    {
        SmartSMAP;
        Xalloc_lock;
        this->SMAPUsed = SMAPEnabled;
        this->UserMapping = UserMode;
        this->BaseVirtualAddress = BaseVirtualAddress;
        Xalloc_unlock;
    }

    V1::~V1()
    {
        SmartSMAP;
        Xalloc_lock;
        Xalloc_trace("Destructor not implemented yet.");
        Xalloc_unlock;
    }
}