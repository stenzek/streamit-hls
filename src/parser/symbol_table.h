#pragma once
#include <string>
#include <unordered_map>

namespace AST
{
class Node;
}

class SymbolTable
{
public:
  using MapType = std::unordered_map<std::string, AST::Node*>;

  SymbolTable(SymbolTable* parent);
  ~SymbolTable() = default;

  MapType::const_iterator begin() const
  {
    return m_map.begin();
  }
  MapType::const_iterator end() const
  {
    return m_map.end();
  }

  bool HasName(const char* name) const;
  bool HasName(const std::string& name) const;

  bool AddName(const char* name, AST::Node* node);
  bool AddName(const std::string& name, AST::Node* node);

  AST::Node* GetName(const char* name) const;
  AST::Node* GetName(const std::string& name) const;

private:
  SymbolTable* m_parent;
  MapType m_map;
};
