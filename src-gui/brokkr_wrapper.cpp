
#include "brokkr_wrapper.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFileDialog>
#include <QStringList>

BrokkrWrapper::BrokkrWrapper(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Brokkr Flasher");
    resize(800, 600);

    auto* mainLayout = new QVBoxLayout(this);

    // --- File Inputs ---
    auto* fileLayout = new QGridLayout();
    setupFileInput(fileLayout, 0, "AP File (-a):", editAP);
    setupFileInput(fileLayout, 1, "BL File (-b):", editBL);
    setupFileInput(fileLayout, 2, "CP File (-c):", editCP);
    setupFileInput(fileLayout, 3, "CSC File (-s):", editCSC);
    setupFileInput(fileLayout, 4, "USERDATA (-u):", editUserData);
    mainLayout->addLayout(fileLayout);

    // --- Target & PIT ---
    auto* pitLayout = new QGridLayout();
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
    mainLayout->addLayout(pitLayout);

    // --- Options ---
    auto* optLayout = new QHBoxLayout();
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
    mainLayout->addLayout(optLayout);

    // --- Action Buttons ---
    auto* actionLayout = new QHBoxLayout();
    btnRun = new QPushButton("Start Operation", this);
    btnRun->setStyleSheet("font-weight: bold; padding: 10px;");
    btnConnected = new QPushButton("Print Connected Devices", this);
    actionLayout->addWidget(btnRun);
    actionLayout->addWidget(btnConnected);
    mainLayout->addLayout(actionLayout);

    // --- Console Output ---
    consoleOutput = new QTextEdit(this);
    consoleOutput->setReadOnly(true);
    consoleOutput->setStyleSheet("background-color: #1e1e1e; color: #4af626; font-family: Consolas, monospace;");
    mainLayout->addWidget(consoleOutput);

    // --- Process Setup ---
    process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardOutput, this, [this]() {
        consoleOutput->append(QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed());
        });
    connect(process, &QProcess::readyReadStandardError, this, [this]() {
        consoleOutput->append("<font color=\"#ff5555\">" + QString::fromLocal8Bit(process->readAllStandardError()).trimmed() + "</font>");
        });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int exitCode) {
        btnRun->setEnabled(true);
        btnConnected->setEnabled(true);
        consoleOutput->append(QString("\n<i>Brokkr finished with exit code %1</i>").arg(exitCode));
        });

    connect(btnRun, &QPushButton::clicked, this, &BrokkrWrapper::onRunClicked);
    connect(btnConnected, &QPushButton::clicked, this, [this]() {
        executeBrokkr({ "--print-connected" });
        });
}

void BrokkrWrapper::setupFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit) {
    layout->addWidget(new QLabel(label), row, 0);
    lineEdit = new QLineEdit(this);
    layout->addWidget(lineEdit, row, 1);
    auto* btn = new QPushButton("Browse", this);
    layout->addWidget(btn, row, 2);

    connect(btn, &QPushButton::clicked, this, [this, lineEdit]() {
        QString file = QFileDialog::getOpenFileName(this, "Select Firmware File");
        if (!file.isEmpty()) lineEdit->setText(file);
        });
}

void BrokkrWrapper::executeBrokkr(const QStringList& args) {
    btnRun->setEnabled(false);
    btnConnected->setEnabled(false);

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

    if (!editAP->text().isEmpty()) { args << "-a" << editAP->text(); }
    if (!editBL->text().isEmpty()) { args << "-b" << editBL->text(); }
    if (!editCP->text().isEmpty()) { args << "-c" << editCP->text(); }
    if (!editCSC->text().isEmpty()) { args << "-s" << editCSC->text(); }
    if (!editUserData->text().isEmpty()) { args << "-u" << editUserData->text(); }

    executeBrokkr(args);
}