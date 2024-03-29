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

#ifndef TARGETSETUPPAGE_H
#define TARGETSETUPPAGE_H

#include "../qt4target.h"
#include "../qt4projectmanager_global.h"
#include <qtsupport/qtversionmanager.h>
#include <coreplugin/featureprovider.h>

#include <QString>
#include <QWizard>


QT_BEGIN_NAMESPACE
class QLabel;
class QMenu;
class QPushButton;
class QSpacerItem;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
QT_END_NAMESPACE

namespace Qt4ProjectManager {
class Qt4Project;

namespace Internal {
namespace Ui {
class TargetSetupPage;
}
}

/// \internal
class QT4PROJECTMANAGER_EXPORT TargetSetupPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit TargetSetupPage(QWidget* parent = 0);
    ~TargetSetupPage();

    /// Initializes the TargetSetupPage
    /// \note The import information is gathered in initializePage(), make sure that the right proFilePath is set before
    void initializePage();
    /// Changes the default set of checked targets.
    /// call this before \sa initializePage()
    void setPreferredFeatures(const QSet<QString> &featureIds);
    /// Sets the features a target must support
    /// call this before \sa initializePage()
    void setRequiredTargetFeatures(const QSet<QString> &featureIds);
    /// Sets the features a qt version must support
    /// call this before \sa initializePage()
    void setRequiredQtFeatures(const Core::FeatureSet &features);
    /// Sets the platform that was selected in the wizard
    void setSelectedPlatform(const QString &platform);
    /// Sets the minimum qt version
    /// calls this before \sa initializePage()
    void setMinimumQtVersion(const QtSupport::QtVersionNumber &number);
    /// Sets the maximum qt version
    /// calls this before \sa initializePage()
    void setMaximumQtVersion(const QtSupport::QtVersionNumber &number);
    /// Sets whether the TargetSetupPage looks on disk for builds of this project
    /// call this before \sa initializePage()
    void setImportSearch(bool b);

    /// Sets whether the targetsetupage uses a scrollarea
    /// to host the widgets from the factories
    /// call this before \sa initializePage()
    void setUseScrollArea(bool b);

    bool isComplete() const;
    bool setupProject(Qt4ProjectManager::Qt4Project *project);
    bool isTargetSelected(const QString &id) const;
    void setProFilePath(const QString &dir);

    /// Overrides the summary text of the targetsetuppage
    void setNoteText(const QString &text);
signals:
    void noteTextLinkActivated();

private slots:
    void newImportBuildConfiguration(const BuildConfigurationInfo &info);

private:
    void setupImportInfos();
    void cleanupImportInfos();
    void setupWidgets();
    void deleteWidgets();

    QSet<QString> m_preferredFeatures;
    QSet<QString> m_requiredTargetFeatures;
    Core::FeatureSet m_requiredQtFeatures;
    QString m_selectedPlatform;
    bool m_importSearch;
    bool m_useScrollArea;
    QtSupport::QtVersionNumber m_minimumQtVersionNumber;
    QtSupport::QtVersionNumber m_maximumQtVersionNumber;
    QString m_proFilePath;
    QString m_defaultShadowBuildLocation;
    QMap<QString, Qt4TargetSetupWidget *> m_widgets;
    QHash<Qt4TargetSetupWidget *, Qt4BaseTargetFactory *> m_factories;

    QSpacerItem *m_spacer;
    Internal::Ui::TargetSetupPage *m_ui;
    QList<BuildConfigurationInfo> m_importInfos;
};

} // namespace Qt4ProjectManager

#endif // TARGETSETUPPAGE_H
