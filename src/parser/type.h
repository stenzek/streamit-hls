#pragma once
#include <string>

class Type
{
public:
  Type(const char* name, const char* llvm_name);
  ~Type();

  const std::string& GetName() const
  {
    return m_name;
  }
  const std::string& GetLLVMName() const
  {
    return m_llvm_name;
  }
  bool IsValid() const
  {
    return !m_llvm_name.empty();
  }

  bool CanImplicitlyConvertTo(const Type* type) const;

  static const Type* GetResultType(const Type* lhs, const Type* rhs);

  // Instances of built-in types.
  static const Type* GetErrorType();
  static const Type* GetBooleanType();
  static const Type* GetBitType();
  static const Type* GetIntType();
  static const Type* GetFloatType();

private:
  std::string m_name;
  std::string m_llvm_name;
};
