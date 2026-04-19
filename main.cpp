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


// This total number of possible calibration words
const uint32_t CAL_WORDS = 0x1000;

// One bit per LVDS lane per cal_word
uint64_t strip_chart[CAL_WORDS];

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

// This describes the starting cal_word and length of a calibration window
struct window_t
{
    int start;
    int length;
};

//=================================================================================================
// find_largest_window() - For a given lane, find the longest continuous stretch of usable
//                         calibration values
//=================================================================================================
window_t find_largest_window(int lane)
{
    window_t best = {0,0};

    // We're not inside a window yet
    bool in_window = 0;
    
    // The length of the current window
    window_t current = {0,0};

    // Loop through each possible calibration word
    for (int cal_word = 0; cal_word < CAL_WORDS; ++cal_word)
    {
        // Find the bit that corresponds to the specified lane
        int bit = (strip_chart[cal_word] >> lane) & 1;

        // If this cal_word was a valid calibration...
        if (bit == 0)
        {
            if (in_window)
                current.length++;
            else
            {
                in_window      = true;
                current.start  = cal_word;
                current.length = 1;                
            }
        }

        // Otherwise, if we just fell out of a window...
        else if (in_window)
        {
            in_window = false;
            if (current.length >= best.length)
                best = current;
        }

    }

    // If we were inside a window, check to see if this is the best one
    if (in_window && current.length >= best.length)
        best = current;

    // Hand the caller the best window
    return best;
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
    for (uint32_t cal_word = 0; cal_word < CAL_WORDS; ++cal_word)
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
        strip_chart[cal_word] = fpga.read(reg.LVDS_ALIGN_ERR);

        // Display the chart line
        if (opt.chart) show_chart_line(cal_word, strip_chart[cal_word]);
    }

    for (int lane=0; lane<64; ++lane)
    {
        window_t best = find_largest_window(lane);
        printf("Lane %2d: %3d 0x%03X\n", lane, best.length, best.start);        
    }

}
//=================================================================================================
