#include "Precompiled.h"
#include "SymbolResolver.h"

#include <imagehlp.h>

bool CacheSim::ResolveSymbols(const UnresolvedAddressData& input, QVector<ResolvedSymbol>* resolvedSymbolsOut, SymbolResolveProgressCallbackType reportProgress)
{
  const HANDLE hproc = (HANDLE)0x1;    // Really doesn't matter. But can't be null.

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
    return false;
  }

  {
    // Ugh.
    QString symDirPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("\\CacheSimSymbols");
    QDir symDir(symDirPath);
    if (!symDir.exists())
    {
      symDir.mkpath(QStringLiteral("."));
    }

    QFileInfo firstModule(QString::fromUtf8(input.m_ModuleNames[0].toUtf8()));

    QString symbolPath = QStringLiteral("%1;srv*%2*https://msdl.microsoft.com/download/symbols")
      .arg(firstModule.absolutePath().replace(QLatin1Char('/'), QLatin1Char('\\')))
      .arg(symDir.absolutePath().replace(QLatin1Char('/'), QLatin1Char('\\')));
    if (!SymSetSearchPath(hproc, symbolPath.toUtf8().constData()))
    {
      qDebug() << "Failed to set symbol path; err:" << GetLastError();
      return false;
    }
  }

  //SymRegisterCallback(hproc, DbgHelpCallback, 0);

  for (uint32_t modIndex = 0; modIndex < input.m_ModuleCount; ++modIndex)
  {
    const SerializedModuleEntry& module = input.m_Modules[modIndex];

    if (0 == SymLoadModule64(hproc, nullptr, input.m_ModuleNames[modIndex].toUtf8(), nullptr, module.m_ImageBase, module.m_SizeBytes))
    {
      //Warn("Failed to load module \"%s\" (base: %016llx, size: %lld); error=%u\n", mod.m_ModuleName, mod.m_ImageBase, mod.m_SizeBytes, GetLastError());
    }
  }

  SYMBOL_INFO* sym = static_cast<SYMBOL_INFO*>(malloc(sizeof(SYMBOL_INFO) + 1024 * sizeof sym->Name[0]));

  QSet<uintptr_t> ripLookup;
  int resolve_count = 0;
  int fail_count = 0;

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

    ResolvedSymbol out_sym;
    out_sym.m_Rip = rip;

    if (SymFromAddr(hproc, rip, &disp64, sym))
    {
      out_sym.m_SymbolName = sym->Name;

      if (SymGetLineFromAddr64(hproc, rip, &disp32, &line_info))
      {
        out_sym.m_FileName = line_info.FileName;
        out_sym.m_LineNumber = line_info.LineNumber;
        out_sym.m_Displacement = disp32;
      }
    }

    if (out_sym.m_SymbolName.isEmpty())
    {
      out_sym.m_SymbolName = QStringLiteral("[%1]").arg(rip, 16, 16, QLatin1Char('0'));
      ++fail_count;
    }

    // Try to find the module..
    out_sym.m_ModuleIndex = ~0u;
    for (uint32_t i = 0; i < input.m_ModuleCount; ++i)
    {
      const SerializedModuleEntry& mod = input.m_Modules[i];
      if (rip >= mod.m_ImageBase && rip <= mod.m_ImageBase + mod.m_SizeBytes)
      {
        out_sym.m_ModuleIndex = i;
        break;
      }
    }

    ripLookup.insert(rip);
    resolvedSymbolsOut->push_back(out_sym);
  };

  int total = input.m_StackCount + input.m_NodeCount;
  int completed = 0;

  reportProgress(completed, total);

  for (uint32_t i = 0; i < input.m_StackCount; ++i, ++completed)
  {
    if (uintptr_t rip = input.m_Stacks[i])
    {
      resolve_symbol(rip);
    }

    if (0 == (completed & 0x400))
    {
      reportProgress(completed, total);
    }
  }

  // Resolve any instructions used in leaf functions.
  for (uint32_t i = 0; i < input.m_NodeCount; ++i, ++completed)
  {
    resolve_symbol(input.m_Nodes[i].m_Rip);

    if (0 == (completed & 0x400))
    {
      reportProgress(completed, total);
    }
  }

  reportProgress(completed, total);

  if (fail_count)
  {
    //Warn("%d out of %d symbols failed to resolve\n", fail_count, resolve_count);
  }


  free(sym);
  sym = nullptr;

  return true;
}

#if 0
static BOOL CALLBACK DbgHelpCallback(
  _In_     HANDLE  hProcess,
  _In_     ULONG   ActionCode,
  _In_opt_ ULONG64 CallbackData,
  _In_opt_ ULONG64 UserContext)
{
  UNREFERENCED_VARIABLE((hProcess, UserContext));

  if (CBA_DEBUG_INFO == ActionCode)
  {
    printf("dbghelp: %s", (const char*)CallbackData);
  }

  return FALSE;
}
#endif
