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
#include "TreeProfileView.h"
#include "NumberFormatters.h"
#include "TreeModel.h"

#include "ui_TreeProfileView.h"

CacheSim::TreeProfileView::TreeProfileView(TreeModel* model, QWidget* parent /*= nullptr*/)
  : BaseProfileView(parent)
  , m_Model(model)
  , m_FilterProxy(new QSortFilterProxyModel(this))
  , ui(new Ui_TreeProfileView)
{
  ui->setupUi(this);

  setItemView(ui->m_TreeView);

  m_FilterProxy->setSourceModel(m_Model);

  DecimalFormatDelegate* decimalDelegate = new DecimalFormatDelegate(this);
  IntegerFormatDelegate* integerDelegate = new IntegerFormatDelegate(this);
  QTreeView* treeView = ui->m_TreeView;
  treeView->setItemDelegateForColumn(TreeModel::kColumnBadness, decimalDelegate);
  treeView->setItemDelegateForColumn(TreeModel::kColumnD1Hit, integerDelegate);
  treeView->setItemDelegateForColumn(TreeModel::kColumnI1Hit, integerDelegate);
  treeView->setItemDelegateForColumn(TreeModel::kColumnL2IMiss, integerDelegate);
  treeView->setItemDelegateForColumn(TreeModel::kColumnL2DMiss, integerDelegate);
  treeView->setItemDelegateForColumn(TreeModel::kColumnInstructionsExecuted, integerDelegate);

  treeView->setModel(m_FilterProxy);
  treeView->sortByColumn(TreeModel::kColumnL2DMiss, Qt::DescendingOrder);

  connect(ui->m_Filter, &QLineEdit::textChanged, this, &TreeProfileView::filterTextEdited);
}

CacheSim::TreeProfileView::~TreeProfileView()
{

}

void CacheSim::TreeProfileView::filterTextEdited()
{
  m_FilterProxy->setFilterFixedString(ui->m_Filter->text());
}

#include "aux_TreeProfileView.moc"
