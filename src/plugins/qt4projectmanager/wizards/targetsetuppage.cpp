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

#include "targetsetuppage.h"

#include "ui_targetsetuppage.h"
#include "buildconfigurationinfo.h"
#include "qt4project.h"
#include "qt4projectmanager.h"
#include "qt4projectmanagerconstants.h"
#include "qt4target.h"
#include "qt4basetargetfactory.h"

#include <extensionsystem/pluginmanager.h>
#include <projectexplorer/task.h>
#include <projectexplorer/taskhub.h>
#include <projectexplorer/toolchainmanager.h>
#include <projectexplorer/toolchain.h>
#include <qtsupport/qtversionfactory.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QLabel>
#include <QLayout>

using namespace Qt4ProjectManager;

TargetSetupPage::TargetSetupPage(QWidget *parent) :
    QWizardPage(parent),
    m_importSearch(false),
    m_useScrollArea(true),
    m_maximumQtVersionNumber(INT_MAX, INT_MAX, INT_MAX),
    m_spacer(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding)),
    m_ui(new Internal::Ui::TargetSetupPage)
{
    m_ui->setupUi(this);
    QWidget *centralWidget = new QWidget(this);
    m_ui->scrollArea->setWidget(centralWidget);
    centralWidget->setLayout(new QVBoxLayout);
    m_ui->centralWidget->setLayout(new QVBoxLayout);
    m_ui->centralWidget->layout()->setMargin(0);

    setTitle(tr("Target Setup"));

    connect(m_ui->descriptionLabel, SIGNAL(linkActivated(QString)),
            this, SIGNAL(noteTextLinkActivated()));
}

void TargetSetupPage::initializePage()
{
    cleanupImportInfos();
    deleteWidgets();

    setupImportInfos();
    setupWidgets();
}

TargetSetupPage::~TargetSetupPage()
{
    deleteWidgets();
    delete m_ui;
    cleanupImportInfos();
}

bool TargetSetupPage::isTargetSelected(const QString &id) const
{
    Qt4TargetSetupWidget *widget = m_widgets.value(id);
    return widget && widget->isTargetSelected();
}

bool TargetSetupPage::isComplete() const
{
    foreach (Qt4TargetSetupWidget *widget, m_widgets)
        if (widget->isTargetSelected())
            return true;
    return false;
}

void TargetSetupPage::setPreferredFeatures(const QSet<QString> &featureIds)
{
    m_preferredFeatures = featureIds;
}

void TargetSetupPage::setRequiredTargetFeatures(const QSet<QString> &featureIds)
{
    m_requiredTargetFeatures = featureIds;
}

void TargetSetupPage::setRequiredQtFeatures(const Core::FeatureSet &features)
{
    m_requiredQtFeatures = features;
}

void TargetSetupPage::setSelectedPlatform(const QString &platform)
{
    m_selectedPlatform = platform;
}

void TargetSetupPage::setMinimumQtVersion(const QtSupport::QtVersionNumber &number)
{
    m_minimumQtVersionNumber = number;
}

void TargetSetupPage::setMaximumQtVersion(const QtSupport::QtVersionNumber &number)
{
    m_maximumQtVersionNumber = number;
}

void TargetSetupPage::setImportSearch(bool b)
{
    m_importSearch = b;
}

void TargetSetupPage::setupWidgets()
{
    QLayout *layout = 0;
    if (m_useScrollArea)
        layout = m_ui->scrollArea->widget()->layout();
    else
        layout = m_ui->centralWidget->layout();

    // Target Page setup
    QList<Qt4BaseTargetFactory *> factories = ExtensionSystem::PluginManager::instance()->getObjects<Qt4BaseTargetFactory>();
    bool atLeastOneTargetSelected = false;
    foreach (Qt4BaseTargetFactory *factory, factories) {
        QStringList ids = factory->supportedTargetIds();
        foreach (const QString &id, ids) {
            if (!factory->targetFeatures(id).contains(m_requiredTargetFeatures))
                continue;

            QList<BuildConfigurationInfo> infos = BuildConfigurationInfo::filterBuildConfigurationInfos(m_importInfos, id);
            const QList<BuildConfigurationInfo> platformFilteredInfos =
                    BuildConfigurationInfo::filterBuildConfigurationInfosByPlatform(factory->availableBuildConfigurations(id,
                                                                                                                          m_proFilePath,
                                                                                                                          m_minimumQtVersionNumber,
                                                                                                                          m_maximumQtVersionNumber,
                                                                                                                          m_requiredQtFeatures),
                                                                                    m_selectedPlatform);


            Qt4TargetSetupWidget *widget =
                    factory->createTargetSetupWidget(id, m_proFilePath,
                                                     m_minimumQtVersionNumber,
                                                     m_maximumQtVersionNumber,
                                                     m_requiredQtFeatures,
                                                     m_importSearch, infos);
            if (widget) {
                bool selectTarget = false;
                if (!m_importInfos.isEmpty()) {
                    selectTarget = !infos.isEmpty();
                } else {
                    if (!m_preferredFeatures.isEmpty()) {
                        selectTarget = factory->targetFeatures(id).contains(m_preferredFeatures)
                                && factory->selectByDefault(id);
                    }
                    if (!m_selectedPlatform.isEmpty()) {
                        selectTarget = !platformFilteredInfos.isEmpty();
                    }
                }
                widget->setTargetSelected(selectTarget);
                atLeastOneTargetSelected |= selectTarget;
                m_widgets.insert(id, widget);
                m_factories.insert(widget, factory);
                layout->addWidget(widget);
                connect(widget, SIGNAL(selectedToggled()),
                        this, SIGNAL(completeChanged()));
                connect(widget, SIGNAL(newImportBuildConfiguration(BuildConfigurationInfo)),
                        this, SLOT(newImportBuildConfiguration(BuildConfigurationInfo)));
            }
        }
    }
    if (!atLeastOneTargetSelected) {
        Qt4TargetSetupWidget *widget = m_widgets.value(QLatin1String(Constants::DESKTOP_TARGET_ID));
        if (widget)
            widget->setTargetSelected(true);
    }

    if (m_useScrollArea)
        layout->addItem(m_spacer);
    if (m_widgets.isEmpty()) {
        // Oh no one can create any targets
        m_ui->scrollArea->setVisible(false);
        m_ui->centralWidget->setVisible(false);
        m_ui->descriptionLabel->setVisible(false);
        m_ui->noValidQtVersionsLabel->setVisible(true);
    } else {
        m_ui->scrollArea->setVisible(m_useScrollArea);
        m_ui->centralWidget->setVisible(!m_useScrollArea);
        m_ui->descriptionLabel->setVisible(true);
        m_ui->noValidQtVersionsLabel->setVisible(false);
    }
}

void TargetSetupPage::deleteWidgets()
{
    QLayout *layout = 0;
    if (m_useScrollArea)
        layout = m_ui->scrollArea->widget()->layout();
    else
        layout = m_ui->centralWidget->layout();
    foreach (Qt4TargetSetupWidget *widget, m_widgets)
        delete widget;
    m_widgets.clear();
    m_factories.clear();
    if (m_useScrollArea)
        layout->removeItem(m_spacer);
}

void TargetSetupPage::setProFilePath(const QString &path)
{
    m_proFilePath = path;
    if (!m_proFilePath.isEmpty()) {
        m_ui->descriptionLabel->setText(tr("Qt Creator can set up the following targets for project <b>%1</b>:",
                                           "%1: Project name").arg(QFileInfo(m_proFilePath).baseName()));
    }

    deleteWidgets();
    setupWidgets();
}

void TargetSetupPage::setNoteText(const QString &text)
{
    m_ui->descriptionLabel->setText(text);
}

void TargetSetupPage::setupImportInfos()
{
    if (m_importSearch)
        m_importInfos = BuildConfigurationInfo::importBuildConfigurations(m_proFilePath);
}

void TargetSetupPage::cleanupImportInfos()
{
    foreach (const BuildConfigurationInfo &info, m_importInfos) {
        if (info.temporaryQtVersion)
            delete info.version;
    }
}

void TargetSetupPage::newImportBuildConfiguration(const BuildConfigurationInfo &info)
{
    m_importInfos.append(info);
}

bool TargetSetupPage::setupProject(Qt4ProjectManager::Qt4Project *project)
{
    QMap<QString, Qt4TargetSetupWidget *>::const_iterator it, end;
    end = m_widgets.constEnd();
    it = m_widgets.constBegin();
    for ( ; it != end; ++it) {
        Qt4BaseTargetFactory *factory = m_factories.value(it.value());

        foreach (const BuildConfigurationInfo &info, it.value()->usedImportInfos()) {
            QtSupport::BaseQtVersion *version = info.version;
            for (int i=0; i < m_importInfos.size(); ++i) {
                if (m_importInfos.at(i).version == version) {
                    if (m_importInfos[i].temporaryQtVersion) {
                        QtSupport::QtVersionManager::instance()->addVersion(m_importInfos[i].version);
                        m_importInfos[i].temporaryQtVersion = false;
                    }
                }
            }
        }

        if (ProjectExplorer::Target *target = factory->create(project, it.key(), it.value()))
            project->addTarget(target);
    }

    // Select active target
    // a) Simulator target
    // b) Desktop target
    // c) the first target
    ProjectExplorer::Target *activeTarget = 0;
    QList<ProjectExplorer::Target *> targets = project->targets();
    foreach (ProjectExplorer::Target *t, targets) {
        if (t->id() == QLatin1String(Constants::QT_SIMULATOR_TARGET_ID))
            activeTarget = t;
        else if (!activeTarget && t->id() == QLatin1String(Constants::DESKTOP_TARGET_ID))
            activeTarget = t;
    }
    if (!activeTarget && !targets.isEmpty())
        activeTarget = targets.first();
    if (activeTarget)
        project->setActiveTarget(activeTarget);

    return true;
}

void TargetSetupPage::setUseScrollArea(bool b)
{
    m_useScrollArea = b;
}
