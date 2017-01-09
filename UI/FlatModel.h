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
#include "CacheSim/CacheSimInternals.h"

namespace CacheSim
{
  class TraceData;

  class FlatModel final : public QAbstractListModel
  {
    Q_OBJECT;

  public:
    enum Column
    {
      kColumnSymbol,
      kColumnD1Hit,
      kColumnI1Hit,
      kColumnL2IMiss,
      kColumnL2DMiss,
      kColumnBadness,
      kColumnInstructionsExecuted,
      kColumnPFD1,
      kColumnPFL2,
      kColumnCount
    };

  public:
    explicit FlatModel(QObject* parent = nullptr);
    ~FlatModel();

  public:
    void setData(const TraceData* data);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

  private:
    Q_SLOT void dataStoreChanged();

  private:
    const TraceData* m_Data = nullptr;

    struct Node
    {
      Node();

      QString m_SymbolName;
      uint32_t m_Stats[CacheSim::kAccessResultCount];
    };

    QVector<Node> m_Rows;
  };

}
