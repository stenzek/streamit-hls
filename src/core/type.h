#pragma once
#include <memory>
#include <string>
#include <vector>

class ParserState;

class Type
{
public:
  enum class TypeId
  {
    Error,
    Void,
    Boolean,
    Bit,
    Int,
    Float,
    APInt,
    Array,
    Struct,
    Function
  };

public:
  virtual ~Type() = default;

  const std::string& GetName() const { return m_name; }
  const Type* GetBaseType() const { return m_base_type; }
  const TypeId GetTypeId() const { return m_type_id; }

  bool IsValid() const;

  // Helper methods
  bool IsPrimitiveType() const;
  bool IsErrorType() const;
  bool IsArrayType() const;
  bool IsStructType() const;
  bool IsFunctionType() const;
  bool HasBooleanBase() const;
  bool HasBitBase() const;
  bool HasIntBase() const;
  bool HasFloatBase() const;
  bool IsVoid() const;
  bool IsBoolean() const;
  bool IsBit() const;
  bool IsInt() const;
  bool IsFloat() const;
  bool IsAPInt() const;

  bool CanImplicitlyConvertTo(const Type* type) const;
  bool HasSameArrayTraits(const Type* type) const;

  static Type* CreatePrimitive(TypeId base);

protected:
  Type() = default;

  std::string m_name;
  TypeId m_type_id = TypeId::Error;
  const Type* m_base_type = nullptr;
};

class ArrayType : public Type
{
public:
  ~ArrayType() = default;

  const std::vector<int>& GetArraySizes() const { return m_array_sizes; }

  static Type* Create(const Type* base_type, const std::vector<int>& array_sizes);

protected:
  ArrayType() = default;

  std::vector<int> m_array_sizes;
};

class FunctionType : public Type
{
public:
  ~FunctionType() = default;

  const Type* GetReturnType() const { return m_return_type; }
  const std::vector<const Type*>& GetParameterTypes() const { return m_parameter_types; }

  static Type* Create(const Type* return_type, const std::vector<const Type*>& param_types);

protected:
  FunctionType() = default;

  const Type* m_return_type = nullptr;
  std::vector<const Type*> m_parameter_types;
};

class APIntType : public Type
{
public:
  ~APIntType() = default;

  unsigned GetNumBits() const { return m_num_bits; }

  static APIntType* Create(unsigned num_bits);

private:
  unsigned m_num_bits = 0;
};