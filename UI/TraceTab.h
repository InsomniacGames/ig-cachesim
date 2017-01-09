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

class Ui_TraceTab;

namespace CacheSim
{
  class TraceData;
  class TreeModel;
  class BaseProfileView;

  class TraceTab : public QWidget
  {
    Q_OBJECT;

  public:
    explicit TraceTab(QString fileName, QWidget* parent = nullptr);
    ~TraceTab();

  public:
    Q_SLOT void openFlatProfile();
    Q_SLOT void openTreeProfile();
    Q_SLOT void openReverseViewForSymbol(QString symbol);
    Q_SLOT void openAnnotationForSymbol(QString symbol);
    Q_SIGNAL void closeTrace();
    Q_SIGNAL void beginLongTask(int id, QString description);
    Q_SIGNAL void endLongTask(int id);

  private:
    Q_SLOT void traceLoadSucceeded();
    Q_SLOT void traceLoadFailed(QString reason);
    Q_SLOT void resolveSymbolsClicked();
    Q_SLOT void symbolResolutionCompleted();
    Q_SLOT void symbolResolutionProgressed(int completed, int total);
    Q_SLOT void symbolResolutionFailed(QString reason);
    Q_SLOT void tabCloseRequested(int index);
    Q_SLOT void closeCurrentTab();
    Q_SIGNAL void treeModelReady(TreeModel* model, QString title, bool isMainView);
    Q_SLOT void createViewFromTreeModel(TreeModel* model, QString title, bool isMainView);

    void updateSymbolStatus();
    void doCreateTreeView(QString rootSymbolOpt, QString label);
    int addProfileView(BaseProfileView* view, QString label);

  private:
    QAction* m_CloseTabAction = nullptr;
    TraceData* m_Data = nullptr;

    int m_FlatProfileTabIndex = -1;
    int m_TreeProfileTabIndex = -1;
    QAtomicInt m_PendingJobs;
    QAtomicInt m_JobCounter;
    Ui_TraceTab* ui;
  };

}
