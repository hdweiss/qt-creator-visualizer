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

#ifndef ICORE_H
#define ICORE_H

#include "core_global.h"

#include <QObject>
#include <QSettings>

QT_BEGIN_NAMESPACE
class QMainWindow;
class QPrinter;
class QStatusBar;
template <class T> class QList;
QT_END_NAMESPACE


namespace Core {
class IWizard;
class ActionManager;
class Context;
class EditorManager;
class DocumentManager;
class HelpManager;
class IContext;
class MessageManager;
class MimeDatabase;
class ModeManager;
class ProgressManager;
class ScriptManager;
class SettingsDatabase;
class VariableManager;
class VcsManager;

namespace Internal { class MainWindow; }

class CORE_EXPORT ICore : public QObject
{
    Q_OBJECT

    friend class Internal::MainWindow;
    explicit ICore(Internal::MainWindow *mw);
    ~ICore();

public:
    // This should only be used to acccess the signals, so it could
    // theoretically return an QObject *. For source compatibility
    // it returns a ICore.
    static ICore *instance();

    static void showNewItemDialog(const QString &title,
                                  const QList<IWizard *> &wizards,
                                  const QString &defaultLocation = QString());

    static bool showOptionsDialog(const QString &group = QString(),
                                  const QString &page = QString(),
                                  QWidget *parent = 0);

    static bool showWarningWithOptions(const QString &title, const QString &text,
                                       const QString &details = QString(),
                                       const QString &settingsCategory = QString(),
                                       const QString &settingsId = QString(),
                                       QWidget *parent = 0);

    static ActionManager *actionManager();
    static QT_DEPRECATED DocumentManager *documentManager(); // Use DocumentManager::... directly.
    static MessageManager *messageManager();
    static EditorManager *editorManager();
    static ProgressManager *progressManager();
    static ScriptManager *scriptManager();
    static VariableManager *variableManager();
    static VcsManager *vcsManager();
    static QT_DEPRECATED ModeManager *modeManager(); // Use ModeManager::... directly.
    static MimeDatabase *mimeDatabase();
    static HelpManager *helpManager();

    static QSettings *settings(QSettings::Scope scope = QSettings::UserScope);
    static SettingsDatabase *settingsDatabase();
    static QPrinter *printer();
    static QString userInterfaceLanguage();

    static QString resourcePath();
    static QString userResourcePath();

    static QMainWindow *mainWindow();
    static QStatusBar *statusBar();

    static IContext *currentContextObject();
    // Adds and removes additional active contexts, these contexts are appended
    // to the currently active contexts.
    static void updateAdditionalContexts(const Context &remove, const Context &add);
    static bool hasContext(int context);
    static void addContextObject(IContext *context);
    static void removeContextObject(IContext *context);

    enum OpenFilesFlags {
        None = 0,
        SwitchMode = 1,
        CanContainLineNumbers = 2,
         /// Stop loading once the first file fails to load
        StopOnLoadFail = 4
    };
    static void openFiles(const QStringList &fileNames, OpenFilesFlags flags = None);

    static void emitNewItemsDialogRequested();

signals:
    void coreAboutToOpen();
    void coreOpened();
    void newItemsDialogRequested();
    void saveSettingsRequested();
    void optionsDialogRequested();
    void coreAboutToClose();
    void contextAboutToChange(Core::IContext *context);
    void contextChanged(Core::IContext *context, const Core::Context &additionalContexts);
};

} // namespace Core

#endif // ICORE_H
