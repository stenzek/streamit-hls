#include "core/type.h"
#include <cassert>
#include "common/string_helpers.h"

bool Type::IsValid() const
{
  return !IsErrorType();
}

bool Type::IsPrimitiveType() const
{
  return m_type_id >= TypeId::Error && m_type_id <= TypeId::Float;
}

bool Type::IsErrorType() const
{
  return m_type_id == TypeId::Error;
}

bool Type::IsArrayType() const
{
  return m_type_id == TypeId::Array;
}

bool Type::IsStructType() const
{
  return m_type_id == TypeId::Struct;
}

bool Type::IsFunctionType() const
{
  return m_type_id == TypeId::Function;
}

bool Type::HasBooleanBase() const
{
  return m_type_id == TypeId::Boolean || (m_base_type && m_base_type->HasBooleanBase());
}

bool Type::HasBitBase() const
{
  return m_type_id == TypeId::Bit || (m_base_type && m_base_type->HasBitBase());
}

bool Type::HasIntBase() const
{
  return m_type_id == TypeId::Int || (m_base_type && m_base_type->HasIntBase());
}

bool Type::HasFloatBase() const
{
  return m_type_id == TypeId::Float || (m_base_type && m_base_type->HasFloatBase());
}

bool Type::IsVoid() const
{
  return m_type_id == TypeId::Void;
}

bool Type::IsBoolean() const
{
  return m_type_id == TypeId::Boolean;
}

bool Type::IsBit() const
{
  return m_type_id == TypeId::Bit;
}

bool Type::IsInt() const
{
  return m_type_id == TypeId::Int;
}

bool Type::IsFloat() const
{
  return m_type_id == TypeId::Float;
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
  if (m_type_id == TypeId::Array && type->m_type_id == TypeId::Array)
  {
    return static_cast<const ArrayType*>(this)->GetArraySizes() == static_cast<const ArrayType*>(type)->GetArraySizes();
  }

  // If one type is not an array, then they're incompatible
  return !((m_type_id == TypeId::Array && type->m_type_id != TypeId::Array) ||
           (m_type_id != TypeId::Array && type->m_type_id == TypeId::Array));
}

Type* Type::CreatePrimitive(TypeId base)
{
  const char* name;
  switch (base)
  {
  case TypeId::Error:
    name = "<error>";
    break;
  case TypeId::Void:
    name = "void";
    break;
  case TypeId::Boolean:
    name = "boolean";
    break;
  case TypeId::Bit:
    name = "bit";
    break;
  case TypeId::Int:
    name = "int";
    break;
  case TypeId::Float:
    name = "float";
    break;
  default:
    assert(0 && "unknown primitive type id");
    return nullptr;
  }

  Type* ty = new Type();
  ty->m_name = name;
  ty->m_type_id = base;
  return ty;
}

Type* ArrayType::Create(const Type* base_type, const std::vector<int>& array_sizes)
{
  ArrayType* ty = new ArrayType();
  assert(base_type && !array_sizes.empty() && base_type->IsValid() && !base_type->IsVoid());
  ty->m_base_type = base_type;
  ty->m_type_id = TypeId::Array;
  ty->m_array_sizes = array_sizes;
  ty->m_name = base_type->GetName();
  for (int sz : array_sizes)
    ty->m_name += StringFromFormat("[%d]", sz);
  return ty;
}

Type* FunctionType::Create(const Type* return_type, const std::vector<const Type*>& param_types)
{
  FunctionType* ty = new FunctionType();
  ty->m_return_type = return_type;
  ty->m_parameter_types = param_types;
  return ty;
}
