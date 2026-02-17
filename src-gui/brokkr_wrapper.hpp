#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QProcess>

class BrokkrWrapper : public QWidget {
    Q_OBJECT // MOC will find this perfectly now!

public:
    explicit BrokkrWrapper(QWidget* parent = nullptr);

private slots:
    void onRunClicked();

private:
    void setupFileInput(class QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit);
    void executeBrokkr(const QStringList& args);
    void setupOdinFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit);

    QLineEdit* editAP, * editBL, * editCP, * editCSC, * editUserData;
    QLineEdit* editTarget, * editGetPit, * editSetPit;
    QCheckBox* chkWireless, * chkReboot, * chkRedownload, * chkNoReboot, * chkPrintPit;
    QPushButton* btnRun, * btnConnected;
    QTextEdit* consoleOutput;
    QProcess* process;
    QList<QLineEdit*> comBoxes;
    QProcess* pollProcess;
	QTimer* deviceTimer;
};