#include "parser/symbol_table.h"

SymbolTable::SymbolTable(SymbolTable* parent) : m_parent(parent)
{
}

bool SymbolTable::HasName(const char* name) const
{
  if (m_parent && m_parent->HasName(name))
    return true;

  return m_map.find(name) != m_map.end();
}

bool SymbolTable::HasName(const std::string& name) const
{
  return HasName(name.c_str());
}

bool SymbolTable::AddName(const char* name, AST::Node* node)
{
  return AddName(std::string(name), node);
}

bool SymbolTable::AddName(const std::string& name, AST::Node* node)
{
  if (HasName(name))
    return false;

  m_map.emplace(name, node);
  return true;
}

AST::Node* SymbolTable::GetName(const char* name) const
{
  // TODO: Drop recursion for speed
  auto it = m_map.find(name);
  if (it != m_map.end())
    return it->second;

  if (m_parent)
    return m_parent->GetName(name);
  else
    return nullptr;
}

AST::Node* SymbolTable::GetName(const std::string& name) const
{
  return GetName(name.c_str());
}
