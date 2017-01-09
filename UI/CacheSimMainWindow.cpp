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
#include "CacheSimMainWindow.h"
#include "TraceTab.h"

CacheSim::MainWindow::MainWindow(QWidget* parent /*= nullptr*/)
  : QMainWindow(parent)
{
  setupUi(this);

  connect(m_OpenTraceAction, &QAction::triggered, this, &MainWindow::openTrace);
  connect(m_QuitAction, &QAction::triggered, qApp, &QApplication::quit);
  connect(m_Tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTrace);
}

CacheSim::MainWindow::~MainWindow()
{

}

void CacheSim::MainWindow::openTrace()
{
  QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Select trace file"), QString(), QStringLiteral("*.csim"));

  if (fn.isEmpty())
  {
    return;
  }

  TraceTab* tab = new TraceTab(fn, this);
  m_Tabs->addTab(tab, QFileInfo(fn).baseName());

  connect(tab, &TraceTab::closeTrace, this, &MainWindow::closeTrace);
  connect(tab, &TraceTab::beginLongTask, this, &MainWindow::longTaskStarted);
  connect(tab, &TraceTab::endLongTask, this, &MainWindow::longTaskFinished);
}

void CacheSim::MainWindow::closeTrace()
{
  if (0 == m_TasksRunning)
  {
    int index = m_Tabs->currentIndex();
    QWidget* widget = m_Tabs->widget(index);
    m_Tabs->removeTab(index);
    delete widget;
  }
  else
  {
    statusBar()->showMessage(QStringLiteral("Can't close tabs while async compute is running!"), 4000);
  }
}

void CacheSim::MainWindow::closeEvent(QCloseEvent* ev)
{
  if (0 == m_TasksRunning)
  {
    QMainWindow::closeEvent(ev);
  }
  else
  {
    ev->ignore();
    statusBar()->showMessage(QStringLiteral("Can't close app while async compute is running!"), 4000);
  }
}

void CacheSim::MainWindow::longTaskStarted(int id, QString description)
{
  statusBar()->showMessage(description, 4000);
  m_LatestTask = id;
  ++m_TasksRunning;
  QApplication::setOverrideCursor(Qt::BusyCursor);
}

void CacheSim::MainWindow::longTaskFinished(int id)
{
  if (id == m_LatestTask)
  {
    statusBar()->showMessage(QStringLiteral("Ready"), 4000);
  }
  --m_TasksRunning;
  QApplication::restoreOverrideCursor();
}

#include "aux_CacheSimMainWindow.moc"
