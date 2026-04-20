#include <unistd.h>
#include <cstdio>
#include <cstdarg>
#include <iostream>
#include "registers.h"
#include "PciDevice.h"
#include "register_struct.h"
#include <vector>
using std::vector;
using std::string;

// Manages FPGA registers
CRegisters fpga;

// The PCI bus
PciDevice PCI;

// Command line options
struct opt_t
{
    bool    table   = false;
    bool    strip   = false;
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
// show_help() - Displays help text and exits
//=================================================================================================
void show_help()
{
    printf("lvds_align [-table] [-strip]\n");
    exit(1);
}
//=================================================================================================



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

        if (token == "-strip" || token == "-chart")
        {
            opt.strip = true;
            continue;
        }

        if (token == "-table")
        {
            opt.table = true;
            continue;
        }

        if (token == "-help")
            show_help();

        fprintf(stderr, "bad command line option %s\n", token.c_str());
        exit(1);
    }

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
void show_chart_line(uint32_t cal_word, uint64_t errors, uint64_t lane_mask)
{
    int delay_tap = cal_word & 0x1FF;
    int bit;

    if (delay_tap == 0)
    {
        printf("===========\n");
        printf("Bitslip : %d\n", cal_word >> 9);
        printf("===========\n");
    }

    // Display the calibration word
    printf("0x%03X : ", cal_word);

    // Loop through each lane
    for (int i=0; i<64; ++i)
    {
        // Convert the loop index to a lane
        int lane = 63 - i;

        // If this is a lane we care about fetch the error bit 
        // for this lane, otherwise pretend the lane failed
        if (lane_mask & (1ULL << lane))
        {
            bit = (errors >> lane) & 1;
            printf("%c", bit ? '.' : '#');
        }
        else printf("-");
    }

    // End of the line
    printf("\n");

    if (delay_tap == 0x1FF) printf("\n\n");
}
//=================================================================================================

// This describes the starting cal_word and length of a calibration window
struct window_t
{
    window_t() {start=0; length=0;}
    int start;
    int length;
    int cal() {return start + length/2;}
};

//=================================================================================================
// find_largest_window() - For a given lane, find the longest continuous stretch of usable
//                         calibration values
//=================================================================================================
window_t find_largest_window(vector<uint64_t>& strip_chart, int lane)
{
    window_t best, current;

    // We're not inside a window yet
    bool in_window = false;

    // Loop through each possible calibration word
    for (int cal_word = 0; cal_word < CAL_WORDS; ++cal_word)
    {
        // What delay-tap is this cal-word?
        int delay_tap = cal_word & 0x1FF;

        // Find the bit that corresponds to the specified lane
        int bit = (strip_chart[cal_word] & (1ULL << lane)) != 0;

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

        // Is this the end of the window?
        bool end_of_window = (bit == 1) || (delay_tap == 0x1FF);

        // If we just fell out of a window...
        if (in_window && end_of_window)
        {
            in_window = false;
            if (current.length >= best.length)
                best = current;
        }

    }

    // Hand the caller the best window
    return best;
}
//=================================================================================================


//=================================================================================================
// collect_calibration_data() - Sweeps through every possible calibration word and returns
//                              a vector of error bits (1 per LVDS lane) that indicates whether
//                              an LVDS alignment error was detected on that lane for that
//                              calibration word.
//=================================================================================================
vector<uint64_t> collect_calibration_data(uint64_t lane_mask)
{
    vector<uint64_t> result;

    // Tell the RTL which LVDS lanes we intend to calibrate
    fpga.write(reg.LVDS_CAL_MASK, lane_mask);

    // Loop through every calibration word...
    for (uint32_t cal_word = 0; cal_word < 4096; ++cal_word)
    {
        // Wait for permission to write a new calibration word
        while (fpga.read(reg.LVDS_CAL_WEN) != 7) usleep(1);
        
        // Write the calibration word, and wait for it to take effect
        fpga.write(reg.LVDS_CAL_WORD, cal_word);
        usleep(20);

        // Clear alignment errors, and wait for more to accumulate
        fpga.write(reg.LVDS_CLEAR_ERRORS, 1);
        usleep(250);

        // Find out which lanes aren't aligned at this calibration word
        uint64_t errors = fpga.read(reg.LVDS_ALIGN_ERR); 

        // Fetch the alignment errors
        result.push_back(errors);
    }

    // Hand the resuting table of alignment errors to the caller
    return result;
}
//=================================================================================================




//=================================================================================================
// execute() - The is the mainline program execution
//=================================================================================================
void execute()
{
    window_t best[64];
    const char* filename = "fpga_reg.h";
    int exit_code = 0;
    int lane;

    // Read our definitions file
    if (!read_register_definitions(reg, filename))
        throwRuntime("file not found: %s", filename);

    // Open a connection to our PCI device
    PCI.open("10ee:903f");

    // Tell our registers what their base address in userspace is
    fpga.set_base_addr(PCI.resourceList()[0].baseAddr);

    // We're going to calibrate all lanes on the first pass
    uint64_t lane_mask = 0xFFFFFFFFFFFFFFFF;

    // We're going to make several calibration passes
    for (int attempt=0; attempt < 3; ++attempt)
    {
        // Collect calibration data
        auto strip_chart = collect_calibration_data(lane_mask);

        // If we're supposed to print a strip-chart, do so
        if (opt.strip) for (uint32_t cal_word = 0; cal_word < CAL_WORDS; ++cal_word)
        {
            show_chart_line(cal_word, strip_chart[cal_word], lane_mask);
        }

        // Find the best (i.e., longest) calibration window for each lane
        for (lane=0; lane<64; lane++) if (lane_mask & (1ULL << lane))
        {
            best[lane] = find_largest_window(strip_chart, lane);
        }

        // Loop through each lane and set the calibration word to the 
        // cal_word in the middle of the longest window
        for (lane=0; lane<64; ++lane) if (lane_mask & (1ULL << lane))
        {
            fpga.write(reg.LVDS_CAL_MASK, (1ULL << lane));
            fpga.write(reg.LVDS_CAL_WORD, best[lane].cal());
        }

        // Clear alignment errors and find out what lanes still have errors
        usleep(250);
        fpga.write(reg.LVDS_CLEAR_ERRORS, 1);
        usleep(250);
        lane_mask = fpga.read(reg.LVDS_ALIGN_ERR);

        // If all lanes are aligned, we're done!
        if (lane_mask == 0) break;
    }

    // If the user wants to see a per-lane table, display it
    if (opt.table)
    {
        printf("Lane - Length - Start\n");
        printf("---------------------\n");
        for (lane=0; lane<64; ++lane)
        {
            printf("%4u     %3d    0x%03X\n", lane, best[lane].length, best[lane].start);
        }
    }

    // Show the user lanes that could not be calibrated
    for (lane=0; lane<64; ++lane) if (lane_mask & (1ULL << lane))
    {
        printf("Calibration failed on lane %u\n", lane);
        exit_code = 1;
    }

    // Tell the operating system whether or not we succeeded
    exit(exit_code);
}
//=================================================================================================
