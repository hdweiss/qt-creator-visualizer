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
#include "visualizer_link.h"
#include "visualizer_node.h"
#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>

namespace Debugger {
namespace Internal {

/////////////////////////////////////////////////////////////////////
//
// Visualizer
//
/////////////////////////////////////////////////////////////////////

class VisualizerWindow : public QAbstractItemView
{
    Q_OBJECT

public:
    explicit VisualizerWindow(QWidget *parent = 0);
    QRect visualRect(const QModelIndex &index) const;
    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible);
    QModelIndex indexAt(const QPoint &point) const;

protected slots:
    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void rowsInserted(const QModelIndex &parent, int start, int end);
    void rowsAboutToBeRemoved(const QModelIndex &parent, int start, int end);

protected:

    QModelIndex moveCursor(QAbstractItemView::CursorAction cursorAction,
                           Qt::KeyboardModifiers modifiers);

    int horizontalOffset() const;
    int verticalOffset() const;

    bool isIndexHidden(const QModelIndex &index) const;

    void setSelection(const QRect&, QItemSelectionModel::SelectionFlags command);

    void mouseReleaseEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
    void scrollContentsBy(int dx, int dy);

    QRegion visualRegionForSelection(const QItemSelection &selection) const;

private:
    VisualizerNode* addNode(QString *name);
    void addLink(VisualizerNode *node1, VisualizerNode *node2);
    void setupNodes();
    QString getWatchData(QModelIndex index);

    QGraphicsScene scene;
    QGraphicsView gview;

    // Hack
    QMultiHash<QString, VisualizerNode*> nodemap;

};


} // namespace Internal
} // namespace Debugger

#endif // DEBUGGER_VISUALIZERWINDOW_H
