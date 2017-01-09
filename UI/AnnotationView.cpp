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
#include "AnnotationView.h"

static constexpr int kContextLines = 5;
static constexpr int kLineSpacing = 2;
static constexpr double kBadnessClamp = 50.0;

CacheSim::AnnotationView::AnnotationView(TraceData::FileInfo fileInfo, QWidget* parent /*= nullptr*/)
  : QWidget(parent)
  , m_FileInfo(fileInfo)
{
  QFont font(QStringLiteral("Consolas"), 9);
  setFont(font);
  QFontMetrics metrics(font);

  m_Locale.setNumberOptions(QLocale::DefaultNumberOptions);

  int maxWidth = 0;

  QFile f(m_FileInfo.m_FileName);
  if (f.open(QIODevice::ReadOnly))
  {
    QTextStream s(&f);

    int lineNo = 0;
    while (++lineNo < m_FileInfo.m_FirstLine - kContextLines)
    {
      s.readLine();
    }

    while (lineNo < m_FileInfo.m_LastLine + kContextLines && !s.atEnd())
    {
      LineInfo lineInfo;
      lineInfo.m_Text = s.readLine();

      auto it = std::lower_bound(fileInfo.m_Samples.constBegin(), fileInfo.m_Samples.constEnd(), lineNo, [](const TraceData::LineData& data, int line)
      {
        return data.m_LineNumber < line;
      });

      if (it != fileInfo.m_Samples.constEnd() && it->m_LineNumber == lineNo)
      {
        lineInfo.m_SampleIndex = std::distance(fileInfo.m_Samples.constBegin(), it);
      }

      m_Lines.push_back(lineInfo);
      maxWidth = std::max(maxWidth, metrics.boundingRect(lineInfo.m_Text).width());
      ++lineNo;
    }
  }

  m_GutterWidth = metrics.boundingRect(QStringLiteral("99999999")).width();
  maxWidth += m_GutterWidth;

  this->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  this->setMinimumSize(maxWidth, (fontMetrics().height() + kLineSpacing) * m_Lines.count());
}

CacheSim::AnnotationView::~AnnotationView()
{

}

static QColor lerpColors(const QColor& a, const QColor& b, double t)
{
  double ar = a.redF();
  double ag = a.greenF();
  double ab = a.blueF();
  double br = b.redF();
  double bg = b.greenF();
  double bb = b.blueF();

  return QColor::fromRgbF(ar + (br - ar) * t, ag + (bg - ag) * t, ab + (bb - ab) * t);
}

void CacheSim::AnnotationView::paintEvent(QPaintEvent *event)
{
  (void) event;

  QPainter p(this);

  p.setFont(font());

  int fh = fontMetrics().height();
  int lh = fh + kLineSpacing;

  QRect br = event->rect();

  int firstLine = std::max(br.y() / lh, 0);
  int lastLine = std::min(firstLine + (br.height() + lh - 1) / lh, m_Lines.count() - 1);

  int line_y = firstLine * lh;
  int text_y = fontMetrics().ascent() + line_y;

  QColor ok  = palette().background().color();
  QColor noData  = ok.darker(110);
  QColor bad = QColor("#ff8080");

  QBrush backgroundBrush(ok);

  for (int i = firstLine; i <= lastLine; ++i)
  {
    LineInfo lineInfo = m_Lines.value(i);

    if (lineInfo.m_SampleIndex >= 0)
    {
      double badness = std::min(BadnessValue(m_FileInfo.m_Samples[lineInfo.m_SampleIndex].m_Stats), kBadnessClamp);
      double ratio = badness / kBadnessClamp;
      backgroundBrush.setColor(lerpColors(ok, bad, ratio));
    }
    else
    {
      backgroundBrush.setColor(noData);
    }

    p.setBackground(backgroundBrush);
    p.fillRect(0, line_y, br.width(), lh, backgroundBrush);
    p.drawText(0, text_y, QString::number(i + m_FileInfo.m_FirstLine - kContextLines));
    p.drawText(m_GutterWidth, text_y, m_Lines.value(i).m_Text);

    text_y += lh;
    line_y += lh;
  }
}

int CacheSim::AnnotationView::lineAtPosition(const QPoint& point)
{
  int fh = fontMetrics().height();
  int lh = fh + kLineSpacing;
  int line = point.y() / lh;
  return line;
}

bool CacheSim::AnnotationView::event(QEvent* ev)
{
  if (ev->type() == QEvent::ToolTip)
  {
    QHelpEvent *helpEvent = static_cast<QHelpEvent*>(ev);
    int line = lineAtPosition(helpEvent->pos());
    if (line >= 0 && line < m_Lines.size())
    {
      if (m_Lines[line].m_SampleIndex == -1)
      {
        QToolTip::showText(helpEvent->globalPos(), QStringLiteral("(no data)"));
      }
      else
      {
        const TraceData::LineData& lineData = m_FileInfo.m_Samples[m_Lines[line].m_SampleIndex];
        QString text = QStringLiteral(
          "<table>"
          "<tr><td>Line Number</td><td align='right'>&nbsp;%1</td></tr>"
          "<tr><td>I1 Hits</td><td align='right'>&nbsp;%2</td></tr>"
          "<tr><td>D1 Hits</td><td align='right'>&nbsp;%3</td></tr>"
          "<tr><td>L2 Data Misses</td><td align='right'>&nbsp;%4</td></tr>"
          "<tr><td>L2 Instruction Misses</td><td align='right'>&nbsp;%5</td></tr>"
          "<tr><td>Badness</td><td align='right'>&nbsp;%6</td></tr>"
          "<tr><td>Instructions Executed</td><td align='right'>&nbsp;%7</td></tr>"
          "<tr><td>Prefetch Hit D1</td><td align='right'>&nbsp;%8</td></tr>"
          "<tr><td>Prefetch Hit L2</td><td align='right'>&nbsp;%9</td></tr>"
          "</table>")
          .arg(lineData.m_LineNumber)
          .arg(m_Locale.toString(lineData.m_Stats[kI1Hit]))
          .arg(m_Locale.toString(lineData.m_Stats[kD1Hit]))
          .arg(m_Locale.toString(lineData.m_Stats[kL2DMiss]))
          .arg(m_Locale.toString(lineData.m_Stats[kL2IMiss]))
          .arg(m_Locale.toString(BadnessValue(lineData.m_Stats), 'f', 2))
          .arg(m_Locale.toString(lineData.m_Stats[kInstructionsExecuted]))
          .arg(m_Locale.toString(lineData.m_Stats[kPrefetchHitD1]))
          .arg(m_Locale.toString(lineData.m_Stats[kPrefetchHitL2]))
          ;
        QToolTip::showText(helpEvent->globalPos(), text);
        return true;
      }
    }
    else
    {
      QToolTip::hideText();
      ev->ignore();
      return true;
    }
  }

  return Base::event(ev);
}
