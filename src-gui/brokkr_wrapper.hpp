#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QProcess>
#include <QRadioButton>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>

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

    QProcess* process;
    QProcess* pollProcess;
    QTimer* deviceTimer;
    QList<QLineEdit*> comBoxes;

    QTextEdit* consoleOutput;

    QLineEdit* editTarget;
    QCheckBox* chkWireless;
    QComboBox* cmbRebootAction;

    QLineEdit* editPit;
    QRadioButton* radNonePit;
    QRadioButton* radSetPit;
    QRadioButton* radGetPit;

    QLineEdit* editBL;
    QLineEdit* editAP;
    QLineEdit* editCP;
    QLineEdit* editCSC;
    QLineEdit* editUserData;
    
    QLabel* statusLabel;
    
    QProgressBar* progressBar;
    
    QPushButton* btnRun;
    QPushButton* btnPrintPit;
    QPushButton* btnRebootDevice;

    QString lastDir;
};