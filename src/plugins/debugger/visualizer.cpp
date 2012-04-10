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


class VisualizerDelegate : public QItemDelegate
{
public:
    explicit VisualizerDelegate(VisualizerWindow *parent)
        : QItemDelegate(parent), m_visualizerWindow(parent)
    {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
        const QModelIndex &index) const
    {
        // Value column: Custom editor. Apply integer-specific settings.
        if (index.column() == 1) {
            const QVariant::Type type =
                static_cast<QVariant::Type>(index.data(LocalsEditTypeRole).toInt());
            switch (type) {
            case QVariant::Bool:
                return new BooleanComboBox(parent);
            default:
                break;
            }
            WatchLineEdit *edit = WatchLineEdit::create(type, parent);
            edit->setFrame(false);
            IntegerWatchLineEdit *intEdit
                = qobject_cast<IntegerWatchLineEdit *>(edit);
            if (intEdit)
                intEdit->setBase(index.data(LocalsIntegerBaseRole).toInt());
            return edit;
        }

        // Standard line edits for the rest.
        QLineEdit *lineEdit = new QLineEdit(parent);
        lineEdit->setFrame(false);
        return lineEdit;
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const
    {
        // Standard handling for anything but the watcher name column (change
        // expression), which removes/recreates a row, which cannot be done
        // in model->setData().
        if (index.column() != 0) {
            QItemDelegate::setModelData(editor, model, index);
            return;
        }
        const QMetaProperty userProperty = editor->metaObject()->userProperty();
        QTC_ASSERT(userProperty.isValid(), return);
        const QString value = editor->property(userProperty.name()).toString();
        const QString exp = index.data(LocalsExpressionRole).toString();
        if (exp == value)
            return;
        m_visualizerWindow->removeWatchExpression(exp);
        m_visualizerWindow->watchExpression(value);
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
        const QModelIndex &) const
    {
        editor->setGeometry(option.rect);
    }

private:
    VisualizerWindow *m_visualizerWindow;
};

// Watch model query helpers.
static inline quint64 addressOf(const QModelIndex &m)
{
    return m.data(LocalsAddressRole).toULongLong();
}

static inline quint64 pointerValueOf(const QModelIndex &m)
{
    return m.data(LocalsPointerValueRole).toULongLong();
}

static inline QString nameOf(const QModelIndex &m)
{
    return m.data().toString();
}

static inline QString typeOf(const QModelIndex &m)
{
    return m.data(LocalsTypeRole).toString();
}

static inline uint sizeOf(const QModelIndex &m)
{
    return m.data(LocalsSizeRole).toUInt();
}

static DebuggerEngine *currentEngine()
{
    return debuggerCore()->currentEngine();
}

/////////////////////////////////////////////////////////////////////
//
// VisualizerWindow
//
/////////////////////////////////////////////////////////////////////

VisualizerWindow::VisualizerWindow(Type type, QWidget *parent)
  : BaseWindow(parent),
    m_type(type)
{
    setObjectName(QLatin1String("VisualizerWindow"));
    m_grabbing = false;
    setWindowTitle(tr("Visualizer"));
    setIndentation(indentation() * 9/10);
    setUniformRowHeights(true);
    setItemDelegate(new VisualizerDelegate(this));
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setAlwaysAdjustColumnsAction(debuggerCore()->action(AlwaysAdjustLocalsColumnWidths));

    connect(this, SIGNAL(expanded(QModelIndex)),
        SLOT(expandNode(QModelIndex)));
    connect(this, SIGNAL(collapsed(QModelIndex)),
        SLOT(collapseNode(QModelIndex)));
}

void VisualizerWindow::expandNode(const QModelIndex &idx)
{
    setModelData(LocalsExpandedRole, true, idx);
}

void VisualizerWindow::collapseNode(const QModelIndex &idx)
{
    setModelData(LocalsExpandedRole, false, idx);
}

void VisualizerWindow::keyPressEvent(QKeyEvent *ev)
{
    if (ev->key() == Qt::Key_Delete && m_type == WatchersType) {
        QModelIndexList indices = selectionModel()->selectedRows();
        if (indices.isEmpty() && selectionModel()->currentIndex().isValid())
            indices.append(selectionModel()->currentIndex());
        QStringList exps;
        foreach (const QModelIndex &idx, indices) {
            QModelIndex idx1 = idx.sibling(idx.row(), 0);
            exps.append(idx1.data(LocalsRawExpressionRole).toString());
        }
        foreach (const QString &exp, exps)
            removeWatchExpression(exp);
    } else if (ev->key() == Qt::Key_Return
            && ev->modifiers() == Qt::ControlModifier
            && m_type == LocalsType) {
        QModelIndex idx = currentIndex();
        QModelIndex idx1 = idx.sibling(idx.row(), 0);
        QString exp = model()->data(idx1).toString();
        watchExpression(exp);
    }
    QTreeView::keyPressEvent(ev);
}

void VisualizerWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    //QTreeView::dragEnterEvent(ev);
    if (ev->mimeData()->hasText()) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
    }
}

void VisualizerWindow::dragMoveEvent(QDragMoveEvent *ev)
{
    //QTreeView::dragMoveEvent(ev);
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
    //QTreeView::dropEvent(ev);
}

void VisualizerWindow::mouseDoubleClickEvent(QMouseEvent *ev)
{
    const QModelIndex idx = indexAt(ev->pos());
    if (!idx.isValid()) {
        // The "<Edit>" case.
        watchExpression(QString());
        return;
    }
    QTreeView::mouseDoubleClickEvent(ev);
}

// Text for add watch action with truncated expression.
static QString addWatchActionText(QString exp)
{
    if (exp.isEmpty())
        return VisualizerWindow::tr("Evaluate Expression");
    if (exp.size() > 30) {
        exp.truncate(30);
        exp.append(QLatin1String("..."));
    }
    return VisualizerWindow::tr("Evaluate Expression \"%1\"").arg(exp);
}

// Text for add watch action with truncated expression.
static QString removeWatchActionText(QString exp)
{
    if (exp.isEmpty())
        return VisualizerWindow::tr("Remove Evaluated Expression");
    if (exp.size() > 30) {
        exp.truncate(30);
        exp.append(QLatin1String("..."));
    }
    return VisualizerWindow::tr("Remove Evaluated Expression \"%1\"").arg(exp);
}

static void copyToClipboard(const QString &clipboardText)
{
    QClipboard *clipboard = QApplication::clipboard();
#ifdef Q_WS_X11
    clipboard->setText(clipboardText, QClipboard::Selection);
#endif
    clipboard->setText(clipboardText, QClipboard::Clipboard);
}

void VisualizerWindow::contextMenuEvent(QContextMenuEvent *ev)
{
    DebuggerEngine *engine = currentEngine();
    WatchHandler *handler = engine->watchHandler();
}

bool VisualizerWindow::event(QEvent *ev)
{
    if (m_grabbing && ev->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mev = static_cast<QMouseEvent *>(ev);
        m_grabbing = false;
        releaseMouse();
        currentEngine()->watchPoint(mapToGlobal(mev->pos()));
    }
    return QTreeView::event(ev);
}

void VisualizerWindow::editItem(const QModelIndex &idx)
{
    Q_UNUSED(idx) // FIXME
}

void VisualizerWindow::setModel(QAbstractItemModel *model)
{
    BaseWindow::setModel(model);
    setRootIsDecorated(true);
    if (header()) {
        header()->setDefaultAlignment(Qt::AlignLeft);
        if (m_type != LocalsType)
            header()->hide();
    }

    connect(model, SIGNAL(layoutChanged()), SLOT(resetHelper()));
}

void VisualizerWindow::resetHelper()
{
    resetHelper(model()->index(0, 0));
}

void VisualizerWindow::resetHelper(const QModelIndex &idx)
{
    if (idx.data(LocalsExpandedRole).toBool()) {
        //qDebug() << "EXPANDING " << model()->data(idx, INameRole);
        if (!isExpanded(idx)) {
            expand(idx);
            for (int i = 0, n = model()->rowCount(idx); i != n; ++i) {
                QModelIndex idx1 = model()->index(i, 0, idx);
                resetHelper(idx1);
            }
        }
    } else {
        //qDebug() << "COLLAPSING " << model()->data(idx, INameRole);
        if (isExpanded(idx))
            collapse(idx);
    }
}

void VisualizerWindow::watchExpression(const QString &exp)
{
    currentEngine()->watchHandler()->watchExpression(exp);
}

void VisualizerWindow::removeWatchExpression(const QString &exp)
{
    currentEngine()->watchHandler()->removeWatchExpression(exp);
}

void VisualizerWindow::setModelData
    (int role, const QVariant &value, const QModelIndex &index)
{
    QTC_ASSERT(model(), return);
    model()->setData(index, value, role);
}

void VisualizerWindow::setWatchpointAtAddress(quint64 address, unsigned size)
{
    BreakpointParameters data(WatchpointAtAddress);
    data.address = address;
    data.size = size;
    BreakpointModelId id = breakHandler()->findWatchpoint(data);
    if (id) {
        qDebug() << "WATCHPOINT EXISTS";
        //   removeBreakpoint(index);
        return;
    }
    breakHandler()->appendBreakpoint(data);
}

void VisualizerWindow::setWatchpointAtExpression(const QString &exp)
{
    BreakpointParameters data(WatchpointAtExpression);
    data.expression = exp;
    BreakpointModelId id = breakHandler()->findWatchpoint(data);
    if (id) {
        qDebug() << "WATCHPOINT EXISTS";
        //   removeBreakpoint(index);
        return;
    }
    breakHandler()->appendBreakpoint(data);
}

} // namespace Internal
} // namespace Debugger

