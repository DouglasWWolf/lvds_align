//========================================
//       This is generated code!
//========================================
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "register_struct.h"

static void eval(registers_t& reg, const char* name, uint64_t descriptor)
{
    if (strcmp(name, "LVDS_CAL_WEN"     ) == 0) {reg.LVDS_CAL_WEN      = descriptor; return;}
    if (strcmp(name, "LVDS_CAL_WORD"    ) == 0) {reg.LVDS_CAL_WORD     = descriptor; return;}
    if (strcmp(name, "LVDS_LANE_SELECT" ) == 0) {reg.LVDS_LANE_SELECT  = descriptor; return;}
    if (strcmp(name, "LVDS_RESET_HSSIO" ) == 0) {reg.LVDS_RESET_HSSIO  = descriptor; return;}
    if (strcmp(name, "LVDS_CLEAR_ERRORS") == 0) {reg.LVDS_CLEAR_ERRORS = descriptor; return;}
    if (strcmp(name, "LVDS_CAL_MASK"    ) == 0) {reg.LVDS_CAL_MASK     = descriptor; return;}
    if (strcmp(name, "LVDS_ALIGN_ERR"   ) == 0) {reg.LVDS_ALIGN_ERR    = descriptor; return;}
    if (strcmp(name, "LVDS_PRBS_ERR"    ) == 0) {reg.LVDS_PRBS_ERR     = descriptor; return;}
    if (strcmp(name, "REG_CHIPIO_ADDR"  ) == 0) {reg.REG_CHIPIO_ADDR   = descriptor; return;}
    if (strcmp(name, "REG_CHIPIO_DATA"  ) == 0) {reg.REG_CHIPIO_DATA   = descriptor; return;}
};

//=============================================================================
// read_register_definitions() - Reads in an FPGA register definiton file and
//                               fills in a registers_t structure with the
//                               values it finds there
//=============================================================================
bool read_register_definitions(registers_t& reg, std::string filename)
{
    char line[1000], *in, *out;
    char name[1000];

    // Open the input file
    FILE* ifile = fopen(filename.c_str(), "r");

    // Complain if we can't
    if (ifile == nullptr) return false;

    // Loop through every line of the input file
    while (fgets(line, sizeof(line), ifile))
    {
        // Skip past any leading whitespace
        in = line;
        while (*in == ' ' || *in == '\t') ++in;

        // Ignore any line that doesn't begin with "#define"
        if (strncmp(in, "#define", 7) != 0) continue;

        // Skip past the #define
        in = in + 7;

        // Skip past whitespace after #define
        while (*in == ' ' || *in == '\t') ++in;

        // Point to the name field
        out = name;

        // Copy the token to "name[]" and nul-terminate it
        while (*in != ' ' && *in != '\t' && *in != 10 && *in != 13) *out++ = *in++;
        *out = 0;

        // If we've hit the end of the line, this isn't a register definition
        if (*in == 10 || *in == 13) continue;

        // Skip past whitespace after register name
        while (*in == ' ' || *in == '\t') ++in;

        // Extract the address of the register
        uint64_t descriptor = strtoull(in, nullptr, 0);

        // Fill in the appropriate field in our register structure
        eval(reg, name, descriptor);
    }

    // We're done with the input file
    fclose(ifile);
    return true;
}
//=============================================================================
