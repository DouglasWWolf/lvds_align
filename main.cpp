#include <unistd.h>
#include <cstdio>
#include <cstdarg>
#include <iostream>
#include "registers.h"
#include "PciDevice.h"
#include "register_struct.h"

using std::string;

// Manages FPGA registers
CRegisters fpga;

// The PCI bus
PciDevice PCI;


// Command line options
struct opt_t
{
    bool    chart  = true;
} opt;


// This is every register this program cares about
registers_t reg;

// Function prototypes
void execute();
void parse_command_line(const char** argv);

int main(int argc, const char** argv)
{
    try
    {
        parse_command_line(argv);
        execute();
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        exit(1);
    }
    
    return 0;
}

//=================================================================================================
// parse_command_line() - Parse the command-line options
//=================================================================================================
void parse_command_line(const char** argv)
{
    int i=1;
    string token;

    while (argv[i])
    {
        token = argv[i++];

        if (token == "-chart")
        {
            opt.chart = true;
            continue;
        }

    }

    // If we made it to the end of the command-line options, all is well
    if (argv[i] == nullptr) return;

    // If we get here, we discovered an invalid option
    fprintf(stderr, "bad command line option %s\n", token.c_str());
    exit(1);
}
//=================================================================================================


//=================================================================================================
// throwRuntime() - Throws a runtime exception
//=================================================================================================
static void throwRuntime(const char* fmt, ...)
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
// show_chart_line() - Show one line from the strip-char
//=================================================================================================
void show_chart_line(uint32_t cal_word, uint64_t errors)
{
    if ((cal_word & 0x1FF) == 0)
    {
        printf("===========\n");
        printf("Bitslip : %d\n", cal_word >> 9);
        printf("===========\n");
    }

    // Display the calibration word
    printf("0x%03X : ", cal_word);

    // Loop through each line
    for (int lane=0; lane<64; ++lane)
    {
        // Get the error bit for this lane
        int bit = (errors >> lane) & 1;

        // Print the error flag for this lane
        printf("%c", bit ? '.' : '#');
    }

    // End of the line
    printf("\n");

    if ((cal_word & 0x1FF) == 0x1FF) printf("\n\n");
}
//=================================================================================================



//=================================================================================================
// execute() - The is the mainline program execution
//=================================================================================================
void execute()
{
    const char* filename = "fpga_reg.h";

    // Read our definitions file
    if (!read_register_definitions(reg, filename))
        throwRuntime("file not found: %s", filename);

    // Open a connection to our PCI device
    PCI.open("10ee:903f");

    // Tell our registers what their base address in userspace is
    fpga.set_base_addr(PCI.resourceList()[0].baseAddr);

    // Writing a cal_word will write it to all LVDS lanes
    fpga.write(reg.LVDS_CAL_MODE, 1);

    // Loop through every calibration word...
    for (uint32_t cal_word = 0; cal_word < 0x1000; ++cal_word)
    {
        // Wait for permission to write a new calibration word
        while (fpga.read(reg.LVDS_CAL_WEN) != 7) usleep(1);
        
        // Write the calibration word
        fpga.write(reg.LVDS_CAL_WORD, cal_word);
        
        // Wait for the calibration word to take effect
        usleep(2);

        // Clear alignment errors
        fpga.write(reg.LVDS_CLR_ALIGN_ERR, 1);

        // Wait just a moment for alignment errors to occur
        usleep(250);

        // Fetch the alignment errors
        uint64_t errors = fpga.read(reg.LVDS_ALIGN_ERR);

        // Display the chart line
        show_chart_line(cal_word, errors);
    }

}
//=================================================================================================
