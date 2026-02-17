#pragma once

#include <QWidget>
#include <QList>
#include <QString>
#include <QStringList>
#include <QPushButton>

#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QSlider>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "platform/platform_all.hpp"

class QGridLayout;
class QGroupBox;
class QCloseEvent;
class QTabWidget;

#if defined(BROKKR_PLATFORM_LINUX)
class QSocketNotifier;
#endif

class DeviceSquare;

class BrokkrWrapper : public QWidget {
    Q_OBJECT

public:
    explicit BrokkrWrapper(QWidget* parent = nullptr);
    ~BrokkrWrapper() override;

public slots:
    void appendLogLineFromEngine(const QString& html);

protected:
    void closeEvent(QCloseEvent* e) override;

#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;
#endif

private slots:
    void onRunClicked();

private:
    void setupOdinFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit);

    void rebuildDeviceBoxes_(int boxCount, bool singleRow);
    void refreshDeviceBoxes_();
    void updateActionButtons_();
    void applyWindowHeightToContents_();

    bool canRunStart_(QString* whyNot = nullptr) const;
    bool canRunPrintPit_(QString* whyNot = nullptr) const;

    void showBlocked_(const QString& title, const QString& msg) const;

    void requestUsbRefresh_() noexcept;
    void refreshConnectedDevices_();

    void startWirelessListener_();
    void stopWirelessListener_();

    void startWorkStart_();
    void startWorkPrintPit_();

    void setBusy_(bool busy);
    void setControlsEnabled_(bool enabled);

    void appendLogLine_(const QString& html);

    void setSquaresProgress_(double frac, bool animate);
    void setSquaresText_(const QString& s);
    void setSquaresActiveColor_(bool enhanced);
    void setSquaresFinal_(bool ok);

private:
    static constexpr int kBoxesNormal    = 8;
    static constexpr int kMassDlMaxBoxes = 24;
    static constexpr int kBoxesColsMany  = 8;

    QStringList connectedDevices_;
    bool overflowDevices_ = false;

    int baseWindowHeight_ = 600;

    QGroupBox* idComGroup_ = nullptr;
    QGridLayout* idComLayout_ = nullptr;

    QList<DeviceSquare*> devSquares_;
    QList<QLineEdit*> comBoxes;

    QTabWidget* tabWidget_ = nullptr;
    QTextEdit* consoleOutput = nullptr;

    QLineEdit* editTarget = nullptr;
    QCheckBox* chkWireless = nullptr;

    QPushButton* btnManyDevices_ = nullptr;
    QSlider* sldDeviceBoxes = nullptr;
    QLabel* lblDeviceBoxes = nullptr;

    QCheckBox* chkAdvanced_ = nullptr;

    QComboBox* cmbRebootAction = nullptr;

    QCheckBox* chkUsePit = nullptr;
    QLineEdit* editPit = nullptr;
    QPushButton* btnPitBrowse = nullptr;

    QLineEdit* editBL = nullptr;
    QLineEdit* editAP = nullptr;
    QLineEdit* editCP = nullptr;
    QLineEdit* editCSC = nullptr;
    QLineEdit* editUserData = nullptr;

    QPushButton* btnRun = nullptr;
    QPushButton* btnPrintPit = nullptr;
    QPushButton* btnReset_ = nullptr;

    QList<QCheckBox*> fileChecks_;
    QList<QPushButton*> fileButtons_;

    QString lastDir;

    QTimer* deviceTimer = nullptr;
    std::atomic_bool usbDirty_{true};
    bool busy_ = false;

#if defined(BROKKR_PLATFORM_LINUX)
    int uevent_fd_ = -1;
    QSocketNotifier* uevent_notifier_ = nullptr;
#endif

    std::jthread worker_;

    std::jthread wireless_thread_;
    mutable std::mutex wireless_mtx_;
    std::optional<brokkr::platform::TcpListener> wireless_listener_;
    std::optional<brokkr::platform::TcpConnection> wireless_conn_;
    QString wireless_sysname_;

    std::vector<QString> plan_names_;
    bool enhanced_speed_ = false;
};
