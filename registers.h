#pragma once
#include <stdint.h>
#include <string>
#include <map>


class CRegisters
{
public:

    // Set the base address of the memory region where our registers live
    void        set_base_addr(unsigned char* base_addr) {base_addr_ = base_addr;}

    // Return a pointer to the specified register
    uint32_t*   get_ptr(uint64_t reg);

    // Write a register
    void        write(uint64_t reg, uint64_t value);

    // Read a register
    uint64_t    read(uint64_t reg);

protected:

    // The base address where our registers live
    unsigned char* base_addr_;
};
