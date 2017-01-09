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
#include "TreeModel.h"
#include "TraceData.h"
#include "ObjectStack.h"

#include "CacheSim/CacheSimInternals.h"
#include "CacheSim/CacheSimData.h"

static const QString kColumnLabels[CacheSim::TreeModel::kColumnCount] =
{
  QStringLiteral("Symbol"),
  QStringLiteral("File"),
  QStringLiteral("D1Hit"),
  QStringLiteral("I1Hit"),
  QStringLiteral("L2IMiss"),
  QStringLiteral("L2DMiss"),
  QStringLiteral("Badness"),
  QStringLiteral("Instructions"),
  QStringLiteral("PF-D1"),
  QStringLiteral("PF-L2"),
};

class CacheSim::TreeModel::Node
{
public:
  Node* m_Parent;
  QString m_SymbolName;
  QString m_FileName;
  uint32_t m_Stats[CacheSim::kAccessResultCount];
  QVector<Node*> m_Children;

  explicit Node(Node* parent) : m_Parent(parent), m_Stats { 0 }
  {}

  ~Node()
  {
  }

  Node* child(QString name, bool* isNew, ObjectStack* stack)
  {
    Q_FOREACH(Node* n, m_Children)
    {
      if (n->m_SymbolName == name)
      {
        *isNew = false;
        return n;
      }
    }

    *isNew = true;
    Node* node = stack->alloc<Node>(this);
    node->m_SymbolName = name;
    m_Children.push_back(node);
    return node;
  }

  int rowInParentSpace() const
  {
    if (m_Parent)
    {
      return m_Parent->m_Children.indexOf(const_cast<Node*>(this));
    }
    return 0;
  }

};

CacheSim::TreeModel::TreeModel(QObject* parent /*= nullptr*/)
  : QAbstractItemModel(parent)
  , m_Allocator(new ObjectStack)
{
}

CacheSim::TreeModel::~TreeModel()
{
  m_RootNode = nullptr;
  delete m_Allocator;
}

QModelIndex CacheSim::TreeModel::index(int row, int column, const QModelIndex &parent /*= QModelIndex()*/) const
{
  Node* node = m_RootNode;

  if (parent.isValid())
  {
    node = static_cast<Node*>(parent.internalPointer());
  }

  if (row < 0 || row >= node->m_Children.size())
    return QModelIndex();

  return createIndex(row, column, node->m_Children[row]);
}

QModelIndex CacheSim::TreeModel::parent(const QModelIndex &child) const
{
  if (!child.isValid())
    return QModelIndex();

  Node* p = static_cast<Node*>(child.internalPointer());
  Node* parent = p->m_Parent;
  if (!parent || parent == m_RootNode)
    return QModelIndex();

  return createIndex(parent->rowInParentSpace(), kColumnSymbol, parent);
}

int CacheSim::TreeModel::rowCount(const QModelIndex &parent /*= QModelIndex()*/) const
{
  Node* p = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_RootNode;

  if (!parent.isValid() || parent.column() == kColumnSymbol)
    return p->m_Children.count();

  return 0;
}

int CacheSim::TreeModel::columnCount(const QModelIndex &parent /*= QModelIndex()*/) const
{
  (void) parent;
  return kColumnCount;
}

QVariant CacheSim::TreeModel::data(const QModelIndex &index, int role /*= Qt::DisplayRole*/) const
{
  Node* node = static_cast<Node*>(index.internalPointer());
  if (!node)
    return QVariant();

  if (role == Qt::DisplayRole)
  {
    switch (index.column())
    {
    case kColumnSymbol: return node->m_SymbolName;
    case kColumnFileName: return node->m_FileName;
    case kColumnD1Hit: return node->m_Stats[CacheSim::kD1Hit];
    case kColumnI1Hit: return node->m_Stats[CacheSim::kI1Hit];
    case kColumnL2IMiss: return node->m_Stats[CacheSim::kL2IMiss];
    case kColumnL2DMiss: return node->m_Stats[CacheSim::kL2DMiss];
    case kColumnBadness: return BadnessValue(node->m_Stats);
    case kColumnInstructionsExecuted: return node->m_Stats[CacheSim::kInstructionsExecuted];
    case kColumnPFD1: return node->m_Stats[CacheSim::kPrefetchHitD1];
    case kColumnPFL2: return node->m_Stats[CacheSim::kPrefetchHitL2];
    }
  }
  else if (role == Qt::TextAlignmentRole)
  {
    if (index.column() > kColumnFileName)
    {
      return Qt::AlignRight;
    }
    return Qt::AlignLeft;
  }
  else if (role == Qt::ToolTipRole)
  {
    if (index.column() == kColumnSymbol)
    {
      return node->m_SymbolName;
    }
    else if (index.column() == kColumnFileName)
    {
      return node->m_FileName;
    }
  }

  return QVariant();

}

QVariant CacheSim::TreeModel::headerData(int section, Qt::Orientation orientation, int role /*= Qt::DisplayRole*/) const
{
  if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section >= 0 && section < kColumnCount)
  {
    return kColumnLabels[section];
  }

  return QVariant();
}

void CacheSim::TreeModel::setTraceData(const TraceData* traceData, QString rootSymbol)
{
  beginResetModel();

  m_Allocator->reset();
  m_RootNode = nullptr;

  m_RootNode = createTree(traceData, rootSymbol);

  endResetModel();
}

CacheSim::TreeModel::Node* CacheSim::TreeModel::createTree(const TraceData* traceData, QString rootSymbol)
{
  const SerializedNode* nodes = traceData->header()->GetStats();
  const uint32_t nodeCount = traceData->header()->GetStatCount();

  const uintptr_t* stackFrames = traceData->header()->GetStacks();

  QVector<uintptr_t> frames;

  Node* root = m_Allocator->alloc<Node>(nullptr);

  for (uint32_t i = 0; i < nodeCount; ++i)
  {
    frames.clear();

    const SerializedNode& node = nodes[i];

    // If we're trying to limit the tree to a particular root symbol, do that.
    if (!rootSymbol.isEmpty())
    {
      if (rootSymbol != traceData->symbolNameForAddress(node.m_Rip))
      {
        continue;
      }
    }

    frames.push_back(node.m_Rip);

    const uintptr_t* fp = stackFrames + node.m_StackIndex;
    while (uintptr_t rip = *fp++)
    {
      frames.push_back(rip);
    }

    if (rootSymbol.isEmpty()) // top down, unless we're looking at a specific symbol in which case we'll reverse the tree
    {
      std::reverse(frames.begin(), frames.end());
    }

    Node* branch = root;

    Q_FOREACH(uintptr_t rip, frames)
    {
      QString symbolName;

      const SerializedSymbol* sym = traceData->header()->FindSymbol(rip);

      if (sym)
      {
        symbolName = traceData->internedSymbolString(sym->m_SymbolName);
      }
      else
      {
        symbolName = QStringLiteral("[%1]").arg(rip, 16, 16, QLatin1Char('0'));
      }

      bool isNew;
      branch = branch->child(symbolName, &isNew, m_Allocator);

      if (isNew && sym)
      {
        branch->m_FileName = traceData->internedSymbolString(sym->m_FileName);
      }

      for (int k = 0; k < CacheSim::kAccessResultCount; ++k)
      {
        branch->m_Stats[k] += node.m_Stats[k];
      }
    }
  }

  return root;
}

#include "aux_TreeModel.moc"
