#pragma once
#include <string>
#include <unordered_map>
#include "parser/helpers.h"

template <typename NameType, typename ValueType> class SymbolTable
{
public:
  using MapType = std::unordered_map<NameType, ValueType*>;

  SymbolTable(SymbolTable* parent = nullptr) : m_parent(parent)
  {
    m_id_counter = m_parent ? m_parent->m_id_counter : 1;
  }
  ~SymbolTable() = default;

  typename MapType::const_iterator begin() const
  {
    return m_map.begin();
  }
  typename MapType::const_iterator end() const
  {
    return m_map.end();
  }

  bool HasName(const NameType& name) const
  {
    if (m_parent && m_parent->HasName(name))
      return true;

    return m_map.find(name) != m_map.end();
  }

  bool AddName(const NameType& name, ValueType* node)
  {
    if (HasName(name))
      return false;

    m_map.emplace(name, node);
    return true;
  }

  ValueType* GetName(const NameType& name) const
  {
    auto* current_table = this;
    while (current_table)
    {
      auto it = current_table->m_map.find(name);
      if (it != current_table->m_map.end())
        return it->second;

      current_table = current_table->m_parent;
    }

    return nullptr;
  }

  unsigned int GenerateNameId()
  {
    return m_id_counter++;
  }

  std::string GenerateName(const char* prefix)
  {
    return StringFromFormat("%s_%u", prefix, m_id_counter);
  }

private:
  SymbolTable* m_parent;
  MapType m_map;
  unsigned int m_id_counter;
};
