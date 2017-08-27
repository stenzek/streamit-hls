#include "hlstarget/vhdl_helpers.h"
#include "common/log.h"
#include "common/string_helpers.h"
Log_SetChannel(HLSTarget::VHDLHelpers);

namespace HLSTarget
{

u32 VHDLHelpers::GetBitWidthForType(const Type* type)
{
  if (type->IsInt())
    return 32;
  else if (type->IsBit() || type->IsBoolean())
    return 8;

  Log_ErrorPrintf("Unknown type for bit width %s", type->GetName().c_str());
  return 1;
}

std::string VHDLHelpers::GetVHDLBitVectorType(const Type* type)
{
  return StringFromFormat("std_logic_vector(%u downto 0)", GetBitWidthForType(type) - 1);
}

} // namespace HLSTarget
