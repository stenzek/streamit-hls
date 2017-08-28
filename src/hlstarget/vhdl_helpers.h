#pragma once
#include <string>
#include "common/types.h"
#include "core/type.h"

namespace HLSTarget
{

namespace VHDLHelpers
{
constexpr const char* HLS_VARIABLE_PREFIX = "llvm_cbe_";
constexpr const char* FIFO_COMPONENT_NAME = "fifo";
constexpr u32 FIFO_SIZE_MULTIPLIER = 4;
u32 GetBitWidthForType(const Type* type);
std::string GetVHDLBitVectorType(const Type* type);
}

} // namespace HLSTarget