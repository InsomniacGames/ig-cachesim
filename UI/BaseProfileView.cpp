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
#include "BaseProfileView.h"

CacheSim::BaseProfileView::BaseProfileView(QWidget* parent /*= nullptr*/)
  : QWidget(parent)
{
  m_ShowReverseAction = new QAction(QStringLiteral("Show call tree"), this);
  m_ShowReverseAction->setShortcut(Qt::Key_R | Qt::CTRL);
  m_ShowReverseAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  connect(m_ShowReverseAction, &QAction::triggered, this, &BaseProfileView::showReverseTriggered);

  m_AnnotateAction = new QAction(QStringLiteral("Annotate"), this);
  m_AnnotateAction->setShortcut(Qt::Key_A | Qt::CTRL | Qt::SHIFT);
  m_AnnotateAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  connect(m_AnnotateAction, &QAction::triggered, this, &BaseProfileView::annotateTriggered);

  this->addAction(m_ShowReverseAction);
  this->addAction(m_AnnotateAction);
}

CacheSim::BaseProfileView::~BaseProfileView()
{

}

void CacheSim::BaseProfileView::setItemView(QAbstractItemView* view)
{
  m_ItemView = view;
  view->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(view, &QAbstractItemView::customContextMenuRequested, this, &BaseProfileView::customContextMenuRequested);
}

void CacheSim::BaseProfileView::showReverseTriggered()
{
  QString sym = selectedSymbol();
  if (!sym.isEmpty())
  {
    Q_EMIT showReverse(sym);
  }
}

void CacheSim::BaseProfileView::customContextMenuRequested(const QPoint &pos)
{
  QModelIndex index = m_ItemView->indexAt(pos);
  if (!index.isValid())
    return;

  QMenu* menu = new QMenu(this);
  menu->addAction(showReverseAction());
  menu->addAction(annotateAction());
  menu->popup(m_ItemView->viewport()->mapToGlobal(pos));
}

QString CacheSim::BaseProfileView::selectedSymbol() const
{
  QModelIndexList selection = m_ItemView->selectionModel()->selectedIndexes();
  if (selection.isEmpty())
  {
    return QString::null;
  }

  QModelIndex first = selection[0];
  return m_ItemView->model()->data(first).toString();
}

void CacheSim::BaseProfileView::annotateTriggered()
{
  QString sym = selectedSymbol();
  if (!sym.isEmpty())
  {
    Q_EMIT annotateSymbol(sym);
  }
}
 
#include "aux_BaseProfileView.moc"
