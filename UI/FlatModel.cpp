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
#include "FlatModel.h"
#include "TraceData.h"
#include "CacheSim/CacheSimData.h"

static const QString kColumnLabels[CacheSim::FlatModel::kColumnCount] =
{
  QStringLiteral("Symbol"),
  QStringLiteral("D1Hit"),
  QStringLiteral("I1Hit"),
  QStringLiteral("L2IMiss"),
  QStringLiteral("L2DMiss"),
  QStringLiteral("Badness"),
  QStringLiteral("InstructionsExecuted"),
  QStringLiteral("PF-D1"),
  QStringLiteral("PF-L2"),
};

CacheSim::FlatModel::FlatModel(QObject* parent /*= nullptr*/)
  : QAbstractListModel(parent)
{
}

CacheSim::FlatModel::~FlatModel()
{
}

void CacheSim::FlatModel::setData(const TraceData* data)
{
  if (m_Data)
  {
    disconnect(m_Data, &TraceData::memoryMappedDataChanged, this, &FlatModel::dataStoreChanged);
  }

  m_Data = data;
  dataStoreChanged();

  if (m_Data)
  {
    connect(m_Data, &TraceData::memoryMappedDataChanged, this, &FlatModel::dataStoreChanged);
  }
}

int CacheSim::FlatModel::rowCount(const QModelIndex &parent /*= QModelIndex()*/) const
{
  (void) parent;
  return m_Rows.count();
}

int CacheSim::FlatModel::columnCount(const QModelIndex &parent /*= QModelIndex()*/) const
{
  (void) parent;
  return kColumnCount;
}

QVariant CacheSim::FlatModel::data(const QModelIndex &index, int role /*= Qt::DisplayRole*/) const
{
  int row = index.row();
  if (row < 0 || row >= m_Rows.count())
  {
    return QVariant();
  }

  const Node& node = m_Rows[row];

  if (role == Qt::DisplayRole)
  {
    switch (index.column())
    {
    case kColumnSymbol: return node.m_SymbolName;
    case kColumnD1Hit: return node.m_Stats[CacheSim::kD1Hit];
    case kColumnI1Hit: return node.m_Stats[CacheSim::kI1Hit];
    case kColumnL2IMiss: return node.m_Stats[CacheSim::kL2IMiss];
    case kColumnL2DMiss: return node.m_Stats[CacheSim::kL2DMiss];
    case kColumnBadness: return BadnessValue(node.m_Stats);
    case kColumnInstructionsExecuted: return node.m_Stats[CacheSim::kInstructionsExecuted];
    case kColumnPFD1: return node.m_Stats[CacheSim::kPrefetchHitD1];
    case kColumnPFL2: return node.m_Stats[CacheSim::kPrefetchHitL2];
    }
  }
  else if (role == Qt::TextAlignmentRole)
  {
    if (index.column() > kColumnSymbol)
    {
      return Qt::AlignRight;
    }
    return Qt::AlignLeft;
  }
  else if (role == Qt::ToolTipRole)
  {
    if (index.column() == kColumnSymbol)
    {
      return node.m_SymbolName;
    }
  }

  return QVariant();
}

QVariant CacheSim::FlatModel::headerData(int section, Qt::Orientation orientation, int role /*= Qt::DisplayRole*/) const
{
  if (role == Qt::DisplayRole && orientation == Qt::Horizontal)
  {
    return kColumnLabels[section];
  }

  return QVariant();
}

void CacheSim::FlatModel::dataStoreChanged()
{
  beginResetModel();

  m_Rows.clear();

  // Aggregate all symbols based on name.
  QHash<QString, int> symbolNameToRow;

  const SerializedHeader* header = m_Data->header();
  uint32_t count = header->GetStatCount();
  const SerializedNode* nodes = header->GetStats();

  for (uint32_t i = 0; i < count; ++i)
  {
    const SerializedNode& node = nodes[i];
    if (const SerializedSymbol* symbol = header->FindSymbol(node.m_Rip))
    {
      int row;
      QString symbolName = m_Data->symbolNameForAddress(node.m_Rip);
      auto it = symbolNameToRow.find(symbolName);
      if (it != symbolNameToRow.end())
      {
        row = it.value();
      }
      else
      {
        row = m_Rows.count();
        m_Rows.push_back(Node());
        m_Rows[row].m_SymbolName = symbolName;
        symbolNameToRow.insert(symbolName, row);
      }

      Node& target = m_Rows[row];

      for (int k = 0; k < CacheSim::kAccessResultCount; ++k)
      {
        target.m_Stats[k] += node.m_Stats[k];
      }
    }
  }

  qDebug() << "collapsed" << count << "nodes to" << m_Rows.count() << "flat entries based on symbol";
  endResetModel();
}

CacheSim::FlatModel::Node::Node()
{
  memset(m_Stats, 0, sizeof m_Stats);
}

#include "aux_FlatModel.moc"
