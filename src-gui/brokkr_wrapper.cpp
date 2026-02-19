#include "brokkr_wrapper.hpp"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QSet>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStringList>
#include <QTabWidget>
#include <QTextCursor>
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
#include "protocol/odin/odin_cmd.hpp"
#include "protocol/odin/pit.hpp"
#include "protocol/odin/pit_transfer.hpp"

#if defined(BROKKR_PLATFORM_LINUX)
  #include <linux/netlink.h>
  #include <QSocketNotifier>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#if defined(Q_OS_WIN)
  #include <dbt.h>
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
        textCol_ = QColor("#000000");
        break;
      case Variant::Blue:
        top_ = QColor("#68b3e4");
        bot_ = QColor("#186ba6");
        textCol_ = QColor("#000033");
        break;
      case Variant::Red:
        top_ = QColor("#d95757");
        bot_ = QColor("#9a0a0a");
        textCol_ = QColor("#000000");
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
      p.setPen(textCol_);

      QFontMetrics fm(f);
      const int maxW = std::max(0, r.width() - 4);
      const QString shown = fm.elidedText(text_, Qt::ElideMiddle, maxW);
      p.drawText(r.adjusted(2, 0, -2, 0), Qt::AlignCenter, shown);
    }
  }

 private:
  double fill_ = 0.0;
  Variant var_ = Variant::Green;
  QColor top_;
  QColor bot_;
  QColor textCol_;
  QString text_;
  QVariantAnimation anim_{};
};

namespace {

static constexpr std::uint16_t SAMSUNG_VID = 0x04E8;
static constexpr std::uint16_t ODIN_PIDS[] = {0x6601, 0x685D, 0x68C3};

static std::vector<std::uint16_t> default_pids() { return {std::begin(ODIN_PIDS), std::end(ODIN_PIDS)}; }

static bool is_pit_name(std::string_view base) { return brokkr::core::ends_with_ci(base, ".pit"); }

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
  log->set_level(spdlog::level::info);
  return log;
}

static std::optional<brokkr::platform::UsbDeviceSysfsInfo> select_target(QString sysname_q) {
  const std::string sysname = sysname_q.trimmed().toStdString();
  if (sysname.empty()) return {};

  auto info = brokkr::platform::find_by_sysname(sysname);
  if (!info) return {};

  if (info->vendor != SAMSUNG_VID) return {};
  const auto pids = default_pids();
  if (std::ranges::find(pids, info->product) == pids.end()) return {};
  return info;
}

static std::vector<brokkr::platform::UsbDeviceSysfsInfo> enumerate_targets() {
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

static void print_pit_table_to_log(const brokkr::odin::pit::PitTable& t) {
  auto d = [&](const std::string& s) { return s.empty() ? "-" : s; };

  spdlog::info("PIT TABLE");
  spdlog::info("cpu_bl_id: {}", d(t.cpu_bl_id));
  spdlog::info("com_tar2:  {}", d(t.com_tar2));
  spdlog::info("lu_count:  {}", t.lu_count);
  spdlog::info("entries:   {}", t.partitions.size());
  spdlog::info(" ");

  for (std::size_t i = 0; i < t.partitions.size(); ++i) {
    const auto& p = t.partitions[i];
    spdlog::info("Partition #{}:", i);
    spdlog::info("id: {}", p.id);
    spdlog::info("dev_type: {}", p.dev_type);
    spdlog::info("block_count: {}", p.block_size);
    spdlog::info("block_size: {}", p.block_bytes);
    spdlog::info("file_size: {}", p.file_size);
    spdlog::info("name: {}", d(p.name));
    spdlog::info("file_name: {}", d(p.file_name));
    spdlog::info(" ");
  }
}

static brokkr::odin::OdinCommands::ShutdownMode shutdown_mode_from_ui(int idx) {
  if (idx == 1) return brokkr::odin::OdinCommands::ShutdownMode::ReDownload;
  if (idx == 2) return brokkr::odin::OdinCommands::ShutdownMode::NoReboot;
  return brokkr::odin::OdinCommands::ShutdownMode::Reboot;
}

static brokkr::core::Status print_pit_over_link(brokkr::core::IByteTransport& link, const brokkr::odin::Cfg& cfg,
                                                brokkr::odin::OdinCommands::ShutdownMode sm) noexcept {
  brokkr::odin::OdinCommands odin(link);
  link.set_timeout_ms(cfg.preflash_timeout_ms);

  BRK_TRY(odin.handshake(cfg.preflash_retries));
  BRK_TRYV(_, odin.get_version(cfg.preflash_retries));

  link.set_timeout_ms(cfg.flash_timeout_ms);

  BRK_TRYV(bytes, brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries));
  BRK_TRYV(tbl, brokkr::odin::pit::parse({bytes.data(), bytes.size()}));

  print_pit_table_to_log(tbl);

  return odin.shutdown(sm);
}

} // namespace

BrokkrWrapper::BrokkrWrapper(QWidget* parent) : QWidget(parent) {
  setWindowTitle("Brokkr Flash");
  setWindowIcon(QIcon(":/brokkr.ico"));
  resize(850, 600);
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

  auto* titleLabel = new QLabel(
      QString(
          R"(<span style="color:#4da6ff; font-size:22px; font-weight:bold;">BROKKR</span>)"
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
  consoleOutput->setStyleSheet("font-family: Consolas, monospace; font-size: 11px;");
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

  optLayout->addSpacing(10);
  optLayout->addWidget(new QLabel("Post-Action:", this));
  cmbRebootAction = new QComboBox(this);
  cmbRebootAction->addItem("Default (Reboot Normally)");
  cmbRebootAction->addItem("Redownload");
  cmbRebootAction->addItem("No Reboot");
  optLayout->addWidget(cmbRebootAction);

  chkAdvanced_ = new QCheckBox("Advanced options", this);
  optLayout->addWidget(chkAdvanced_);

  manyRowWidget_ = new QWidget(this);
  auto* manyRow = new QHBoxLayout(manyRowWidget_);
  manyRow->setContentsMargins(0, 0, 0, 0);
  lblDeviceBoxes = new QLabel("Count: 8", this);
  sldDeviceBoxes = new QSlider(Qt::Horizontal, this);
  sldDeviceBoxes->setRange(kBoxesNormal, kMassDlMaxBoxes);
  sldDeviceBoxes->setValue(kMassDlMaxBoxes);
  sldDeviceBoxes->setEnabled(false);
  manyRow->addWidget(lblDeviceBoxes);
  manyRow->addWidget(sldDeviceBoxes, 1);
  manyRowWidget_->setVisible(false);
  optLayout->addWidget(manyRowWidget_);

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

  tabWidget_->addTab(pitTab, "Pit");

  connect(chkAdvanced_, &QCheckBox::toggled, this, [this](bool on) {
    if (busy_) {
      chkAdvanced_->setChecked(!on);
      return;
    }
    if (btnPrintPit) btnPrintPit->setVisible(on);
    if (manyRowWidget_) manyRowWidget_->setVisible(on);
  });

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

    sldDeviceBoxes->setEnabled(checked);

    if (!checked) {
      lblDeviceBoxes->setText(QString("Count: %1").arg(kBoxesNormal));
      rebuildDeviceBoxes_(kBoxesNormal, true);
      layout()->invalidate();
      layout()->activate();
      setMinimumHeight(0);
      resize(width(), baseWindowHeight_);
    } else {
      const int v = std::min(sldDeviceBoxes->value(), kMassDlMaxBoxes);
      lblDeviceBoxes->setText(QString("Count: %1").arg(v));
      rebuildDeviceBoxes_(v, false);
      applyWindowHeightToContents_();
    }

    refreshDeviceBoxes_();
    updateActionButtons_();
  });

  connect(sldDeviceBoxes, &QSlider::valueChanged, this, [this](int v) {
    if (busy_) return;
    if (!btnManyDevices_ || !btnManyDevices_->isChecked()) return;
    if (chkWireless && chkWireless->isChecked()) return;

    v = std::min(v, kMassDlMaxBoxes);
    lblDeviceBoxes->setText(QString("Count: %1").arg(v));
    rebuildDeviceBoxes_(v, false);
    applyWindowHeightToContents_();
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
      if (sldDeviceBoxes) sldDeviceBoxes->setEnabled(false);
      if (lblDeviceBoxes) lblDeviceBoxes->setText("Count: 1");

      rebuildDeviceBoxes_(1, true);
      layout()->invalidate();
      layout()->activate();
      setMinimumHeight(0);
      resize(width(), baseWindowHeight_);

      startWirelessListener_();
    } else {
      stopWirelessListener_();

      if (btnManyDevices_) btnManyDevices_->setEnabled(true);
      if (sldDeviceBoxes) sldDeviceBoxes->setEnabled(false);
      if (lblDeviceBoxes) lblDeviceBoxes->setText(QString("Count: %1").arg(kBoxesNormal));

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
  tipsLabel->setStyleSheet(
      "color: gray; font-size: 11px;"
      "border: 1px solid palette(mid);"
      "border-radius: 4px;"
      "padding: 6px;");
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
  btnPrintPit = new QPushButton("Print PIT", this);

  const int bottomW = 135;
  const int bottomH = 32;
  btnRun->setMinimumSize(bottomW, bottomH);
  btnReset_->setMinimumSize(bottomW, bottomH);
  btnPrintPit->setMinimumSize(bottomW, bottomH);

  btnPrintPit->setVisible(false);

  auto* footerLabel = new QLabel(
      R"(<a href="https://github.com/Gabriel2392/brokkr-flash" style="color: #4c8ddc;">GitHub Repo</a>)",
      this);
  footerLabel->setOpenExternalLinks(true);
  footerLabel->setStyleSheet("font-size: 10px;");
  footerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  bottomLayout->addWidget(footerLabel, 0, Qt::AlignBottom);
  bottomLayout->addStretch();
  bottomLayout->addWidget(btnPrintPit, 0, Qt::AlignBottom);
  bottomLayout->addSpacing(10);

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

  connect(btnPrintPit, &QPushButton::clicked, this, [this]() {
    if (busy_) return;
    QString why;
    if (!canRunPrintPit_(&why)) {
      showBlocked_("Cannot print PIT", why);
      return;
    }
    startWorkPrintPit_();
  });

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

    setSquaresText_("");
    setSquaresProgress_(0.0, false);
    setSquaresActiveColor_(false);

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
          const int n = ::recv(uevent_fd_, buf, sizeof(buf) - 1, 0);
          if (n <= 0) return;
          buf[n] = '\0';
          const QString s = QString::fromLocal8Bit(buf, n);
          if (s.contains("SUBSYSTEM=usb") || s.contains("SUBSYSTEM=tty")) requestUsbRefresh_();
        });
      } else {
        ::close(uevent_fd_);
        uevent_fd_ = -1;
      }
    }
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
}

void BrokkrWrapper::appendLogLineFromEngine(const QString& html) { appendLogLine_(html); }

void BrokkrWrapper::closeEvent(QCloseEvent* e) {
  if (busy_) {
    QMessageBox::warning(this, "Brokkr Flasher", "An operation is in progress. Please wait for it to complete.");
    e->ignore();
    return;
  }
  e->accept();
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
  (void)result;
  MSG* msg = reinterpret_cast<MSG*>(message);
  if (!msg) return false;

  if (msg->message == WM_DEVICECHANGE) {
    if (msg->wParam == DBT_DEVICEARRIVAL || msg->wParam == DBT_DEVICEREMOVECOMPLETE) requestUsbRefresh_();
  }
  return false;
}
#endif

void BrokkrWrapper::requestUsbRefresh_() noexcept { usbDirty_.store(true, std::memory_order_relaxed); }

void BrokkrWrapper::refreshConnectedDevices_() {
  QStringList shown;
  QStringList physicalUsb;

  for (const auto& d : enumerate_targets()) physicalUsb << QString::fromStdString(d.sysname);

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

  {
    const QSet<QString> prev = QSet<QString>(physicalUsbPrev_.begin(), physicalUsbPrev_.end());
    const QSet<QString> now = QSet<QString>(physicalUsb.begin(), physicalUsb.end());

    for (const auto& s : now)
      if (!prev.contains(s)) spdlog::info("Connected: {}", s.toStdString());
    for (const auto& s : prev)
      if (!now.contains(s)) spdlog::info("Disconnected: {}", s.toStdString());

    if (physicalWirelessPrev_ && (!physicalWireless || physicalWirelessIdPrev_ != physicalWirelessId)) {
      if (!physicalWirelessIdPrev_.isEmpty()) spdlog::info("Disconnected: {}", physicalWirelessIdPrev_.toStdString());
    }
    if (physicalWireless && (!physicalWirelessPrev_ || physicalWirelessIdPrev_ != physicalWirelessId)) {
      if (!physicalWirelessId.isEmpty()) spdlog::info("Connected: {}", physicalWirelessId.toStdString());
    }

    physicalUsbPrev_ = physicalUsb;
    physicalWirelessPrev_ = physicalWireless;
    physicalWirelessIdPrev_ = physicalWirelessId;
  }

  const QString tgt = editTarget ? editTarget->text().trimmed() : QString{};
  if (!tgt.isEmpty()) {
    auto info = brokkr::platform::find_by_sysname(tgt.toStdString());
    if (info) shown << QString::fromStdString(info->sysname);
  } else {
    shown = physicalUsb;
  }

  if (physicalWireless) shown.prepend(physicalWirelessId);

  if (busy_) return;

  connectedDevices_ = shown;
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

  if (dark) {
    headerWidget_->setStyleSheet(
        "#headerBanner {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #3a3c3e, stop:1 #242527);"
        "  border: 1px solid #1a1a1a;"
        "  border-radius: 8px;"
        "}"
        "#headerBanner QLabel { background: transparent; border: none; }");
  } else {
    headerWidget_->setStyleSheet(
        "#headerBanner {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #e8eaec, stop:1 #d0d2d4);"
        "  border: 1px solid #b0b0b0;"
        "  border-radius: 8px;"
        "}"
        "#headerBanner QLabel { background: transparent; border: none; }");
  }
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
  if (sldDeviceBoxes)
    sldDeviceBoxes->setEnabled(enabled && !wireless && btnManyDevices_ && btnManyDevices_->isChecked());

  if (chkAdvanced_) chkAdvanced_->setEnabled(enabled);

  for (auto* chk : fileChecks_)
    if (chk) chk->setEnabled(enabled);
  for (auto* btn : fileButtons_)
    if (btn) btn->setEnabled(enabled);

  if (btnReset_) btnReset_->setEnabled(enabled);
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

    auto* box = new QLineEdit(cell);
    box->setReadOnly(true);
    box->setAlignment(Qt::AlignCenter);
    box->setMinimumHeight(22);
    box->setStyleSheet(
        "border: 1px solid palette(mid);"
        "font-size: 9px;"
        "color: palette(dark);");

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
    auto pal = box->palette();
    pal.setColor(QPalette::Base, palette().color(QPalette::Window));
    box->setPalette(pal);
    box->setStyleSheet(
        "border: 1px solid palette(mid);"
        "font-size: 9px;"
        "color: palette(dark);");
  }

  auto elideFor = [&](QLineEdit* box, const QString& s) {
    const int w = std::max(0, box->width() - 10);
    return box->fontMetrics().elidedText(s, Qt::ElideMiddle, w);
  };

  const int shown = std::min<int>(connectedDevices_.size(), comBoxes.size());
  for (int i = 0; i < shown; ++i) {
    const QString sysname = connectedDevices_[i].trimmed();
    const QString raw = QString("%1:[%2]").arg(i).arg(sysname);
    comBoxes[i]->setText(elideFor(comBoxes[i], raw));
    comBoxes[i]->setToolTip(sysname);
    comBoxes[i]->setStyleSheet(
        "font-weight: bold;"
        "font-size: 9px;"
        "color: white;"
        "background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "  stop:0 #5a9fd4, stop:0.5 #4080c0, stop:1 #35608a);"
        "border: 1px solid #2a5070;"
        "border-radius: 2px;");
  }

  if (connectedDevices_.size() > comBoxes.size() && !comBoxes.isEmpty()) {
    overflowDevices_ = true;
    const int extra = connectedDevices_.size() - comBoxes.size();
    auto* last = comBoxes.back();
    const QString raw = QString("... +%1 more").arg(extra);
    last->setText(elideFor(last, raw));
    {
      auto pal = last->palette();
      QColor base = palette().color(QPalette::Base);
      QColor warn(255, 183, 77);
      warn.setAlpha(50);
      const int r2 = (base.red() * (255 - warn.alpha()) + warn.red() * warn.alpha()) / 255;
      const int g2 = (base.green() * (255 - warn.alpha()) + warn.green() * warn.alpha()) / 255;
      const int b2 = (base.blue() * (255 - warn.alpha()) + warn.blue() * warn.alpha()) / 255;
      pal.setColor(QPalette::Base, QColor(r2, g2, b2));
      last->setPalette(pal);
    }
    last->setStyleSheet(
        "font-weight: bold;"
        "border: 1px solid #ffb74d;"
        "font-size: 9px;");
  }
}

bool BrokkrWrapper::canRunStart_(QString* whyNot) const {
  const bool wireless = chkWireless->isChecked();
  const bool hasTarget = !editTarget->text().trimmed().isEmpty();

  const bool hasAnyFile = (!editBL->text().isEmpty() && editBL->isEnabled()) ||
                          (!editAP->text().isEmpty() && editAP->isEnabled()) ||
                          (!editCP->text().isEmpty() && editCP->isEnabled()) ||
                          (!editCSC->text().isEmpty() && editCSC->isEnabled()) ||
                          (!editUserData->text().isEmpty() && editUserData->isEnabled()) ||
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

bool BrokkrWrapper::canRunPrintPit_(QString* whyNot) const {
  const bool wireless = chkWireless->isChecked();
  const bool hasTarget = !editTarget->text().trimmed().isEmpty();

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

  if (connectedDevices_.size() != 1) {
    if (whyNot) *whyNot = "Printing PIT from device requires exactly one connected device.";
    return false;
  }

  if (overflowDevices_) {
    if (whyNot) *whyNot = "Too many devices for PIT print.";
    return false;
  }

  return true;
}

void BrokkrWrapper::updateActionButtons_() {
  if (busy_) {
    btnRun->setEnabled(false);
    if (btnPrintPit) btnPrintPit->setEnabled(false);
    return;
  }

  QString why;
  btnRun->setEnabled(canRunStart_(&why));
  if (btnPrintPit) btnPrintPit->setEnabled(canRunPrintPit_(&why));
}

void BrokkrWrapper::showBlocked_(const QString& title, const QString& msg) const {
  QMessageBox::warning(const_cast<BrokkrWrapper*>(this), title, msg);
}

void BrokkrWrapper::setupOdinFileInput(QGridLayout* layout, int row, const QString& label, QLineEdit*& lineEdit) {
  auto* chk = new QCheckBox(this);
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

  connect(btn, &QPushButton::clicked, this, [this, lineEdit, chk]() {
    if (busy_) return;
    QString file = QFileDialog::getOpenFileName(this, "Select Firmware File", lastDir,
                                                "Firmware Archives (*.tar *.tar.md5);;All Files (*)");
    if (!file.isEmpty()) {
      lastDir = QFileInfo(file).absolutePath();
      lineEdit->setText(file);
      chk->setChecked(true);
    }
    updateActionButtons_();
  });

  connect(chk, &QCheckBox::toggled, this, [this, lineEdit](bool checked) {
    if (busy_) return;
    lineEdit->setEnabled(checked);
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
    for (int i = 0; i < activeCount; ++i) slotActive_[static_cast<std::size_t>(i)] = 1;
  }

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

    auto fail_ui = [&](const QString& msg) {
      QMetaObject::invokeMethod(
          this,
          [this, msg]() {
            const int z = logDevCount_.load(std::memory_order_relaxed);
            appendLogLine_(QString("<font color=\"#ff5555\">&lt;%1&gt; FAIL! %2</font>").arg(z).arg(htmlEsc(msg)));
            setSquaresFinal_(false);
            setBusy_(false);
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
    cfg.redownload_after = (actionIndex == 1);
    cfg.reboot_after = (actionIndex != 2);

    brokkr::odin::Ui ui;

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
            if (qs.contains("handshake", Qt::CaseInsensitive)) setSquaresText_("HANDSHAKE");
            if (qs.contains("shutdown", Qt::CaseInsensitive) || qs.contains("reboot", Qt::CaseInsensitive) ||
                qs.contains("reset", Qt::CaseInsensitive) || qs.contains("finalizing", Qt::CaseInsensitive))
              setSquaresText_("RESET");
          },
          Qt::QueuedConnection);
    };

    ui.on_progress = [&](std::uint64_t d, std::uint64_t t, std::uint64_t, std::uint64_t) {
      const double frac = (t > 0) ? (static_cast<double>(d) / static_cast<double>(t)) : 0.0;
      QMetaObject::invokeMethod(this, [this, frac]() { setSquaresProgress_(frac, true); }, Qt::QueuedConnection);
    };

    ui.on_error = [&](const std::string& s) {
      QMetaObject::invokeMethod(
          this,
          [this, qs = QString::fromStdString(s)]() {
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
                if (static_cast<std::size_t>(idx) >= slotFailed_.size())
                  slotFailed_.resize(static_cast<std::size_t>(idx) + 1, 0);
                slotFailed_[static_cast<std::size_t>(idx)] = 1;

                if (idx < devSquares_.size() && devSquares_[idx]) {
                  devSquares_[idx]->setVariant(DeviceSquare::Variant::Red);
                  devSquares_[idx]->setText("FAIL!");
                  devSquares_[idx]->setFill(1.0);
                }
                if (idx < comBoxes.size() && comBoxes[idx]) {
                  auto pal = comBoxes[idx]->palette();
                  QColor base = palette().color(QPalette::Base);
                  QColor fail(176, 0, 0);
                  fail.setAlpha(45);
                  const int r2 = (base.red() * (255 - fail.alpha()) + fail.red() * fail.alpha()) / 255;
                  const int g2 = (base.green() * (255 - fail.alpha()) + fail.green() * fail.alpha()) / 255;
                  const int b2 = (base.blue() * (255 - fail.alpha()) + fail.blue() * fail.alpha()) / 255;
                  pal.setColor(QPalette::Base, QColor(r2, g2, b2));
                  comBoxes[idx]->setPalette(pal);
                  comBoxes[idx]->setStyleSheet("font-weight: bold; border: 2px solid #b00000; font-size: 9px;");
                }

                shown = reason.isEmpty() ? "FAIL!" : QString("FAIL! (Device %1) %2").arg(idx).arg(reason);
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
    if (editBL->isEnabled() && !editBL->text().isEmpty()) inputs.emplace_back(editBL->text().toStdString());
    if (editAP->isEnabled() && !editAP->text().isEmpty()) inputs.emplace_back(editAP->text().toStdString());
    if (editCP->isEnabled() && !editCP->text().isEmpty()) inputs.emplace_back(editCP->text().toStdString());
    if (editCSC->isEnabled() && !editCSC->text().isEmpty()) inputs.emplace_back(editCSC->text().toStdString());
    if (editUserData->isEnabled() && !editUserData->text().isEmpty())
      inputs.emplace_back(editUserData->text().toStdString());

    struct Provider {
      std::vector<std::unique_ptr<brokkr::odin::UsbTarget>> usb; // owns USB transports
      std::vector<brokkr::odin::Target> owned;                   // owns Targets
      std::vector<brokkr::odin::Target*> ptrs;                   // passed to engine
    };

    auto make_provider = [&]() -> std::optional<Provider> {
      Provider p;

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
        return p;
      }

      std::vector<brokkr::platform::UsbDeviceSysfsInfo> targets;
      if (!tgt.isEmpty()) {
        auto one = select_target(tgt);
        if (!one) {
          fail_ui("Target sysname not found or not supported.");
          return std::nullopt;
        }
        targets.push_back(*one);
      } else {
        for (const auto& sys : uiDevicesSnapshot)
          if (auto one = select_target(sys)) targets.push_back(*one);
        if (targets.empty()) targets = enumerate_targets();
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
      (void)st; // UI handles per-device fails via ui.on_error; overall uses setSquaresFinal_(true)
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

    spdlog::info("MD5 check ({}), Please wait.", human_bytes(totalBytes).toStdString());
    QMetaObject::invokeMethod(
        this,
        [this]() {
          setSquaresActiveColor_(false);
          setSquaresText_("MD5");
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

    const bool dl_mode = std::ranges::any_of(*specs,
                                             [](const brokkr::odin::ImageSpec& s) { return s.download_list_mode; });

    if (!pit_to_upload && !dl_mode) {
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

    QMetaObject::invokeMethod(this, [this]() { setSquaresProgress_(0.0, false); }, Qt::QueuedConnection);

    run_engine(std::move(srcs));
  });
}

void BrokkrWrapper::startWorkPrintPit_() {
  if (busy_) return;
  setBusy_(true);

  slotFailed_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
  slotActive_.assign(static_cast<std::size_t>(devSquares_.size()), 0);
  {
    const int activeCount = std::min<int>(connectedDevices_.size(), devSquares_.size());
    for (int i = 0; i < activeCount; ++i) slotActive_[static_cast<std::size_t>(i)] = 1;
  }

  setSquaresProgress_(0.0, false);
  setSquaresText_("PIT");
  setSquaresActiveColor_(false);

  const int actionIndex = cmbRebootAction->currentIndex();

  worker_ = std::jthread([this, actionIndex](std::stop_token) {
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

    auto fail_ui = [&](const QString& msg) {
      QMetaObject::invokeMethod(
          this,
          [this, msg]() {
            appendLogLine_(QString("<font color=\"#ff5555\">%1</font>").arg(htmlEsc(msg)));
            setSquaresFinal_(false);
            setBusy_(false);
          },
          Qt::QueuedConnection);
    };

    const auto sm = shutdown_mode_from_ui(actionIndex);

    const QString tgt = editTarget->text().trimmed();
    const bool wireless = chkWireless->isChecked();
    if (wireless && !tgt.isEmpty()) {
      fail_ui("Wireless cannot be used together with Target Sysname.");
      return;
    }

    brokkr::odin::Cfg cfg;

    auto run_link = [&](brokkr::core::IByteTransport& link) {
      QMetaObject::invokeMethod(this, [this]() { setSquaresText_("HANDSHAKE"); }, Qt::QueuedConnection);

      auto st = print_pit_over_link(link, cfg, sm);
      if (!st) {
        fail_ui(QString::fromStdString(st.error()));
        return;
      }

      QMetaObject::invokeMethod(this, [this]() { setSquaresText_("RESET"); }, Qt::QueuedConnection);
      done_ui();
    };

    if (wireless) {
      brokkr::platform::TcpConnection* connp = nullptr;
      {
        std::lock_guard lk(wireless_mtx_);
        if (wireless_conn_) connp = &*wireless_conn_;
      }
      if (!connp || !connp->connected()) {
        fail_ui("No wireless device connected.");
        return;
      }
      run_link(*connp);
      return;
    }

    std::vector<brokkr::platform::UsbDeviceSysfsInfo> targets;
    if (!tgt.isEmpty()) {
      auto one = select_target(tgt);
      if (!one) {
        fail_ui("Target sysname not found or not supported.");
        return;
      }
      targets.push_back(*one);
    } else {
      targets = enumerate_targets();
    }

    if (targets.size() != 1) {
      fail_ui("Printing PIT requires exactly one device.");
      return;
    }

    brokkr::platform::UsbFsDevice dev(targets.front().devnode());
    auto dst = dev.open_and_init();
    if (!dst) {
      fail_ui(QString::fromStdString(dst.error()));
      return;
    }

    brokkr::platform::UsbFsConnection conn(dev);
    auto cst = conn.open();
    if (!cst) {
      fail_ui(QString::fromStdString(cst.error()));
      return;
    }

    run_link(conn);
  });
}
