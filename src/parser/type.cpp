#include "parser/type.h"
#include <cassert>
#include "parser/helpers.h"
#include "parser/parser_state.h"

const std::string& Type::GetName() const
{
  return m_name;
}

const Type* Type::GetBaseType() const
{
  return m_base_type;
}

const Type::BaseTypeId Type::GetBaseTypeId() const
{
  return m_base_type_id;
}

const std::vector<int>& Type::GetArraySizes() const
{
  return m_array_sizes;
}

bool Type::IsValid() const
{
  return !IsErrorType();
}

bool Type::IsErrorType() const
{
  return m_base_type_id == BaseTypeId::Error;
}

bool Type::IsArrayType() const
{
  return !m_array_sizes.empty();
}

bool Type::HasBooleanBase() const
{
  return m_base_type_id == BaseTypeId::Boolean;
}

bool Type::HasBitBase() const
{
  return m_base_type_id == BaseTypeId::Bit;
}

bool Type::HasIntBase() const
{
  return m_base_type_id == BaseTypeId::Int;
}

bool Type::HasFloatBase() const
{
  return m_base_type_id == BaseTypeId::Float;
}

bool Type::HasStructBase() const
{
  return m_base_type_id == BaseTypeId::Struct;
}

bool Type::IsVoid() const
{
  return m_base_type_id == BaseTypeId::Void;
}

bool Type::IsBoolean() const
{
  return HasBooleanBase() && !IsArrayType();
}

bool Type::IsBit() const
{
  return HasBitBase() && !IsArrayType();
}

bool Type::IsInt() const
{
  return HasIntBase() && !IsArrayType();
}

bool Type::IsFloat() const
{
  return HasFloatBase() && !IsArrayType();
}

bool Type::CanImplicitlyConvertTo(const Type* type) const
{
  // Same type, no conversion needed.
  if (type == this)
  {
    // Except error->error, just fail early here
    return IsValid();
  }

  // int->float, int->bit can convert implicitly
  return (IsInt() && (type->IsFloat() || type->IsBit()));
}

bool Type::HasSameArrayTraits(const Type* type) const
{
  return m_array_sizes == type->m_array_sizes;
}

Type* Type::CreatePrimitiveType(const std::string& name, BaseTypeId base)
{
  Type* ty = new Type();
  ty->m_name = name;
  ty->m_base_type_id = base;
  return ty;
}

Type* Type::CreateArrayType(const Type* base_type, const std::vector<int>& array_sizes)
{
  Type* ty = new Type();
  assert(base_type && !array_sizes.empty() && base_type->IsValid() && !base_type->IsVoid());
  ty->m_base_type = base_type;
  ty->m_base_type_id = base_type->m_base_type_id;
  ty->m_array_sizes = array_sizes;
  ty->m_name = base_type->GetName();
  for (int sz : array_sizes)
    ty->m_name += StringFromFormat("[%d]", sz);
  return ty;
}

const Type* Type::GetResultType(ParserState* state, const Type* lhs, const Type* rhs)
{
  // same type -> same type
  if (lhs == rhs)
    return lhs;

  // int + float -> float
  if ((lhs->IsInt() || rhs->IsInt()) && (lhs->IsFloat() || rhs->IsFloat()))
    return state->GetFloatType();

  return state->GetErrorType();
}

const Type* Type::GetArrayElementType(ParserState* state, const Type* ty)
{
  if (ty->m_array_sizes.empty())
    return state->GetErrorType();

  // Last array index, or single-dimension array
  if (ty->m_array_sizes.size() == 1)
    return ty->m_base_type;

  // Drop one of the arrays off, so int[10][11][12] -> int[10][11].
  std::vector<int> temp = ty->m_array_sizes;
  temp.pop_back();
  return state->GetArrayType(ty->m_base_type, temp);
}
