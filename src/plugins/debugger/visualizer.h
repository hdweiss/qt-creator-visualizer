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

#ifndef DEBUGGER_VISUALIZER_H
#define DEBUGGER_VISUALIZER_H

#include "basewindow.h"

namespace Debugger {
namespace Internal {

/////////////////////////////////////////////////////////////////////
//
// Visualizer
//
/////////////////////////////////////////////////////////////////////

class VisualizerWindow : public QWidget
{
    Q_OBJECT

public:
    enum Type { ReturnType, LocalsType, TooltipType, WatchersType };

    explicit VisualizerWindow(Type type, QWidget *parent = 0);
    Type type() const { return m_type; }
    void setModel(QAbstractItemModel *model);

public slots:
    void watchExpression(const QString &exp);
    void removeWatchExpression(const QString &exp);

private:
    Q_SLOT void resetHelper();

    void keyPressEvent(QKeyEvent *ev);
    void contextMenuEvent(QContextMenuEvent *ev);
    void dragEnterEvent(QDragEnterEvent *ev);
    void dropEvent(QDropEvent *ev);
    void dragMoveEvent(QDragMoveEvent *ev);
    void mouseDoubleClickEvent(QMouseEvent *ev);
    bool event(QEvent *ev);

    void resetHelper(const QModelIndex &idx);

    void setModelData(int role, const QVariant &value = QVariant(),
        const QModelIndex &index = QModelIndex());

    Type m_type;
    bool m_grabbing;
};


} // namespace Internal
} // namespace Debugger

#endif // DEBUGGER_VISUALIZERWINDOW_H
