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

#include "behaviorsettingswidget.h"
#include "ui_behaviorsettingswidget.h"

#include <texteditor/tabsettings.h>
#include <texteditor/typingsettings.h>
#include <texteditor/storagesettings.h>
#include <texteditor/behaviorsettings.h>
#include <texteditor/extraencodingsettings.h>

#include <QList>
#include <QString>
#include <QByteArray>
#include <QTextCodec>
#include <QTextStream>

#include <algorithm>
#include <functional>

namespace TextEditor {

struct BehaviorSettingsWidgetPrivate
{
    Internal::Ui::BehaviorSettingsWidget m_ui;
    QList<QTextCodec *> m_codecs;
};

BehaviorSettingsWidget::BehaviorSettingsWidget(QWidget *parent)
    : QWidget(parent)
    , d(new BehaviorSettingsWidgetPrivate)
{
    d->m_ui.setupUi(this);

    QList<int> mibs = QTextCodec::availableMibs();
    qSort(mibs);
    QList<int>::iterator firstNonNegative =
        std::find_if(mibs.begin(), mibs.end(), std::bind2nd(std::greater_equal<int>(), 0));
    if (firstNonNegative != mibs.end())
        std::rotate(mibs.begin(), firstNonNegative, mibs.end());
    foreach (int mib, mibs) {
        QTextCodec *codec = QTextCodec::codecForMib(mib);
        QString compoundName = QLatin1String(codec->name());
        foreach (const QByteArray &alias, codec->aliases()) {
            compoundName += QLatin1String(" / ");
            compoundName += QString::fromLatin1(alias);
        }
        d->m_ui.encodingBox->addItem(compoundName);
        d->m_codecs.append(codec);
    }

    connect(d->m_ui.autoIndent, SIGNAL(toggled(bool)),
            this, SLOT(slotTypingSettingsChanged()));
    connect(d->m_ui.smartBackspaceBehavior, SIGNAL(currentIndexChanged(int)),
            this, SLOT(slotTypingSettingsChanged()));
    connect(d->m_ui.tabKeyBehavior, SIGNAL(currentIndexChanged(int)),
            this, SLOT(slotTypingSettingsChanged()));
    connect(d->m_ui.cleanWhitespace, SIGNAL(clicked(bool)),
            this, SLOT(slotStorageSettingsChanged()));
    connect(d->m_ui.inEntireDocument, SIGNAL(clicked(bool)),
            this, SLOT(slotStorageSettingsChanged()));
    connect(d->m_ui.addFinalNewLine, SIGNAL(clicked(bool)),
            this, SLOT(slotStorageSettingsChanged()));
    connect(d->m_ui.cleanIndentation, SIGNAL(clicked(bool)),
            this, SLOT(slotStorageSettingsChanged()));
    connect(d->m_ui.mouseNavigation, SIGNAL(clicked()),
            this, SLOT(slotBehaviorSettingsChanged()));
    connect(d->m_ui.scrollWheelZooming, SIGNAL(clicked(bool)),
            this, SLOT(slotBehaviorSettingsChanged()));
    connect(d->m_ui.constrainTooltips, SIGNAL(clicked()),
            this, SLOT(slotBehaviorSettingsChanged()));
    connect(d->m_ui.camelCaseNavigation, SIGNAL(clicked()),
            this, SLOT(slotBehaviorSettingsChanged()));
    connect(d->m_ui.keyboardTooltips, SIGNAL(clicked()),
            this, SLOT(slotBehaviorSettingsChanged()));
    connect(d->m_ui.utf8BomBox, SIGNAL(currentIndexChanged(int)),
            this, SLOT(slotExtraEncodingChanged()));
    connect(d->m_ui.encodingBox, SIGNAL(currentIndexChanged(int)),
            this, SLOT(slotEncodingBoxChanged(int)));
}

BehaviorSettingsWidget::~BehaviorSettingsWidget()
{
    delete d;
}

void BehaviorSettingsWidget::setActive(bool active)
{
    d->m_ui.tabPreferencesWidget->setEnabled(active);
    d->m_ui.groupBoxTyping->setEnabled(active);
    d->m_ui.groupBoxEncodings->setEnabled(active);
    d->m_ui.groupBoxMouse->setEnabled(active);
    d->m_ui.groupBoxStorageSettings->setEnabled(active);
}

void BehaviorSettingsWidget::setAssignedCodec(QTextCodec *codec)
{
    for (int i = 0; i < d->m_codecs.size(); ++i) {
        if (codec == d->m_codecs.at(i)) {
            d->m_ui.encodingBox->setCurrentIndex(i);
            break;
        }
    }
}

QTextCodec *BehaviorSettingsWidget::assignedCodec() const
{
    return d->m_codecs.at(d->m_ui.encodingBox->currentIndex());
}

void BehaviorSettingsWidget::setCodeStyle(ICodeStylePreferences *preferences)
{
    d->m_ui.tabPreferencesWidget->setPreferences(preferences);
}

void BehaviorSettingsWidget::setAssignedTypingSettings(const TypingSettings &typingSettings)
{
    d->m_ui.autoIndent->setChecked(typingSettings.m_autoIndent);
    d->m_ui.smartBackspaceBehavior->setCurrentIndex(typingSettings.m_smartBackspaceBehavior);
    d->m_ui.tabKeyBehavior->setCurrentIndex(typingSettings.m_tabKeyBehavior);
}

void BehaviorSettingsWidget::assignedTypingSettings(TypingSettings *typingSettings) const
{
    typingSettings->m_autoIndent = d->m_ui.autoIndent->isChecked();
    typingSettings->m_smartBackspaceBehavior =
        (TypingSettings::SmartBackspaceBehavior)(d->m_ui.smartBackspaceBehavior->currentIndex());
    typingSettings->m_tabKeyBehavior =
        (TypingSettings::TabKeyBehavior)(d->m_ui.tabKeyBehavior->currentIndex());
}

void BehaviorSettingsWidget::setAssignedStorageSettings(const StorageSettings &storageSettings)
{
    d->m_ui.cleanWhitespace->setChecked(storageSettings.m_cleanWhitespace);
    d->m_ui.inEntireDocument->setChecked(storageSettings.m_inEntireDocument);
    d->m_ui.cleanIndentation->setChecked(storageSettings.m_cleanIndentation);
    d->m_ui.addFinalNewLine->setChecked(storageSettings.m_addFinalNewLine);
}

void BehaviorSettingsWidget::assignedStorageSettings(StorageSettings *storageSettings) const
{
    storageSettings->m_cleanWhitespace = d->m_ui.cleanWhitespace->isChecked();
    storageSettings->m_inEntireDocument = d->m_ui.inEntireDocument->isChecked();
    storageSettings->m_cleanIndentation = d->m_ui.cleanIndentation->isChecked();
    storageSettings->m_addFinalNewLine = d->m_ui.addFinalNewLine->isChecked();
}

void BehaviorSettingsWidget::setAssignedBehaviorSettings(const BehaviorSettings &behaviorSettings)
{
    d->m_ui.mouseNavigation->setChecked(behaviorSettings.m_mouseNavigation);
    d->m_ui.scrollWheelZooming->setChecked(behaviorSettings.m_scrollWheelZooming);
    d->m_ui.constrainTooltips->setChecked(behaviorSettings.m_constrainHoverTooltips);
    d->m_ui.camelCaseNavigation->setChecked(behaviorSettings.m_camelCaseNavigation);
    d->m_ui.keyboardTooltips->setChecked(behaviorSettings.m_keyboardTooltips);
}

void BehaviorSettingsWidget::assignedBehaviorSettings(BehaviorSettings *behaviorSettings) const
{
    behaviorSettings->m_mouseNavigation = d->m_ui.mouseNavigation->isChecked();
    behaviorSettings->m_scrollWheelZooming = d->m_ui.scrollWheelZooming->isChecked();
    behaviorSettings->m_constrainHoverTooltips = d->m_ui.constrainTooltips->isChecked();
    behaviorSettings->m_camelCaseNavigation = d->m_ui.camelCaseNavigation->isChecked();
    behaviorSettings->m_keyboardTooltips = d->m_ui.keyboardTooltips->isChecked();
}

void BehaviorSettingsWidget::setAssignedExtraEncodingSettings(
    const ExtraEncodingSettings &encodingSettings)
{
    d->m_ui.utf8BomBox->setCurrentIndex(encodingSettings.m_utf8BomSetting);
}

void BehaviorSettingsWidget::assignedExtraEncodingSettings(
    ExtraEncodingSettings *encodingSettings) const
{
    encodingSettings->m_utf8BomSetting =
        (ExtraEncodingSettings::Utf8BomSetting)d->m_ui.utf8BomBox->currentIndex();
}

QString BehaviorSettingsWidget::collectUiKeywords() const
{
    static const QLatin1Char sep(' ');
    QString keywords;
    QTextStream(&keywords)
        << sep << d->m_ui.tabPreferencesWidget->searchKeywords()
        << sep << d->m_ui.autoIndent->text()
        << sep << d->m_ui.smartBackspaceLabel->text()
        << sep << d->m_ui.tabKeyBehaviorLabel->text()
        << sep << d->m_ui.cleanWhitespace->text()
        << sep << d->m_ui.inEntireDocument->text()
        << sep << d->m_ui.cleanIndentation->text()
        << sep << d->m_ui.addFinalNewLine->text()
        << sep << d->m_ui.encodingLabel->text()
        << sep << d->m_ui.utf8BomLabel->text()
        << sep << d->m_ui.mouseNavigation->text()
        << sep << d->m_ui.scrollWheelZooming->text()
        << sep << d->m_ui.constrainTooltips->text()
        << sep << d->m_ui.camelCaseNavigation->text()
        << sep << d->m_ui.keyboardTooltips->text()
        << sep << d->m_ui.groupBoxStorageSettings->title()
        << sep << d->m_ui.groupBoxEncodings->title()
        << sep << d->m_ui.groupBoxMouse->title();
    keywords.remove(QLatin1Char('&'));
    return keywords;
}

void BehaviorSettingsWidget::slotTypingSettingsChanged()
{
    TypingSettings settings;
    assignedTypingSettings(&settings);
    emit typingSettingsChanged(settings);
}

void BehaviorSettingsWidget::slotStorageSettingsChanged()
{
    StorageSettings settings;
    assignedStorageSettings(&settings);
    emit storageSettingsChanged(settings);
}

void BehaviorSettingsWidget::slotBehaviorSettingsChanged()
{
    BehaviorSettings settings;
    assignedBehaviorSettings(&settings);
    emit behaviorSettingsChanged(settings);
}

void BehaviorSettingsWidget::slotExtraEncodingChanged()
{
    ExtraEncodingSettings settings;
    assignedExtraEncodingSettings(&settings);
    emit extraEncodingSettingsChanged(settings);
}

void BehaviorSettingsWidget::slotEncodingBoxChanged(int index)
{
    emit textCodecChanged(d->m_codecs.at(index));
}

} // TextEditor
