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

BrokkrWrapper::BrokkrWrapper(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Brokkr Flasher");
    resize(850, 600);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // --- 1. Top Banner ---
    auto* bannerLabel = new QLabel("<b>brokkr v1.0.0 flasher</b>", this);
    bannerLabel->setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4c8ddc, stop:1 #8bbceb); color: white; font-size: 26px; padding: 10px; border-radius: 3px;");
    bannerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(bannerLabel);

    // --- 2. ID:COM Section ---
    auto* idComGroup = new QGroupBox("ID:COM", this);
    auto* idComLayout = new QGridLayout(idComGroup);
    idComLayout->setSpacing(2);
    idComLayout->setContentsMargins(5, 5, 5, 5);

    // Create the Odin-style grid of ports and store them for the background poller
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 8; ++col) {
            auto* box = new QLineEdit(this);
            box->setReadOnly(true);
            box->setAlignment(Qt::AlignCenter);
            box->setMinimumHeight(35);
            box->setStyleSheet("background-color: transparent; border: 1px solid gray;");
            idComLayout->addWidget(box, row, col);
            comBoxes.append(box);
        }
    }
    mainLayout->addWidget(idComGroup);

    // --- 3. Middle Section (Split Pane) ---
    auto* middleLayout = new QHBoxLayout();

    // -- Left Pane: Tabs --
    auto* tabWidget = new QTabWidget(this);
    tabWidget->setFixedWidth(320);

    // Log Tab
    consoleOutput = new QTextEdit(this);
    consoleOutput->setReadOnly(true);
    consoleOutput->setStyleSheet("font-family: Consolas, monospace;");
    tabWidget->addTab(consoleOutput, "Log");

    // Options Tab (Retaining all Brokkr logic)
    auto* optTab = new QWidget();
    auto* optLayout = new QVBoxLayout(optTab);
    optLayout->setAlignment(Qt::AlignTop);
    chkWireless = new QCheckBox("Wireless (-w)", this);
    chkReboot = new QCheckBox("Reboot Only", this);
    chkRedownload = new QCheckBox("Redownload", this);
    chkNoReboot = new QCheckBox("No Reboot", this);
    chkPrintPit = new QCheckBox("Print PIT", this);

    optLayout->addWidget(chkWireless);
    optLayout->addWidget(chkReboot);
    optLayout->addWidget(chkRedownload);
    optLayout->addWidget(chkNoReboot);
    optLayout->addWidget(chkPrintPit);
    tabWidget->addTab(optTab, "Options");

    // Pit Tab
    auto* pitTab = new QWidget();
    auto* pitLayout = new QGridLayout(pitTab);
    pitLayout->setAlignment(Qt::AlignTop);

    pitLayout->addWidget(new QLabel("Target Sysname:"), 0, 0);
    editTarget = new QLineEdit(this);
    editTarget->setPlaceholderText("e.g. COM12 or 1-1.4");
    pitLayout->addWidget(editTarget, 0, 1);

    pitLayout->addWidget(new QLabel("Get PIT out:"), 1, 0);
    editGetPit = new QLineEdit(this);
    pitLayout->addWidget(editGetPit, 1, 1);

    pitLayout->addWidget(new QLabel("Set PIT in:"), 2, 0);
    editSetPit = new QLineEdit(this);
    pitLayout->addWidget(editSetPit, 2, 1);
    tabWidget->addTab(pitTab, "Pit");

    middleLayout->addWidget(tabWidget);

    // -- Right Pane: File Inputs --
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

    // --- 4. Bottom Action Buttons ---
    auto* bottomLayout = new QHBoxLayout();

    QLabel* linkLabel = new QLabel("<a href='#'>Brokkr Community</a>", this);
    bottomLayout->addWidget(linkLabel);
    bottomLayout->addStretch();

    btnRun = new QPushButton("Start", this);
    btnRun->setFixedSize(100, 30);

    QPushButton* btnReset = new QPushButton("Reset", this);
    btnReset->setFixedSize(100, 30);

    QPushButton* btnExit = new QPushButton("Exit", this);
    btnExit->setFixedSize(100, 30);

    bottomLayout->addWidget(btnRun);
    bottomLayout->addWidget(btnReset);
    bottomLayout->addWidget(btnExit);

    mainLayout->addLayout(bottomLayout);

    // --- Main Flashing Process Setup ---
    process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardOutput, this, [this]() {
        consoleOutput->append(QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed());
        });
    connect(process, &QProcess::readyReadStandardError, this, [this]() {
        consoleOutput->append("<font color=\"#ff5555\">" + QString::fromLocal8Bit(process->readAllStandardError()).trimmed() + "</font>");
        });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int exitCode) {
        btnRun->setEnabled(true);
        consoleOutput->append(QString("\n<i>Brokkr finished with exit code %1</i>").arg(exitCode));
        });

    // --- Silent Device Polling Setup ---
    pollProcess = new QProcess(this);
    deviceTimer = new QTimer(this);

    connect(pollProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int exitCode) {
        if (exitCode != 0) return;

        QString output = QString::fromLocal8Bit(pollProcess->readAllStandardOutput()).trimmed();
        QStringList connectedDevices = output.split('\n', Qt::SkipEmptyParts);

        // Reset all boxes back to empty/default styling first
        for (auto* box : comBoxes) {
            box->clear();
            box->setStyleSheet("background-color: transparent; border: 1px solid gray;");
        }

        // Light up a box for each connected device found
        for (int i = 0; i < connectedDevices.size() && i < comBoxes.size(); ++i) {
            QString portName = connectedDevices[i].trimmed();
            comBoxes[i]->setText(QString("%1:[%2]").arg(i).arg(portName));
            comBoxes[i]->setStyleSheet("background-color: #00e5ff; color: black; font-weight: bold; border: 1px solid #00acc1;");
        }
        });

    connect(deviceTimer, &QTimer::timeout, this, [this]() {
        // Only poll if the main flashing process ISN'T currently running
        if (process->state() == QProcess::NotRunning && pollProcess->state() == QProcess::NotRunning) {
            QString program = "brokkr";
#ifdef Q_OS_WIN
            program += ".exe";
#endif
            pollProcess->start(program, { "--print-connected-only" });
        }
        });

    deviceTimer->start(2000); // Check for new devices every 2 seconds

    // --- Button Connections ---
    connect(btnRun, &QPushButton::clicked, this, &BrokkrWrapper::onRunClicked);
    connect(btnExit, &QPushButton::clicked, this, &QWidget::close);
    connect(btnReset, &QPushButton::clicked, this, [this, fileLayout]() {
        editAP->clear(); editBL->clear(); editCP->clear();
        editCSC->clear(); editUserData->clear(); consoleOutput->clear();
        editTarget->clear();

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
        // Added the filter for .tar and .tar.md5 files
        QString file = QFileDialog::getOpenFileName(
            this,
            "Select Firmware File",
            "",
            "Firmware Archives (*.tar *.tar.md5);;All Files (*)"
        );

        if (!file.isEmpty()) {
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

    QString program = "brokkr";
#ifdef Q_OS_WIN
    program += ".exe";
#endif

    consoleOutput->append("<b>> " + program + " " + args.join(" ") + "</b>\n");
    process->start(program, args);
}

void BrokkrWrapper::onRunClicked() {
    QStringList args;

    if (chkReboot->isChecked()) {
        args << "--reboot";
        executeBrokkr(args);
        return;
    }

    if (chkPrintPit->isChecked()) args << "--print-pit";
    if (chkWireless->isChecked()) args << "-w";
    if (chkRedownload->isChecked()) args << "--redownload";
    if (chkNoReboot->isChecked()) args << "--no-reboot";

    if (!editTarget->text().isEmpty()) { args << "--target" << editTarget->text(); }
    if (!editGetPit->text().isEmpty()) { args << "--get-pit" << editGetPit->text(); }
    if (!editSetPit->text().isEmpty()) { args << "--set-pit" << editSetPit->text(); }

    if (editAP->isEnabled() && !editAP->text().isEmpty()) { args << "-a" << editAP->text(); }
    if (editBL->isEnabled() && !editBL->text().isEmpty()) { args << "-b" << editBL->text(); }
    if (editCP->isEnabled() && !editCP->text().isEmpty()) { args << "-c" << editCP->text(); }
    if (editCSC->isEnabled() && !editCSC->text().isEmpty()) { args << "-s" << editCSC->text(); }
    if (editUserData->isEnabled() && !editUserData->text().isEmpty()) { args << "-u" << editUserData->text(); }

    executeBrokkr(args);
}