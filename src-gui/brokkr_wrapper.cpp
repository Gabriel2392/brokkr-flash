#include "brokkr_wrapper.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFileDialog>
#include <QStringList>
#include <QTabWidget>
#include <QGroupBox>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressBar>
#include <QMessageBox>
#include <QTextCursor>
#include <QSizePolicy>

BrokkrWrapper::BrokkrWrapper(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Brokkr Flasher");
    resize(850, 600);
    baseWindowHeight_ = height();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    auto* bannerLabel = new QLabel("<b>brokkr v1.0.0 flasher</b>", this);
    bannerLabel->setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4c8ddc, stop:1 #8bbceb); color: white; font-size: 26px; padding: 10px; border-radius: 3px;");
    bannerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(bannerLabel);

    idComGroup_ = new QGroupBox("ID:COM", this);
    idComLayout_ = new QGridLayout(idComGroup_);
    idComLayout_->setSpacing(2);
    idComLayout_->setContentsMargins(5, 5, 5, 5);

    rebuildDeviceBoxes_(kBoxesNormal, true);
    mainLayout->addWidget(idComGroup_);

    auto* middleLayout = new QHBoxLayout();

    auto* tabWidget = new QTabWidget(this);
    tabWidget->setFixedWidth(360);

    consoleOutput = new QTextEdit(this);
    consoleOutput->setReadOnly(true);
    consoleOutput->setStyleSheet("font-family: Consolas, monospace;");
    tabWidget->addTab(consoleOutput, "Log");

    auto* optTab = new QWidget();
    auto* optLayout = new QVBoxLayout(optTab);
    optLayout->setAlignment(Qt::AlignTop);

    auto* targetLayout = new QHBoxLayout();
    targetLayout->addWidget(new QLabel("Target Sysname:", this));
    editTarget = new QLineEdit(this);
    editTarget->setPlaceholderText("e.g. COM12 or 1-1.4");
    targetLayout->addWidget(editTarget);
    optLayout->addLayout(targetLayout);

    chkWireless = new QCheckBox("Wireless (-w)", this);
    optLayout->addWidget(chkWireless);

    chkManyDevices = new QCheckBox("Many device flashing", this);
    optLayout->addWidget(chkManyDevices);

    auto* manyRow = new QHBoxLayout();
    lblDeviceBoxes = new QLabel("Count: 8", this);
    sldDeviceBoxes = new QSlider(Qt::Horizontal, this);
    sldDeviceBoxes->setRange(kBoxesNormal, kBoxesMax);
    sldDeviceBoxes->setValue(24);
    sldDeviceBoxes->setEnabled(false);
    manyRow->addWidget(lblDeviceBoxes);
    manyRow->addWidget(sldDeviceBoxes, 1);
    optLayout->addLayout(manyRow);

    optLayout->addSpacing(10);
    optLayout->addWidget(new QLabel("Post-Flash Action:", this));
    cmbRebootAction = new QComboBox(this);
    cmbRebootAction->addItem("Default (Reboot Normally)");
    cmbRebootAction->addItem("Redownload");
    cmbRebootAction->addItem("No Reboot");
    optLayout->addWidget(cmbRebootAction);

    tabWidget->addTab(optTab, "Options");

    auto* pitTab = new QWidget();
    auto* pitLayout = new QGridLayout(pitTab);
    pitLayout->setAlignment(Qt::AlignTop);

    chkUsePit = new QCheckBox("Use PIT (--set-pit)", this);
    pitLayout->addWidget(chkUsePit, 0, 0, 1, 3);

    pitLayout->addWidget(new QLabel("PIT File:"), 1, 0);

    editPit = new QLineEdit(this);
    editPit->setEnabled(false);
    pitLayout->addWidget(editPit, 1, 1);

    btnPitBrowse = new QPushButton("Browse", this);
    btnPitBrowse->setEnabled(false);
    pitLayout->addWidget(btnPitBrowse, 1, 2);

    tabWidget->addTab(pitTab, "Pit");

    connect(chkUsePit, &QCheckBox::toggled, this, [this](bool checked) {
        editPit->setEnabled(checked);
        btnPitBrowse->setEnabled(checked);
        if (!checked) editPit->clear();
        updateActionButtons_();
    });

    connect(btnPitBrowse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(
            this,
            "Select PIT File",
            lastDir,
            "PIT Files (*.pit);;All Files (*)"
        );
        if (!file.isEmpty()) {
            lastDir = QFileInfo(file).absolutePath();
            editPit->setText(file);
            updateActionButtons_();
        }
    });

    connect(chkManyDevices, &QCheckBox::toggled, this, [this](bool checked) {
        sldDeviceBoxes->setEnabled(checked);

        if (!checked) {
            lblDeviceBoxes->setText(QString("Count: %1").arg(kBoxesNormal));
            rebuildDeviceBoxes_(kBoxesNormal, true);
            resize(width(), baseWindowHeight_);
        } else {
            lblDeviceBoxes->setText(QString("Count: %1").arg(sldDeviceBoxes->value()));
            rebuildDeviceBoxes_(sldDeviceBoxes->value(), false);
            applyWindowHeightToContents_();
        }

        refreshDeviceBoxes_();
        updateActionButtons_();
    });

    connect(sldDeviceBoxes, &QSlider::valueChanged, this, [this](int v) {
        lblDeviceBoxes->setText(QString("Count: %1").arg(v));
        if (chkManyDevices->isChecked()) {
            rebuildDeviceBoxes_(v, false);
            applyWindowHeightToContents_();
            refreshDeviceBoxes_();
            updateActionButtons_();
        }
    });

    connect(chkWireless, &QCheckBox::toggled, this, [this](bool) {
        updateActionButtons_();
    });

    connect(editTarget, &QLineEdit::textChanged, this, [this](const QString&) {
        updateActionButtons_();
    });

    middleLayout->addWidget(tabWidget);

    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setAlignment(Qt::AlignTop);

    QLabel* tipsLabel = new QLabel(
        "Tips - How to download HOME binary\n"
        "  OLD model : Download one binary ...\n"
        "  NEW model : Download BL + AP + CP + CSC",
        this
    );
    tipsLabel->setStyleSheet("color: gray; font-size: 11px; margin-bottom: 5px;");
    rightLayout->addWidget(tipsLabel);

    auto* fileLayout = new QGridLayout();
    fileLayout->setVerticalSpacing(8);

    setupOdinFileInput(fileLayout, 0, "BL", editBL);
    setupOdinFileInput(fileLayout, 1, "AP", editAP);
    setupOdinFileInput(fileLayout, 2, "CP", editCP);
    setupOdinFileInput(fileLayout, 3, "CSC", editCSC);
    setupOdinFileInput(fileLayout, 4, "USERDATA", editUserData);

    fileLayout->setColumnStretch(0, 0);
    fileLayout->setColumnStretch(1, 0);
    fileLayout->setColumnStretch(2, 1);

    rightLayout->addLayout(fileLayout);
    rightLayout->addStretch();
    middleLayout->addWidget(rightWidget, 1);

    mainLayout->addLayout(middleLayout);

    auto* bottomLayout = new QHBoxLayout();

    statusLabel = new QLabel("Ready", this);
    statusLabel->setStyleSheet("color: #0078D7; font-weight: bold; padding-left: 15px;");
    bottomLayout->addWidget(statusLabel);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    bottomLayout->addWidget(progressBar, 1);

    bottomLayout->addStretch();

    btnPrintPit = new QPushButton("Print PIT", this);
    btnRebootDevice = new QPushButton("Reboot Device", this);
    btnRun = new QPushButton("Start", this);
    QPushButton* btnReset = new QPushButton("Reset", this);

    const int bottomW = 135;
    const int bottomH = 32;
    btnPrintPit->setMinimumSize(bottomW, bottomH);
    btnRebootDevice->setMinimumSize(bottomW, bottomH);
    btnRun->setMinimumSize(bottomW, bottomH);
    btnReset->setMinimumSize(bottomW, bottomH);

    bottomLayout->addWidget(btnPrintPit);
    bottomLayout->addWidget(btnRebootDevice);
    bottomLayout->addWidget(btnRun);
    bottomLayout->addWidget(btnReset);

    mainLayout->addLayout(bottomLayout);

    process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString key = "PROGRESSUPDATE";

        while (process->canReadLine()) {
            QString rawLine = QString::fromLocal8Bit(process->readLine()).trimmed();
            if (rawLine.isEmpty()) continue;

            if (rawLine.startsWith(key)) {
                const QString jsonString = rawLine.mid(key.size());
                QJsonParseError parseError;
                QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);

                if (parseError.error == QJsonParseError::NoError && jsonDoc.isObject()) {
                    QJsonObject obj = jsonDoc.object();

                    QString stage = obj["stage"].toString();
                    double done = obj["overall_done"].toDouble();
                    double total = obj["overall_total"].toDouble();
                    QString notice = obj["notice"].toString();

                    if (total > 0) {
                        int percent = static_cast<int>((done / total) * 100.0);
                        progressBar->setValue(percent);
                    }

                    QString statusText = stage;
                    if (!notice.isEmpty()) statusText += " (" + notice + ")";
                    statusLabel->setText(statusText);
                }
                continue;
            }

            if (rawLine.contains(key)) continue;

            consoleOutput->append(rawLine.toHtmlEscaped());
            QTextCursor cursor = consoleOutput->textCursor();
            cursor.movePosition(QTextCursor::End);
            consoleOutput->setTextCursor(cursor);
        }
    });

    connect(process, &QProcess::readyReadStandardError, this, [this]() {
        while (process->canReadLine()) {
            QString rawLine = QString::fromLocal8Bit(process->readLine()).trimmed();
            if (!rawLine.isEmpty()) {
                consoleOutput->append("<font color=\"#ff5555\">" + rawLine.toHtmlEscaped() + "</font>");
                QTextCursor cursor = consoleOutput->textCursor();
                cursor.movePosition(QTextCursor::End);
                consoleOutput->setTextCursor(cursor);
            }
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        btnRun->setEnabled(true);
        btnRebootDevice->setEnabled(true);
        btnPrintPit->setEnabled(true);
        updateActionButtons_();
        statusLabel->setText("Failed to start brokkr");
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode) {
        btnRun->setEnabled(true);
        btnRebootDevice->setEnabled(true);
        btnPrintPit->setEnabled(true);
        updateActionButtons_();

        consoleOutput->append(QString("\n<i>Brokkr finished with exit code %1</i>").arg(exitCode));
        statusLabel->setText(exitCode == 0 ? "Done" : "Failed");
    });

    pollProcess = new QProcess(this);
    deviceTimer = new QTimer(this);

    connect(pollProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode) {
        if (exitCode != 0) return;

        QString output = QString::fromLocal8Bit(pollProcess->readAllStandardOutput()).trimmed();
        connectedDevices_ = output.split('\n', Qt::SkipEmptyParts);

        refreshDeviceBoxes_();
        updateActionButtons_();
    });

    connect(deviceTimer, &QTimer::timeout, this, [this]() {
        if (process->state() == QProcess::NotRunning && pollProcess->state() == QProcess::NotRunning) {
            QString program = "brokkr";
#ifdef Q_OS_WIN
            program += ".exe";
#endif
            pollProcess->start(program, {"--gui-mode", "--print-connected-only"});
        }
    });

    deviceTimer->start(2000);

    connect(btnPrintPit, &QPushButton::clicked, this, [this]() {
        QString why;
        if (!canRunPrintPit_(&why)) { showBlocked_("Cannot print PIT", why); return; }

        QStringList args;
        if (!editTarget->text().isEmpty()) args << "--target" << editTarget->text();
        args << "--print-pit";
        executeBrokkr(args);
    });

    connect(btnRebootDevice, &QPushButton::clicked, this, [this]() {
        QString why;
        if (!canRunReboot_(&why)) { showBlocked_("Cannot reboot", why); return; }

        QStringList args;
        if (!editTarget->text().isEmpty()) args << "--target" << editTarget->text();
        args << "--reboot";
        executeBrokkr(args);
    });

    connect(btnRun, &QPushButton::clicked, this, &BrokkrWrapper::onRunClicked);

    connect(btnReset, &QPushButton::clicked, this, [this, fileLayout]() {
        editAP->clear(); editBL->clear(); editCP->clear();
        editCSC->clear(); editUserData->clear();
        consoleOutput->clear(); editTarget->clear();
        editPit->clear();
        chkUsePit->setChecked(false);
        cmbRebootAction->setCurrentIndex(0);

        progressBar->setValue(0);
        statusLabel->setText("Ready");

        for (int i = 0; i < fileLayout->count(); ++i) {
            if (auto* chk = qobject_cast<QCheckBox*>(fileLayout->itemAt(i)->widget())) {
                chk->setChecked(false);
            }
        }
        updateActionButtons_();
    });

    updateActionButtons_();
}

void BrokkrWrapper::applyWindowHeightToContents_() {
    if (!layout()) return;

    const int want = layout()->sizeHint().height() + 30;
    if (height() < want) {
        resize(width(), want);
    }
}

void BrokkrWrapper::rebuildDeviceBoxes_(int boxCount, bool singleRow) {
    while (idComLayout_->count() > 0) {
        QLayoutItem* it = idComLayout_->takeAt(0);
        if (!it) break;
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }
    comBoxes.clear();

    if (singleRow) {
        for (int col = 0; col < boxCount; ++col) {
            auto* box = new QLineEdit(idComGroup_);
            box->setReadOnly(true);
            box->setAlignment(Qt::AlignCenter);
            box->setMinimumHeight(35);
            box->setStyleSheet("background-color: transparent; border: 1px solid gray;");
            idComLayout_->addWidget(box, 0, col);
            comBoxes.append(box);
        }
        return;
    }

    for (int i = 0; i < boxCount; ++i) {
        const int row = i / kBoxesColsMany;
        const int col = i % kBoxesColsMany;

        auto* box = new QLineEdit(idComGroup_);
        box->setReadOnly(true);
        box->setAlignment(Qt::AlignCenter);
        box->setMinimumHeight(35);
        box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        box->setStyleSheet("background-color: transparent; border: 1px solid gray;");
        idComLayout_->addWidget(box, row, col);
        comBoxes.append(box);
    }
}

void BrokkrWrapper::refreshDeviceBoxes_() {
    overflowDevices_ = false;

    for (auto* box : comBoxes) {
        box->clear();
        box->setToolTip(QString());
        box->setStyleSheet("background-color: transparent; border: 1px solid gray;");
    }

    const int shown = std::min<int>(connectedDevices_.size(), comBoxes.size());
    for (int i = 0; i < shown; ++i) {
        const QString sysname = connectedDevices_[i].trimmed();
        comBoxes[i]->setText(QString("%1:[%2]").arg(i).arg(sysname));
        comBoxes[i]->setToolTip(sysname);
        comBoxes[i]->setStyleSheet("background-color: #00e5ff; color: black; font-weight: bold; border: 1px solid #00acc1;");
    }

    if (connectedDevices_.size() > comBoxes.size() && !comBoxes.isEmpty()) {
        overflowDevices_ = true;
        const int extra = connectedDevices_.size() - comBoxes.size();

        auto* last = comBoxes.back();
        last->setText(QString("... +%1 more (use CLI)").arg(extra));
        last->setStyleSheet("background-color: #ffcc80; color: black; font-weight: bold; border: 1px solid #fb8c00;");
    }
}

bool BrokkrWrapper::canRunStart_(QString* whyNot) const {
    const bool wireless = chkWireless->isChecked();
    const bool hasTarget = !editTarget->text().trimmed().isEmpty();

    if (wireless) return true;
    if (hasTarget) return true;

    if (connectedDevices_.isEmpty()) {
        if (whyNot) *whyNot = "No connected devices detected. Connect a device or enable Wireless.";
        return false;
    }
    if (overflowDevices_) {
        if (whyNot) *whyNot =
            "Too many devices are connected for the current GUI box limit.\n"
            "Increase the slider, set a specific target, or use the CLI.";
        return false;
    }
    return true;
}

bool BrokkrWrapper::canRunReboot_(QString* whyNot) const {
    const bool wireless = chkWireless->isChecked();
    const bool hasTarget = !editTarget->text().trimmed().isEmpty();

    if (wireless) return true;
    if (hasTarget) return true;

    if (connectedDevices_.isEmpty()) {
        if (whyNot) *whyNot = "No connected devices detected.";
        return false;
    }
    if (overflowDevices_) {
        if (whyNot) *whyNot = "Too many devices for GUI reboot. Increase the slider, set a target, or use the CLI.";
        return false;
    }
    return true;
}

bool BrokkrWrapper::canRunPrintPit_(QString* whyNot) const {
    const bool wireless = chkWireless->isChecked();
    const bool hasTarget = !editTarget->text().trimmed().isEmpty();

    if (wireless) return true;
    if (hasTarget) return true;

    if (connectedDevices_.isEmpty()) {
        if (whyNot) *whyNot = "No connected devices detected.";
        return false;
    }

    if (connectedDevices_.size() != 1) {
        if (whyNot) *whyNot =
            "Printing PIT from device requires exactly one connected device.\n"
            "Disconnect extra devices or set a specific target.";
        return false;
    }

    if (overflowDevices_) {
        if (whyNot) *whyNot = "Too many devices for GUI PIT print. Increase the slider, set a target, or use the CLI.";
        return false;
    }

    return true;
}

void BrokkrWrapper::updateActionButtons_() {
    const bool busy = (process->state() != QProcess::NotRunning);

    if (busy) {
        btnRun->setEnabled(false);
        btnRebootDevice->setEnabled(false);
        btnPrintPit->setEnabled(false);
        return;
    }

    QString why;
    btnRun->setEnabled(canRunStart_(&why));
    btnRebootDevice->setEnabled(canRunReboot_(&why));
    btnPrintPit->setEnabled(canRunPrintPit_(&why));
}

void BrokkrWrapper::showBlocked_(const QString& title, const QString& msg) const {
    QMessageBox::warning(const_cast<BrokkrWrapper*>(this), title, msg);
}

void BrokkrWrapper::setupOdinFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit) {
    auto* chk = new QCheckBox(this);
    layout->addWidget(chk, row, 0);

    auto* btn = new QPushButton(label, this);
    btn->setMinimumWidth(95);
    btn->setFixedHeight(28);
    layout->addWidget(btn, row, 1);

    lineEdit = new QLineEdit(this);
    lineEdit->setReadOnly(true);
    layout->addWidget(lineEdit, row, 2);

    connect(btn, &QPushButton::clicked, this, [this, lineEdit, chk]() {
        QString file = QFileDialog::getOpenFileName(
            this,
            "Select Firmware File",
            lastDir,
            "Firmware Archives (*.tar *.tar.md5);;All Files (*)"
        );
        if (!file.isEmpty()) {
            lastDir = QFileInfo(file).absolutePath();
            lineEdit->setText(file);
            chk->setChecked(true);
        }
        updateActionButtons_();
    });

    connect(chk, &QCheckBox::toggled, this, [this, lineEdit](bool checked) {
        lineEdit->setEnabled(checked);
        updateActionButtons_();
    });
}

void BrokkrWrapper::executeBrokkr(const QStringList& args) {
    btnRun->setEnabled(false);
    btnRebootDevice->setEnabled(false);
    btnPrintPit->setEnabled(false);

    progressBar->setValue(0);
    statusLabel->setText("Starting...");

    QString program = "brokkr";
#ifdef Q_OS_WIN
    program += ".exe";
#endif

    QStringList finalArgs;
    finalArgs << "--gui-mode" << args;

    consoleOutput->append("<b>&gt; " + program.toHtmlEscaped() + " " + finalArgs.join(" ").toHtmlEscaped() + "</b>\n");
    process->start(program, finalArgs);
}

void BrokkrWrapper::onRunClicked() {
    QString why;
    if (!canRunStart_(&why)) {
        showBlocked_("Cannot start", why);
        return;
    }

    QStringList args;

    if (chkWireless->isChecked()) args << "-w";

    const int actionIndex = cmbRebootAction->currentIndex();
    if (actionIndex == 1) args << "--redownload";
    else if (actionIndex == 2) args << "--no-reboot";

    if (!editTarget->text().isEmpty()) args << "--target" << editTarget->text();

    if (chkUsePit->isChecked() && !editPit->text().isEmpty()) {
        args << "--set-pit" << editPit->text();
    }

    if (editAP->isEnabled() && !editAP->text().isEmpty()) args << "-a" << editAP->text();
    if (editBL->isEnabled() && !editBL->text().isEmpty()) args << "-b" << editBL->text();
    if (editCP->isEnabled() && !editCP->text().isEmpty()) args << "-c" << editCP->text();
    if (editCSC->isEnabled() && !editCSC->text().isEmpty()) args << "-s" << editCSC->text();
    if (editUserData->isEnabled() && !editUserData->text().isEmpty()) args << "-u" << editUserData->text();

    const bool hasAnyFlash =
        (args.contains("-a") || args.contains("-b") || args.contains("-c") || args.contains("-s") || args.contains("-u"));

    const bool hasPit = args.contains("--set-pit");

    if (!hasAnyFlash && !hasPit) {
        showBlocked_("Cannot start", "Select at least one firmware file or a PIT file.");
        return;
    }

    executeBrokkr(args);
}
