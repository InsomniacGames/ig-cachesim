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
#include "FlatProfileView.h"
#include "FlatModel.h"
#include "NumberFormatters.h"

#include "ui_FlatProfileView.h"

CacheSim::FlatProfileView::FlatProfileView(const TraceData* traceData, QWidget* parent /*= nullptr*/)
  : BaseProfileView(parent)
  , m_TraceData(traceData)
  , m_FlatProxy(new QSortFilterProxyModel(this))
  , ui(new Ui_FlatProfileView)
{
  ui->setupUi(this);

  setItemView(ui->m_FlatTableView);

  DecimalFormatDelegate* decimalDelegate = new DecimalFormatDelegate(this);
  IntegerFormatDelegate* integerDelegate = new IntegerFormatDelegate(this);
  QTableView* tableView = ui->m_FlatTableView;
  tableView->setItemDelegateForColumn(FlatModel::kColumnBadness, decimalDelegate);
  tableView->setItemDelegateForColumn(FlatModel::kColumnD1Hit, integerDelegate);
  tableView->setItemDelegateForColumn(FlatModel::kColumnI1Hit, integerDelegate);
  tableView->setItemDelegateForColumn(FlatModel::kColumnL2IMiss, integerDelegate);
  tableView->setItemDelegateForColumn(FlatModel::kColumnL2DMiss, integerDelegate);
  tableView->setItemDelegateForColumn(FlatModel::kColumnInstructionsExecuted, integerDelegate);

  m_Model = new FlatModel(this);
  m_Model->setData(traceData);

  m_FlatProxy->setSourceModel(m_Model);

  tableView->setModel(m_FlatProxy);
  tableView->sortByColumn(FlatModel::kColumnL2DMiss, Qt::DescendingOrder);

  QHeaderView* verticalHeader = tableView->verticalHeader();
  verticalHeader->sectionResizeMode(QHeaderView::Fixed);
  verticalHeader->setDefaultSectionSize(tableView->viewport()->fontMetrics().height() * 1.25);

  connect(ui->m_FlatFilter, &QLineEdit::textChanged, this, &FlatProfileView::filterTextEdited);
}

CacheSim::FlatProfileView::~FlatProfileView()
{
  delete ui;
}

void CacheSim::FlatProfileView::filterTextEdited()
{
  m_FlatProxy->setFilterFixedString(ui->m_FlatFilter->text());
}

#include "aux_FlatProfileView.moc"

