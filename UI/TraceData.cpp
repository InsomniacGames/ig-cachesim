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

#include "aux_TraceData.moc"

