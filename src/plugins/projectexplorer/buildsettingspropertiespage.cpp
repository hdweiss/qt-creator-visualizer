/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "buildsettingspropertiespage.h"
#include "buildstep.h"
#include "buildstepspage.h"
#include "project.h"
#include "buildconfiguration.h"

#include <coreplugin/coreconstants.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/qtcassert.h>

#include <QtCore/QPair>
#include <QtGui/QApplication>
#include <QtGui/QInputDialog>
#include <QtGui/QLabel>
#include <QtGui/QVBoxLayout>
#include <QtGui/QMenu>

using namespace ProjectExplorer;
using namespace ProjectExplorer::Internal;

///
/// BuildSettingsPanelFactory
///

bool BuildSettingsPanelFactory::supports(Project *project)
{
    return project->hasBuildSettings();
}

IPropertiesPanel *BuildSettingsPanelFactory::createPanel(Project *project)
{
    return new BuildSettingsPanel(project);
}

///
/// BuildSettingsPanel
///

BuildSettingsPanel::BuildSettingsPanel(Project *project) :
    m_widget(new BuildSettingsWidget(project)),
    m_icon(":/projectexplorer/images/rebuild.png")
{
}

BuildSettingsPanel::~BuildSettingsPanel()
{
    delete m_widget;
}

QString BuildSettingsPanel::name() const
{
    return QApplication::tr("Build Settings");
}

QWidget *BuildSettingsPanel::widget() const
{
    return m_widget;
}

QIcon BuildSettingsPanel::icon() const
{
    return m_icon;
}

///
// BuildSettingsSubWidgets
///

BuildSettingsSubWidgets::~BuildSettingsSubWidgets()
{
    clear();
}

void BuildSettingsSubWidgets::addWidget(const QString &name, QWidget *widget)
{
    QSpacerItem *item = new QSpacerItem(1, 10, QSizePolicy::Fixed, QSizePolicy::Fixed);

    QLabel *label = new QLabel(this);
    label->setText(name);
    QFont f = label->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() *1.2);
    label->setFont(f);

    layout()->addItem(item);
    layout()->addWidget(label);
    layout()->addWidget(widget);

    m_spacerItems.append(item);
    m_labels.append(label);
    m_widgets.append(widget);
}

void BuildSettingsSubWidgets::clear()
{
    foreach(QSpacerItem *item, m_spacerItems)
        layout()->removeItem(item);
    qDeleteAll(m_spacerItems);
    qDeleteAll(m_widgets);
    qDeleteAll(m_labels);
    m_widgets.clear();
    m_labels.clear();
    m_spacerItems.clear();
}

QList<QWidget *> BuildSettingsSubWidgets::widgets() const
{
    return m_widgets;
}

BuildSettingsSubWidgets::BuildSettingsSubWidgets(QWidget *parent)
    : QWidget(parent)
{
    new QVBoxLayout(this);
    layout()->setMargin(0);
}

///
/// BuildSettingsWidget
///

BuildSettingsWidget::~BuildSettingsWidget()
{
}

BuildSettingsWidget::BuildSettingsWidget(Project *project)
    : m_project(project), m_buildConfiguration(0)
{
    QVBoxLayout *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, -1, 0, -1);

    { // Edit Build Configuration row
        QHBoxLayout *hbox = new QHBoxLayout();
        hbox->addWidget(new QLabel(tr("Edit Build Configuration:"), this));
        m_buildConfigurationComboBox = new QComboBox(this);
        m_buildConfigurationComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        hbox->addWidget(m_buildConfigurationComboBox);

        m_addButton = new QPushButton(this);
        m_addButton->setText(tr("Add"));
        m_addButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        hbox->addWidget(m_addButton);

        m_removeButton = new QPushButton(this);
        m_removeButton->setText(tr("Remove"));
        m_removeButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        hbox->addWidget(m_removeButton);
        hbox->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed));
        vbox->addLayout(hbox);
    }

    m_makeActiveLabel = new QLabel(this);
    m_makeActiveLabel->setVisible(false);
    vbox->addWidget(m_makeActiveLabel);

    m_subWidgets = new BuildSettingsSubWidgets(this);
    vbox->addWidget(m_subWidgets);

    m_addButtonMenu = new QMenu(this);
    m_addButton->setMenu(m_addButtonMenu);
    updateAddButtonMenu();

    m_buildConfiguration = m_project->activeBuildConfiguration();

    connect(m_makeActiveLabel, SIGNAL(linkActivated(QString)),
            this, SLOT(makeActive()));

    connect(m_buildConfigurationComboBox, SIGNAL(currentIndexChanged(int)),
            this, SLOT(currentIndexChanged(int)));

    connect(m_removeButton, SIGNAL(clicked()),
            this, SLOT(deleteConfiguration()));

    // TODO update on displayNameChange

    connect(m_project, SIGNAL(activeBuildConfigurationChanged()),
            this, SLOT(checkMakeActiveLabel()));

    if (m_project->buildConfigurationFactory())
        connect(m_project->buildConfigurationFactory(), SIGNAL(availableCreationTypesChanged()), SLOT(updateAddButtonMenu()));

    updateBuildSettings();
}

void BuildSettingsWidget::makeActive()
{
    m_project->setActiveBuildConfiguration(m_buildConfiguration);
}

void BuildSettingsWidget::updateAddButtonMenu()
{
    m_addButtonMenu->clear();
    m_addButtonMenu->addAction(tr("&Clone Selected"),
                             this, SLOT(cloneConfiguration()));
    IBuildConfigurationFactory *factory = m_project->buildConfigurationFactory();
    if (factory) {
        foreach (const QString &type, factory->availableCreationTypes()) {
            QAction *action = m_addButtonMenu->addAction(factory->displayNameForType(type), this, SLOT(createConfiguration()));
            action->setData(type);
        }
    }
}

void BuildSettingsWidget::updateBuildSettings()
{
    // TODO save position, entry from combbox

    // Delete old tree items
    bool blocked = m_buildConfigurationComboBox->blockSignals(true);
    m_buildConfigurationComboBox->clear();
    m_subWidgets->clear();

    // update buttons
    m_removeButton->setEnabled(m_project->buildConfigurations().size() > 1);

    // Add pages
    BuildConfigWidget *generalConfigWidget = m_project->createConfigWidget();
    m_subWidgets->addWidget(generalConfigWidget->displayName(), generalConfigWidget);

    m_subWidgets->addWidget(tr("Build Steps"), new BuildStepsPage(m_project));
    m_subWidgets->addWidget(tr("Clean Steps"), new BuildStepsPage(m_project, true));

    QList<BuildConfigWidget *> subConfigWidgets = m_project->subConfigWidgets();
    foreach (BuildConfigWidget *subConfigWidget, subConfigWidgets)
        m_subWidgets->addWidget(subConfigWidget->displayName(), subConfigWidget);

    // Add tree items
    foreach (const BuildConfiguration *bc, m_project->buildConfigurations()) {
        m_buildConfigurationComboBox->addItem(bc->displayName(), bc);
        if (bc == m_buildConfiguration)
            m_buildConfigurationComboBox->setCurrentIndex(m_buildConfigurationComboBox->count() - 1);
    }

    m_buildConfigurationComboBox->blockSignals(blocked);

    // TODO Restore position, entry from combbox
    // TODO? select entry from combobox ?
    activeBuildConfigurationChanged();
}

void BuildSettingsWidget::currentIndexChanged(int index)
{
    m_buildConfiguration = (BuildConfiguration *) m_buildConfigurationComboBox->itemData(index).value<BuildConfiguration *>();
    activeBuildConfigurationChanged();
}

void BuildSettingsWidget::activeBuildConfigurationChanged()
{
    for (int i = 0; i < m_buildConfigurationComboBox->count(); ++i) {
        if (m_buildConfigurationComboBox->itemData(i).value<BuildConfiguration *>() == m_buildConfiguration) {
            m_buildConfigurationComboBox->setCurrentIndex(i);
            break;
        }
    }
    foreach (QWidget *widget, m_subWidgets->widgets()) {
        if (BuildConfigWidget *buildStepWidget = qobject_cast<BuildConfigWidget*>(widget)) {
            buildStepWidget->init(m_buildConfiguration);
        }
    }
    checkMakeActiveLabel();
}

void BuildSettingsWidget::checkMakeActiveLabel()
{
    m_makeActiveLabel->setVisible(false);
    if (!m_project->activeBuildConfiguration() || m_project->activeBuildConfiguration() != m_buildConfiguration) {
        m_makeActiveLabel->setText(tr("<a href=\"#\">Make %1 active.</a>").arg(m_buildConfiguration->displayName()));
        m_makeActiveLabel->setVisible(true);
    }
}

void BuildSettingsWidget::createConfiguration()
{
    QAction *action = qobject_cast<QAction *>(sender());
    const QString &type = action->data().toString();
    BuildConfiguration *bc = m_project->buildConfigurationFactory()->create(type);
    if (bc) {
        m_buildConfiguration = bc;
        updateBuildSettings();
    }
}

void BuildSettingsWidget::cloneConfiguration()
{
    int index = m_buildConfigurationComboBox->currentIndex();
    BuildConfiguration *bc = m_buildConfigurationComboBox->itemData(index).value<BuildConfiguration *>();
    cloneConfiguration(bc);
}

void BuildSettingsWidget::deleteConfiguration()
{
    int index = m_buildConfigurationComboBox->currentIndex();
    BuildConfiguration *bc = m_buildConfigurationComboBox->itemData(index).value<BuildConfiguration *>();
    deleteConfiguration(bc);
}

void BuildSettingsWidget::cloneConfiguration(BuildConfiguration *sourceConfiguration)
{
    if (!sourceConfiguration)
        return;

    QString newDisplayName = QInputDialog::getText(this, tr("Clone configuration"), tr("New Configuration Name:"));
    if (newDisplayName.isEmpty())
        return;

    QStringList buildConfigurationDisplayNames;
    foreach(BuildConfiguration *bc, m_project->buildConfigurations())
        buildConfigurationDisplayNames << bc->displayName();
    newDisplayName = Project::makeUnique(newDisplayName, buildConfigurationDisplayNames);

    m_buildConfiguration = m_project->buildConfigurationFactory()->clone(sourceConfiguration);
    m_buildConfiguration->setDisplayName(newDisplayName);

    updateBuildSettings();
}

void BuildSettingsWidget::deleteConfiguration(BuildConfiguration *deleteConfiguration)
{
    if (!deleteConfiguration || m_project->buildConfigurations().size() <= 1)
        return;

    if (m_project->activeBuildConfiguration() == deleteConfiguration) {
        foreach (BuildConfiguration *bc, m_project->buildConfigurations()) {
            if (bc != deleteConfiguration) {
                m_project->setActiveBuildConfiguration(bc);
                break;
            }
        }
    }

    if (m_buildConfiguration == deleteConfiguration) {
        foreach (BuildConfiguration *bc, m_project->buildConfigurations()) {
            if (bc != deleteConfiguration) {
                m_buildConfiguration = bc;
                break;
            }
        }
    }

    m_project->removeBuildConfiguration(deleteConfiguration);

    updateBuildSettings();
}
