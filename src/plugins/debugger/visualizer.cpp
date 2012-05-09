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

#include "visualizer.h"
#include "visualizer_link.h"
#include "visualizer_node.h"

#include "breakhandler.h"
#include "registerhandler.h"
#include "debuggeractions.h"
#include "debuggerconstants.h"
#include "debuggerinternalconstants.h"
#include "debuggercore.h"
#include "debuggerdialogs.h"
#include "debuggerengine.h"
#include "debuggerstartparameters.h"
#include "watchdelegatewidgets.h"
#include "watchhandler.h"
#include "debuggertooltipmanager.h"
#include "memoryagent.h"

#include <utils/qtcassert.h>
#include <utils/savedaction.h>

#include <QDebug>
#include <QMetaObject>
#include <QMetaProperty>
#include <QVariant>
#include <QMimeData>

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QScrollBar>
#include <QApplication>
#include <QPalette>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QItemDelegate>
#include <QMenu>
#include <QPainter>
#include <QInputDialog>
#include <QMessageBox>

namespace Debugger {
namespace Internal {

static DebuggerEngine *currentEngine()
{
    return debuggerCore()->currentEngine();
}

VisualizerNode* VisualizerWindow::addNode(QString *name) {
    VisualizerNode *node = new VisualizerNode(viewport());
    node->setText(*name);
    node->setPos(QPoint(40, 50));

    scene->addItem(node);
    return node;
}

void VisualizerWindow::addLink(VisualizerNode* node1, VisualizerNode* node2) {
    VisualizerLink *link = new VisualizerLink(node1, node2);
    scene->addItem(link);
}

void VisualizerWindow::setupNodes()
{
    VisualizerNode *node1 = addNode(new QString("Node 1"));
    VisualizerNode *node2 = addNode(new QString("Node 2"));
    addLink(node1, node2);
}

VisualizerWindow::VisualizerWindow(QWidget *parent)
    : QAbstractItemView(parent)
{
    setObjectName(QLatin1String("VisualizerWindow"));
    setWindowTitle(tr("Visualizer"));
    setAcceptDrops(true);
    qDebug() << "VisualizeWindow()";

    scene = new QGraphicsScene(0, 0, 600, 500);
    gview = new QGraphicsView(scene);

    setViewport(gview);

    setupNodes();
}

void VisualizerWindow::paintEvent(QPaintEvent *event) {
}

void VisualizerWindow::mouseReleaseEvent(QMouseEvent *event)
{
    QAbstractItemView::mouseReleaseEvent(event);
    viewport()->update();
}

QModelIndex VisualizerWindow::moveCursor(QAbstractItemView::CursorAction cursorAction,
                                         Qt::KeyboardModifiers modifiers)
{
    QModelIndex current = currentIndex();
    viewport()->update();
    return current;
}

int VisualizerWindow::horizontalOffset() const
{
    return horizontalScrollBar()->value();
}

int VisualizerWindow::verticalOffset() const
{
    return verticalScrollBar()->value();
}

void VisualizerWindow::resizeEvent(QResizeEvent * /* event */)
{
    updateGeometries();
}

void VisualizerWindow::rowsInserted(const QModelIndex &parent, int start, int end)
{
    for (int row = start; row <= end; ++row) {

        QModelIndex index = model()->index(row, 1, rootIndex());
        double value = model()->data(index).toDouble();
    }

    QAbstractItemView::rowsInserted(parent, start, end);
}

void VisualizerWindow::rowsAboutToBeRemoved(const QModelIndex &parent, int start, int end)
{
    for (int row = start; row <= end; ++row) {

        QModelIndex index = model()->index(row, 1, rootIndex());
        double value = model()->data(index).toDouble();
    }

    QAbstractItemView::rowsAboutToBeRemoved(parent, start, end);
}

void VisualizerWindow::scrollContentsBy(int dx, int dy)
{
    viewport()->scroll(dx, dy);
}

void VisualizerWindow::dataChanged(const QModelIndex &topLeft,
                                   const QModelIndex &bottomRight)
{
    QAbstractItemView::dataChanged(topLeft, bottomRight);

    for (int row = 0; row < model()->rowCount(rootIndex()); ++row) {

        QModelIndex index = model()->index(row, 1, rootIndex());
        double value = model()->data(index).toDouble();

    }
    viewport()->update();
}

bool VisualizerWindow::isIndexHidden(const QModelIndex & /*index*/) const
{
    return false;
}

QModelIndex VisualizerWindow::indexAt(const QPoint &point) const
{
    return QModelIndex();
}

QRect VisualizerWindow::visualRect(const QModelIndex &index) const
{
    QRect rect = QRect();
    if (rect.isValid())
        return QRect(rect.left() - horizontalScrollBar()->value(),
                     rect.top() - verticalScrollBar()->value(),
                     rect.width(), rect.height());
    else
        return rect;
}

void VisualizerWindow::scrollTo(const QModelIndex &index, ScrollHint)
{
    QRect area = viewport()->rect();
    QRect rect = visualRect(index);

    if (rect.left() < area.left())
        horizontalScrollBar()->setValue(
                    horizontalScrollBar()->value() + rect.left() - area.left());
    else if (rect.right() > area.right())
        horizontalScrollBar()->setValue(
                    horizontalScrollBar()->value() + qMin(
                        rect.right() - area.right(), rect.left() - area.left()));

    if (rect.top() < area.top())
        verticalScrollBar()->setValue(
                    verticalScrollBar()->value() + rect.top() - area.top());
    else if (rect.bottom() > area.bottom())
        verticalScrollBar()->setValue(
                    verticalScrollBar()->value() + qMin(
                        rect.bottom() - area.bottom(), rect.top() - area.top()));

    update();
}


void VisualizerWindow::setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags command)
{
}

QRegion VisualizerWindow::visualRegionForSelection(const QItemSelection &selection) const
{
    int ranges = selection.count();

    if (ranges == 0)
        return QRect();

    // Note that we use the top and bottom functions of the selection range
    // since the data is stored in rows.

    int firstRow = selection.at(0).top();
    int lastRow = selection.at(0).top();

    for (int i = 0; i < ranges; ++i) {
        firstRow = qMin(firstRow, selection.at(i).top());
        lastRow = qMax(lastRow, selection.at(i).bottom());
    }

    QModelIndex firstItem = model()->index(qMin(firstRow, lastRow), 0, rootIndex());
    QModelIndex lastItem = model()->index(qMax(firstRow, lastRow), 0, rootIndex());

    QRect firstRect = visualRect(firstItem);
    QRect lastRect = visualRect(lastItem);

    return firstRect.unite(lastRect);
}


void VisualizerWindow::watchExpression(const QString &exp)
{
}

void VisualizerWindow::removeWatchExpression(const QString &exp)
{
}


} // namespace Internal
} // namespace Debugger

