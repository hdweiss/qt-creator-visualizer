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

VisualizerWindow::VisualizerWindow(Type type, QWidget *parent)
  : m_type(type)
{
    setObjectName(QLatin1String("VisualizerWindow"));
    m_grabbing = false;
    setWindowTitle(tr("Visualizer"));
    setAcceptDrops(true);
    drawCircle();
}

void drawCircle() {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::black, 12, Qt::DashDotLine, Qt::RoundCap));
    painter.setBrush(QBrush(Qt::green, Qt::SolidPattern));
    painter.drawEllipse(80, 80, 400, 240);
}

void VisualizerWindow::keyPressEvent(QKeyEvent *ev)
{
}

void VisualizerWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasText()) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
    }
}

void VisualizerWindow::dragMoveEvent(QDragMoveEvent *ev)
{
    if (ev->mimeData()->hasText()) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
    }
}

void VisualizerWindow::dropEvent(QDropEvent *ev)
{
    if (ev->mimeData()->hasText()) {
        watchExpression(ev->mimeData()->text());
        //ev->acceptProposedAction();
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
    }
}

void VisualizerWindow::mouseDoubleClickEvent(QMouseEvent *ev)
{
}

void VisualizerWindow::contextMenuEvent(QContextMenuEvent *ev)
{
    DebuggerEngine *engine = currentEngine();
    WatchHandler *handler = engine->watchHandler();
}

bool VisualizerWindow::event(QEvent *ev)
{
    return false;
}


void VisualizerWindow::watchExpression(const QString &exp)
{
}

void VisualizerWindow::removeWatchExpression(const QString &exp)
{
}

// Relics from AbstractItem

void VisualizerWindow::resetHelper(const QModelIndex &idx)
{
}

void VisualizerWindow::setModel(QAbstractItemModel *model)
{
}

void VisualizerWindow::setModelData
    (int role, const QVariant &value, const QModelIndex &index)
{
    //QTC_ASSERT(model(), return);
    //model()->setData(index, value, role);
}

} // namespace Internal
} // namespace Debugger

