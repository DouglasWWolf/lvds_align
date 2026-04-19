#include "registers.h"
#include <cstring>
#include <cstdarg>
#include <stdexcept>

//=================================================================================================
// throw_runtime() - Throws a runtime exception
//=================================================================================================
static void throw_runtime(const char* fmt, ...)
{
    char buffer[1024];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buffer, fmt, ap);
    va_end(ap);

    throw std::runtime_error(buffer);
}
//=================================================================================================



//=================================================================================================
// write() - Store a 32-bit or 64-bit value
//=================================================================================================
void CRegisters::write(uint64_t reg, uint64_t value)
{
    // Make sure we were passed a legal register descriptor
    if (reg == 0xFFFFFFFF) throw_runtime("undefined register");

    // Break the register definition into an offset and a descriptor
    uint32_t reg_offset = reg & 0xFFFFFFFF;
    uint32_t descriptor = reg >> 32;

    // Store an ordinary 32-bit value
    if (descriptor == 0x00000000 || descriptor == 0x20000000)
    {
        *(uint32_t*)(base_addr_ + reg_offset) = (uint32_t)value;
        return;
    }

    // Store an ordinary 64-bit value
    if (descriptor == 0x40000000)
    {
        *(uint32_t*)(base_addr_ + reg_offset + 0) = (uint32_t)(value >> 32);
        *(uint32_t*)(base_addr_ + reg_offset + 4) = (uint32_t)value;
        return;
    }

    // If we get here, we didn't understand the register descriptor
    throw_runtime("Bad register descriptor 0x%lX", reg);    
}
//=================================================================================================


//=================================================================================================
// read() - Returns a 32-bit or 64-bit value from a register
//=================================================================================================
uint64_t CRegisters::read(uint64_t reg)
{
    // Make sure we were passed a legal register descriptor
    if (reg == 0xFFFFFFFF) throw_runtime("undefined register");

    // Break the register definition into an offset and a descriptor
    uint32_t reg_offset = reg & 0xFFFFFFFF;
    uint32_t descriptor = reg >> 32;

    // Read an ordinary 32-bit value
    if (descriptor == 0x00000000 || descriptor == 0x20000000)
    {
        return *(uint32_t*)(base_addr_ + reg_offset);
    }

    // Read an ordinary 64-bit value
    if (descriptor == 0x40000000)
    {
        uint64_t hi = *(uint32_t*)(base_addr_ + reg_offset + 0);
        uint64_t lo = *(uint32_t*)(base_addr_ + reg_offset + 4);
        return (hi << 32) | lo;
    }

    // If we get here, we didn't understand the register type
    throw_runtime("Bad register descriptor 0x%lX", reg);

    // This is just here to keep the compiler happy
    return 0;
}
//=================================================================================================


//=================================================================================================
// get_ptr() - Returns a userspace pointer to the 32-bit register
//=================================================================================================
uint32_t* CRegisters::get_ptr(uint64_t reg)
{
    return (uint32_t*)(base_addr_ + (reg & 0xFFFFFFFF));
}
//=================================================================================================
