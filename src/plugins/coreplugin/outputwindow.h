/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2011 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (info@qt.nokia.com)
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

#ifndef OUTPUTWINDOW_H
#define OUTPUTWINDOW_H

#include <utils/outputformatter.h>
#include <coreplugin/icontext.h>

#include <QtGui/QPlainTextEdit>

namespace Core {
    class IContext;
}

namespace ProjectExplorer {

namespace Internal {

class OutputWindow : public QPlainTextEdit
{
    Q_OBJECT

public:
    OutputWindow(Core::Context context, QWidget *parent = 0);
    ~OutputWindow();

    Utils::OutputFormatter* formatter() const;
    void setFormatter(Utils::OutputFormatter *formatter);

    void appendMessage(const QString &out, Utils::OutputFormat format);
    /// appends a \p text using \p format without using formater
    void appendText(const QString &text, const QTextCharFormat &format, int maxLineCount);

    void grayOutOldContent();
    void clear();

    void showEvent(QShowEvent *);

    void scrollToBottom();

public slots:
    void setWordWrapEnabled(bool wrap);

protected:
    bool isScrollbarAtBottom() const;

    virtual void mousePressEvent(QMouseEvent *e);
    virtual void mouseReleaseEvent(QMouseEvent *e);
    virtual void mouseMoveEvent(QMouseEvent *e);
    virtual void resizeEvent(QResizeEvent *e);
    virtual void keyPressEvent(QKeyEvent *ev);

private:
    void enableUndoRedo();
    QString doNewlineEnfocement(const QString &out);

    Core::IContext *m_outputWindowContext;
    Utils::OutputFormatter *m_formatter;

    bool m_enforceNewline;
    bool m_scrollToBottom;
    bool m_linksActive;
    bool m_mousePressed;
};

} // namespace Internal
} // namespace ProjectExplorer

#endif // OUTPUTWINDOW_H