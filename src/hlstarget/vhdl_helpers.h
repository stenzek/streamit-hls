#pragma once
#include <string>
#include "common/types.h"

namespace llvm
{
class Type;
}

namespace HLSTarget
{

namespace VHDLHelpers
{
constexpr const char* TARGET_PART_ID = "xc7a35ticsg324-1l";
constexpr const char* HLS_VARIABLE_PREFIX = "";
constexpr const char* FIFO_COMPONENT_NAME = "fifo";
constexpr const char* FIFO_SRL16_COMPONENT_NAME = "fifo_srl16";
constexpr const char* FIFO_SRL32_COMPONENT_NAME = "fifo_srl32";
constexpr u32 FIFO_SIZE_MULTIPLIER = 4;
u32 GetBitWidthForType(const llvm::Type* type);
std::string GetVHDLBitVectorType(const llvm::Type* type, u32 width = 1);
}

} // namespace HLSTarget