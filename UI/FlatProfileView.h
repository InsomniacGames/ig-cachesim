#pragma once
#include "Precompiled.h"
#include "BaseProfileView.h"

class Ui_FlatProfileView;

namespace CacheSim
{
  struct SerializedNode;
  class TraceData;
  class FlatModel;

  class FlatProfileView : public BaseProfileView
  {
    Q_OBJECT;

  public:
    explicit FlatProfileView(const TraceData* traceData, QWidget* parent = nullptr);
    ~FlatProfileView();

  private:
    Q_SLOT void filterTextEdited();

  private:
    const TraceData* m_TraceData;
    FlatModel* m_Model = nullptr;
    QSortFilterProxyModel* m_FlatProxy = nullptr;
    Ui_FlatProfileView* ui;
  };
}