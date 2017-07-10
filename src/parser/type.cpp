#include "parser/type.h"

Type::Type(const char* name, const char* llvm_name) : m_name(name), m_llvm_name(llvm_name)
{
}

Type::~Type()
{
}

bool Type::CanImplicitlyConvertTo(const Type* type) const
{
  // Same type, no conversion needed.
  if (type == this)
  {
    // Except error->error, just fail early here
    return (type != GetErrorType());
  }

  // int->float, int->bit can convert implicitly
  return (this == GetIntegerType() && (type == GetFloatType() || type == GetBitType()));
}

const Type* Type::GetResultType(const Type* lhs, const Type* rhs)
{
  // same type -> same type
  if (lhs == rhs)
    return lhs;

  // int + float -> float
  if ((lhs == GetIntegerType() || rhs == GetIntegerType()) && (lhs == GetFloatType() || rhs == GetFloatType()))
  {
    return GetFloatType();
  }

  return GetErrorType();
}

static const Type s_error_type{"error", ""};
static const Type s_boolean_type{"boolean", "boolean"};
static const Type s_bit_type{"bit", "u8"};
static const Type s_integer_type{"integer", "i32"};
static const Type s_float_type{"float", "f32"};

const Type* Type::GetErrorType()
{
  return &s_error_type;
}

const Type* Type::GetBooleanType()
{
  return &s_boolean_type;
}

const Type* Type::GetBitType()
{
  return &s_bit_type;
}

const Type* Type::GetIntegerType()
{
  return &s_integer_type;
}

const Type* Type::GetFloatType()
{
  return &s_float_type;
}
