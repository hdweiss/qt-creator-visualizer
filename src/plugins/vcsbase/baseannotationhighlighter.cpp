/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#include "baseannotationhighlighter.h"

#include <math.h>
#include <QSet>
#include <QDebug>
#include <QColor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextCharFormat>

typedef QMap<QString, QTextCharFormat> ChangeNumberFormatMap;

/*!
    \class VcsBase::BaseAnnotationHighlighter

    \brief Base for a highlighter for annotation lines of the form 'changenumber:XXXX'.

    The change numbers are assigned a color gradient. Example:
    \code
    112: text1 <color 1>
    113: text2 <color 2>
    112: text3 <color 1>
    \endcode
*/

namespace VcsBase {
namespace Internal {

class BaseAnnotationHighlighterPrivate
{
public:
    ChangeNumberFormatMap m_changeNumberMap;
};

} // namespace Internal

BaseAnnotationHighlighter::BaseAnnotationHighlighter(const ChangeNumbers &changeNumbers,
                                             QTextDocument *document) :
    TextEditor::SyntaxHighlighter(document),
    d(new Internal::BaseAnnotationHighlighterPrivate)
{
    setChangeNumbers(changeNumbers);
}

BaseAnnotationHighlighter::~BaseAnnotationHighlighter()
{
    delete d;
}

void BaseAnnotationHighlighter::setChangeNumbers(const ChangeNumbers &changeNumbers)
{
    d->m_changeNumberMap.clear();
    if (!changeNumbers.isEmpty()) {
        // Assign a color gradient to annotation change numbers. Give
        // each change number a unique color.
        const double oneThird = 1.0 / 3.0;
        const int step = qRound(ceil(pow(double(changeNumbers.count()), oneThird)));
        QList<QColor> colors;
        const int factor = 255 / step;
        for (int i=0; i<step; ++i)
            for (int j=0; j<step; ++j)
                for (int k=0; k<step; ++k)
                    colors.append(QColor(i*factor, j*factor, k*factor));

        int m = 0;
        const int cstep = colors.count() / changeNumbers.count();
        const ChangeNumbers::const_iterator cend = changeNumbers.constEnd();
        for (ChangeNumbers::const_iterator it =  changeNumbers.constBegin(); it != cend; ++it) {
            QTextCharFormat format;
            format.setForeground(colors.at(m));
            d->m_changeNumberMap.insert(*it, format);
            m += cstep;
        }
    }
}

void BaseAnnotationHighlighter::highlightBlock(const QString &text)
{
    if (text.isEmpty() || d->m_changeNumberMap.empty())
        return;
    const QString change = changeNumber(text);
    const ChangeNumberFormatMap::const_iterator it = d->m_changeNumberMap.constFind(change);
    if (it != d->m_changeNumberMap.constEnd())
        setFormat(0, text.length(), it.value());
}

} // namespace VcsBase
