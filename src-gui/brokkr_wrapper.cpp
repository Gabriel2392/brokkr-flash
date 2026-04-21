#include "brokkr_wrapper.hpp"

#include <QCloseEvent>
#include <QChildEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QDropEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QRegularExpression>
#include <QSet>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStringList>
#include <QTabWidget>
#include <QTextCursor>
#include <QUrl>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "app/md5_verify.hpp"
#include "app/version.hpp"
#include "core/str.hpp"
#include "protocol/odin/flash.hpp"
#include "protocol/odin/group_flasher.hpp"

#if defined(BROKKR_PLATFORM_LINUX)
  #include <linux/netlink.h>
  #include <QSocketNotifier>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#if defined(Q_OS_WIN)
  #include <dbt.h>
  #include <shellapi.h>
  #include <windows.h>
#endif

class DeviceSquare final : public QWidget {
 public:
  enum class Variant { Green, Blue, Red };

  explicit DeviceSquare(QWidget* parent = nullptr) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);

    auto sp = sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    sp.setVerticalPolicy(QSizePolicy::Fixed);
    setSizePolicy(sp);
    setFixedHeight(58);

    setVariant(Variant::Green);
    anim_.setEasingCurve(QEasingCurve::InOutQuad);
    anim_.setDuration(120);
    QObject::connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
      fill_ = std::clamp(v.toDouble(), 0.0, 1.0);
      updateGeometry();
      update();
    });
  }

  QSize sizeHint() const override { return QSize(30, 18); }
  QSize minimumSizeHint() const override { return QSize(14, 18); }

  void setFill(double v) {
    fill_ = std::clamp(v, 0.0, 1.0);
    updateGeometry();
    update();
  }

  void setFillAnimated(double v, int ms) {
    v = std::clamp(v, 0.0, 1.0);
    anim_.stop();
    anim_.setDuration(ms);
    anim_.setStartValue(fill_);
    anim_.setEndValue(v);
    anim_.start();
  }

  void setVariant(Variant v) {
    var_ = v;
    switch (var_) {
      case Variant::Green:
        top_ = QColor("#b4e051");
        bot_ = QColor("#5ba30a");
        break;
      case Variant::Blue:
        top_ = QColor("#68b3e4");
        bot_ = QColor("#186ba6");
        break;
      case Variant::Red:
        top_ = QColor("#d95757");
        bot_ = QColor("#9a0a0a");
        break;
    }
    update();
  }

  void setText(const QString& s) {
    text_ = s;
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(r, palette().color(QPalette::Base));

    const int fillW = static_cast<int>(std::lround(r.width() * fill_));
    if (fillW > 0) {
      QRect fr = r;
      fr.setWidth(fillW);
      QLinearGradient g(fr.topLeft(), fr.bottomLeft());
      g.setColorAt(0.0, top_);
      g.setColorAt(1.0, bot_);
      p.fillRect(fr, g);
    }

    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawRect(r);

    if (!text_.isEmpty()) {
      QFont f("Arial");
      f.setStyleHint(QFont::SansSerif);
      f.setBold(true);

      int pt = 10;
      for (; pt >= 6; --pt) {
        f.setPointSize(pt);
        QFontMetrics fm(f);
        if (fm.height() <= (r.height() - 2)) break;
      }
      if (pt < 6) f.setPointSize(6);

      p.setFont(f);

      QFontMetrics fm(f);
      const int maxW = std::max(0, r.width() - 4);
      const QString shown = fm.elidedText(text_, Qt::ElideMiddle, maxW);
      const QRect textR = r.adjusted(2, 0, -2, 0);

      const bool dark_theme = palette().color(QPalette::Window).lightness() < 128;
      const QColor shadow_col = dark_theme ? QColor(0, 0, 0, 160) : QColor(255, 255, 255, 180);
      const QColor text_col = dark_theme ? QColor("#ffffff") : QColor("#111111");

      p.setPen(shadow_col);
      p.drawText(textR.translated(1, 1), Qt::AlignCenter, shown);

      p.setPen(text_col);
      p.drawText(textR, Qt::AlignCenter, shown);
    }
  }

 private:
  double fill_ = 0.0;
  Variant var_ = Variant::Green;
  QColor top_;
  QColor bot_;
  QString text_;
  QVariantAnimation anim_{};
};

namespace {

static constexpr std::uint16_t SAMSUNG_VID = 0x04E8;
static constexpr std::uint16_t ODIN_PIDS[] = {0x6601, 0x685D, 0x68C3};

static std::vector<std::uint16_t> default_pids() { return {std::begin(ODIN_PIDS), std::end(ODIN_PIDS)}; }

static bool is_odin_product(std::uint16_t pid) {
  const auto pids = default_pids();
  return std::ranges::find(pids, pid) != pids.end();
}

static bool is_pit_name(std::string_view base) { return brokkr::core::ends_with_ci(base, ".pit"); }

static bool is_pit_drop_name(const QString& file_name) {
  return file_name.trimmed().toLower().endsWith(".pit");
}

static bool is_firmware_drop_name(const QString& file_name) {
  const QString name = file_name.trimmed().toLower();
  return name.endsWith(".tar") || name.endsWith(".md5");
}

static bool is_token_char(QChar c) { return c.isLetterOrNumber(); }

static bool contains_token(const QString& upper_name, const QString& token) {
  int from = 0;
  for (;;) {
    const int pos = upper_name.indexOf(token, from, Qt::CaseSensitive);
    if (pos < 0) return false;

    const int left = pos - 1;
    const int right = pos + token.size();

    const bool left_ok = (left < 0) || !is_token_char(upper_name[left]);
    const bool right_ok = (right >= upper_name.size()) || !is_token_char(upper_name[right]);

    if (left_ok && right_ok) return true;
    from = pos + 1;
  }
}

static constexpr const char* kDropSlotProperty = "brokkr.dropSlot";

static QString drop_file_identity_key(const QString& path_like) {
  QFileInfo fi(QDir::cleanPath(path_like.trimmed()));
  QString key;

  if (fi.exists()) {
    key = fi.canonicalFilePath();
    if (key.isEmpty()) key = fi.absoluteFilePath();
  } else {
    key = fi.absoluteFilePath();
  }

  key = QDir::cleanPath(key);
#if defined(Q_OS_WIN)
  key = key.toLower();
#endif
  return key;
}

#if defined(Q_OS_WIN)
static void allow_windows_drop_messages(HWND hwnd) {
  if (!hwnd) return;
  (void)ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
  (void)ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
  (void)ChangeWindowMessageFilterEx(hwnd, 0x0049, MSGFLT_ALLOW, nullptr); // WM_COPYGLOBALDATA
  DragAcceptFiles(hwnd, TRUE);
}

static bool is_windows_process_elevated() {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;

  TOKEN_ELEVATION elev{};
  DWORD out = 0;
  const BOOL ok = GetTokenInformation(tok, TokenElevation, &elev, sizeof(elev), &out);
  CloseHandle(tok);
  return ok && elev.TokenIsElevated;
}

static QStringList parse_hdrop_files(HDROP hdrop) {
  QStringList out;
  if (!hdrop) return out;

  const UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
  for (UINT i = 0; i < count; ++i) {
    const UINT need = DragQueryFileW(hdrop, i, nullptr, 0);
    if (need == 0) continue;

    std::wstring tmp;
    tmp.resize(static_cast<std::size_t>(need));
    const UINT got = DragQueryFileW(hdrop, i, tmp.data(), need + 1);
    if (got == 0) continue;

    out.push_back(QDir::cleanPath(QString::fromWCharArray(tmp.c_str())));
  }
  return out;
}

static QStringList parse_windows_filenamew_payload(const QByteArray& raw) {
  QStringList out;
  if (raw.isEmpty()) return out;

  const auto* w = reinterpret_cast<const wchar_t*>(raw.constData());
  const int wlen = static_cast<int>(raw.size() / static_cast<int>(sizeof(wchar_t)));
  if (wlen <= 0) return out;

  QString packed = QString::fromWCharArray(w, wlen);
  packed.replace('\r', '\0');
  packed.replace('\n', '\0');

  const auto parts = packed.split('\0', Qt::SkipEmptyParts);
  for (const auto& p : parts) {
    const QString t = QDir::cleanPath(p.trimmed());
    if (!t.isEmpty()) out.push_back(t);
  }
  return out;
}
#endif

static QString htmlEsc(const QString& s) { return s.toHtmlEscaped(); }

static QString human_bytes(std::uint64_t b) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(b);
  int u = 0;
  while (v >= 1024.0 && u < 4) {
    v /= 1024.0;
    ++u;
  }
  const int prec = (u == 0) ? 0 : 1;
  return QString("%1 %2").arg(v, 0, 'f', prec).arg(units[u]);
}

template <class Mutex>
class QtTextSink final : public spdlog::sinks::base_sink<Mutex> {
 public:
  explicit QtTextSink(BrokkrWrapper* w) : w_(w) {}

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    spdlog::memory_buf_t formatted;
    this->formatter_->format(msg, formatted);

    QString line = QString::fromUtf8(formatted.data(), static_cast<int>(formatted.size()));
    while (!line.isEmpty() && (line.endsWith('\n') || line.endsWith('\r'))) line.chop(1);
    if (line.isEmpty()) return;

    const int z = w_ ? w_->logDeviceCountForLog() : 0;
    line = QString("<%1> %2").arg(z).arg(line);

    QMetaObject::invokeMethod(
        w_, [w = w_, s = htmlEsc(line)]() { w->appendLogLineFromEngine(s); }, Qt::QueuedConnection);
  }

  void flush_() override {}

 private:
  BrokkrWrapper* w_ = nullptr;
};

static std::shared_ptr<spdlog::logger> make_qt_logger(BrokkrWrapper* w) {
  auto sink = std::make_shared<QtTextSink<std::mutex>>(w);
  sink->set_pattern("%v");
  auto log = std::make_shared<spdlog::logger>("qt", spdlog::sinks_init_list{sink});
#ifndef NDEBUG
  log->set_level(spdlog::level::debug);
#else
  log->set_level(spdlog::level::info);
#endif
  return log;
}

static std::optional<brokkr::platform::UsbDeviceSysfsInfo> select_samsung_target(QString sysname_q) {
  const std::string sysname = sysname_q.trimmed().toStdString();
  if (sysname.empty()) return {};

  auto info = brokkr::platform::find_by_sysname(sysname);
  if (!info) return {};

  if (info->vendor != SAMSUNG_VID) return {};
  return info;
}

static std::optional<brokkr::platform::UsbDeviceSysfsInfo> select_odin_target(QString sysname_q) {
  auto info = select_samsung_target(std::move(sysname_q));
  if (!info) return {};
  if (!is_odin_product(info->product)) return {};
  return info;
}

static std::vector<brokkr::platform::UsbDeviceSysfsInfo> enumerate_samsung_targets() {
  brokkr::platform::EnumerateFilter f{.vendor = SAMSUNG_VID};
  return brokkr::platform::enumerate_usb_devices_sysfs(f);
}

static std::vector<brokkr::platform::UsbDeviceSysfsInfo> enumerate_odin_targets() {
  brokkr::platform::EnumerateFilter f{.vendor = SAMSUNG_VID, .products = default_pids()};
  return brokkr::platform::enumerate_usb_devices_sysfs(f);
}

static brokkr::core::Result<std::vector<std::byte>> read_all_source(brokkr::io::ByteSource& src) noexcept {
  constexpr std::uint64_t kMax = 256ull * 1024ull * 1024ull;
  const auto sz64 = src.size();
  if (sz64 > kMax) return brokkr::core::fail("Source too large: " + src.display_name());

  std::vector<std::byte> out(static_cast<std::size_t>(sz64));
  for (std::size_t off = 0; off < out.size();) {
    const std::size_t got = src.read({out.data() + off, out.size() - off});
    if (!got) {
      auto st = src.status();
      if (!st) return brokkr::core::fail(std::move(st.error()));
      return brokkr::core::fail("Short read: " + src.display_name());
    }
    off += got;
  }
  return out;
}

static std::shared_ptr<const std::vector<std::byte>> pit_from_specs(const std::vector<brokkr::odin::ImageSpec>& specs) {
  const brokkr::odin::ImageSpec* pit = nullptr;
  for (const auto& s : specs)
    if (is_pit_name(s.basename)) pit = &s;
  if (!pit) return {};

  auto sr = pit->open();
  if (!sr) {
    spdlog::error("PIT open failed: {}", sr.error());
    return {};
  }

  auto rr = read_all_source(**sr);
  if (!rr) {
    spdlog::error("PIT read failed: {}", rr.error());
    return {};
  }

  return std::make_shared<const std::vector<std::byte>>(std::move(*rr));
}

} // namespace

#if defined(Q_OS_MACOS)
void BrokkrWrapper::macOsUsbDeviceChanged(void* refCon, io_iterator_t iterator) {
  bool changed = false;
  spdlog::debug("macOS USB device change detected");
  while (io_service_t object = IOIteratorNext(iterator)) {
    changed = true;
    spdlog::debug("Device changed: {}", object);
    IOObjectRelease(object);
  }
  spdlog::debug("End of device change events");
  if (changed && refCon) {
    auto* wrapper = static_cast<BrokkrWrapper*>(refCon);
    spdlog::debug("Requesting USB refresh from macOS device change event");
    static_cast<BrokkrWrapper*>(refCon)->requestUsbRefresh_();
  }
  spdlog::debug("Finished processing macOS USB device change events");
}
#endif

BrokkrWrapper::BrokkrWrapper(QWidget* parent) : QWidget(parent) {
  setWindowTitle("Brokkr Flash");
  setWindowIcon(QIcon(":/brokkr.ico"));
  resize(850, 600);
  setAcceptDrops(true);
#if defined(Q_OS_WIN)
  allow_windows_drop_messages(reinterpret_cast<HWND>(winId()));
#endif
  baseWindowHeight_ = height();

  spdlog::set_default_logger(make_qt_logger(this));

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);

  headerWidget_ = new QWidget(this);
  headerWidget_->setObjectName("headerBanner");
  headerWidget_->setFixedHeight(56);
  applyHeaderStyle_();

  auto* headerLayout = new QHBoxLayout(headerWidget_);
  headerLayout->setContentsMargins(24, 0, 24, 0);

  auto* titleLabel =
      new QLabel(QString(R"(<span style="color:#4da6ff; font-size:22px; font-weight:bold;">BROKKR</span>)"
                         R"(<span style="color:#ff9900; font-size:22px; font-weight:bold;">&nbsp; FLASH TOOL</span>)"
                         R"(<span style="color:#4da6ff; font-size:11px; font-weight:bold;">&nbsp; v%1</span>)")
                     .arg(QString::fromStdString(brokkr::app::version_string())),
                 headerWidget_);
  headerLayout->addWidget(titleLabel);
  headerLayout->addStretch();

  ledContainer_ = new QWidget(headerWidget_);
  auto* ledInner = new QHBoxLayout(ledContainer_);
  ledInner->setContentsMargins(0, 0, 0, 0);
  ledInner->setSpacing(8);
  headerLayout->addWidget(ledContainer_);

  mainLayout->addWidget(headerWidget_);

  idComGroup_ = new QGroupBox("ID:COM", this);
  {
    auto pal = idComGroup_->palette();
    QColor bg = pal.color(QPalette::Window);
    bg = bg.darker(110);
    pal.setColor(QPalette::Window, bg);
    idComGroup_->setPalette(pal);
    idComGroup_->setAutoFillBackground(true);
  }
  idComLayout_ = new QGridLayout(idComGroup_);
  idComLayout_->setSpacing(4);
  idComLayout_->setContentsMargins(5, 5, 5, 5);

  rebuildDeviceBoxes_(kBoxesNormal, true);
  mainLayout->addWidget(idComGroup_);

  auto* middleLayout = new QHBoxLayout();

  tabWidget_ = new QTabWidget(this);
  tabWidget_->setFixedWidth(360);

  consoleOutput = new QTextEdit(this);
  consoleOutput->setReadOnly(true);
  consoleOutput->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  QFont logFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  logFont.setPointSize(9);
  consoleOutput->setFont(logFont);
  tabWidget_->addTab(consoleOutput, "Log");

  auto* optTab = new QWidget();
  auto* optLayout = new QVBoxLayout(optTab);
  optLayout->setAlignment(Qt::AlignTop);

  auto* targetLayout = new QHBoxLayout();
  targetLayout->addWidget(new QLabel("Target Sysname:", this));
  editTarget = new QLineEdit(this);
  editTarget->setPlaceholderText("e.g. COM12 or 1-1.4");
  targetLayout->addWidget(editTarget);
  optLayout->addLayout(targetLayout);

  chkWireless = new QCheckBox("Wireless", this);
  optLayout->addWidget(chkWireless);

#if defined(BROKKR_PLATFORM_WINDOWS)
  btnRebootDownloadMode_ = new QPushButton("Try to Reboot them into Download Mode", this);
  btnRebootDownloadMode_->setEnabled(false);
  optLayout->addWidget(btnRebootDownloadMode_);

  connect(btnRebootDownloadMode_, &QPushButton::clicked, this, [this]() {
    if (busy_) return;
    tryRebootIntoDownloadMode_();
  });
#endif

  optLayout->addSpacing(10);
  optLayout->addWidget(new QLabel("Post-Action:", this));
  cmbRebootAction = new QComboBox(this);
  cmbRebootAction->addItem("Default (Reboot Normally)");
  cmbRebootAction->addItem("No Reboot");
  optLayout->addWidget(cmbRebootAction);

  tabWidget_->addTab(optTab, "Options");

  auto* pitTab = new QWidget();
  auto* pitLayout = new QGridLayout(pitTab);
  pitLayout->setAlignment(Qt::AlignTop);

  chkUsePit = new QCheckBox("Use PIT", this);
  pitLayout->addWidget(chkUsePit, 0, 0, 1, 3);

  pitLayout->addWidget(new QLabel("PIT File:"), 1, 0);

  editPit = new QLineEdit(this);
  editPit->setEnabled(false);
  pitLayout->addWidget(editPit, 1, 1);

  btnPitBrowse = new QPushButton("Browse", this);
  btnPitBrowse->setEnabled(false);
  pitLayout->addWidget(btnPitBrowse, 1, 2);

  bindDropTarget_(pitTab, kDropSlotPIT);
  bindDropTarget_(chkUsePit, kDropSlotPIT);
  bindDropTarget_(editPit, kDropSlotPIT);
  bindDropTarget_(btnPitBrowse, kDropSlotPIT);

  tabWidget_->addTab(pitTab, "Pit");

  connect(chkUsePit, &QCheckBox::toggled, this, [this](bool checked) {
    if (busy_) {
      chkUsePit->setChecked(!checked);
      return;
    }
    editPit->setEnabled(checked);
    btnPitBrowse->setEnabled(checked);
    if (!checked) editPit->clear();
    updateActionButtons_();
  });

  connect(btnPitBrowse, &QPushButton::clicked, this, [this]() {
    if (busy_) return;
    const QString file = QFileDialog::getOpenFileName(this, "Select PIT File", lastDir,
                                                      "PIT Files (*.pit);;All Files (*)");
    if (!file.isEmpty()) {
      lastDir = QFileInfo(file).absolutePath();
      editPit->setText(file);
      updateActionButtons_();
    }
  });

  btnManyDevices_ = new QPushButton("Mass D/L", this);
  btnManyDevices_->setCheckable(true);
  btnManyDevices_->setMinimumWidth(135);
  btnManyDevices_->setFixedHeight(24);

  connect(btnManyDevices_, &QPushButton::toggled, this, [this](bool checked) {
    if (busy_) {
      btnManyDevices_->setChecked(!checked);
      return;
    }
    if (chkWireless && chkWireless->isChecked()) {
      btnManyDevices_->setChecked(false);
      return;
    }

    if (!checked) {
      rebuildDeviceBoxes_(kBoxesNormal, true);
      layout()->invalidate();
      layout()->activate();
      setMinimumHeight(0);
      resize(width(), baseWindowHeight_);
    } else {
      rebuildDeviceBoxes_(kMassDlMaxBoxes, false);
      applyWindowHeightToContents_();
    }

    refreshDeviceBoxes_();
    updateActionButtons_();
  });

  connect(chkWireless, &QCheckBox::toggled, this, [this](bool checked) {
    if (busy_) {
      chkWireless->setChecked(!checked);
      return;
    }

    if (checked) {
      if (btnManyDevices_) {
        btnManyDevices_->setChecked(false);
        btnManyDevices_->setEnabled(false);
      }

      rebuildDeviceBoxes_(1, true);
      layout()->invalidate();
      layout()->activate();
      setMinimumHeight(0);
      resize(width(), baseWindowHeight_);

      startWirelessListener_();
    } else {
      stopWirelessListener_();

      if (btnManyDevices_) btnManyDevices_->setEnabled(true);

      rebuildDeviceBoxes_(kBoxesNormal, true);
      layout()->invalidate();
      layout()->activate();
      setMinimumHeight(0);
      resize(width(), baseWindowHeight_);
    }

    refreshDeviceBoxes_();
    updateActionButtons_();
  });

  connect(editTarget, &QLineEdit::textChanged, this, [this](const QString&) {
    if (busy_) return;
    updateActionButtons_();
    requestUsbRefresh_();
  });

  middleLayout->addWidget(tabWidget_);

  auto* rightWidget = new QWidget(this);
  auto* rightLayout = new QVBoxLayout(rightWidget);
  rightLayout->setAlignment(Qt::AlignTop);

  QLabel* tipsLabel = new QLabel(
      "Tips - How to download HOME binary\n"
      "  OLD model : Download one binary ...\n"
      "  NEW model : Download BL + AP + CP + CSC",
      this);
  tipsLabel->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
  tipsLabel->setMargin(6);
  QFont tipsFont = tipsLabel->font();
  tipsFont.setPointSize(9);
  tipsLabel->setFont(tipsFont);
  QPalette tipsPal = tipsLabel->palette();
  tipsPal.setColor(QPalette::WindowText, Qt::gray);
  tipsLabel->setPalette(tipsPal);
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

  btnRun = new QPushButton("Start", this);
  btnReset_ = new QPushButton("Reset", this);

  const int bottomW = 135;
  const int bottomH = 32;
  btnRun->setMinimumSize(bottomW, bottomH);
  btnReset_->setMinimumSize(bottomW, bottomH);

  auto* footerLabel = new QLabel(
      R"(<a href="https://github.com/Gabriel2392/brokkr-flash" style="color: #4c8ddc;">GitHub Repo</a>)", this);
  footerLabel->setOpenExternalLinks(true);
  QFont footerFont = footerLabel->font();
  footerFont.setPointSize(10);
  footerLabel->setFont(footerFont);
  footerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* telegramLabel = new QLabel(
      R"(<a href="https://t.me/BrokkrCommunity" style="color: #4c8ddc;">Telegram Community</a>)", this);
    telegramLabel->setOpenExternalLinks(true);
    telegramLabel->setFont(footerFont);
    telegramLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  bottomLayout->addWidget(footerLabel, 0, Qt::AlignBottom);
    bottomLayout->addSpacing(14);
    bottomLayout->addWidget(telegramLabel, 0, Qt::AlignBottom);
  bottomLayout->addStretch();

  auto* resetColWidget = new QWidget(this);
  auto* resetColLayout = new QVBoxLayout(resetColWidget);
  resetColLayout->setContentsMargins(0, 0, 0, 0);
  resetColLayout->setSpacing(6);
  resetColLayout->addWidget(btnManyDevices_);
  resetColLayout->addWidget(btnReset_);
  bottomLayout->addWidget(resetColWidget, 0, Qt::AlignBottom);

  bottomLayout->addSpacing(10);
  bottomLayout->addWidget(btnRun, 0, Qt::AlignBottom);

  mainLayout->addLayout(bottomLayout);

  connect(btnRun, &QPushButton::clicked, this, &BrokkrWrapper::onRunClicked);

  connect(btnReset_, &QPushButton::clicked, this, [this, fileLayout]() {
    if (busy_) return;

    editAP->clear();
    editBL->clear();
    editCP->clear();
    editCSC->clear();
    editUserData->clear();
    consoleOutput->clear();
    editTarget->clear();
    editPit->clear();
    chkUsePit->setChecked(false);
    cmbRebootAction->setCurrentIndex(0);

    slotFailed_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
    slotActive_.assign(static_cast<std::size_t>(devSquares_.size()), 0);

    for (auto* sq : devSquares_) {
      if (!sq) continue;
      sq->setVariant(DeviceSquare::Variant::Green);
      sq->setText("");
      sq->setFill(0.0);
    }

    refreshDeviceBoxes_();
    updateHeaderLeds_();

    for (int i = 0; i < fileLayout->count(); ++i) {
      if (auto* chk = qobject_cast<QCheckBox*>(fileLayout->itemAt(i)->widget())) chk->setChecked(false);
    }

    updateActionButtons_();
    requestUsbRefresh_();
  });

#if defined(BROKKR_PLATFORM_LINUX)
  {
    uevent_fd_ = ::socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (uevent_fd_ >= 0) {
      sockaddr_nl addr{};
      addr.nl_family = AF_NETLINK;
      addr.nl_groups = 1;
      if (::bind(uevent_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        uevent_notifier_ = new QSocketNotifier(uevent_fd_, QSocketNotifier::Read, this);
        connect(uevent_notifier_, &QSocketNotifier::activated, this, [this]() {
          char buf[4096];
          const ssize_t n = ::recv(uevent_fd_, buf, sizeof(buf) - 1, 0);
          if (n <= 0) return;
          buf[n] = '\0';
          const QString s = QString::fromLocal8Bit(buf, n);
          if (s.contains("SUBSYSTEM=usb") || s.contains("SUBSYSTEM=tty")) {
            // SAFETY: n is always positive here due to the check above, and we ensure null-termination at buf[n] = '\0'.
            spdlog::debug("Uevent received: {}", std::string_view{buf, static_cast<size_t>(n)});
            requestUsbRefresh_();
          }
        });
      } else {
        ::close(uevent_fd_);
        uevent_fd_ = -1;
      }
    }
  }
#endif
#if defined(Q_OS_MACOS)
  {
    mac_notify_port_ = IONotificationPortCreate(kIOMasterPortDefault);
    CFRunLoopAddSource(CFRunLoopGetMain(), IONotificationPortGetRunLoopSource(mac_notify_port_), kCFRunLoopDefaultMode);

    CFMutableDictionaryRef matchDict = IOServiceMatching(kIOUSBDeviceClassName);
    matchDict = (CFMutableDictionaryRef)CFRetain(matchDict);

    IOServiceAddMatchingNotification(mac_notify_port_, kIOFirstMatchNotification, matchDict,
                                     &BrokkrWrapper::macOsUsbDeviceChanged, this, &mac_added_iter_);

    macOsUsbDeviceChanged(nullptr, mac_added_iter_);

    IOServiceAddMatchingNotification(mac_notify_port_, kIOTerminatedNotification, matchDict,
                                     &BrokkrWrapper::macOsUsbDeviceChanged, this, &mac_removed_iter_);

    macOsUsbDeviceChanged(nullptr, mac_removed_iter_);
  }
#endif

  deviceTimer = new QTimer(this);
  connect(deviceTimer, &QTimer::timeout, this, [this]() {
    if (!usbDirty_.exchange(false)) return;
    refreshConnectedDevices_();
  });
  deviceTimer->start(2000);

  requestUsbRefresh_();
  setControlsEnabled_(true);
  updateActionButtons_();

  // Ensure drops are handled regardless of the exact child widget under cursor.
  bindDropTarget_(this, kDropSlotAny);
  for (auto* w : findChildren<QWidget*>()) {
    if (!w) continue;
    if (w->property(kDropSlotProperty).isValid()) continue;
    bindDropTarget_(w, kDropSlotAny);
  }

#if defined(Q_OS_WIN)
  if (!windowsElevatedHintShown_ && is_windows_process_elevated()) {
    windowsElevatedHintShown_ = true;
#ifndef NDEBUG
    appendLogLine_(
        "<font color=\"#ffb366\">Notice: Running as Administrator can block Explorer drag &amp; drop. "
        "Run Brokkr normally for reliable DnD.</font>");
#endif
  }
#endif
}

BrokkrWrapper::~BrokkrWrapper() {
  stopWirelessListener_();
#if defined(BROKKR_PLATFORM_LINUX)
  if (uevent_notifier_) uevent_notifier_->setEnabled(false);
  if (uevent_fd_ >= 0) {
    ::close(uevent_fd_);
    uevent_fd_ = -1;
  }
#endif
#if defined(Q_OS_MACOS)
  if (mac_notify_port_) {
    CFRunLoopRemoveSource(CFRunLoopGetMain(), IONotificationPortGetRunLoopSource(mac_notify_port_),
                          kCFRunLoopDefaultMode);
    IONotificationPortDestroy(mac_notify_port_);
    mac_notify_port_ = nullptr;
  }
  if (mac_added_iter_) {
    IOObjectRelease(mac_added_iter_);
    mac_added_iter_ = 0;
  }
  if (mac_removed_iter_) {
    IOObjectRelease(mac_removed_iter_);
    mac_removed_iter_ = 0;
  }
#endif
}

void BrokkrWrapper::appendLogLineFromEngine(const QString& html) { appendLogLine_(html); }

void BrokkrWrapper::closeEvent(QCloseEvent* e) {
  if (busy_) {
    QMessageBox::warning(this, "Brokkr Flash", "An operation is in progress. Please wait for it to complete.");
    e->ignore();
    return;
  }
  e->accept();
}

QString BrokkrWrapper::dropSlotName_(int slotId) {
  switch (slotId) {
    case kDropSlotBL: return "BL";
    case kDropSlotAP: return "AP";
    case kDropSlotCP: return "CP";
    case kDropSlotCSC: return "CSC";
    case kDropSlotUSERDATA: return "USERDATA";
    case kDropSlotPIT: return "PIT";
    default: return "UNKNOWN";
  }
}

int BrokkrWrapper::inferDropSlotFromFileName_(const QString& fileName) {
  const QString leaf = QFileInfo(fileName).fileName();
  if (leaf.isEmpty()) return kDropSlotUnknown;

  if (is_pit_drop_name(leaf)) return kDropSlotPIT;

  const QString upper = leaf.toUpper();

  if (contains_token(upper, "HOME_CSC")) return kDropSlotCSC;
  if (contains_token(upper, "USERDATA")) return kDropSlotUSERDATA;
  if (contains_token(upper, "CSC")) return kDropSlotCSC;
  if (contains_token(upper, "BL")) return kDropSlotBL;
  if (contains_token(upper, "AP")) return kDropSlotAP;
  if (contains_token(upper, "CP")) return kDropSlotCP;

  return kDropSlotUnknown;
}

bool BrokkrWrapper::isDropFileAllowedForSlot_(int slotId, const QString& fileName) {
  if (slotId == kDropSlotPIT) return is_pit_drop_name(fileName);
  if (slotId >= kDropSlotBL && slotId <= kDropSlotUSERDATA) return is_firmware_drop_name(fileName);
  return false;
}

QStringList BrokkrWrapper::extractLocalDropFiles_(const QMimeData* mime) const {
  QStringList out;
  if (!mime) return out;

  QSet<QString> seen;

  auto add_if_local = [&](const QString& pathLike) {
    const QString p = QDir::cleanPath(pathLike.trimmed());
    if (p.isEmpty()) return;
    const QString key = drop_file_identity_key(p);
    if (key.isEmpty() || seen.contains(key)) return;
    seen.insert(key);
    out.push_back(p);
  };

  if (mime->hasUrls()) {
    for (const QUrl& u : mime->urls()) {
      if (!u.isLocalFile()) continue;
      add_if_local(u.toLocalFile());
    }
  }

  if (mime->hasText()) {
    const auto lines = mime->text().split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    for (const auto& line : lines) {
      const QString t = line.trimmed();
      if (t.isEmpty()) continue;
      const QUrl maybeUrl(t);
      if (maybeUrl.isValid() && maybeUrl.isLocalFile()) {
        add_if_local(maybeUrl.toLocalFile());
      } else if (QFileInfo(t).isAbsolute()) {
        add_if_local(t);
      }
    }
  }

#if defined(Q_OS_WIN)
  static constexpr const char* kFmtFileNameW = "application/x-qt-windows-mime;value=\"FileNameW\"";
  static constexpr const char* kFmtFileNameA = "application/x-qt-windows-mime;value=\"FileName\"";

  if (mime->hasFormat(kFmtFileNameW)) {
    for (const auto& p : parse_windows_filenamew_payload(mime->data(kFmtFileNameW))) add_if_local(p);
  }
  if (mime->hasFormat(kFmtFileNameA)) {
    const auto raw = mime->data(kFmtFileNameA);
    for (const auto& part : QString::fromLocal8Bit(raw).split(QChar('\0'), Qt::SkipEmptyParts)) add_if_local(part);
  }
#endif

  return out;
}

void BrokkrWrapper::bindDropTarget_(QWidget* widget, int slotId) {
  if (!widget) return;
  widget->setAcceptDrops(true);
  widget->setProperty(kDropSlotProperty, slotId);
  widget->removeEventFilter(this);
  widget->installEventFilter(this);
}

bool BrokkrWrapper::assignDroppedFileToSlot_(int slotId, const QString& localPath, QString* reason, bool* replaced) {
  if (replaced) *replaced = false;

  QFileInfo fi(localPath);
  if (!fi.exists()) {
    if (reason) *reason = "File does not exist.";
    return false;
  }
  if (!fi.isFile()) {
    if (reason) *reason = "Dropped item is not a regular file.";
    return false;
  }
  if (!fi.isReadable()) {
    if (reason) *reason = "File is not readable.";
    return false;
  }
  if (!isDropFileAllowedForSlot_(slotId, fi.fileName())) {
    if (reason) {
      if (slotId == kDropSlotPIT)
        *reason = "PIT slot accepts only .pit files.";
      else
        *reason = "Firmware slots accept only .tar or .md5 files.";
    }
    return false;
  }

  const QString finalPath = QDir::toNativeSeparators(fi.absoluteFilePath());
  lastDir = fi.absolutePath();

  if (slotId == kDropSlotPIT) {
    if (!chkUsePit || !editPit) {
      if (reason) *reason = "PIT controls are unavailable.";
      return false;
    }

    const QString old = editPit->text().trimmed();
    if (replaced) *replaced = !old.isEmpty() && old.compare(finalPath, Qt::CaseInsensitive) != 0;

    if (!chkUsePit->isChecked()) chkUsePit->setChecked(true);
    editPit->setText(finalPath);
    return true;
  }

  if (slotId < kDropSlotBL || slotId > kDropSlotUSERDATA) {
    if (reason) *reason = "Unknown destination slot.";
    return false;
  }
  if (slotId >= fileLineEdits_.size() || slotId >= fileChecks_.size()) {
    if (reason) *reason = "Destination slot is not available in this UI.";
    return false;
  }

  auto* lineEdit = fileLineEdits_[slotId];
  auto* check = fileChecks_[slotId];
  if (!lineEdit || !check) {
    if (reason) *reason = "Destination slot controls are not available.";
    return false;
  }

  const QString old = lineEdit->text().trimmed();
  if (replaced) *replaced = !old.isEmpty() && old.compare(finalPath, Qt::CaseInsensitive) != 0;

  lineEdit->setText(finalPath);
  check->setEnabled(true);
  check->setChecked(true);
  return true;
}

void BrokkrWrapper::handleDroppedFiles_(const QStringList& localFiles, int forcedSlotId) {
  if (busy_) {
    showBlocked_("Cannot accept files", "Cannot change files while flashing is in progress.");
    return;
  }
  if (localFiles.isEmpty()) return;

  QStringList accepted;
  QStringList rejected;
  QSet<int> usedSlots;
  QSet<QString> seenFiles;

  for (const QString& p : localFiles) {
    const QString fileKey = drop_file_identity_key(p);
    if (!fileKey.isEmpty() && seenFiles.contains(fileKey)) {
      // Ignore duplicate payload entries (e.g., Windows long-path + 8.3 alias of the same file).
      continue;
    }
    if (!fileKey.isEmpty()) seenFiles.insert(fileKey);

    const QFileInfo fi(p);
    const QString leaf = fi.fileName().isEmpty() ? p : fi.fileName();

    int slotId = forcedSlotId;
    if (slotId == kDropSlotAny) slotId = inferDropSlotFromFileName_(leaf);
    if (slotId == kDropSlotUnknown) {
      rejected.push_back(
          QString("%1 -> rejected (cannot infer destination: expected AP/BL/CP/CSC/USERDATA token or .pit)")
              .arg(leaf));
      continue;
    }

    if (usedSlots.contains(slotId)) {
      rejected.push_back(QString("%1 -> rejected (%2 already assigned in this drop)").arg(leaf, dropSlotName_(slotId)));
      continue;
    }

    QString reason;
    bool replaced = false;
    if (!assignDroppedFileToSlot_(slotId, p, &reason, &replaced)) {
      rejected.push_back(QString("%1 -> rejected (%2)").arg(leaf, reason));
      continue;
    }

    usedSlots.insert(slotId);
    accepted.push_back(QString("%1 -> %2%3")
                           .arg(leaf, dropSlotName_(slotId), replaced ? " (replaced existing file)" : ""));
  }

  updateActionButtons_();

#ifndef NDEBUG
  for (const auto& msg : accepted)
    appendLogLine_(QString("<font color=\"#7ecf7e\">DnD route: %1</font>").arg(htmlEsc(msg)));
  for (const auto& msg : rejected)
    appendLogLine_(QString("<font color=\"#ff7777\">DnD reject: %1</font>").arg(htmlEsc(msg)));
#endif

  if (accepted.isEmpty() && !rejected.isEmpty()) {
    showBlocked_("Drop rejected", rejected.join("\n"));
  }
}

void BrokkrWrapper::dragEnterEvent(QDragEnterEvent* e) {
  if (busy_) {
    e->ignore();
    return;
  }
  const auto files = extractLocalDropFiles_(e->mimeData());
  if (files.isEmpty()) {
    e->ignore();
    return;
  }
  e->acceptProposedAction();
}

void BrokkrWrapper::dragMoveEvent(QDragMoveEvent* e) {
  if (busy_) {
    e->ignore();
    return;
  }
  const auto files = extractLocalDropFiles_(e->mimeData());
  if (files.isEmpty()) {
    e->ignore();
    return;
  }
  e->acceptProposedAction();
}

void BrokkrWrapper::dropEvent(QDropEvent* e) {
  if (busy_) {
    e->ignore();
    return;
  }
  const auto files = extractLocalDropFiles_(e->mimeData());
  if (files.isEmpty()) {
#ifndef NDEBUG
    appendLogLine_(
        "<font color=\"#ff7777\">DnD reject: unsupported payload (drop local files from Explorer)</font>");
#endif
    e->ignore();
    return;
  }
  handleDroppedFiles_(files, kDropSlotAny);
  e->acceptProposedAction();
}

bool BrokkrWrapper::eventFilter(QObject* watched, QEvent* event) {
  if (!watched || !event) return QWidget::eventFilter(watched, event);

  auto* ww = qobject_cast<QWidget*>(watched);
  if (!ww) return QWidget::eventFilter(watched, event);
  if (ww != this && !isAncestorOf(ww)) return QWidget::eventFilter(watched, event);

  int forcedSlotId = kDropSlotAny;
  const QVariant slotVar = ww->property(kDropSlotProperty);
  if (slotVar.isValid()) forcedSlotId = slotVar.toInt();

  switch (event->type()) {
    case QEvent::DragEnter: {
      auto* de = static_cast<QDragEnterEvent*>(event);
      if (!busy_ && !extractLocalDropFiles_(de->mimeData()).isEmpty())
        de->acceptProposedAction();
      else
        de->ignore();
      return true;
    }
    case QEvent::DragMove: {
      auto* de = static_cast<QDragMoveEvent*>(event);
      if (!busy_ && !extractLocalDropFiles_(de->mimeData()).isEmpty())
        de->acceptProposedAction();
      else
        de->ignore();
      return true;
    }
    case QEvent::Drop: {
      auto* de = static_cast<QDropEvent*>(event);
      if (busy_) {
        de->ignore();
        return true;
      }
      const auto files = extractLocalDropFiles_(de->mimeData());
      if (files.isEmpty()) {
#ifndef NDEBUG
        appendLogLine_(
            "<font color=\"#ff7777\">DnD reject: unsupported payload (drop local files from Explorer)</font>");
#endif
        de->ignore();
        return true;
      }
      handleDroppedFiles_(files, forcedSlotId);
      de->acceptProposedAction();
      return true;
    }
    default: break;
  }

  return QWidget::eventFilter(watched, event);
}

void BrokkrWrapper::childEvent(QChildEvent* e) {
  QWidget::childEvent(e);
  if (!e || !e->added()) return;

  auto* w = qobject_cast<QWidget*>(e->child());
  if (!w || w == this) return;

  if (!w->property(kDropSlotProperty).isValid()) bindDropTarget_(w, kDropSlotAny);

  for (auto* sub : w->findChildren<QWidget*>()) {
    if (!sub || sub == this) continue;
    if (sub->property(kDropSlotProperty).isValid()) continue;
    bindDropTarget_(sub, kDropSlotAny);
  }
}

void BrokkrWrapper::changeEvent(QEvent* e) {
  QWidget::changeEvent(e);
  if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange) {
    applyHeaderStyle_();

    {
      auto pal = palette();
      QColor bg = pal.color(QPalette::Window).darker(110);
      pal.setColor(QPalette::Window, bg);
      idComGroup_->setPalette(pal);
    }

    consoleOutput->setPalette(palette());
    refreshDeviceBoxes_();
  }
}

#if defined(Q_OS_WIN)
bool BrokkrWrapper::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
  (void)eventType;
  MSG* msg = reinterpret_cast<MSG*>(message);
  if (!msg) return false;

  if (msg->message == WM_DROPFILES) {
    HDROP hdrop = reinterpret_cast<HDROP>(msg->wParam);
    int forcedSlotId = kDropSlotAny;

    POINT pt{};
    if (DragQueryPoint(hdrop, &pt)) {
      QWidget* hit = childAt(QPoint(pt.x, pt.y));
      while (hit) {
        const QVariant v = hit->property(kDropSlotProperty);
        if (v.isValid()) {
          forcedSlotId = v.toInt();
          break;
        }
        if (hit == this) break;
        hit = hit->parentWidget();
      }
    }

    const auto files = parse_hdrop_files(hdrop);
    DragFinish(hdrop);

    if (!files.isEmpty()) {
      handleDroppedFiles_(files, forcedSlotId);
    } else {
#ifndef NDEBUG
      appendLogLine_(
          "<font color=\"#ff7777\">DnD reject: WM_DROPFILES payload did not contain local files</font>");
#endif
    }

    if (result) *result = 0;
    return true;
  }

  if (msg->message == WM_DEVICECHANGE) {
    if (msg->wParam == DBT_DEVICEARRIVAL || msg->wParam == DBT_DEVICEREMOVECOMPLETE) requestUsbRefresh_();
  }

  if (result) *result = 0;
  return false;
}
#endif

void BrokkrWrapper::requestUsbRefresh_() noexcept {
  spdlog::debug("USB refresh requested");
  usbDirty_.store(true, std::memory_order_relaxed);
}

void BrokkrWrapper::refreshConnectedDevices_() {
  QStringList shown;
  QStringList physicalUsb;

  for (const auto& d : enumerate_samsung_targets()) physicalUsb << QString::fromStdString(d.sysname);

  bool physicalWireless = false;
  QString physicalWirelessId;
  {
    std::lock_guard lk(wireless_mtx_);
    const bool wantWireless = (chkWireless && chkWireless->isChecked());
    if (wantWireless) {
      if (wireless_conn_ && wireless_conn_->connected() && !wireless_sysname_.isEmpty()) {
        physicalWireless = true;
        physicalWirelessId = wireless_sysname_;
      } else {
        wireless_conn_.reset();
        wireless_sysname_.clear();
      }
    }
  }

  const int physicalCount = physicalUsb.size() + (physicalWireless ? 1 : 0);
  logDevCount_.store(physicalCount, std::memory_order_relaxed);

  const QString tgt = editTarget ? editTarget->text().trimmed() : QString{};
  if (!tgt.isEmpty()) {
    auto info = brokkr::platform::find_by_sysname(tgt.toStdString());
    if (info) shown << QString::fromStdString(info->sysname);
  } else {
    shown = physicalUsb;
  }

  if (physicalWireless) shown.prepend(physicalWirelessId);

  if (busy_) return;

  {
    const QSet<QString> prev = QSet<QString>(physicalUsbPrev_.begin(), physicalUsbPrev_.end());
    const QSet<QString> now = QSet<QString>(physicalUsb.begin(), physicalUsb.end());

    bool anyAttached = false;

    for (const auto& s : now) {
      if (!prev.contains(s)) {
        spdlog::info("Connected: {}", s.toStdString());
        anyAttached = true;
      }
    }
    for (const auto& s : prev)
      if (!now.contains(s)) spdlog::info("Disconnected: {}", s.toStdString());

    if (physicalWirelessPrev_ && (!physicalWireless || physicalWirelessIdPrev_ != physicalWirelessId)) {
      if (!physicalWirelessIdPrev_.isEmpty()) spdlog::info("Disconnected: {}", physicalWirelessIdPrev_.toStdString());
    }
    if (physicalWireless && (!physicalWirelessPrev_ || physicalWirelessIdPrev_ != physicalWirelessId)) {
      if (!physicalWirelessId.isEmpty()) {
        spdlog::info("Connected: {}", physicalWirelessId.toStdString());
        anyAttached = true;
      }
    }

    physicalUsbPrev_ = physicalUsb;
    physicalWirelessPrev_ = physicalWireless;
    physicalWirelessIdPrev_ = physicalWirelessId;
  }

  const QStringList prevShown = connectedDevices_;
  connectedDevices_ = shown;

  auto clearRowState = [this](int i) {
    if (i < 0 || i >= devSquares_.size()) return;
    if (static_cast<std::size_t>(i) < slotFailed_.size()) slotFailed_[static_cast<std::size_t>(i)] = 0;
    if (static_cast<std::size_t>(i) < slotActive_.size()) slotActive_[static_cast<std::size_t>(i)] = 0;

    if (auto* sq = devSquares_[i]) {
      sq->setVariant(DeviceSquare::Variant::Green);
      sq->setText("");
      sq->setFill(0.0);
    }
  };

  const int rows = devSquares_.size();
  for (int i = 0; i < rows; ++i) {
    const QString prevSys = (i < prevShown.size()) ? prevShown[i].trimmed() : QString{};
    const QString nowSys = (i < connectedDevices_.size()) ? connectedDevices_[i].trimmed() : QString{};
    if (!nowSys.isEmpty() && prevSys != nowSys) clearRowState(i);
  }

  refreshDeviceBoxes_();
  updateHeaderLeds_();
  updateActionButtons_();
}

void BrokkrWrapper::startWirelessListener_() {
  if (busy_) return;

  stopWirelessListener_();

  wireless_sysname_.clear();
  {
    std::lock_guard lk(wireless_mtx_);
    wireless_conn_.reset();
    wireless_listener_.emplace();
    auto st = wireless_listener_->bind_and_listen("0.0.0.0", 13579);
    if (!st) {
      spdlog::error("Wireless listen failed: {}", st.error());
      wireless_listener_.reset();
      requestUsbRefresh_();
      return;
    }
  }

  wireless_thread_ = std::jthread([this](std::stop_token st) {
    for (;;) {
      if (st.stop_requested()) return;

      {
        bool connected = false;
        {
          std::lock_guard lk(wireless_mtx_);
          if (wireless_conn_) {
            if (wireless_conn_->connected())
              connected = true;
            else {
              wireless_conn_.reset();
              wireless_sysname_.clear();
              QMetaObject::invokeMethod(this, [this]() { requestUsbRefresh_(); }, Qt::QueuedConnection);
            }
          }
        }
        if (connected) {
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          continue;
        }
      }

      brokkr::platform::TcpListener* lst = nullptr;
      {
        std::lock_guard lk(wireless_mtx_);
        if (!wireless_listener_) return;
        lst = &*wireless_listener_;
      }

      auto ar = lst->accept_one();
      if (!ar) {
        if (st.stop_requested()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }

      const QString peer = QString::fromStdString(ar->peer_label());
      const int sep = peer.lastIndexOf(':');
      const QString sys = (sep > 0) ? peer.left(sep) : peer;

      {
        std::lock_guard lk(wireless_mtx_);
        wireless_conn_.emplace(std::move(*ar));
        wireless_sysname_ = sys;
      }

      QMetaObject::invokeMethod(this, [this]() { refreshConnectedDevices_(); }, Qt::QueuedConnection);
    }
  });

  refreshConnectedDevices_();
}

void BrokkrWrapper::stopWirelessListener_() {
  brokkr::platform::TcpListener* lst = nullptr;
  {
    std::lock_guard lk(wireless_mtx_);
    if (wireless_listener_) lst = &*wireless_listener_;
  }
  if (lst) lst->close();

  if (wireless_thread_.joinable()) {
    wireless_thread_.request_stop();
    wireless_thread_.join();
  }

  {
    std::lock_guard lk(wireless_mtx_);
    wireless_conn_.reset();
    wireless_listener_.reset();
    wireless_sysname_.clear();
  }
  requestUsbRefresh_();
}

void BrokkrWrapper::appendLogLine_(const QString& html) {
  consoleOutput->append(html);
  QTextCursor cursor = consoleOutput->textCursor();
  cursor.movePosition(QTextCursor::End);
  consoleOutput->setTextCursor(cursor);
}

void BrokkrWrapper::applyHeaderStyle_() {
  if (!headerWidget_) return;
  const int luma = palette().color(QPalette::Window).lightness();
  const bool dark = (luma < 128);

  headerWidget_->setStyleSheet(""); // Clear stylesheet to fallback natively

  QPalette pal = headerWidget_->palette();
  QColor bgColor = dark ? QColor("#2d2e30") : QColor("#dce0e4");
  pal.setColor(QPalette::Window, bgColor);

  headerWidget_->setAutoFillBackground(true);
  headerWidget_->setPalette(pal);
}

void BrokkrWrapper::updateHeaderLeds_() {
  if (!ledContainer_) return;

  auto* lo = ledContainer_->layout();
  if (!lo) return;

  while (lo->count() > 0) {
    QLayoutItem* it = lo->takeAt(0);
    if (!it) break;
    if (auto* w = it->widget()) delete w;
    delete it;
  }

  const int count = std::min<int>(connectedDevices_.size(), 12);
  for (int i = 0; i < count; ++i) {
    auto* led = new QWidget(ledContainer_);
    led->setFixedSize(14, 14);

    // NOTE: This single style sheet is maintained because there is no native Mac
    // widget for a "glowing orb". Styling a pure QWidget does not break Mac theming.
    led->setStyleSheet(
        "background: qradialgradient(cx:0.3, cy:0.3, radius:0.7, fx:0.3, fy:0.3,"
        "  stop:0 #aaddff, stop:1 #0066cc);"
        "border: 1px solid #000;"
        "border-radius: 7px;");
    auto* glow = new QGraphicsDropShadowEffect(led);
    glow->setColor(QColor(0, 153, 255, 200));
    glow->setBlurRadius(10);
    glow->setOffset(0, 0);
    led->setGraphicsEffect(glow);
    lo->addWidget(led);
  }
}

void BrokkrWrapper::setControlsEnabled_(bool enabled) {
  if (tabWidget_) {
    tabWidget_->setEnabled(true);
    if (tabWidget_->count() > 0) tabWidget_->setTabEnabled(0, true);
    for (int i = 1; i < tabWidget_->count(); ++i) tabWidget_->setTabEnabled(i, enabled);
    if (!enabled) tabWidget_->setCurrentIndex(0);
  }
  if (consoleOutput) consoleOutput->setEnabled(true);

  if (editTarget) editTarget->setEnabled(enabled);
  if (cmbRebootAction) cmbRebootAction->setEnabled(enabled);

  if (chkUsePit) chkUsePit->setEnabled(enabled);
  if (editPit) editPit->setEnabled(enabled && chkUsePit && chkUsePit->isChecked());
  if (btnPitBrowse) btnPitBrowse->setEnabled(enabled && chkUsePit && chkUsePit->isChecked());

  if (chkWireless) chkWireless->setEnabled(enabled);

  const bool wireless = (chkWireless && chkWireless->isChecked());

  if (btnManyDevices_) btnManyDevices_->setEnabled(enabled && !wireless);

  for (auto* chk : fileChecks_)
    (void)chk;
  for (int i = 0; i < fileChecks_.size(); ++i) {
    auto* chk = fileChecks_[i];
    auto* edit = (i < fileLineEdits_.size()) ? fileLineEdits_[i] : nullptr;
    if (!chk) continue;
    const bool has_file = (edit && !edit->text().trimmed().isEmpty());
    chk->setEnabled(enabled && has_file);
  }
  for (auto* btn : fileButtons_)
    if (btn) btn->setEnabled(enabled);

  if (btnReset_) btnReset_->setEnabled(enabled);

  updateRebootDownloadButton_();
}

void BrokkrWrapper::setBusy_(bool busy) {
  busy_ = busy;
  if (busy_ && tabWidget_) tabWidget_->setCurrentIndex(0);
  setControlsEnabled_(!busy_);
  updateActionButtons_();
}

void BrokkrWrapper::setSquaresProgress_(double frac, bool animate) {
  const int n = devSquares_.size();
  for (int i = 0; i < n; ++i) {
    auto* sq = devSquares_[i];
    if (!sq) continue;
    if (static_cast<std::size_t>(i) < slotActive_.size() && !slotActive_[static_cast<std::size_t>(i)]) continue;
    if (static_cast<std::size_t>(i) < slotFailed_.size() && slotFailed_[static_cast<std::size_t>(i)]) continue;
    if (animate)
      sq->setFillAnimated(frac, 120);
    else
      sq->setFill(frac);
  }
}

void BrokkrWrapper::setSquaresText_(const QString& s) {
  const int n = devSquares_.size();
  for (int i = 0; i < n; ++i) {
    auto* sq = devSquares_[i];
    if (!sq) continue;
    if (static_cast<std::size_t>(i) < slotActive_.size() && !slotActive_[static_cast<std::size_t>(i)]) continue;
    if (static_cast<std::size_t>(i) < slotFailed_.size() && slotFailed_[static_cast<std::size_t>(i)]) continue;
    sq->setText(s);
  }
}

void BrokkrWrapper::setSquaresActiveColor_(bool enhanced) {
  enhanced_speed_ = enhanced;
  const int n = devSquares_.size();
  for (int i = 0; i < n; ++i) {
    auto* sq = devSquares_[i];
    if (!sq) continue;
    if (static_cast<std::size_t>(i) < slotActive_.size() && !slotActive_[static_cast<std::size_t>(i)]) continue;
    if (static_cast<std::size_t>(i) < slotFailed_.size() && slotFailed_[static_cast<std::size_t>(i)]) continue;
    sq->setVariant(enhanced ? DeviceSquare::Variant::Blue : DeviceSquare::Variant::Green);
  }
}

void BrokkrWrapper::setSquaresFinal_(bool ok) {
  if (!ok) {
    const int n = devSquares_.size();
    for (int i = 0; i < n; ++i) {
      auto* sq = devSquares_[i];
      if (!sq) continue;
      if (static_cast<std::size_t>(i) < slotActive_.size() && !slotActive_[static_cast<std::size_t>(i)]) continue;
      sq->setVariant(DeviceSquare::Variant::Red);
      sq->setText("FAIL!");
      sq->setFillAnimated(1.0, 250);
    }
    return;
  }

  const auto passV = (enhanced_speed_ ? DeviceSquare::Variant::Green : DeviceSquare::Variant::Blue);
  const int n = devSquares_.size();
  for (int i = 0; i < n; ++i) {
    auto* sq = devSquares_[i];
    if (!sq) continue;
    if (static_cast<std::size_t>(i) < slotActive_.size() && !slotActive_[static_cast<std::size_t>(i)]) continue;
    const bool failed = (static_cast<std::size_t>(i) < slotFailed_.size() && slotFailed_[static_cast<std::size_t>(i)]);
    if (failed) {
      sq->setVariant(DeviceSquare::Variant::Red);
      sq->setText("FAIL!");
      sq->setFillAnimated(1.0, 200);
    } else {
      sq->setVariant(passV);
      sq->setText("PASS");
      sq->setFillAnimated(1.0, 350);
    }
  }
}

void BrokkrWrapper::applyWindowHeightToContents_() {
  if (!layout()) return;
  layout()->invalidate();
  layout()->activate();
  const int want = layout()->sizeHint().height() + 30;
  if (height() < want) resize(width(), want);
}

void BrokkrWrapper::rebuildDeviceBoxes_(int boxCount, bool singleRow) {
  while (idComLayout_->count() > 0) {
    QLayoutItem* it = idComLayout_->takeAt(0);
    if (!it) break;
    if (auto* w = it->widget()) delete w;
    delete it;
  }

  for (int c = 0; c < kMassDlMaxBoxes + 2; ++c) idComLayout_->setColumnStretch(c, 0);
  for (int r = 0; r < (kMassDlMaxBoxes / std::max(1, kBoxesColsMany)) + 2; ++r) idComLayout_->setRowStretch(r, 0);

  comBoxes.clear();
  devSquares_.clear();

  const bool singleCellNoStretch = (singleRow && boxCount == 1);

  int fixedCellW = -1;
  if (singleCellNoStretch) {
    const int cols = kBoxesNormal;
    int sp = idComLayout_->horizontalSpacing();
    if (sp < 0) sp = idComLayout_->spacing();
    const auto m = idComLayout_->contentsMargins();
    const int avail = std::max(0, idComGroup_->contentsRect().width() - m.left() - m.right());
    const int totalSp = std::max(0, (cols - 1) * std::max(0, sp));
    fixedCellW = (cols > 0) ? ((avail - totalSp) / cols) : -1;
    if (fixedCellW < 30) fixedCellW = 30;
  }

  auto make_cell = [&](int row, int col) {
    auto* cell = new QWidget(idComGroup_);
    auto* v = new QVBoxLayout(cell);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);

    auto* sq = new DeviceSquare(cell);

    auto* box = new QLabel(cell);
    box->setAlignment(Qt::AlignCenter);
    box->setMinimumHeight(22);
    box->setAutoFillBackground(true);
    box->setBackgroundRole(QPalette::Base);
    box->setForegroundRole(QPalette::Text);
    box->setFrameShape(QFrame::StyledPanel);
    box->setFrameShadow(QFrame::Sunken);
    box->setMargin(1);

    // Use QFont instead of CSS for text size
    QFont boxFont = box->font();
    boxFont.setPointSize(9);
    box->setFont(boxFont);

    if (singleCellNoStretch && fixedCellW > 0) {
      cell->setFixedWidth(fixedCellW);
      box->setFixedWidth(fixedCellW);
      sq->setMinimumWidth(fixedCellW);
      sq->setMaximumWidth(fixedCellW);
    }

    v->addWidget(sq);
    v->addWidget(box);

    idComLayout_->addWidget(cell, row, col);

    devSquares_.append(sq);
    comBoxes.append(box);
  };

  if (singleRow) {
    if (boxCount > 0) make_cell(0, 0);

    if (singleCellNoStretch) {
      idComLayout_->setColumnStretch(0, 0);
      idComLayout_->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum), 0, 1);
      idComLayout_->setColumnStretch(1, 1);
    } else {
      for (int col = 1; col < boxCount; ++col) make_cell(0, col);
      for (int col = 0; col < boxCount; ++col) idComLayout_->setColumnStretch(col, 1);
    }

    slotFailed_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
    slotActive_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
    return;
  }

  for (int c = 0; c < kBoxesColsMany; ++c) idComLayout_->setColumnStretch(c, 1);

  for (int i = 0; i < boxCount; ++i) {
    const int row = i / kBoxesColsMany;
    const int col = i % kBoxesColsMany;
    make_cell(row, col);
  }

  slotFailed_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
  slotActive_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
}

void BrokkrWrapper::refreshDeviceBoxes_() {
  overflowDevices_ = false;

  for (auto* box : comBoxes) {
    box->clear();
    box->setToolTip(QString());
    box->setStyleSheet(""); // Clear any leftover inline styling

    QPalette pal = box->palette();
    pal.setColor(QPalette::Base, palette().color(QPalette::Base));
    pal.setColor(QPalette::Text, palette().color(QPalette::Text));
    box->setPalette(pal);

    QFont f = box->font();
    f.setBold(false);
    box->setFont(f);
  }

  auto elideFor = [&](QLabel* box, const QString& s) {
    const int w = std::max(0, box->width() - 10);
    return box->fontMetrics().elidedText(s, Qt::ElideMiddle, w);
  };

  const int shown = std::min<int>(connectedDevices_.size(), comBoxes.size());
  for (int i = 0; i < shown; ++i) {
    const QString sysname = connectedDevices_[i].trimmed();
    const QString raw = QString("%1:[%2]").arg(i).arg(sysname);
    comBoxes[i]->setText(elideFor(comBoxes[i], raw));
    comBoxes[i]->setToolTip(sysname);

    bool odin_mode = false;
    if (chkWireless && chkWireless->isChecked() && !wireless_sysname_.isEmpty() && sysname == wireless_sysname_) {
      odin_mode = true;
    } else if (auto info = select_samsung_target(sysname)) {
      odin_mode = is_odin_product(info->product);
    }

    QPalette pal = comBoxes[i]->palette();
    if (odin_mode) {
      pal.setColor(QPalette::Base, QColor("#4080c0")); // Odin mode: blue tint
      pal.setColor(QPalette::Text, Qt::white);
    } else {
      pal.setColor(QPalette::Base, QColor("#d9822b")); // Non-Odin Samsung device: orange tint
      pal.setColor(QPalette::Text, Qt::white);
    }
    comBoxes[i]->setPalette(pal);

    QFont f = comBoxes[i]->font();
    f.setBold(true);
    comBoxes[i]->setFont(f);
  }

  if (connectedDevices_.size() > comBoxes.size() && !comBoxes.isEmpty()) {
    overflowDevices_ = true;
    const int extra = connectedDevices_.size() - comBoxes.size();
    auto* last = comBoxes.back();
    const QString raw = QString("... +%1 more").arg(extra);
    last->setText(elideFor(last, raw));

    QPalette pal = last->palette();
    QColor base = palette().color(QPalette::Base);
    QColor warn(255, 183, 77);
    warn.setAlpha(50);
    const int r2 = (base.red() * (255 - warn.alpha()) + warn.red() * warn.alpha()) / 255;
    const int g2 = (base.green() * (255 - warn.alpha()) + warn.green() * warn.alpha()) / 255;
    const int b2 = (base.blue() * (255 - warn.alpha()) + warn.blue() * warn.alpha()) / 255;

    pal.setColor(QPalette::Base, QColor(r2, g2, b2));
    pal.setColor(QPalette::Text, palette().color(QPalette::Text));
    last->setPalette(pal);

    QFont f = last->font();
    f.setBold(true);
    last->setFont(f);
  }
}

bool BrokkrWrapper::canRunStart_(QString* whyNot) const {
  const bool wireless = chkWireless->isChecked();
  const bool hasTarget = !editTarget->text().trimmed().isEmpty();

  auto selected = [&](int idx, const QLineEdit* e) {
    const bool checked = (idx < fileChecks_.size() && fileChecks_[idx] && fileChecks_[idx]->isChecked());
    return checked && e && !e->text().trimmed().isEmpty();
  };

  const bool hasAnyFile = selected(0, editBL) || selected(1, editAP) || selected(2, editCP) || selected(3, editCSC) ||
                          selected(4, editUserData) ||
                          (chkUsePit->isChecked() && !editPit->text().isEmpty());

  if (!hasAnyFile) {
    if (whyNot) *whyNot = "No files selected.";
    return false;
  }

  if (wireless && hasTarget) {
    if (whyNot) *whyNot = "Wireless cannot be used together with Target Sysname.";
    return false;
  }

  if (wireless) {
    std::lock_guard lk(wireless_mtx_);
    if (!wireless_conn_ || !wireless_conn_->connected()) {
      if (whyNot) *whyNot = "Wireless is enabled but no device is connected yet.";
      return false;
    }
    return true;
  }

  if (hasTarget) return true;

  if (connectedDevices_.isEmpty()) {
    if (whyNot) *whyNot = "No connected devices detected.";
    return false;
  }
  if (overflowDevices_) {
    if (whyNot) *whyNot = "Too many devices are connected for the current GUI box limit.";
    return false;
  }
  return true;
}

bool BrokkrWrapper::confirmOdinModeDevicesForStart_() {
  if (chkWireless && chkWireless->isChecked()) return true;

  auto show_odin_popup = [this](const QString& text, bool allowIgnore) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Brokkr Flash");
    box.setText(text);

    auto* ok_btn = box.addButton(QMessageBox::Ok);
  #if defined(BROKKR_PLATFORM_WINDOWS)
    auto* reboot_btn = box.addButton("Try to Reboot them into Download Mode", QMessageBox::ActionRole);
  #endif
    QAbstractButton* ignore_btn = nullptr;
    if (allowIgnore) ignore_btn = box.addButton("Ignore them", QMessageBox::AcceptRole);

    box.setDefaultButton(qobject_cast<QPushButton*>(ok_btn));
    box.exec();

#if defined(BROKKR_PLATFORM_WINDOWS)
    if (box.clickedButton() == reboot_btn) {
      tryRebootIntoDownloadMode_();
      return false;
    }
#endif
    return allowIgnore && ignore_btn && box.clickedButton() == ignore_btn;
  };

  const QString tgt = editTarget ? editTarget->text().trimmed() : QString{};
  if (!tgt.isEmpty()) {
    if (auto one = select_samsung_target(tgt); one && !is_odin_product(one->product)) {
      return show_odin_popup("The device is not in Odin Mode, reboot to download mode first.", false);
    }
  }

  const auto all_samsung = enumerate_samsung_targets();
  if (all_samsung.empty()) return true;

  QStringList not_odin;
  int odin_count = 0;
  for (const auto& d : all_samsung) {
    if (is_odin_product(d.product)) {
      ++odin_count;
    } else {
      not_odin << QString::fromStdString(d.sysname);
    }
  }

  if (not_odin.isEmpty()) return true;

  const int total = static_cast<int>(all_samsung.size());

  if (total == 1) {
    return show_odin_popup("The device is not in Odin Mode, reboot to download mode first.", false);
  }

  if (odin_count == 0) {
    return show_odin_popup("None of the devices are in Odin Mode", false);
  }

  return show_odin_popup(QString("The following device(s) are not in Odin Mode:\n%1").arg(not_odin.join("\n")),
                         true);
}

void BrokkrWrapper::updateActionButtons_() {
  if (busy_) {
    btnRun->setEnabled(false);
    updateRebootDownloadButton_();
    return;
  }

  QString why;
  btnRun->setEnabled(canRunStart_(&why));
  updateRebootDownloadButton_();
}

void BrokkrWrapper::updateRebootDownloadButton_() {
  if (!btnRebootDownloadMode_) return;
#if !defined(BROKKR_PLATFORM_WINDOWS)
  btnRebootDownloadMode_->setEnabled(false);
  btnRebootDownloadMode_->hide();
  return;
#endif
  if (busy_) {
    btnRebootDownloadMode_->setEnabled(false);
    return;
  }

  bool hasNonOdin = false;
  for (const auto& sys : connectedDevices_) {
    if (auto one = select_samsung_target(sys); one && !is_odin_product(one->product)) {
      hasNonOdin = true;
      break;
    }
  }

  btnRebootDownloadMode_->setEnabled(hasNonOdin);
}

void BrokkrWrapper::showBlocked_(const QString& title, const QString& msg) const {
  QMessageBox::warning(const_cast<BrokkrWrapper*>(this), title, msg);
}

void BrokkrWrapper::tryRebootIntoDownloadMode_() {
  if (busy_) return;

#if !defined(BROKKR_PLATFORM_WINDOWS)
  QMessageBox::information(this, "Brokkr Flash", "This action is available only on Windows builds.");
  return;
#else

  int nonOdinCount = 0;
  for (const auto& sys : connectedDevices_) {
    if (auto one = select_samsung_target(sys); one && !is_odin_product(one->product)) {
      ++nonOdinCount;
    }
  }

  if (nonOdinCount <= 0) {
    QMessageBox::information(this, "Brokkr Flash", "No connected device needs reboot into Download Mode.");
    return;
  }

  const auto r = brokkr::platform::send_suddlmod_to_samsung_serial(SAMSUNG_VID, nonOdinCount);
  if (r.ports_seen == 0) {
    QMessageBox::warning(this, "Brokkr Flash", "No Samsung serial port found.");
    return;
  }

  if (r.sent_ok == 0) {
    QString detail;
    for (const auto& f : r.failures) {
      if (!detail.isEmpty()) detail += "\n";
      detail += QString::fromStdString(f);
    }
    QMessageBox::warning(this, "Brokkr Flash",
                         detail.isEmpty() ? "Failed to send reboot command to Samsung serial ports."
                                          : QString("Failed to send reboot command to Samsung serial ports.\n%1")
                                                .arg(detail));
    return;
  }

  if (r.failures.empty()) {
    QMessageBox::information(this, "Brokkr Flash",
                             QString("Reboot command sent to %1 Samsung serial port(s).")
                                 .arg(r.sent_ok));
  } else {
    QString detail;
    for (const auto& f : r.failures) {
      if (!detail.isEmpty()) detail += "\n";
      detail += QString::fromStdString(f);
    }
    QMessageBox::warning(this, "Brokkr Flash",
                         QString("Reboot command sent to %1 port(s), but %2 failed.\n%3")
                             .arg(r.sent_ok)
                             .arg(static_cast<int>(r.failures.size()))
                             .arg(detail));
  }

  requestUsbRefresh_();
#endif
}

void BrokkrWrapper::setupOdinFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit) {
  auto* chk = new QCheckBox(this);
  chk->setEnabled(false);
  layout->addWidget(chk, row, 0);

  auto* btn = new QPushButton(label, this);
  btn->setMinimumWidth(95);
  btn->setFixedHeight(28);
  layout->addWidget(btn, row, 1);

  lineEdit = new QLineEdit(this);
  lineEdit->setReadOnly(true);
  layout->addWidget(lineEdit, row, 2);

  fileChecks_.append(chk);
  fileButtons_.append(btn);
  fileLineEdits_.append(lineEdit);

  bindDropTarget_(chk, row);
  bindDropTarget_(btn, row);
  bindDropTarget_(lineEdit, row);

  connect(btn, &QPushButton::clicked, this, [this, lineEdit, chk]() {
    if (busy_) return;
    QString file = QFileDialog::getOpenFileName(this, "Select Firmware File", lastDir,
                                                "Firmware Archives (*.tar *.md5);;All Files (*)");
    if (!file.isEmpty()) {
      lastDir = QFileInfo(file).absolutePath();
      lineEdit->setText(file);
      chk->setEnabled(true);
      chk->setChecked(true);
    }
    updateActionButtons_();
  });

  connect(chk, &QCheckBox::toggled, this, [this](bool) {
    if (busy_) return;
    updateActionButtons_();
  });

  connect(lineEdit, &QLineEdit::textChanged, this, [this, chk](const QString& txt) {
    if (busy_) return;
    const bool has_file = !txt.trimmed().isEmpty();
    chk->setEnabled(has_file);
    if (!has_file) chk->setChecked(false);
    updateActionButtons_();
  });
}

void BrokkrWrapper::onRunClicked() {
  if (busy_) return;
  QString why;
  if (!canRunStart_(&why)) {
    showBlocked_("Cannot start", why);
    return;
  }
  if (!confirmOdinModeDevicesForStart_()) return;
  startWorkStart_();
}

void BrokkrWrapper::startWorkStart_() {
  if (busy_) return;

  const QStringList uiDevicesSnapshot = connectedDevices_;
  setBusy_(true);

  plan_names_.clear();
  plan_from_names_.clear();
  enhanced_speed_ = false;

  slotFailed_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
  slotActive_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
  {
    const int activeCount = std::min<int>(uiDevicesSnapshot.size(), devSquares_.size());
    for (int i = 0; i < activeCount; ++i) {
      bool active = false;
      const QString sysname = uiDevicesSnapshot[i].trimmed();
      if (chkWireless && chkWireless->isChecked() && !wireless_sysname_.isEmpty() && sysname == wireless_sysname_) {
        active = true;
      } else if (auto info = select_odin_target(sysname)) {
        (void)info;
        active = true;
      }
      slotActive_[static_cast<std::size_t>(i)] = active ? 1 : 0;
    }
  }

  int progressSteps = 1;
  for (int i = 0; i < devSquares_.size(); ++i) {
    auto* sq = devSquares_[i];
    if (!sq) continue;
    if (static_cast<std::size_t>(i) < slotActive_.size() && !slotActive_[static_cast<std::size_t>(i)]) continue;
    progressSteps = std::max(progressSteps, std::max(sq->width(), sq->sizeHint().width()));
  }
  progressVisualSteps_.store(progressSteps, std::memory_order_relaxed);
  progressVisualBucket_.store(-1, std::memory_order_relaxed);

  setSquaresProgress_(0.0, false);
  setSquaresText_("");
  setSquaresActiveColor_(false);

  worker_ = std::jthread([this, uiDevicesSnapshot](std::stop_token) {
    auto done_ui = [&] {
      QMetaObject::invokeMethod(
          this,
          [this]() {
            setSquaresText_("PASS");
            setSquaresFinal_(true);
            setBusy_(false);
          },
          Qt::QueuedConnection);
    };

    auto fail_ui = [&](const QString& msg, bool showPopup = false) {
      QMetaObject::invokeMethod(
          this,
          [this, msg, showPopup]() {
            const int z = logDevCount_.load(std::memory_order_relaxed);
            appendLogLine_(QString("<font color=\"#ff5555\">&lt;%1&gt; FAIL! %2</font>").arg(z).arg(htmlEsc(msg)));
            setSquaresFinal_(false);
            setBusy_(false);
            if (showPopup) QMessageBox::warning(this, "Brokkr Flash", "The connected devices do not match!");
          },
          Qt::QueuedConnection);
    };

    const int actionIndex = cmbRebootAction->currentIndex();

    const QString tgt = editTarget->text().trimmed();
    const bool wireless = chkWireless->isChecked();
    if (wireless && !tgt.isEmpty()) {
      fail_ui("Wireless cannot be used together with Target Sysname.");
      return;
    }

    QMetaObject::invokeMethod(this, [this]() { setSquaresText_("HANDSHAKE"); }, Qt::QueuedConnection);

    brokkr::odin::Cfg cfg;
    cfg.reboot_after = (actionIndex == 0);

    brokkr::odin::Ui ui;
    std::atomic_bool sawPerDeviceFail{false};
    std::vector<int> targetToUiSlots;

    auto findUiIndexBySysname = [&](const QString& sysname) {
      const QString needle = sysname.trimmed();
      for (int i = 0; i < uiDevicesSnapshot.size(); ++i) {
        if (uiDevicesSnapshot[i].trimmed() == needle) return i;
      }
      return -1;
    };

    ui.on_plan = [&](const std::vector<brokkr::odin::PlanItem>& p, std::uint64_t) {
      std::vector<QString> parts, froms;
      parts.reserve(p.size());
      froms.reserve(p.size());
      for (const auto& it : p) {
        std::string part = it.part_name;
        if (part.empty() && it.kind == brokkr::odin::PlanItem::Kind::Pit) part = "PIT";
        std::string from = it.pit_file_name;
        if (from.empty()) from = part;
        parts.push_back(QString::fromStdString(part));
        froms.push_back(QString::fromStdString(from));
      }
      QMetaObject::invokeMethod(
          this,
          [this, parts = std::move(parts), froms = std::move(froms)]() mutable {
            plan_names_ = std::move(parts);
            plan_from_names_ = std::move(froms);
          },
          Qt::QueuedConnection);
    };

    ui.on_item_active = [&](std::size_t i) {
      QMetaObject::invokeMethod(
          this,
          [this, i]() {
            if (i >= plan_names_.size()) return;
            setSquaresText_(plan_names_[i]);
          },
          Qt::QueuedConnection);
    };

    ui.on_stage = [&](const std::string& s) {
      const QString qs = QString::fromStdString(s);
      QMetaObject::invokeMethod(
          this,
          [this, qs]() {
            if (qs.contains("Enhanced", Qt::CaseInsensitive))
              setSquaresActiveColor_(true);
            else if (qs.contains("Normal", Qt::CaseInsensitive))
              setSquaresActiveColor_(false);
            if (qs.contains("xxh3", Qt::CaseInsensitive)) setSquaresText_("XXH3");
            if (qs.contains("md5", Qt::CaseInsensitive)) setSquaresText_("MD5");
            if (qs.contains("handshake", Qt::CaseInsensitive)) setSquaresText_("HANDSHAKE");
            if (qs.contains("shutdown", Qt::CaseInsensitive) || qs.contains("reboot", Qt::CaseInsensitive) ||
                qs.contains("reset", Qt::CaseInsensitive) || qs.contains("finalizing", Qt::CaseInsensitive))
              setSquaresText_("RESET");
          },
          Qt::QueuedConnection);
    };

    ui.on_progress = [&](std::uint64_t d, std::uint64_t t, std::uint64_t, std::uint64_t) {
      const double frac = (t > 0) ? (static_cast<double>(d) / static_cast<double>(t)) : 0.0;
      const int steps = std::max(1, progressVisualSteps_.load(std::memory_order_relaxed));
      const int bucket = (t > 0)
                             ? static_cast<int>(std::clamp<std::uint64_t>((d * static_cast<std::uint64_t>(steps)) / t, 0,
                                                                         static_cast<std::uint64_t>(steps)))
                             : 0;

      int prev = progressVisualBucket_.load(std::memory_order_relaxed);
      while (prev != bucket &&
             !progressVisualBucket_.compare_exchange_weak(prev, bucket, std::memory_order_relaxed,
                                                          std::memory_order_relaxed)) {
      }
      if (prev == bucket) return;

      QMetaObject::invokeMethod(this, [this, frac]() { setSquaresProgress_(frac, true); }, Qt::QueuedConnection);
    };

    ui.on_error = [&](const std::string& s) {
      if (s.rfind("DEVFAIL idx=", 0) == 0) sawPerDeviceFail.store(true, std::memory_order_relaxed);
      const std::vector<int> mapSnapshot = targetToUiSlots;
      QMetaObject::invokeMethod(
          this,
          [this, qs = QString::fromStdString(s), mapSnapshot]() {
            const int z = logDevCount_.load(std::memory_order_relaxed);

            QString shown = qs;
            int idx = -1;

            const QString pref = "DEVFAIL idx=";
            if (qs.startsWith(pref)) {
              int p = pref.size();
              int sp = qs.indexOf(' ', p);
              if (sp < 0) sp = qs.size();
              bool okNum = false;
              idx = qs.mid(p, sp - p).toInt(&okNum);
              QString reason;
              if (sp < qs.size()) reason = qs.mid(sp + 1).trimmed();

              if (okNum && idx >= 0) {
                int uiIdx = -1;
                if (static_cast<std::size_t>(idx) < mapSnapshot.size()) uiIdx = mapSnapshot[static_cast<std::size_t>(idx)];

                if (uiIdx >= 0) {
                  if (static_cast<std::size_t>(uiIdx) >= slotFailed_.size())
                    slotFailed_.resize(static_cast<std::size_t>(uiIdx) + 1, 0);
                  slotFailed_[static_cast<std::size_t>(uiIdx)] = 1;
                }

                if (uiIdx >= 0 && uiIdx < devSquares_.size() && devSquares_[uiIdx]) {
                  devSquares_[uiIdx]->setVariant(DeviceSquare::Variant::Red);
                  devSquares_[uiIdx]->setText("FAIL!");
                  devSquares_[uiIdx]->setFill(1.0);
                }

                // Keep native but set red palette for failure
                if (uiIdx >= 0 && uiIdx < comBoxes.size() && comBoxes[uiIdx]) {
                  QPalette pal = comBoxes[uiIdx]->palette();
                  pal.setColor(QPalette::Base, QColor("#a02020"));
                  pal.setColor(QPalette::Text, Qt::white);
                  comBoxes[uiIdx]->setPalette(pal);
                }

                if (reason.isEmpty()) {
                  shown = (uiIdx >= 0) ? QString("FAIL! (Device %1)").arg(uiIdx) : QString("FAIL!");
                } else {
                  shown = (uiIdx >= 0) ? QString("FAIL! (Device %1) %2").arg(uiIdx).arg(reason)
                                       : QString("FAIL! %1").arg(reason);
                }
              }
            }

            appendLogLine_(QString("<font color=\"#ff5555\">&lt;%1&gt; %2</font>").arg(z).arg(htmlEsc(shown)));
          },
          Qt::QueuedConnection);
    };

    std::shared_ptr<const std::vector<std::byte>> pit_to_upload;
    if (chkUsePit->isChecked() && !editPit->text().isEmpty()) {
      const std::filesystem::path p = editPit->text().toStdString();
      std::error_code ec;
      const auto sz = std::filesystem::file_size(p, ec);
      if (ec) {
        fail_ui("Cannot stat PIT file.");
        return;
      }

      std::vector<std::byte> buf(static_cast<std::size_t>(sz));
      std::ifstream in(p, std::ios::binary);
      if (!in.is_open()) {
        fail_ui("Cannot open PIT file.");
        return;
      }
      if (!buf.empty()) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        if (!in.good()) {
          fail_ui("Failed to read PIT file.");
          return;
        }
      }
      pit_to_upload = std::make_shared<const std::vector<std::byte>>(std::move(buf));
    }

    std::vector<std::filesystem::path> inputs;
    auto push_if_selected = [&](int idx, QLineEdit* e) {
      if (!e) return;
      const bool checked = (idx < fileChecks_.size() && fileChecks_[idx] && fileChecks_[idx]->isChecked());
      if (checked && !e->text().trimmed().isEmpty()) inputs.emplace_back(e->text().toStdString());
    };

    push_if_selected(0, editBL);
    push_if_selected(1, editAP);
    push_if_selected(2, editCP);
    push_if_selected(3, editCSC);
    push_if_selected(4, editUserData);

    struct Provider {
      std::vector<std::unique_ptr<brokkr::odin::UsbTarget>> usb; // owns USB transports
      std::vector<brokkr::odin::Target> owned;                   // owns Targets
      std::vector<brokkr::odin::Target*> ptrs;                   // passed to engine
    };

    auto make_provider = [&]() -> std::optional<Provider> {
      Provider p;
      targetToUiSlots.clear();

      if (wireless) {
        brokkr::platform::TcpConnection* connp = nullptr;
        {
          std::lock_guard lk(wireless_mtx_);
          if (wireless_conn_) connp = &*wireless_conn_;
        }
        if (!connp || !connp->connected()) {
          fail_ui("No wireless device connected.");
          return std::nullopt;
        }

        p.owned.push_back(brokkr::odin::Target{.id = wireless_sysname_.toStdString(), .link = connp});
        p.ptrs.push_back(&p.owned.back());
        int uiIdx = findUiIndexBySysname(wireless_sysname_);
        if (uiIdx < 0 && !devSquares_.isEmpty()) uiIdx = 0;
        targetToUiSlots.push_back(uiIdx);
        return p;
      }

      std::vector<brokkr::platform::UsbDeviceSysfsInfo> targets;
      if (!tgt.isEmpty()) {
        auto one = select_odin_target(tgt);
        if (!one) {
          fail_ui("Target sysname not found or not supported.");
          return std::nullopt;
        }
        targets.push_back(*one);
        targetToUiSlots.push_back(findUiIndexBySysname(QString::fromStdString(one->sysname)));
      } else {
        for (int i = 0; i < uiDevicesSnapshot.size(); ++i) {
          const QString sys = uiDevicesSnapshot[i].trimmed();
          if (auto one = select_odin_target(sys)) {
            targets.push_back(*one);
            targetToUiSlots.push_back(i);
          }
        }
        if (targets.empty()) {
          targets = enumerate_odin_targets();
          targetToUiSlots.clear();
          for (const auto& td : targets) {
            targetToUiSlots.push_back(findUiIndexBySysname(QString::fromStdString(td.sysname)));
          }
        }
      }

      if (targets.empty()) {
        fail_ui("No supported devices found.");
        return std::nullopt;
      }

      p.usb.reserve(targets.size());
      p.owned.reserve(targets.size());
      p.ptrs.reserve(targets.size());

      for (const auto& td : targets) {
        auto ut = std::make_unique<brokkr::odin::UsbTarget>(td.devnode());

        auto st = ut->dev.open_and_init();
        if (!st) {
          fail_ui(QString::fromStdString(st.error()));
          return std::nullopt;
        }

        auto cst = ut->conn.open();
        if (!cst) {
          fail_ui(QString::fromStdString(cst.error()));
          return std::nullopt;
        }

        ut->conn.set_timeout_ms(cfg.preflash_timeout_ms);

        p.owned.push_back(brokkr::odin::Target{.id = ut->devnode, .link = &ut->conn});
        p.ptrs.push_back(&p.owned.back());
        p.usb.push_back(std::move(ut));
      }

      return p;
    };

    auto run_engine = [&](std::vector<brokkr::odin::ImageSpec> srcs) {
      auto prov = make_provider();
      if (!prov) return;

      auto st = brokkr::odin::flash(prov->ptrs, srcs, pit_to_upload, cfg, ui);
      if (!st) {
        if (sawPerDeviceFail.load(std::memory_order_relaxed)) {
          done_ui();
        } else {
          const QString err = QString::fromStdString(st.error());
          const bool mismatch =
              err.contains("mismatch across devices", Qt::CaseInsensitive) ||
              err.contains("differs across devices", Qt::CaseInsensitive);
          fail_ui(mismatch ? QString("The connected devices do not match!") : err, mismatch);
        }
        requestUsbRefresh_();
        return;
      }
      done_ui();
      requestUsbRefresh_();
    };

    if (inputs.empty()) {
      run_engine({});
      return;
    }

    auto jobsr = brokkr::app::md5_jobs(inputs);
    if (!jobsr) {
      fail_ui(QString::fromStdString(jobsr.error()));
      return;
    }

    std::uint64_t totalBytes = 0;
    for (const auto& p : inputs) {
      std::error_code ec;
      const auto sz = std::filesystem::file_size(p, ec);
      if (!ec) totalBytes += static_cast<std::uint64_t>(sz);
    }

    const QString verifyName = QString::fromStdString(std::string(brokkr::app::md5_verify_name(*jobsr)));

    spdlog::info("{} check ({}), Please wait.", verifyName.toStdString(), human_bytes(totalBytes).toStdString());
    QMetaObject::invokeMethod(
        this,
        [this, verifyName]() {
          setSquaresActiveColor_(false);
          setSquaresText_(verifyName);
          setSquaresProgress_(0.0, false);
        },
        Qt::QueuedConnection);

    auto vst = brokkr::app::md5_verify(*jobsr, ui);
    if (!vst) {
      fail_ui(QString::fromStdString(vst.error()));
      return;
    }

    auto specs = brokkr::odin::expand_inputs_tar_or_raw(inputs);
    if (!specs) {
      fail_ui(QString::fromStdString(specs.error()));
      return;
    }

    if (!pit_to_upload) {
      auto pit = pit_from_specs(*specs);
      if (pit) pit_to_upload = pit;
    }

    std::vector<brokkr::odin::ImageSpec> srcs;
    for (auto& s : *specs)
      if (!is_pit_name(s.basename)) srcs.push_back(std::move(s));
    if (srcs.empty() && !pit_to_upload) {
      fail_ui("No valid flashable files.");
      return;
    }

    progressVisualBucket_.store(-1, std::memory_order_relaxed);
    QMetaObject::invokeMethod(this, [this]() { setSquaresProgress_(0.0, false); }, Qt::QueuedConnection);

    run_engine(std::move(srcs));
  });
}
