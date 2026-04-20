#pragma once
//========================================
//       This is generated code!
//========================================
#include <cstdint>

struct registers_t
{
    std::uint64_t LVDS_CAL_WEN      = 0xFFFFFFFF;
    std::uint64_t LVDS_CAL_WORD     = 0xFFFFFFFF;
    std::uint64_t LVDS_LANE_SELECT  = 0xFFFFFFFF;
    std::uint64_t LVDS_RESET_HSSIO  = 0xFFFFFFFF;
    std::uint64_t LVDS_CLEAR_ERRORS = 0xFFFFFFFF;
    std::uint64_t LVDS_CAL_MASK     = 0xFFFFFFFF;
    std::uint64_t LVDS_ALIGN_ERR    = 0xFFFFFFFF;
    std::uint64_t LVDS_PRBS_ERR     = 0xFFFFFFFF;
    std::uint64_t REG_CHIPIO_ADDR   = 0xFFFFFFFF;
    std::uint64_t REG_CHIPIO_DATA   = 0xFFFFFFFF;
};

bool read_register_definitions(registers_t& reg, std::string filename = "fpga_reg.h");
