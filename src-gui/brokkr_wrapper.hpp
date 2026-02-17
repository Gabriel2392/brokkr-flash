#pragma once

#include <QWidget>
#include <QList>
#include <QString>
#include <QStringList>

#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QProcess>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QSlider>

class QGridLayout;
class QGroupBox;

class BrokkrWrapper : public QWidget {
    Q_OBJECT

public:
    explicit BrokkrWrapper(QWidget* parent = nullptr);

private slots:
    void onRunClicked();

private:
    void setupOdinFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit);
    void executeBrokkr(const QStringList& args);

    void rebuildDeviceBoxes_(int boxCount, bool singleRow);
    void refreshDeviceBoxes_();
    void updateActionButtons_();
    void applyWindowHeightToContents_();

    bool canRunStart_(QString* whyNot = nullptr) const;
    bool canRunReboot_(QString* whyNot = nullptr) const;
    bool canRunPrintPit_(QString* whyNot = nullptr) const;

    void showBlocked_(const QString& title, const QString& msg) const;

private:
    static constexpr int kBoxesNormal = 8;
    static constexpr int kBoxesMax    = 64;
    static constexpr int kBoxesColsMany = 8;

    QProcess* process = nullptr;
    QProcess* pollProcess = nullptr;
    QTimer* deviceTimer = nullptr;

    QStringList connectedDevices_;
    bool overflowDevices_ = false;

    int baseWindowHeight_ = 600;

    QGroupBox* idComGroup_ = nullptr;
    QGridLayout* idComLayout_ = nullptr;
    QList<QLineEdit*> comBoxes;

    QTextEdit* consoleOutput = nullptr;

    QLineEdit* editTarget = nullptr;
    QCheckBox* chkWireless = nullptr;

    QCheckBox* chkManyDevices = nullptr;
    QSlider* sldDeviceBoxes = nullptr;
    QLabel* lblDeviceBoxes = nullptr;

    QComboBox* cmbRebootAction = nullptr;

    // PIT (GUI only supports --set-pit)
    QCheckBox* chkUsePit = nullptr;
    QLineEdit* editPit = nullptr;
    QPushButton* btnPitBrowse = nullptr;

    QLineEdit* editBL = nullptr;
    QLineEdit* editAP = nullptr;
    QLineEdit* editCP = nullptr;
    QLineEdit* editCSC = nullptr;
    QLineEdit* editUserData = nullptr;

    QLabel* statusLabel = nullptr;
    QProgressBar* progressBar = nullptr;

    QPushButton* btnRun = nullptr;
    QPushButton* btnPrintPit = nullptr;
    QPushButton* btnRebootDevice = nullptr;

    QString lastDir;
};
