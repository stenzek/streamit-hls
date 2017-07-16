#pragma once
#include <memory>
#include <string>
#include <vector>

class ParserState;

class Type
{
public:
  enum class BaseTypeId
  {
    Error,
    Void,
    Boolean,
    Bit,
    Int,
    Float,
    Struct
  };

public:
  ~Type() = default;

  const std::string& GetName() const;
  const Type* GetBaseType() const;
  const BaseTypeId GetBaseTypeId() const;
  const std::vector<int>& GetArraySizes() const;
  bool IsValid() const;

  // Helper methods
  bool IsErrorType() const;
  bool IsArrayType() const;
  bool HasBooleanBase() const;
  bool HasBitBase() const;
  bool HasIntBase() const;
  bool HasFloatBase() const;
  bool HasStructBase() const;
  bool IsVoid() const;
  bool IsBoolean() const;
  bool IsBit() const;
  bool IsInt() const;
  bool IsFloat() const;

  bool CanImplicitlyConvertTo(const Type* type) const;
  bool HasSameArrayTraits(const Type* type) const;

  static Type* CreatePrimitiveType(const std::string& name, BaseTypeId base);
  static Type* CreateArrayType(const Type* base_type, const std::vector<int>& array_sizes);

  static const Type* GetResultType(ParserState* state, const Type* lhs, const Type* rhs);
  static const Type* GetArrayElementType(ParserState* state, const Type* ty);

private:
  Type() = default;

  std::string m_name;
  const Type* m_base_type = nullptr;
  BaseTypeId m_base_type_id = BaseTypeId::Error;
  std::vector<int> m_array_sizes;
};
