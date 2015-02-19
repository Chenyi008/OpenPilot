/**
 ******************************************************************************
 *
 * @file       configoutputwidget.cpp
 * @author     E. Lafargue & The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup ConfigPlugin Config Plugin
 * @{
 * @brief Servo output configuration panel for the config gadget
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "configoutputwidget.h"
#include "outputchannelform.h"
#include "configvehicletypewidget.h"

#include "mixersettings.h"
#include "actuatorcommand.h"
#include "actuatorsettings.h"
#include "systemsettings.h"
#include "uavsettingsimportexport/uavsettingsimportexportfactory.h"
#include <extensionsystem/pluginmanager.h>
#include <coreplugin/generalsettings.h>

#include <QDebug>
#include <QStringList>
#include <QWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

ConfigOutputWidget::ConfigOutputWidget(QWidget *parent) : ConfigTaskWidget(parent)
{
    ui = new Ui_OutputWidget();
    ui->setupUi(this);

    ui->gvFrame->setVisible(false);

    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    Core::Internal::GeneralSettings *settings = pm->getObject<Core::Internal::GeneralSettings>();
    if (!settings->useExpertMode()) {
        ui->saveRCOutputToRAM->setVisible(false);
    }

    UAVSettingsImportExportFactory *importexportplugin = pm->getObject<UAVSettingsImportExportFactory>();
    connect(importexportplugin, SIGNAL(importAboutToBegin()), this, SLOT(stopTests()));

    connect(ui->channelOutTest, SIGNAL(toggled(bool)), this, SLOT(runChannelTests(bool)));

    // Configure the task widget
    // Connect the help button
    connect(ui->outputHelp, SIGNAL(clicked()), this, SLOT(openHelp()));

    addApplySaveButtons(ui->saveRCOutputToRAM, ui->saveRCOutputToSD);

    // Track the ActuatorSettings object
    addUAVObject("ActuatorSettings");

    // NOTE: we have channel indices from 0 to 9, but the convention for OP is Channel 1 to Channel 10.
    // Register for ActuatorSettings changes:
    for (unsigned int i = 0; i < ActuatorCommand::CHANNEL_NUMELEM; i++) {
        OutputChannelForm *form = new OutputChannelForm(i, this);
        form->moveTo(*(ui->channelLayout));

        connect(ui->channelOutTest, SIGNAL(toggled(bool)), form, SLOT(enableChannelTest(bool)));
        connect(form, SIGNAL(channelChanged(int, int)), this, SLOT(sendChannelTest(int, int)));

        addWidget(form->ui.actuatorMin);
        addWidget(form->ui.actuatorNeutral);
        addWidget(form->ui.actuatorMax);
        addWidget(form->ui.actuatorRev);
        addWidget(form->ui.actuatorLink);
    }

    // Associate the buttons with their UAVO fields
    addWidget(ui->cb_outputRate6);
    addWidget(ui->cb_outputRate5);
    addWidget(ui->cb_outputRate4);
    addWidget(ui->cb_outputRate3);
    addWidget(ui->cb_outputRate2);
    addWidget(ui->cb_outputRate1);
    addWidget(ui->spinningArmed);

    addWidgetBinding("ActuatorSettings", "BankMode", ui->cb_outputMode1, 0, 0, true);
    addWidgetBinding("ActuatorSettings", "BankMode", ui->cb_outputMode2, 1, 0, true);
    addWidgetBinding("ActuatorSettings", "BankMode", ui->cb_outputMode3, 2, 0, true);
    addWidgetBinding("ActuatorSettings", "BankMode", ui->cb_outputMode4, 3, 0, true);
    addWidgetBinding("ActuatorSettings", "BankMode", ui->cb_outputMode5, 4, 0, true);
    addWidgetBinding("ActuatorSettings", "BankMode", ui->cb_outputMode6, 5, 0, true);

    SystemAlarms *systemAlarmsObj = SystemAlarms::GetInstance(getObjectManager());
    connect(systemAlarmsObj, SIGNAL(objectUpdated(UAVObject *)), this, SLOT(updateWarnings(UAVObject *)));

    disconnect(this, SLOT(refreshWidgetsValues(UAVObject *)));

    populateWidgets();
    refreshWidgetsValues();

    updateEnableControls();
}

ConfigOutputWidget::~ConfigOutputWidget()
{
    SystemAlarms *systemAlarmsObj = SystemAlarms::GetInstance(getObjectManager());

    disconnect(systemAlarmsObj, SIGNAL(objectUpdated(UAVObject *)), this, SLOT(updateWarnings(UAVObject *)));
    // Do nothing
}

void ConfigOutputWidget::enableControls(bool enable)
{
    ConfigTaskWidget::enableControls(enable);

    if (!enable) {
        ui->channelOutTest->setChecked(false);
    }
    ui->channelOutTest->setEnabled(enable);
}

/**
   Force update all channels with the values in the OutputChannelForms.
 */
void ConfigOutputWidget::sendAllChannelTests()
{
    for (unsigned int i = 0; i < ActuatorCommand::CHANNEL_NUMELEM; i++) {
        OutputChannelForm *form = getOutputChannelForm(i);
        sendChannelTest(i, form->neutral());
    }
}

/**
   Toggles the channel testing mode by making the GCS take over
   the ActuatorCommand objects
 */
void ConfigOutputWidget::runChannelTests(bool state)
{
    SystemAlarms *systemAlarmsObj = SystemAlarms::GetInstance(getObjectManager());
    SystemAlarms::DataFields systemAlarms = systemAlarmsObj->getData();

    if (state && systemAlarms.Alarm[SystemAlarms::ALARM_ACTUATOR] != SystemAlarms::ALARM_OK) {
        QMessageBox mbox;
        mbox.setText(QString(tr("The actuator module is in an error state. This can also occur because there are no inputs. "
                                "Please fix these before testing outputs.")));
        mbox.setStandardButtons(QMessageBox::Ok);
        mbox.exec();

        // Unfortunately must cache this since callback will reoccur
        accInitialData = ActuatorCommand::GetInstance(getObjectManager())->getMetadata();

        ui->channelOutTest->setChecked(false);
        return;
    }

    // Confirm this is definitely what they want
    if (state) {
        QMessageBox mbox;
        mbox.setText(QString(tr("This option will start your motors by the amount selected on the sliders regardless of transmitter."
                                "It is recommended to remove any blades from motors. Are you sure you want to do this?")));
        mbox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        int retval = mbox.exec();
        if (retval != QMessageBox::Yes) {
            state = false;
            qDebug() << "Cancelled";
            ui->channelOutTest->setChecked(false);
            return;
        }
    }

    ActuatorCommand *obj = ActuatorCommand::GetInstance(getObjectManager());
    UAVObject::Metadata mdata = obj->getMetadata();
    if (state) {
        accInitialData = mdata;
        UAVObject::SetFlightAccess(mdata, UAVObject::ACCESS_READONLY);
        UAVObject::SetFlightTelemetryUpdateMode(mdata, UAVObject::UPDATEMODE_ONCHANGE);
        UAVObject::SetGcsTelemetryAcked(mdata, false);
        UAVObject::SetGcsTelemetryUpdateMode(mdata, UAVObject::UPDATEMODE_ONCHANGE);
        mdata.gcsTelemetryUpdatePeriod = 100;
    } else {
        mdata = accInitialData; // Restore metadata
    }
    obj->setMetadata(mdata);
    obj->updated();

    // Setup the correct initial channel values when the channel testing mode is turned on.
    if (state) {
        sendAllChannelTests();
    }

    // Add info at end
    if (!state && isDirty()) {
        QMessageBox mbox;
        mbox.setText(QString(tr("You may want to save your neutral settings.")));
        mbox.setStandardButtons(QMessageBox::Ok);
        mbox.setIcon(QMessageBox::Information);
        mbox.exec();
    }
}

OutputChannelForm *ConfigOutputWidget::getOutputChannelForm(const int index) const
{
    QList<OutputChannelForm *> outputChannelForms = findChildren<OutputChannelForm *>();
    foreach(OutputChannelForm * outputChannelForm, outputChannelForms) {
        if (outputChannelForm->index() == index) {
            return outputChannelForm;
        }
    }

    // no OutputChannelForm found with given index
    return NULL;
}

/**
 * Set the label for a channel output assignement
 */
void ConfigOutputWidget::assignOutputChannel(UAVDataObject *obj, QString &str)
{
    // FIXME: use signal/ slot approach
    UAVObjectField *field = obj->getField(str);
    QStringList options   = field->getOptions();
    int index = options.indexOf(field->getValue().toString());

    OutputChannelForm *outputChannelForm = getOutputChannelForm(index);

    if (outputChannelForm) {
        outputChannelForm->setName(str);
    }
}

/**
   Sends the channel value to the UAV to move the servo.
   Returns immediately if we are not in testing mode
 */
void ConfigOutputWidget::sendChannelTest(int index, int value)
{
    if (!ui->channelOutTest->isChecked()) {
        return;
    }

    if (index < 0 || (unsigned)index >= ActuatorCommand::CHANNEL_NUMELEM) {
        return;
    }

    ActuatorCommand *actuatorCommand = ActuatorCommand::GetInstance(getObjectManager());
    Q_ASSERT(actuatorCommand);
    ActuatorCommand::DataFields actuatorCommandFields = actuatorCommand->getData();
    actuatorCommandFields.Channel[index] = value;
    actuatorCommand->setData(actuatorCommandFields);
}

void ConfigOutputWidget::setColor(QWidget *widget, const QColor color)
{
    QPalette p(palette());

    p.setColor(QPalette::Background, color);
    p.setColor(QPalette::Base, color);
    p.setColor(QPalette::Active, QPalette::Button, color);
    p.setColor(QPalette::Inactive, QPalette::Button, color);
    widget->setAutoFillBackground(true);
    widget->setPalette(p);
}

/********************************
 *  Output settings
 *******************************/

/**
   Request the current config from the board (RC Output)
 */
void ConfigOutputWidget::refreshWidgetsValues(UAVObject *obj)
{
    bool dirty = isDirty();

    ConfigTaskWidget::refreshWidgetsValues(obj);

    // Get Actuator Settings
    ActuatorSettings *actuatorSettings = ActuatorSettings::GetInstance(getObjectManager());

    Q_ASSERT(actuatorSettings);
    ActuatorSettings::DataFields actuatorSettingsData = actuatorSettings->getData();

    // Get channel descriptions
    QStringList channelDesc = ConfigVehicleTypeWidget::getChannelDescriptions();

    QList<int> channelBanks;
    QList<QColor> bankColors;
    bankColors
        << QColor("#C6ECAE")
        << QColor("#91E5D3")
        << QColor("#FCEC52")
        << QColor("#C3A8FF")
        << QColor("#F7F7F2")
        << QColor("#FF9F51");

    // Initialize output forms
    QList<OutputChannelForm *> outputChannelForms = findChildren<OutputChannelForm *>();
    foreach(OutputChannelForm * outputChannelForm, outputChannelForms) {
        outputChannelForm->setName(channelDesc[outputChannelForm->index()]);

        // init min,max,neutral
        int minValue = actuatorSettingsData.ChannelMin[outputChannelForm->index()];
        int maxValue = actuatorSettingsData.ChannelMax[outputChannelForm->index()];
        outputChannelForm->setRange(minValue, maxValue);

        int neutral  = actuatorSettingsData.ChannelNeutral[outputChannelForm->index()];
        outputChannelForm->setNeutral(neutral);
    }

    // Get the SpinWhileArmed setting
    ui->spinningArmed->setChecked(actuatorSettingsData.MotorsSpinWhileArmed == ActuatorSettings::MOTORSSPINWHILEARMED_TRUE);

    QList<QLabel *> bank;
    bank << ui->chBank1 << ui->chBank2 << ui->chBank3 << ui->chBank4 << ui->chBank5 << ui->chBank6;

    QList<QComboBox *> outputRateCombos;
    outputRateCombos << ui->cb_outputRate1 << ui->cb_outputRate2 << ui->cb_outputRate3 <<
        ui->cb_outputRate4 << ui->cb_outputRate5 << ui->cb_outputRate6;

    QList<QComboBox *> outputModeCombos;
    outputModeCombos << ui->cb_outputMode1 << ui->cb_outputMode2 << ui->cb_outputMode3 <<
        ui->cb_outputMode4 << ui->cb_outputMode5 << ui->cb_outputMode6;

    Q_ASSERT(outputModeCombos.count() == outputRateCombos.count());
    Q_ASSERT(outputRateCombos.count() == bank.count());

    for (int i = 0; i < outputModeCombos.count(); i++) {
        // Setup output rates for all banks
        if (outputRateCombos.at(i)->findText(QString::number(actuatorSettingsData.BankUpdateFreq[i])) == -1) {
            outputRateCombos.at(i)->addItem(QString::number(actuatorSettingsData.BankUpdateFreq[i]));
        }
        outputRateCombos.at(i)->setCurrentIndex(outputRateCombos.at(i)->findText(QString::number(actuatorSettingsData.BankUpdateFreq[i])));

        // Reset to all disabled
        bank.at(i)->setText("-");

        outputRateCombos.at(i)->setEnabled(false);
        setColor(outputRateCombos.at(i), palette().color(QPalette::Background));

        outputModeCombos.at(i)->setEnabled(false);
        setColor(outputModeCombos.at(i), palette().color(QPalette::Background));
    }

    // Get connected board model
    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    Q_ASSERT(pm);
    UAVObjectUtilManager *utilMngr     = pm->getObject<UAVObjectUtilManager>();
    Q_ASSERT(utilMngr);
    QStringList bankLabels;

    if (utilMngr) {
        int board = utilMngr->getBoardModel();
        // Setup labels and combos for banks according to board type
        if ((board & 0xff00) == 0x0400) {
            // Coptercontrol family of boards 4 timer banks
            bankLabels << "1 (1-3)" << "2 (4)" << "3 (5,7-8)" << "4 (6,9-10)";
            channelBanks << 1 << 1 << 1 << 2 << 3 << 4 << 3 << 3 << 4 << 4;
        } else if ((board & 0xff00) == 0x0900) {
            // Revolution family of boards 6 timer banks
            bankLabels << "1 (1-2)" << "2 (3)" << "3 (4)" << "4 (5-6)" << "5 (7-8)" << "6 (9-10)";
            channelBanks << 1 << 1 << 2 << 3 << 4 << 4 << 5 << 5 << 6 << 6;
        }
    }

    int i = 0;
    foreach(QString banklabel, bankLabels) {
        bank[i]->setText(banklabel);
        outputRateCombos[i]->setEnabled(true);
        setColor(outputRateCombos[i], bankColors[i]);
        outputModeCombos[i]->setEnabled(true);
        setColor(outputModeCombos[i], bankColors[i]);
        i++;
    }
    // Get Channel ranges:
    i = 0;
    foreach(OutputChannelForm * outputChannelForm, outputChannelForms) {
        int minValue = actuatorSettingsData.ChannelMin[outputChannelForm->index()];
        int maxValue = actuatorSettingsData.ChannelMax[outputChannelForm->index()];

        outputChannelForm->setRange(minValue, maxValue);
        if (channelBanks.count() > i) {
            outputChannelForm->setBank(QString("%1").arg(channelBanks.at(i)));
            outputChannelForm->setColor(bankColors[channelBanks.at(i++) - 1]);
        }
        int neutral = actuatorSettingsData.ChannelNeutral[outputChannelForm->index()];
        outputChannelForm->setNeutral(neutral);
    }

    setDirty(dirty);
}

/**
 * Sends the config to the board, without saving to the SD card (RC Output)
 */
void ConfigOutputWidget::updateObjectsFromWidgets()
{
    ConfigTaskWidget::updateObjectsFromWidgets();

    ActuatorSettings *actuatorSettings = ActuatorSettings::GetInstance(getObjectManager());

    Q_ASSERT(actuatorSettings);
    if (actuatorSettings) {
        ActuatorSettings::DataFields actuatorSettingsData = actuatorSettings->getData();

        // Set channel ranges
        QList<OutputChannelForm *> outputChannelForms     = findChildren<OutputChannelForm *>();
        foreach(OutputChannelForm * outputChannelForm, outputChannelForms) {
            actuatorSettingsData.ChannelMax[outputChannelForm->index()]     = outputChannelForm->max();
            actuatorSettingsData.ChannelMin[outputChannelForm->index()]     = outputChannelForm->min();
            actuatorSettingsData.ChannelNeutral[outputChannelForm->index()] = outputChannelForm->neutral();
        }

        // Set update rates
        actuatorSettingsData.BankUpdateFreq[0]    = ui->cb_outputRate1->currentText().toUInt();
        actuatorSettingsData.BankUpdateFreq[1]    = ui->cb_outputRate2->currentText().toUInt();
        actuatorSettingsData.BankUpdateFreq[2]    = ui->cb_outputRate3->currentText().toUInt();
        actuatorSettingsData.BankUpdateFreq[3]    = ui->cb_outputRate4->currentText().toUInt();
        actuatorSettingsData.BankUpdateFreq[4]    = ui->cb_outputRate5->currentText().toUInt();
        actuatorSettingsData.BankUpdateFreq[5]    = ui->cb_outputRate6->currentText().toUInt();

        actuatorSettingsData.MotorsSpinWhileArmed = ui->spinningArmed->isChecked() ?
                                                    ActuatorSettings::MOTORSSPINWHILEARMED_TRUE :
                                                    ActuatorSettings::MOTORSSPINWHILEARMED_FALSE;

        // Apply settings
        actuatorSettings->setData(actuatorSettingsData);
    }
}

void ConfigOutputWidget::openHelp()
{
    QDesktopServices::openUrl(QUrl(tr("http://wiki.openpilot.org/x/WIGf"), QUrl::StrictMode));
}

void ConfigOutputWidget::stopTests()
{
    ui->channelOutTest->setChecked(false);
}

void ConfigOutputWidget::updateWarnings(UAVObject *)
{
    SystemAlarms *systemAlarmsObj = SystemAlarms::GetInstance(getObjectManager());
    SystemAlarms::DataFields systemAlarms = systemAlarmsObj->getData();

    if (systemAlarms.Alarm[SystemAlarms::ALARM_SYSTEMCONFIGURATION] > SystemAlarms::ALARM_WARNING) {
        switch (systemAlarms.ExtendedAlarmStatus[SystemAlarms::EXTENDEDALARMSTATUS_SYSTEMCONFIGURATION]) {
        case SystemAlarms::EXTENDEDALARMSTATUS_UNSUPPORTEDCONFIG_ONESHOT:
            setWarning(tr("OneShot only works with Receiver Port settings marked with '+OneShot'<br>"
                          "When using Receiver Port setting 'PPM_PIN6+OneShot' "
                          "<b><font color='#C3A8FF'>Bank 4 (output 6,9-10)</font></b> must be set to PWM"));
            return;
        }
    }
    setWarning(NULL);
}

void ConfigOutputWidget::setWarning(QString message)
{
    ui->gvFrame->setVisible(!message.isNull());
    ui->picWarning->setPixmap(message.isNull() ? QPixmap() : QPixmap(":/configgadget/images/error.svg"));
    ui->txtWarning->setText(message);
}
