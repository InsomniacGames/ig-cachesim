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
#include "TraceTab.h"
#include "TraceData.h"
#include "FlatProfileView.h"
#include "TreeProfileView.h"
#include "TreeModel.h"
#include "AnnotationView.h"

#include "ui_TraceTab.h"

CacheSim::TraceTab::TraceTab(QString fileName, QWidget* parent /*= nullptr*/)
  : QWidget(parent)
  , m_Data(new TraceData(this))
  , m_PendingJobs(0)
  , m_JobCounter(0)
  , ui(new Ui_TraceTab)
{
  ui->setupUi(this);

  connect(this, &TraceTab::treeModelReady, this, &TraceTab::createViewFromTreeModel, Qt::QueuedConnection);

  connect(m_Data, &TraceData::traceLoadSucceeded, this, &TraceTab::traceLoadSucceeded);
  connect(m_Data, &TraceData::traceLoadFailed, this, &TraceTab::traceLoadFailed);
  connect(m_Data, &TraceData::symbolResolutionProgressed, this, &TraceTab::symbolResolutionProgressed);
  connect(m_Data, &TraceData::symbolResolutionCompleted, this, &TraceTab::symbolResolutionCompleted);
  connect(m_Data, &TraceData::symbolResolutionFailed, this, &TraceTab::symbolResolutionFailed);
  connect(ui->m_ResolveSymbolsButton, &QPushButton::clicked, this, &TraceTab::resolveSymbolsClicked);

  this->setEnabled(false);
  m_Data->beginLoadTrace(fileName);

  connect(ui->m_TabWidget, &QTabWidget::tabCloseRequested, this, &TraceTab::tabCloseRequested);

  connect(ui->m_FlatProfileButton, &QPushButton::clicked, this, &TraceTab::openFlatProfile);
  connect(ui->m_TreeProfileButton, &QPushButton::clicked, this, &TraceTab::openTreeProfile);

  m_CloseTabAction = new QAction(QStringLiteral("Close tab"), this);
  this->addAction(m_CloseTabAction);
  ui->m_TabWidget->addAction(m_CloseTabAction);
  m_CloseTabAction->setShortcut(Qt::Key_W | Qt::CTRL);
  m_CloseTabAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  connect(m_CloseTabAction, &QAction::triggered, this, &TraceTab::closeCurrentTab);
}

CacheSim::TraceTab::~TraceTab()
{
  delete ui;
}

void CacheSim::TraceTab::openFlatProfile()
{
  if (-1 == m_FlatProfileTabIndex)
  {
    m_FlatProfileTabIndex = addProfileView(new FlatProfileView(m_Data), QStringLiteral("Flat Profile"));
  }

  ui->m_TabWidget->setCurrentIndex(m_FlatProfileTabIndex);
}

void CacheSim::TraceTab::openTreeProfile()
{
  if (-1 != m_TreeProfileTabIndex)
  {
    ui->m_TabWidget->setCurrentIndex(m_TreeProfileTabIndex);
    return;
  }

  ui->m_TreeProfileButton->setEnabled(false);

  doCreateTreeView(QString::null, QStringLiteral("Top-down tree"));
}

void CacheSim::TraceTab::createViewFromTreeModel(TreeModel* model, QString title, bool isMainTree)
{
  TreeProfileView* v = new TreeProfileView(model);
  model->setParent(v);

  int index = addProfileView(v, title);

  if (isMainTree)
  {
    m_TreeProfileTabIndex = index;
    ui->m_TreeProfileButton->setEnabled(true);
  }
}

void CacheSim::TraceTab::openReverseViewForSymbol(QString symbol)
{
  doCreateTreeView(symbol, QStringLiteral("Reverse: %1").arg(symbol));
}

void CacheSim::TraceTab::openAnnotationForSymbol(QString symbol)
{
  TraceData::FileInfo fileInfo = m_Data->findFileData(symbol);
  if (!fileInfo.m_FileName.isEmpty() && QFileInfo::exists(fileInfo.m_FileName))
  {
    QScrollArea* scrollArea = new QScrollArea(this);
    AnnotationView* v = new AnnotationView(fileInfo, scrollArea);
    scrollArea->setWidget(v);
    v->addAction(m_CloseTabAction);
    int index = ui->m_TabWidget->addTab(scrollArea, QStringLiteral("Source: %1").arg(symbol));
    ui->m_TabWidget->setCurrentIndex(index);
  }
}

void CacheSim::TraceTab::traceLoadSucceeded()
{
  this->setEnabled(true);
  updateSymbolStatus();
}

void CacheSim::TraceTab::traceLoadFailed(QString reason)
{

}

void CacheSim::TraceTab::resolveSymbolsClicked()
{
  ui->m_ResolveSymbolsButton->setEnabled(false);
  m_Data->beginResolveSymbols();
}

void CacheSim::TraceTab::symbolResolutionCompleted()
{
  ui->m_ResolveSymbolsButton->setEnabled(true);
  updateSymbolStatus();
}

void CacheSim::TraceTab::symbolResolutionProgressed(int completed, int total)
{
  QLocale loc = QLocale::system();
  ui->m_SymbolStatus->setText(QStringLiteral("Resolving: %1% done (%2/%3)").arg(loc.toString(100.0*completed/total, 'f', 2)).arg(loc.toString(completed)).arg(loc.toString(total)));
}

void CacheSim::TraceTab::symbolResolutionFailed(QString reason)
{
  QMessageBox::warning(this, QStringLiteral("Symbol resolution failed"), reason);
  ui->m_ResolveSymbolsButton->setEnabled(true);
  updateSymbolStatus();
}

void CacheSim::TraceTab::tabCloseRequested(int index)
{
  if (0 == index)
  {
    Q_EMIT closeTrace();
  }
  else
  {
    QWidget* w = ui->m_TabWidget->widget(index);
    ui->m_TabWidget->removeTab(index);
    delete w;
  }

  if (index == m_FlatProfileTabIndex)
  {
    m_FlatProfileTabIndex = -1;
  }
  else if (index == m_TreeProfileTabIndex)
  {
    m_TreeProfileTabIndex = -1;
  }
}

void CacheSim::TraceTab::closeCurrentTab()
{
  int index = ui->m_TabWidget->currentIndex();
  tabCloseRequested(index);
}

void CacheSim::TraceTab::updateSymbolStatus()
{
  ui->m_SymbolStatus->setText(m_Data->isResolved() ? QStringLiteral("Resolved") : QStringLiteral("Unresolved"));
}

void CacheSim::TraceTab::doCreateTreeView(QString rootSymbolOpt, QString title)
{
  const int id = m_JobCounter++;
  Q_EMIT beginLongTask(id, QStringLiteral("Computing tree profile"));

  ++m_PendingJobs;

  QtConcurrent::run([self = this, data = m_Data, tr = QThread::currentThread(), id = id, title = title, sym=rootSymbolOpt]()
  {
    TreeModel* model = new TreeModel(nullptr);
    model->setTraceData(data, sym);
    model->moveToThread(tr);
    Q_EMIT self->treeModelReady(model, title, /* isMain=*/sym.isEmpty());
    Q_EMIT self->endLongTask(id);
    --self->m_PendingJobs;
  });
}

int CacheSim::TraceTab::addProfileView(BaseProfileView* view, QString label)
{
  int index = ui->m_TabWidget->addTab(view, label);

  connect(view, &BaseProfileView::showReverse, this, &TraceTab::openReverseViewForSymbol);
  connect(view, &BaseProfileView::annotateSymbol, this, &TraceTab::openAnnotationForSymbol);
  view->addAction(m_CloseTabAction);
  ui->m_TabWidget->setCurrentIndex(index);

  return index;
}

#include "aux_TraceTab.moc"

