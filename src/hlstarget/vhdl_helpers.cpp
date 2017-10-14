#include "hlstarget/vhdl_helpers.h"
#include "common/log.h"
#include "common/string_helpers.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
Log_SetChannel(HLSTarget::VHDLHelpers);

namespace HLSTarget
{

u32 VHDLHelpers::GetBitWidthForType(const llvm::Type* type)
{
  if (type->isIntegerTy())
    return static_cast<const llvm::IntegerType*>(type)->getBitWidth();
  if (type->isFloatingPointTy())
    return type->getPrimitiveSizeInBits();

  Log_ErrorPrintf("Unknown type for bit width %p", type);
  return 1;
}

std::string VHDLHelpers::GetVHDLBitVectorType(const llvm::Type* type, u32 width)
{
  return StringFromFormat("std_logic_vector(%u downto 0)", (GetBitWidthForType(type) * width) - 1);
}

} // namespace HLSTarget
