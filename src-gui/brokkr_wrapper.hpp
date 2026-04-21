#pragma once

#include <QList>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "platform/platform_all.hpp"

class QGridLayout;
class QGroupBox;
class QTextEdit;
class QCloseEvent;
class QTabWidget;
class QMimeData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QChildEvent;

#if defined(BROKKR_PLATFORM_LINUX)
class QSocketNotifier;
#endif
#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#endif

class DeviceSquare;

class BrokkrWrapper : public QWidget {
 public:
  explicit BrokkrWrapper(QWidget* parent = nullptr);
  ~BrokkrWrapper() override;
  
  int logDeviceCountForLog() const noexcept { return logDevCount_.load(std::memory_order_relaxed); }

 public slots:
  void appendLogLineFromEngine(const QString& html);

 protected:
  void closeEvent(QCloseEvent* e) override;
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dragMoveEvent(QDragMoveEvent* e) override;
  void dropEvent(QDropEvent* e) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void childEvent(QChildEvent* e) override;

#if defined(Q_OS_WIN)
  bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

  void changeEvent(QEvent* e) override;

 private slots:
  void onRunClicked();

 private:
  static constexpr int kDropSlotAny = -1;
  static constexpr int kDropSlotUnknown = -2;
  static constexpr int kDropSlotBL = 0;
  static constexpr int kDropSlotAP = 1;
  static constexpr int kDropSlotCP = 2;
  static constexpr int kDropSlotCSC = 3;
  static constexpr int kDropSlotUSERDATA = 4;
  static constexpr int kDropSlotPIT = 5;

  static QString dropSlotName_(int slotId);
  static int inferDropSlotFromFileName_(const QString& fileName);
  static bool isDropFileAllowedForSlot_(int slotId, const QString& fileName);

  QStringList extractLocalDropFiles_(const QMimeData* mime) const;
  void bindDropTarget_(QWidget* widget, int slotId);
  void handleDroppedFiles_(const QStringList& localFiles, int forcedSlotId);
  bool assignDroppedFileToSlot_(int slotId, const QString& localPath, QString* reason, bool* replaced);

  void setupOdinFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit);

  void rebuildDeviceBoxes_(int boxCount, bool singleRow);
  void refreshDeviceBoxes_();
  void updateActionButtons_();
  void updateRebootDownloadButton_();
  void applyWindowHeightToContents_();

  bool canRunStart_(QString* whyNot = nullptr) const;
  bool confirmOdinModeDevicesForStart_();

  void showBlocked_(const QString& title, const QString& msg) const;

  void requestUsbRefresh_() noexcept;
  void refreshConnectedDevices_();

  void startWirelessListener_();
  void stopWirelessListener_();

  void startWorkStart_();
  void tryRebootIntoDownloadMode_();

  void setBusy_(bool busy);
  void setControlsEnabled_(bool enabled);

  void appendLogLine_(const QString& html);
  void applyHeaderStyle_();
  void updateHeaderLeds_();

  void setSquaresProgress_(double frac, bool animate);
  void setSquaresText_(const QString& s);
  void setSquaresActiveColor_(bool enhanced);
  void setSquaresFinal_(bool ok);

 private:
  static constexpr int kBoxesNormal = 8;
  static constexpr int kMassDlMaxBoxes = 24;
  static constexpr int kBoxesColsMany = 8;

  QStringList connectedDevices_;
  bool overflowDevices_ = false;

  int baseWindowHeight_ = 600;

  QGroupBox* idComGroup_ = nullptr;
  QGridLayout* idComLayout_ = nullptr;
  QWidget* headerWidget_ = nullptr;
  QWidget* ledContainer_ = nullptr;

  QList<DeviceSquare*> devSquares_;
  QList<QLabel*> comBoxes;

  std::vector<std::uint8_t> slotFailed_;
  std::vector<std::uint8_t> slotActive_;

  QTabWidget* tabWidget_ = nullptr;
  QTextEdit* consoleOutput = nullptr;

  QLineEdit* editTarget = nullptr;
  QCheckBox* chkWireless = nullptr;

  QPushButton* btnManyDevices_ = nullptr;
  QPushButton* btnRebootDownloadMode_ = nullptr;

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
  QPushButton* btnReset_ = nullptr;

  QList<QCheckBox*> fileChecks_;
  QList<QPushButton*> fileButtons_;
  QList<QLineEdit*> fileLineEdits_;

  QString lastDir;

  QTimer* deviceTimer = nullptr;
  std::atomic_bool usbDirty_{true};
  bool busy_ = false;
  bool windowsElevatedHintShown_ = false;

#if defined(BROKKR_PLATFORM_LINUX)
  int uevent_fd_ = -1;
  QSocketNotifier* uevent_notifier_ = nullptr;
#endif 
#if defined(Q_OS_MACOS)
  IONotificationPortRef mac_notify_port_ = nullptr;
  io_iterator_t mac_added_iter_ = 0;
  io_iterator_t mac_removed_iter_ = 0;
  static void macOsUsbDeviceChanged(void* refCon, io_iterator_t iterator);
#endif

  std::jthread worker_;

  std::jthread wireless_thread_;
  mutable std::mutex wireless_mtx_;
  std::optional<brokkr::platform::TcpListener> wireless_listener_;
  std::optional<brokkr::platform::TcpConnection> wireless_conn_;
  QString wireless_sysname_;
  // While true the watcher thread must NOT touch wireless_conn_ (no
  // connected() probes, no reset()), because the flash engine is using the
  // underlying socket. Set during a flash run.
  std::atomic_bool wireless_watcher_paused_{false};

  std::atomic_int logDevCount_{0};
  std::atomic_int progressVisualSteps_{1};
  std::atomic_int progressVisualBucket_{-1};

  QStringList physicalUsbPrev_;
  bool physicalWirelessPrev_ = false;
  QString physicalWirelessIdPrev_;

  std::vector<QString> plan_names_;
  std::vector<QString> plan_from_names_;
  bool enhanced_speed_ = false;
};
