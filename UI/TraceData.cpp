/*
Copyright (c) 2017, Insomniac Games
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "Precompiled.h"
#include "TraceData.h"
#include "CacheSim/CacheSimData.h"

#include <imagehlp.h>

Q_DECLARE_METATYPE(CacheSim::TraceData::ResolveResult);

CacheSim::TraceData::TraceData(QObject* parent /*= nullptr*/)
  : QObject(parent)
  , m_Watcher(new QFutureWatcher<ResolveResult>(this))
{
  connect(m_Watcher, &QFutureWatcher<ResolveResult>::finished, this, &TraceData::symbolsResolved);
}

CacheSim::TraceData::~TraceData()
{

}

bool CacheSim::TraceData::isResolved() const
{
  return header()->m_SymbolCount > 0;
}

void CacheSim::TraceData::beginLoadTrace(QString fn)
{
  // Could make this fully async later.

  if (m_Data)
  {
    emitLoadFailure(QStringLiteral("A trace file is already loaded"));
    return;
  }

  m_SymbolStringCache.clear();
  m_StringToSymbolNameIndex.clear();

  m_File.close();
  m_File.setFileName(fn);

  if (!m_File.open(QIODevice::ReadWrite))
  {
    emitLoadFailure(QStringLiteral("Failed to open file"));
    m_File.close();
    return;
  }

  m_DataSize = m_File.size();
  if (nullptr == (m_Data = (char*) m_File.map(0, m_DataSize)))
  {
    emitLoadFailure(QStringLiteral("Failed to memory map file"));
    m_File.close();
    return;
  }

  Q_EMIT memoryMappedDataChanged();

  QTimer::singleShot(0, [p = QPointer<TraceData>(this)]()
  {
    if (TraceData* td = p)
    {
      Q_EMIT td->traceLoadSucceeded();
    }
  });
}

void CacheSim::TraceData::beginResolveSymbols()
{
  m_Watcher->setFuture(QtConcurrent::run(this, &TraceData::symbolResolveTask));
}

QString CacheSim::TraceData::symbolNameForAddress(uintptr_t rip) const
{
  const SerializedSymbol* symbol = header()->FindSymbol(rip);

  if (!symbol)
  {
    return QString::null;
  }

  return internedSymbolString(symbol->m_SymbolName);
}

QString CacheSim::TraceData::fileNameForAddress(uintptr_t rip) const
{
  const SerializedSymbol* symbol = header()->FindSymbol(rip);

  if (!symbol)
  {
    return QString::null;
  }

  return internedSymbolString(symbol->m_FileName);
}

QString CacheSim::TraceData::internedSymbolString(uint32_t offset) const
{
  auto it = m_SymbolStringCache.constFind(offset);
  if (it != m_SymbolStringCache.constEnd())
    return it.value();

  const SerializedHeader* hdr = header();

  QString result(reinterpret_cast<const QChar*>(m_Data + hdr->m_SymbolTextOffset + sizeof(QChar) * offset));
  m_SymbolStringCache.insert(offset, result);
  m_StringToSymbolNameIndex.insert(result, offset);
  return result;
}

CacheSim::TraceData::FileInfo CacheSim::TraceData::findFileData(QString symbol) const
{
  uint32_t stringIndex = m_StringToSymbolNameIndex.value(symbol);

  if (stringIndex == 0)
  {
    return FileInfo();
  }

  const SerializedHeader* hdr = header();
  const SerializedNode* nodes = hdr->GetStats();
  const uint32_t nodeCount = hdr->GetStatCount();

  int minLine = INT_MAX;
  int maxLine = INT_MIN;

  QString fileName;

  QHash<int, LineData> lineStats;

  for (uint32_t i = 0; i < nodeCount; ++i)
  {
    const SerializedNode& node = nodes[i];

    if (const SerializedSymbol* sym = hdr->FindSymbol(node.m_Rip))
    {
      if (internedSymbolString(sym->m_SymbolName) == symbol)
      {
        if (fileName.isEmpty())
        {
          fileName = internedSymbolString(sym->m_FileName);
        }

        int lineNo = sym->m_LineNumber;

        LineData& data = lineStats[lineNo];

        data.m_LineNumber = lineNo;
        for (int k = 0; k < kAccessResultCount; ++k)
        {
          data.m_Stats[k] += node.m_Stats[k];
        }

        minLine = std::min(minLine, lineNo);
        maxLine = std::max(maxLine, lineNo);
      }
    }
  }

  QVector<LineData> lineData;
  Q_FOREACH(LineData data, lineStats)
  {
    lineData.push_back(data);
  }

  std::sort(lineData.begin(), lineData.end(), [](const LineData& l, const LineData& r)
  {
    return l.m_LineNumber < r.m_LineNumber;
  });

  FileInfo result;
  result.m_FileName = fileName;
  result.m_FirstLine = minLine;
  result.m_LastLine = maxLine;
  result.m_Samples = lineData;
  return result;
}

void CacheSim::TraceData::symbolsResolved()
{
  ResolveResult result = m_Watcher->future().result();

  qDebug() << "Resolve completed with" << result.m_StringData.size() << "chars of string data," << result.m_Symbols.size() << "symbols";

  // Create a new, temporary file we can swap to later.
  const SerializedHeader* hdr = header();

  SerializedHeader newHeader = *hdr;
  newHeader.m_SymbolOffset = m_DataSize;
  newHeader.m_SymbolCount = result.m_Symbols.size();
  newHeader.m_SymbolTextOffset = m_DataSize + newHeader.m_SymbolCount * sizeof(SerializedSymbol);

  uint32_t totalSize = newHeader.m_SymbolTextOffset + result.m_StringData.size() * sizeof(QChar);

  //uint32_t oldSize = m_File.size();
  m_File.resize(totalSize);
  m_DataSize = totalSize;
  m_File.unmap((uchar*)(m_Data));
  m_Data = (char*)(m_File.map(0, m_DataSize));
  m_SymbolStringCache.clear();
  m_StringToSymbolNameIndex.clear();

  Q_EMIT memoryMappedDataChanged();

  memcpy(m_Data + newHeader.m_SymbolOffset, reinterpret_cast<const char*>(result.m_Symbols.constData()), result.m_Symbols.size() * sizeof(SerializedSymbol));
  memcpy(m_Data + newHeader.m_SymbolTextOffset, reinterpret_cast<const char*>(result.m_StringData.constData()), result.m_StringData.size() * sizeof(QChar));
  memcpy(m_Data, &newHeader, sizeof newHeader);

  //m_File.write(reinterpret_cast<const char*>(result.m_Symbols.constData()), result.m_Symbols.size() * sizeof(SerializedSymbol));
  //m_File.write(reinterpret_cast<const char*>(result.m_StringData.constData()), result.m_StringData.size() * sizeof(QChar));

  //temp.write(reinterpret_cast<const char*>(&newHeader), sizeof newHeader);
  //temp.write(reinterpret_cast<const char*>(m_Data) + sizeof newHeader, m_DataSize - sizeof newHeader);

  m_File.flush();

  Q_EMIT symbolResolutionCompleted();
}

void CacheSim::TraceData::emitLoadFailure(QString errorMessage)
{
  QTimer::singleShot(0, [p = QPointer<TraceData>(this), msg = errorMessage]()
  {
    if (TraceData* td = p)
    {
      Q_EMIT td->traceLoadFailed(msg);
    }
  });
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
    printf("dbghelp: %s", (const char*) CallbackData);
  }
  
  return FALSE;
}
#endif

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

#include "aux_TraceData.moc"
