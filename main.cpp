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
    bool    verbose = false;
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
    printf("lvds_align [-table] [-strip] [-verbose]\n");
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

        if (token == "-verbose")
        {
            opt.verbose = true;
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
    for (int i=0; i<64; ++i)
    {
        // Convert the loop index to a lane
        int lane = 63 - i;

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
        int delay_tap = cal_word & 0x1FF;

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
vector<uint64_t> collect_calibration_data(int lane)
{
    vector<uint64_t> result;

    // If we're collecting data for all lanes, enter calibration mode
    fpga.write(reg.LVDS_CAL_MODE, (lane == -1));

    // If the caller is interested in a particular lane, select that lane
    if (lane != -1) fpga.write(reg.LVDS_LANE_SELECT, lane);

    // Loop through every calibration word...
    for (uint32_t cal_word = 0; cal_word < 4096; ++cal_word)
    {
        // Wait for permission to write a new calibration word
        while (fpga.read(reg.LVDS_CAL_WEN) != 7) usleep(1);
        
        // Write the calibration word
        fpga.write(reg.LVDS_CAL_WORD, cal_word);
        
        // Wait for the calibration word to take effect
        usleep(20);

        // Clear alignment errors
        fpga.write(reg.LVDS_CLEAR_ERRORS, 1);

        // Wait just a moment for alignment errors to occur
        usleep(400);

        // Fetch the alignment errors
        volatile uint64_t errors = fpga.read(reg.LVDS_ALIGN_ERR);
        result.push_back((uint64_t)errors);
    }

    // Writing a cal_word will affect only the selected lane
    fpga.write(reg.LVDS_CAL_MODE, 0);

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
    int lane;
    int exit_code = 0;
    uint64_t errors;

    // Read our definitions file
    if (!read_register_definitions(reg, filename))
        throwRuntime("file not found: %s", filename);

    // Open a connection to our PCI device
    PCI.open("10ee:903f");

    // Tell our registers what their base address in userspace is
    fpga.set_base_addr(PCI.resourceList()[0].baseAddr);

    // Collect calibration data
    auto strip_chart = collect_calibration_data(-1);

    // If we're supposed to print a strip-chart, do so
    if (opt.strip) for (uint32_t cal_word = 0; cal_word < CAL_WORDS; ++cal_word)
    {
        show_chart_line(cal_word, strip_chart[cal_word]);
    }

    // Find the best (i.e., longest) calibration window for each lane
    for (lane=0; lane<64; lane++)
    {
         best[lane] = find_largest_window(strip_chart, lane);

         if (best[lane].length == 0)
         {
            fprintf(stderr,"Lane %d could not be calibrated!\n", lane);
            exit_code = 1;
            continue;
         }

         if (best[lane].length < 10)
         {
            fprintf(stderr,"Lane %d has very short window %d !\n", lane, best[lane].length);
            exit_code = 1;
         }
    }

    // If the user wants to see a per-lane table, display it
    if (opt.table)
    {
        printf("Lane - Length - Start\n");
        printf("---------------------\n");
        for (lane=0; lane<64; ++lane)
        {
            window_t& best_win = best[lane];
            printf("%4d     %3d    0x%03X\n", lane, best_win.length, best_win.start);
        }
    }


    // Loop through each line and set the calibration word to the 
    // cal_word in the middle of the longest window
    for (lane=0; lane<64; ++lane)
    {
        fpga.write(reg.LVDS_LANE_SELECT, lane);
        fpga.write(reg.LVDS_CAL_WORD, best[lane].cal());
    }

    // Clear alignment errors and find out what lanes still have errors
    usleep(250);
    fpga.write(reg.LVDS_CLEAR_ERRORS, 1);
    usleep(250);
    errors = fpga.read(reg.LVDS_ALIGN_ERR);
    
    // Attempt to recalibrate lanes that have errors
    for (lane=0; lane<64; ++lane) if ((errors >> lane) & 1)
    {
        if (opt.verbose)
        {   printf("Recalibrating lane %2d : ", lane);
            printf("Orig = 0x%03X  ", best[lane].cal());
            fflush(stdout);
        }
        
        strip_chart = collect_calibration_data(lane);
        best[lane] = find_largest_window(strip_chart, lane);
        fpga.write(reg.LVDS_LANE_SELECT, lane);
        fpga.write(reg.LVDS_CAL_WORD, best[lane].cal());
        
        if (opt.verbose) printf("New = 0x%03X\n", best[lane].cal());        
    }


    // Clear alignment errors and find out what lanes still have errors
    usleep(250);
    fpga.write(reg.LVDS_CLEAR_ERRORS, 1);
    usleep(250);
    errors = fpga.read(reg.LVDS_ALIGN_ERR);

    // Show the user lanes that could not be calibrated
    for (lane=0; lane<64; ++lane) if ((errors >> lane) & 1)
    {
        printf("Calibration failed on lane %d\n", lane);
        exit_code = 1;
    }


    // Tell the operating system whether or not we succeeded
    exit(exit_code);
}
//=================================================================================================
