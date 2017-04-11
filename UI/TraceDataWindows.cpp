#include "Precompiled.h"
#include "TraceData.h"
#include "CacheSim/CacheSimData.h"

#include <imagehlp.h>

CacheSim::TraceData::ResolveResult CacheSim::TraceData::symbolResolveTask()
{
  // Pick out input data.
  const SerializedHeader* hdr = reinterpret_cast<const SerializedHeader*>(m_Data);

  const SerializedModuleEntry* modules = hdr->GetModules();
  const uint32_t moduleCount = hdr->GetModuleCount();
  const uintptr_t* stacks  = hdr->GetStacks();
  const uint32_t stackCount = hdr->GetStackCount();
  const SerializedNode* nodes   = hdr->GetStats();
  const uint32_t nodeCount = hdr->GetStatCount();

  const HANDLE hproc = (HANDLE) 0x1;    // Really doesn't matter. But can't be null.

  {
    DWORD sym_options = SymGetOptions();
    //sym_options |= SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS;
    sym_options |= SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_DEBUG | SYMOPT_DISABLE_SYMSRV_AUTODETECT | SYMOPT_DEFERRED_LOADS;
    sym_options &= ~(SYMOPT_UNDNAME);

    SymSetOptions(sym_options);
  }

  if (!SymInitialize(hproc, nullptr, FALSE))
  {
    qDebug() << "Failed to initialize DbgHelp library; Last Error:" << GetLastError();
    return ResolveResult();
  }

  {
    // Ugh.
    QString symDirPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("\\CacheSimSymbols");
    QDir symDir(symDirPath);
    if (!symDir.exists())
    {
      symDir.mkpath(QStringLiteral("."));
    }

    QFileInfo firstModule(QString::fromUtf8(hdr->GetModuleName(modules[0])));

    QString symbolPath = QStringLiteral("%1;srv*%2*https://msdl.microsoft.com/download/symbols")
        .arg(firstModule.absolutePath().replace(QLatin1Char('/'), QLatin1Char('\\')))
        .arg(symDir.absolutePath().replace(QLatin1Char('/'), QLatin1Char('\\')));
    if (!SymSetSearchPath(hproc, symbolPath.toUtf8().constData()))
    {
      qDebug() << "Failed to set symbol path; err:" << GetLastError();
      return ResolveResult();
    }
  }

  //SymRegisterCallback(hproc, DbgHelpCallback, 0);

  for (uint32_t modIndex = 0; modIndex < moduleCount; ++modIndex)
  {
    const SerializedModuleEntry& module = modules[modIndex];

    if (0 == SymLoadModule64(hproc, nullptr, hdr->GetModuleName(module), nullptr, module.m_ImageBase, module.m_SizeBytes))
    {
      //Warn("Failed to load module \"%s\" (base: %016llx, size: %lld); error=%u\n", mod.m_ModuleName, mod.m_ImageBase, mod.m_SizeBytes, GetLastError());
    }
  }

  SYMBOL_INFO* sym = static_cast<SYMBOL_INFO*>(malloc(sizeof(SYMBOL_INFO) + 1024 * sizeof sym->Name[0]));

  QSet<uintptr_t> ripLookup;
  int resolve_count = 0;
  int fail_count = 0;

  QVector<SerializedSymbol> symbols;

  QVector<QChar> stringData;
  stringData.push_back(QChar(0));    // Zero offset strings point here.

  QHash<QString, uint32_t> stringLookup;

  auto intern_qstring = [&](const QString& s) -> uint32_t
  {
    auto it = stringLookup.find(s);
    if (it != stringLookup.end())
    {
      return it.value();
    }
    uint32_t result = stringData.size();
    Q_FOREACH (QChar ch, s)
    {
      stringData.append(ch);
    }
    stringData.append(QChar(0));
    return result;
  };

  auto intern_string = [&](const char* str) -> uint32_t
  {
    return intern_qstring(QString::fromUtf8(str));
  };

  auto resolve_symbol = [&](uintptr_t rip) -> void
  {
    if (ripLookup.contains(rip))
      return;

    ++resolve_count;

    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 1024;

    DWORD64 disp64 = 0;
    DWORD disp32 = 0;
    IMAGEHLP_LINE64 line_info = { sizeof line_info, 0 };

    SerializedSymbol out_sym = { rip };

    if (SymFromAddr(hproc, rip, &disp64, sym))
    {
      out_sym.m_SymbolName = intern_string(sym->Name);

      if (SymGetLineFromAddr64(hproc, rip, &disp32, &line_info))
      {
        out_sym.m_FileName = intern_string(line_info.FileName);
        out_sym.m_LineNumber = line_info.LineNumber;
        out_sym.m_Displacement = disp32;
      }
    }

    if (!out_sym.m_SymbolName)
    {
      QString fakeName = QStringLiteral("[%1]").arg(rip, 16, 16, QLatin1Char('0'));
      out_sym.m_SymbolName = intern_qstring(fakeName);
      ++fail_count;
    }

    // Try to find the module..
    out_sym.m_ModuleIndex = ~0u;
    for (uint32_t i = 0; i < moduleCount; ++i)
    {
      const SerializedModuleEntry& mod = modules[i];
      if (rip >= mod.m_ImageBase && rip <= mod.m_ImageBase + mod.m_SizeBytes)
      {
        out_sym.m_ModuleIndex = i;
        break;
      }
    }

    ripLookup.insert(rip);
    symbols.push_back(out_sym);
  };
  
  int total = stackCount + nodeCount;
  int completed = 0;

  Q_EMIT symbolResolutionProgressed(completed, total);

  for (uint32_t i = 0; i < stackCount; ++i, ++completed)
  {
    if (uintptr_t rip = stacks[i])
    {
      resolve_symbol(rip);
    }

    if (0 == (completed & 0x400))
    {
      Q_EMIT symbolResolutionProgressed(completed, total);
    }
  }

  // Resolve any instructions used in leaf functions.
  for (uint32_t i = 0; i < nodeCount; ++i, ++completed)
  {
    resolve_symbol(nodes[i].m_Rip);

    if (0 == (completed & 0x400))
    {
      Q_EMIT symbolResolutionProgressed(completed, total);
    }
  }

  Q_EMIT symbolResolutionProgressed(completed, total);

  if (fail_count)
  {
    //Warn("%d out of %d symbols failed to resolve\n", fail_count, resolve_count);
  }

  // Sort the symbol data.
  std::sort(symbols.begin(), symbols.end(), [](const SerializedSymbol& l, const SerializedSymbol& r) -> bool
  {
    return l.m_Rip < r.m_Rip;
  });

  free(sym);
  sym = nullptr;

  qDebug() << "resolve result ready";

  ResolveResult result;
  result.m_StringData = stringData;
  result.m_Symbols = symbols;
  return result;
}