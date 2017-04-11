#pragma once

#include "Precompiled.h"
#include "CacheSim/CacheSimData.h"

#include <functional>

namespace CacheSim
{
  struct UnresolvedAddressData
  {
    const SerializedModuleEntry*  m_Modules = nullptr;
    const QString*                m_ModuleNames = nullptr;
    uint32_t                      m_ModuleCount = 0;
    const uintptr_t*              m_Stacks = nullptr;
    uint32_t                      m_StackCount = 0;
    const SerializedNode*         m_Nodes = nullptr;
    uint32_t                      m_NodeCount = 0;
  };

  struct ResolvedSymbol
  {
    uintptr_t   m_Rip;
    QString     m_FileName;
    QString     m_SymbolName;
    uint32_t    m_ModuleIndex;
    uint32_t    m_LineNumber;
    uint32_t    m_Displacement;
  };

  using SymbolResolveProgressCallbackType = std::function<void(int, int)>;

  bool ResolveSymbols(const UnresolvedAddressData& input, QVector<ResolvedSymbol>* resolvedSymbolsOut, SymbolResolveProgressCallbackType reportProgress);
}