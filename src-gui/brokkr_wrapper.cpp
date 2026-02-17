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
    editPit->setEnabled(false); // Initially disabled
    pitLayout->addWidget(editPit, 0, 1);

    auto* btnPit = new QPushButton("Browse", this);
    btnPit->setEnabled(false); // Initially disabled
    pitLayout->addWidget(btnPit, 0, 2);

    radNonePit = new QRadioButton("None", this);
    radSetPit = new QRadioButton("Set PIT (Flash)", this);
    radGetPit = new QRadioButton("Get PIT (Extract)", this);
    radNonePit->setChecked(true);

    auto* radioLayout = new QHBoxLayout();
    radioLayout->addWidget(radNonePit);
    radioLayout->addWidget(radSetPit);
    radioLayout->addWidget(radGetPit);
    radioLayout->addStretch();
    pitLayout->addLayout(radioLayout, 1, 1, 1, 2);

    tabWidget->addTab(pitTab, "Pit");

    // Toggle the PIT inputs when "None" is checked/unchecked
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

    QLabel* linkLabel = new QLabel("<a href='#'>Brokkr Community</a>", this);
    bottomLayout->addWidget(linkLabel);

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

    connect(process, &QProcess::readyReadStandardOutput, this, [this]() {
        consoleOutput->append(QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed());
        });
    connect(process, &QProcess::readyReadStandardError, this, [this]() {
        consoleOutput->append("<font color=\"#ff5555\">" + QString::fromLocal8Bit(process->readAllStandardError()).trimmed() + "</font>");
        });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int exitCode) {
        btnRun->setEnabled(true);
        btnRebootDevice->setEnabled(true);
        btnPrintPit->setEnabled(true);
        consoleOutput->append(QString("\n<i>Brokkr finished with exit code %1</i>").arg(exitCode));
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
        radNonePit->setChecked(true); // Automatically triggers the toggle signal to disable the picker
        cmbRebootAction->setCurrentIndex(0);

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

    QString program = "brokkr";
#ifdef Q_OS_WIN
    program += ".exe";
#endif

    QStringList finalArgs;
    finalArgs << "--gui-mode" << args;

    consoleOutput->append("<b>> " + program + " " + finalArgs.join(" ") + "</b>\n");
    process->start(program, finalArgs);
}

void BrokkrWrapper::onRunClicked() {
    QStringList args;

    if (chkWireless->isChecked()) args << "-w";

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