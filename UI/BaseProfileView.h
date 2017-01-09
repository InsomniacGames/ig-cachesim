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

namespace CacheSim
{
  class BaseProfileView : public QWidget
  {
    Q_OBJECT;

  protected:
    explicit BaseProfileView(QWidget* parent = nullptr);
    ~BaseProfileView();

  protected:
    QAction* showReverseAction() const { return m_ShowReverseAction; }
    QAction* annotateAction() const { return m_AnnotateAction; }

  public:
    Q_SIGNAL void showReverse(QString symbolName);
    Q_SIGNAL void annotateSymbol(QString symbolName);

  protected:
    void setItemView(QAbstractItemView* view);

  private:
    Q_SLOT void showReverseTriggered();
    Q_SLOT void annotateTriggered();
    Q_SLOT void customContextMenuRequested(const QPoint &pos);

  private:
    QString selectedSymbol() const;

  private:
    QAbstractItemView* m_ItemView = nullptr;
    QAction* m_ShowReverseAction = nullptr;
    QAction* m_AnnotateAction = nullptr;
  };
}
