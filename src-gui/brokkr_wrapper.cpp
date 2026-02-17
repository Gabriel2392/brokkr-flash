#include "brokkr_wrapper.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFileDialog>
#include <QStringList>
#include <QTabWidget>
#include <QGroupBox>
#include <QTimer>
#include <QRadioButton>
#include <QComboBox>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProgressBar>

BrokkrWrapper::BrokkrWrapper(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Brokkr Flasher");
    resize(850, 600);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    auto* bannerLabel = new QLabel("<b>brokkr v1.0.0 flasher</b>", this);
    bannerLabel->setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4c8ddc, stop:1 #8bbceb); color: white; font-size: 26px; padding: 10px; border-radius: 3px;");
    bannerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(bannerLabel);

    auto* idComGroup = new QGroupBox("ID:COM", this);
    auto* idComLayout = new QGridLayout(idComGroup);
    idComLayout->setSpacing(2);
    idComLayout->setContentsMargins(5, 5, 5, 5);

    for (int col = 0; col < 8; ++col) {
        auto* box = new QLineEdit(this);
        box->setReadOnly(true);
        box->setAlignment(Qt::AlignCenter);
        box->setMinimumHeight(35);
        box->setStyleSheet("background-color: transparent; border: 1px solid gray;");
        idComLayout->addWidget(box, 0, col);
        comBoxes.append(box);
    }
    mainLayout->addWidget(idComGroup);

    auto* middleLayout = new QHBoxLayout();

    auto* tabWidget = new QTabWidget(this);
    tabWidget->setFixedWidth(320);

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

    pitLayout->addWidget(new QLabel("PIT File:"), 0, 0);
    editPit = new QLineEdit(this);
    editPit->setEnabled(false);
    pitLayout->addWidget(editPit, 0, 1);

    auto* btnPit = new QPushButton("Browse", this);
    btnPit->setEnabled(false);
    pitLayout->addWidget(btnPit, 0, 2);

    radNonePit = new QRadioButton("None", this);
    radSetPit = new QRadioButton("Set PIT", this);
    radGetPit = new QRadioButton("Get PIT", this);
    radNonePit->setChecked(true);

    auto* radioLayout = new QHBoxLayout();
    radioLayout->addWidget(radNonePit);
    radioLayout->addWidget(radSetPit);
    radioLayout->addWidget(radGetPit);
    radioLayout->addStretch();
    pitLayout->addLayout(radioLayout, 1, 1, 1, 2);

    tabWidget->addTab(pitTab, "Pit");

    connect(radNonePit, &QRadioButton::toggled, this, [this, btnPit](bool checked) {
        editPit->setEnabled(!checked);
        btnPit->setEnabled(!checked);
        });

    connect(btnPit, &QPushButton::clicked, this, [this]() {
        QString file;
        if (radGetPit->isChecked()) {
            file = QFileDialog::getSaveFileName(this, "Save PIT File As", lastDir, "PIT Files (*.pit);;All Files (*)");
        }
        else {
            file = QFileDialog::getOpenFileName(this, "Select PIT File", lastDir, "PIT Files (*.pit);;All Files (*)");
        }
        if (!file.isEmpty()) {
            lastDir = QFileInfo(file).absolutePath();
            editPit->setText(file);
        }
        });

    middleLayout->addWidget(tabWidget);

    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setAlignment(Qt::AlignTop);

    QLabel* tipsLabel = new QLabel("Tips - How to download HOME binary\n  OLD model : Download one binary ...\n  NEW model : Download BL + AP + CP + CSC", this);
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

    // Progress Bar & Status
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
    btnPrintPit->setFixedSize(100, 30);

    btnRebootDevice = new QPushButton("Reboot Device", this);
    btnRebootDevice->setFixedSize(100, 30);

    btnRun = new QPushButton("Start", this);
    btnRun->setFixedSize(100, 30);

    QPushButton* btnReset = new QPushButton("Reset", this);
    btnReset->setFixedSize(100, 30);

    bottomLayout->addWidget(btnPrintPit);
    bottomLayout->addWidget(btnRebootDevice);
    bottomLayout->addWidget(btnRun);
    bottomLayout->addWidget(btnReset);

    mainLayout->addLayout(bottomLayout);

    process = new QProcess(this);

    // Live JSON & Log Parser
    connect(process, &QProcess::readyReadStandardOutput, this, [this]() {
        while (process->canReadLine()) {
            QString rawLine = QString::fromLocal8Bit(process->readLine()).trimmed();
            if (rawLine.isEmpty()) continue;
			const static QString key = "PROGRESSUPDATE";

            if (rawLine.startsWith(key)) {
                QString jsonString = rawLine.mid(14);
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
                    if (!notice.isEmpty() && notice != "{}") {
                        statusText += " (" + notice + ")";
                    }
                    statusLabel->setText(statusText);
                }
            }
            else {
				// Can be malformed JSON or just a regular log line, either way we print it
                if (rawLine.contains(key)) {
                    // drop
                    return;
                }
                consoleOutput->append(rawLine.toHtmlEscaped());
                QTextCursor cursor = consoleOutput->textCursor();
                cursor.movePosition(QTextCursor::End);
                consoleOutput->setTextCursor(cursor);
            }
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

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int exitCode) {
        btnRun->setEnabled(true);
        btnRebootDevice->setEnabled(true);
        btnPrintPit->setEnabled(true);
        consoleOutput->append(QString("\n<i>Brokkr finished with exit code %1</i>").arg(exitCode));
        statusLabel->setText(exitCode == 0 ? "Done" : "Failed");
        });

    pollProcess = new QProcess(this);
    deviceTimer = new QTimer(this);

    connect(pollProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int exitCode) {
        if (exitCode != 0) return;

        QString output = QString::fromLocal8Bit(pollProcess->readAllStandardOutput()).trimmed();
        QStringList connectedDevices = output.split('\n', Qt::SkipEmptyParts);

        for (auto* box : comBoxes) {
            box->clear();
            box->setStyleSheet("background-color: transparent; border: 1px solid gray;");
        }

        for (int i = 0; i < connectedDevices.size() && i < comBoxes.size(); ++i) {
            QString portName = connectedDevices[i].trimmed();
            comBoxes[i]->setText(QString("%1:[%2]").arg(i).arg(portName));
            comBoxes[i]->setStyleSheet("background-color: #00e5ff; color: black; font-weight: bold; border: 1px solid #00acc1;");
        }
        });

    connect(deviceTimer, &QTimer::timeout, this, [this]() {
        if (process->state() == QProcess::NotRunning && pollProcess->state() == QProcess::NotRunning) {
            QString program = "brokkr";
#ifdef Q_OS_WIN
            program += ".exe";
#endif
            pollProcess->start(program, { "--gui-mode", "--print-connected-only" });
        }
        });

    deviceTimer->start(2000);

    connect(btnPrintPit, &QPushButton::clicked, this, [this]() {
        QStringList args;
        if (!editTarget->text().isEmpty()) {
            args << "--target" << editTarget->text();
        }
        args << "--print-pit";
        executeBrokkr(args);
        });

    connect(btnRebootDevice, &QPushButton::clicked, this, [this]() {
        QStringList args;
        if (!editTarget->text().isEmpty()) {
            args << "--target" << editTarget->text();
        }
        args << "--reboot";
        executeBrokkr(args);
        });

    connect(btnRun, &QPushButton::clicked, this, &BrokkrWrapper::onRunClicked);

    connect(btnReset, &QPushButton::clicked, this, [this, fileLayout]() {
        editAP->clear(); editBL->clear(); editCP->clear();
        editCSC->clear(); editUserData->clear();
        consoleOutput->clear(); editTarget->clear();
        editPit->clear();
        radNonePit->setChecked(true);
        cmbRebootAction->setCurrentIndex(0);

        // Reset Progress UI
        progressBar->setValue(0);
        statusLabel->setText("Ready");

        for (int i = 0; i < fileLayout->count(); ++i) {
            if (auto* chk = qobject_cast<QCheckBox*>(fileLayout->itemAt(i)->widget())) {
                chk->setChecked(false);
            }
        }
        });
}

void BrokkrWrapper::setupOdinFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit) {
    auto* chk = new QCheckBox(this);
    layout->addWidget(chk, row, 0);

    auto* btn = new QPushButton(label, this);
    btn->setFixedSize(70, 25);
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
        });

    connect(chk, &QCheckBox::toggled, this, [lineEdit](bool checked) {
        lineEdit->setEnabled(checked);
        });
}

void BrokkrWrapper::executeBrokkr(const QStringList& args) {
    btnRun->setEnabled(false);
    btnRebootDevice->setEnabled(false);
    btnPrintPit->setEnabled(false);

    // Auto-reset progress on new action
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
    QStringList args;

	args << "--gui-mode";

    if (chkWireless->isChecked()) args << "-w";

    // Fixed ComboBox logic
    int actionIndex = cmbRebootAction->currentIndex();
    if (actionIndex == 1) {
        args << "--redownload";
    }
    else if (actionIndex == 2) {
        args << "--no-reboot";
    }

    if (!editTarget->text().isEmpty()) { args << "--target" << editTarget->text(); }

    if (!editPit->text().isEmpty() && !radNonePit->isChecked()) {
        if (radSetPit->isChecked()) {
            args << "--set-pit" << editPit->text();
        }
        else if (radGetPit->isChecked()) {
            args << "--get-pit" << editPit->text();
        }
    }

    if (editAP->isEnabled() && !editAP->text().isEmpty()) { args << "-a" << editAP->text(); }
    if (editBL->isEnabled() && !editBL->text().isEmpty()) { args << "-b" << editBL->text(); }
    if (editCP->isEnabled() && !editCP->text().isEmpty()) { args << "-c" << editCP->text(); }
    if (editCSC->isEnabled() && !editCSC->text().isEmpty()) { args << "-s" << editCSC->text(); }
    if (editUserData->isEnabled() && !editUserData->text().isEmpty()) { args << "-u" << editUserData->text(); }

    executeBrokkr(args);
}