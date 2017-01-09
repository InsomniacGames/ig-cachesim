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

#pragma once

#include "Precompiled.h"
#include "CacheSim/CacheSimData.h"

namespace CacheSim
{
  class TraceData : public QObject
  {
    Q_OBJECT;

  public:
    struct ResolveResult
    {
      QVector<QChar> m_StringData;
      QVector<SerializedSymbol> m_Symbols;
    };

  public:
    explicit TraceData(QObject* parent = nullptr);
    ~TraceData();

  public:
    bool isResolved() const;

  public:
    Q_SLOT void beginLoadTrace(QString fn);
    Q_SLOT void beginResolveSymbols();

    Q_SIGNAL void traceLoadSucceeded();
    Q_SIGNAL void traceLoadFailed(QString errorMessasge);

    Q_SIGNAL void symbolResolutionProgressed(int completed, int total);
    Q_SIGNAL void symbolResolutionCompleted();
    Q_SIGNAL void symbolResolutionFailed(QString errorMessage);
    Q_SIGNAL void memoryMappedDataChanged();

    const SerializedHeader* header() const { return reinterpret_cast<const SerializedHeader*>(m_Data); }

  public:
    QString symbolNameForAddress(uintptr_t rip) const;
    QString fileNameForAddress(uintptr_t rip) const;
    QString internedSymbolString(uint32_t offset) const;

    struct LineData
    {
      int m_LineNumber;
      uint32_t m_Stats[kAccessResultCount];
    };

    struct FileInfo
    {
      QString m_FileName;
      int m_FirstLine = 0;
      int m_LastLine = 0;
      QVector<LineData> m_Samples;
    };

    FileInfo findFileData(QString symbol) const;

  private:
    Q_SLOT void symbolsResolved();

  private:
    // Emit a load failed on the next tick of the event loop.
    void emitLoadFailure(QString errorMessage);

    ResolveResult symbolResolveTask();

  private:
    QFile           m_File;
    char*           m_Data = nullptr;
    uint64_t        m_DataSize = 0;

    QFutureWatcher<ResolveResult>* m_Watcher = nullptr;
    mutable QHash<uint32_t, QString> m_SymbolStringCache;
    mutable QHash<QString, uint32_t> m_StringToSymbolNameIndex;
  };

}
