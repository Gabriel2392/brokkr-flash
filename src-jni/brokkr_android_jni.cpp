/*
 * Copyright (c) 2026 Gabriel2392
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "app/md5_verify.hpp"
#include "core/str.hpp"
#include "io/random_access.hpp"
#include "platform/android/app_dirs.hpp"
#include "platform/android/java_tcp_transport.hpp"
#include "platform/android/libusb_transport.hpp"
#include "protocol/odin/flash.hpp"

#include <spdlog/spdlog.h>

#include <jni.h>

#include <charconv>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

JavaVM* g_vm = nullptr;

constexpr jint kLogTrace = 0;
constexpr jint kLogDebug = 1;
constexpr jint kLogInfo = 2;
constexpr jint kLogWarn = 3;
constexpr jint kLogError = 4;
constexpr jint kLogCritical = 5;

spdlog::level::level_enum native_log_level() noexcept {
#ifdef NDEBUG
  return spdlog::level::info;
#else
  return spdlog::level::debug;
#endif
}

int callback_log_level(spdlog::level::level_enum level) noexcept {
  switch (level) {
    case spdlog::level::trace:
      return kLogTrace;
    case spdlog::level::debug:
      return kLogDebug;
    case spdlog::level::info:
      return kLogInfo;
    case spdlog::level::warn:
      return kLogWarn;
    case spdlog::level::err:
      return kLogError;
    case spdlog::level::critical:
      return kLogCritical;
    case spdlog::level::off:
      return kLogInfo;
  }
  return kLogInfo;
}

class SessionCallbackProxy {
 public:
  SessionCallbackProxy(JavaVM* vm, JNIEnv* env, jobject callback, std::string cache_dir)
      : vm_(vm), cache_dir_(std::move(cache_dir)) {
    callback_ref_ = env->NewGlobalRef(callback);

    jclass local_class = env->GetObjectClass(callback);
    callback_class_ = static_cast<jclass>(env->NewGlobalRef(local_class));
    env->DeleteLocalRef(local_class);

    on_log_method_ = env->GetMethodID(callback_class_, "onLog", "(ILjava/lang/String;)V");
    on_devices_method_ = env->GetMethodID(callback_class_, "onDevices", "([Ljava/lang/String;)V");
    on_model_method_ = env->GetMethodID(callback_class_, "onModel", "(Ljava/lang/String;)V");
    on_stage_method_ = env->GetMethodID(callback_class_, "onStage", "(Ljava/lang/String;)V");
    on_plan_item_method_ = env->GetMethodID(callback_class_, "onPlanItem", "(IIIILjava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V");
    on_plan_ready_method_ = env->GetMethodID(callback_class_, "onPlanReady", "(IJ)V");
    on_item_active_method_ = env->GetMethodID(callback_class_, "onItemActive", "(I)V");
    on_item_done_method_ = env->GetMethodID(callback_class_, "onItemDone", "(I)V");
    on_progress_method_ = env->GetMethodID(callback_class_, "onProgress", "(JJJJ)V");
    on_device_error_method_ = env->GetMethodID(callback_class_, "onDeviceError", "(ILjava/lang/String;)V");
    on_error_method_ = env->GetMethodID(callback_class_, "onError", "(Ljava/lang/String;)V");
    on_finished_method_ = env->GetMethodID(callback_class_, "onFinished", "(ZLjava/lang/String;)V");

    if (on_log_method_ == nullptr || on_devices_method_ == nullptr || on_model_method_ == nullptr ||
        on_stage_method_ == nullptr || on_plan_item_method_ == nullptr || on_plan_ready_method_ == nullptr ||
        on_item_active_method_ == nullptr || on_item_done_method_ == nullptr || on_progress_method_ == nullptr ||
        on_device_error_method_ == nullptr || on_error_method_ == nullptr || on_finished_method_ == nullptr) {
      if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
      }
      spdlog::error("SessionCallbackProxy: required JNI method missing");
    }
  }

  ~SessionCallbackProxy() {
    bool did_attach = false;
    JNIEnv* env = get_env(did_attach);
    if (env != nullptr) {
      if (callback_class_ != nullptr) env->DeleteGlobalRef(callback_class_);
      if (callback_ref_ != nullptr) env->DeleteGlobalRef(callback_ref_);
    }
    if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
  }

  const std::string& cache_dir() const noexcept { return cache_dir_; }

  void silence() noexcept { silenced_.store(true, std::memory_order_release); }
  bool silenced() const noexcept { return silenced_.load(std::memory_order_acquire); }

  void on_log(spdlog::level::level_enum level, const std::string& message) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      jstring text = env->NewStringUTF(message.c_str());
      env->CallVoidMethod(callback_ref_, on_log_method_, static_cast<jint>(callback_log_level(level)), text);
      env->DeleteLocalRef(text);
      clear_exception(env, "onLog");
    });
  }

  void on_devices(const std::vector<std::string>& ids) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      jclass string_class = env->FindClass("java/lang/String");
      jobjectArray array = env->NewObjectArray(static_cast<jsize>(ids.size()), string_class, nullptr);
      env->DeleteLocalRef(string_class);

      for (std::size_t i = 0; i < ids.size(); ++i) {
        jstring value = env->NewStringUTF(ids[i].c_str());
        env->SetObjectArrayElement(array, static_cast<jsize>(i), value);
        env->DeleteLocalRef(value);
      }

      env->CallVoidMethod(callback_ref_, on_devices_method_, array);
      env->DeleteLocalRef(array);
      clear_exception(env, "onDevices");
    });
  }

  void on_model(const std::string& model) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      jstring text = env->NewStringUTF(model.c_str());
      env->CallVoidMethod(callback_ref_, on_model_method_, text);
      env->DeleteLocalRef(text);
      clear_exception(env, "onModel");
    });
  }

  void on_stage(const std::string& stage) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      jstring text = env->NewStringUTF(stage.c_str());
      env->CallVoidMethod(callback_ref_, on_stage_method_, text);
      env->DeleteLocalRef(text);
      clear_exception(env, "onStage");
    });
  }

  void on_plan(const std::vector<brokkr::odin::PlanItem>& items, std::uint64_t total_bytes) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        jstring part_name = env->NewStringUTF(item.part_name.c_str());
        jstring pit_file_name = env->NewStringUTF(item.pit_file_name.c_str());
        jstring source_base = env->NewStringUTF(item.source_base.c_str());
        env->CallVoidMethod(callback_ref_, on_plan_item_method_, static_cast<jint>(i), static_cast<jint>(item.kind),
                            static_cast<jint>(item.part_id), static_cast<jint>(item.dev_type), part_name, pit_file_name,
                            source_base, static_cast<jlong>(item.size));
        env->DeleteLocalRef(part_name);
        env->DeleteLocalRef(pit_file_name);
        env->DeleteLocalRef(source_base);
        clear_exception(env, "onPlanItem");
      }

      env->CallVoidMethod(callback_ref_, on_plan_ready_method_, static_cast<jint>(items.size()),
                          static_cast<jlong>(total_bytes));
      clear_exception(env, "onPlanReady");
    });
  }

  void on_item_active(std::size_t index) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      env->CallVoidMethod(callback_ref_, on_item_active_method_, static_cast<jint>(index));
      clear_exception(env, "onItemActive");
    });
  }

  void on_item_done(std::size_t index) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      env->CallVoidMethod(callback_ref_, on_item_done_method_, static_cast<jint>(index));
      clear_exception(env, "onItemDone");
    });
  }

  void on_progress(std::uint64_t overall_done, std::uint64_t overall_total, std::uint64_t item_done,
                   std::uint64_t item_total) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      env->CallVoidMethod(callback_ref_, on_progress_method_, static_cast<jlong>(overall_done),
                          static_cast<jlong>(overall_total), static_cast<jlong>(item_done),
                          static_cast<jlong>(item_total));
      clear_exception(env, "onProgress");
    });
  }

  void on_device_error(int index, const std::string& message) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      jstring text = env->NewStringUTF(message.c_str());
      env->CallVoidMethod(callback_ref_, on_device_error_method_, static_cast<jint>(index), text);
      env->DeleteLocalRef(text);
      clear_exception(env, "onDeviceError");
    });
  }

  void on_error(const std::string& message) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      jstring text = env->NewStringUTF(message.c_str());
      env->CallVoidMethod(callback_ref_, on_error_method_, text);
      env->DeleteLocalRef(text);
      clear_exception(env, "onError");
    });
  }

  void on_finished(bool success, const std::string& message) {
    if (silenced()) return;
    with_env([&](JNIEnv* env) {
      jstring text = env->NewStringUTF(message.c_str());
      env->CallVoidMethod(callback_ref_, on_finished_method_, static_cast<jboolean>(success), text);
      env->DeleteLocalRef(text);
      clear_exception(env, "onFinished");
    });
  }

 private:
  JNIEnv* get_env(bool& did_attach) const noexcept {
    did_attach = false;
    if (vm_ == nullptr) return nullptr;

    JNIEnv* env = nullptr;
    const jint status = vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_OK) return env;
    if (status != JNI_EDETACHED) return nullptr;
    if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    did_attach = true;
    return env;
  }

  template <typename Fn>
  void with_env(Fn&& fn) {
    bool did_attach = false;
    JNIEnv* env = get_env(did_attach);
    if (env != nullptr) {
      fn(env);
    }
    if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
  }

  void clear_exception(JNIEnv* env, const char* context) {
    if (!env->ExceptionCheck()) return;
    env->ExceptionDescribe();
    env->ExceptionClear();
    spdlog::error("JNI callback exception during {}", context);
  }

 private:
  JavaVM* vm_ = nullptr;
  jobject callback_ref_ = nullptr;
  jclass callback_class_ = nullptr;
  std::string cache_dir_;

  jmethodID on_log_method_ = nullptr;
  jmethodID on_devices_method_ = nullptr;
  jmethodID on_model_method_ = nullptr;
  jmethodID on_stage_method_ = nullptr;
  jmethodID on_plan_item_method_ = nullptr;
  jmethodID on_plan_ready_method_ = nullptr;
  jmethodID on_item_active_method_ = nullptr;
  jmethodID on_item_done_method_ = nullptr;
  jmethodID on_progress_method_ = nullptr;
  jmethodID on_device_error_method_ = nullptr;
  jmethodID on_error_method_ = nullptr;
  jmethodID on_finished_method_ = nullptr;

  std::atomic<bool> silenced_{false};
};

std::optional<std::pair<int, std::string>> parse_devfail(std::string_view message) {
  constexpr std::string_view prefix = "DEVFAIL idx=";
  if (!message.starts_with(prefix)) return std::nullopt;

  message.remove_prefix(prefix.size());
  const auto space = message.find(' ');
  if (space == std::string_view::npos) return std::nullopt;

  int index = 0;
  const auto [ptr, ec] = std::from_chars(message.data(), message.data() + space, index);
  if (ec != std::errc{} || ptr != message.data() + space) return std::nullopt;

  return std::pair<int, std::string>(index, std::string(message.substr(space + 1)));
}

struct NativeSession {
  explicit NativeSession(std::unique_ptr<SessionCallbackProxy> proxy) : callback(std::move(proxy)) {}

  std::unique_ptr<SessionCallbackProxy> callback;

  std::unique_ptr<brokkr::android_platform::LibusbContext> usb_context;

  std::atomic<bool> cancel{false};
};

std::mutex& sessions_mutex() {
  static std::mutex m;
  return m;
}
std::unordered_set<NativeSession*>& sessions_set() {
  static std::unordered_set<NativeSession*> s;
  return s;
}

void register_session(NativeSession* s) {
  std::lock_guard<std::mutex> lk(sessions_mutex());
  sessions_set().insert(s);
}

void unregister_session(NativeSession* s) {
  std::lock_guard<std::mutex> lk(sessions_mutex());
  sessions_set().erase(s);
}

void request_session_cancel(NativeSession* s) {
  std::lock_guard<std::mutex> lk(sessions_mutex());
  if (sessions_set().count(s) == 0) return;
  if (s->callback != nullptr) s->callback->silence();
  brokkr::app::clear_session_verify_cache();
  // Never abort by fd: the kernel reuses fd numbers, so it could hit a newer
  // session's transport.
  s->cancel.store(true, std::memory_order_release);
  if (s->usb_context != nullptr && s->usb_context->raw() != nullptr) {
    s->usb_context->interrupt_event_handler();
  }
}

NativeSession* as_session(jlong handle) { return reinterpret_cast<NativeSession*>(handle); }

std::vector<int> collect_fds(JNIEnv* env, jintArray package_fds) {
  std::vector<int> fds;
  if (package_fds == nullptr) return fds;

  const jsize size = env->GetArrayLength(package_fds);
  fds.resize(static_cast<std::size_t>(size));
  env->GetIntArrayRegion(package_fds, 0, size, reinterpret_cast<jint*>(fds.data()));
  return fds;
}

std::string jstring_to_string(JNIEnv* env, jstring value) {
  if (value == nullptr) return {};

  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (chars == nullptr) return {};

  std::string out(chars);
  env->ReleaseStringUTFChars(value, chars);
  return out;
}

std::vector<std::string> collect_labels(JNIEnv* env, jobjectArray label_array, std::size_t count, std::string_view prefix) {
  std::vector<std::string> labels(count);
  for (std::size_t i = 0; i < count; ++i) {
    labels[i] = std::string(prefix) + std::to_string(i + 1);
  }

  if (label_array == nullptr) return labels;

  const auto size = static_cast<std::size_t>(env->GetArrayLength(label_array));
  for (std::size_t i = 0; i < std::min(count, size); ++i) {
    jstring value = static_cast<jstring>(env->GetObjectArrayElement(label_array, static_cast<jsize>(i)));
    labels[i] = jstring_to_string(env, value);
    if (labels[i].empty()) labels[i] = std::string(prefix) + std::to_string(i + 1);
    env->DeleteLocalRef(value);
  }

  return labels;
}

brokkr::core::Result<std::shared_ptr<const std::vector<std::byte>>> load_pit_fd(int fd, std::string label) noexcept {
  BRK_TRYV(source, brokkr::io::open_fd_source(fd, std::move(label)));

  const std::uint64_t size = source->size();
  if (size == 0) return brokkr::core::fail("Manual PIT file is empty");
  if (size > std::numeric_limits<std::size_t>::max()) return brokkr::core::fail("Manual PIT file is too large");

  auto pit = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(size));
  BRK_TRY(source->read_exact_at(0, std::span<std::byte>(*pit)));

  spdlog::info("Using manual PIT: {}", source->label());
  return std::shared_ptr<const std::vector<std::byte>>(std::move(pit));
}

std::expected<brokkr::android_platform::LibusbUsbTransport::OpenParams, std::string>
read_usb_open_params(JNIEnv* env, jobject transport) {
  if (transport == nullptr) return std::unexpected{"null USB transport"};

  jclass cls = env->GetObjectClass(transport);
  if (cls == nullptr) return std::unexpected{"missing BrokkrUsbTransport class"};

  const auto field_id = [&](const char* name) {
    return env->GetFieldID(cls, name, "I");
  };
  jfieldID fd_field = field_id("nativeFd");
  jfieldID iface_field = field_id("interfaceNumber");
  jfieldID in_addr_field = field_id("bulkInAddress");
  jfieldID out_addr_field = field_id("bulkOutAddress");
  jmethodID id_method = env->GetMethodID(cls, "getId", "()Ljava/lang/String;");
  env->DeleteLocalRef(cls);

  if (fd_field == nullptr || iface_field == nullptr || in_addr_field == nullptr || out_addr_field == nullptr ||
      id_method == nullptr) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return std::unexpected{"BrokkrUsbTransport layout mismatch"};
  }

  brokkr::android_platform::LibusbUsbTransport::OpenParams params{};
  params.fd = env->GetIntField(transport, fd_field);
  params.interface_number = env->GetIntField(transport, iface_field);
  params.bulk_in_addr = static_cast<std::uint8_t>(env->GetIntField(transport, in_addr_field));
  params.bulk_out_addr = static_cast<std::uint8_t>(env->GetIntField(transport, out_addr_field));

  auto id_string = static_cast<jstring>(env->CallObjectMethod(transport, id_method));
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    params.id = "device";
  } else {
    params.id = jstring_to_string(env, id_string);
    if (id_string != nullptr) env->DeleteLocalRef(id_string);
  }
  if (params.id.empty()) params.id = "device";

  return params;
}

brokkr::core::Status execute_flash(JNIEnv* env, NativeSession& session,
                                   std::vector<std::unique_ptr<brokkr::core::IByteTransport>> transports,
                                   std::vector<brokkr::odin::Target> targets,
                                   std::vector<brokkr::odin::Target*> target_ptrs,
                                   std::vector<std::string> device_ids, jintArray package_fds,
                                   jobjectArray package_labels, jint manual_pit_fd, jstring manual_pit_label,
                                   jboolean auto_reboot) noexcept {
  brokkr::android_platform::set_app_cache_dir(session.callback->cache_dir());

  if (transports.empty()) return brokkr::core::fail("No devices are ready");

  session.callback->on_devices(device_ids);

  const auto raw_fds = collect_fds(env, package_fds);
  const auto labels = collect_labels(env, package_labels, raw_fds.size(), "Package ");
  if (raw_fds.empty() && manual_pit_fd < 0) return brokkr::core::fail("No package files or PIT selected");

  std::vector<brokkr::io::RandomAccessSourcePtr> packages;
  packages.reserve(raw_fds.size());
  for (std::size_t i = 0; i < raw_fds.size(); ++i) {
    BRK_TRYV(package, brokkr::io::open_fd_source(raw_fds[i], labels[i]));
    packages.push_back(std::move(package));
  }

  spdlog::set_level(native_log_level());
  spdlog::set_log_callback([proxy = session.callback.get()](spdlog::level::level_enum level, const std::string& msg) {
    proxy->on_log(level, msg);
  });
  struct CallbackGuard {
    ~CallbackGuard() { spdlog::set_log_callback({}); }
  } callback_guard;

  brokkr::odin::Ui ui;
  ui.on_devices = [proxy = session.callback.get()](std::size_t, const std::vector<std::string>& ids) { proxy->on_devices(ids); };
  ui.on_model = [proxy = session.callback.get()](const std::string& model) { proxy->on_model(model); };
  ui.on_stage = [proxy = session.callback.get()](const std::string& stage) { proxy->on_stage(stage); };
  ui.on_plan = [proxy = session.callback.get()](const std::vector<brokkr::odin::PlanItem>& items, std::uint64_t total) {
    proxy->on_plan(items, total);
  };
  ui.on_item_active = [proxy = session.callback.get()](std::size_t index) { proxy->on_item_active(index); };
  ui.on_item_done = [proxy = session.callback.get()](std::size_t index) { proxy->on_item_done(index); };
  using clock_t = std::chrono::steady_clock;
  ui.on_progress = [proxy = session.callback.get(),
                    last = std::make_shared<std::atomic<std::int64_t>>(0)](std::uint64_t overall_done,
                                                                          std::uint64_t overall_total,
                                                                          std::uint64_t item_done,
                                                                          std::uint64_t item_total) {
    constexpr std::int64_t kMinIntervalMs = 33;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         clock_t::now().time_since_epoch())
                         .count();
    const std::int64_t prev = last->load(std::memory_order_relaxed);
    const bool is_terminal = (item_total > 0 && item_done >= item_total) ||
                             (overall_total > 0 && overall_done >= overall_total);
    if (!is_terminal && now - prev < kMinIntervalMs) return;
    last->store(now, std::memory_order_relaxed);
    proxy->on_progress(overall_done, overall_total, item_done, item_total);
  };
  ui.on_error = [proxy = session.callback.get()](const std::string& message) {
    if (auto device_error = parse_devfail(message)) {
      proxy->on_device_error(device_error->first, device_error->second);
      return;
    }
    proxy->on_error(message);
  };

  BRK_TRYV(md5_jobs, brokkr::app::md5_jobs_from_sources(packages));
  BRK_TRY(brokkr::app::md5_verify(md5_jobs, ui, &session.cancel));

  BRK_TRYV(specs, brokkr::odin::expand_inputs(packages, {.allow_raw_files = false}));
  std::shared_ptr<const std::vector<std::byte>> pit_to_upload;
  if (manual_pit_fd >= 0) {
    const auto label = jstring_to_string(env, manual_pit_label);
    BRK_TRYV(manual_pit, load_pit_fd(manual_pit_fd, label.empty() ? "Manual PIT" : label));
    pit_to_upload = std::move(manual_pit);
  }

  if (!pit_to_upload) pit_to_upload = brokkr::odin::pit_from_specs(specs);

  std::vector<brokkr::odin::ImageSpec> flash_specs;
  flash_specs.reserve(specs.size());
  for (auto& spec : specs) {
    if (!brokkr::core::ends_with_ci(spec.basename, ".pit")) {
      flash_specs.push_back(std::move(spec));
    }
  }

  if (flash_specs.empty() && !pit_to_upload) return brokkr::core::fail("No valid flashable files selected");

  brokkr::odin::Cfg cfg;
  cfg.reboot_after = (auto_reboot == JNI_TRUE);
  cfg.post_close_delay_ms = 0;
  cfg.step_delay_ms = 0;

  auto status = brokkr::odin::flash(target_ptrs, flash_specs, pit_to_upload, cfg, ui);
  return status;
}

brokkr::core::Status run_usb_flash(JNIEnv* env, NativeSession& session, jobjectArray transport_array,
                                  jintArray package_fds, jobjectArray package_labels, jint manual_pit_fd,
                                  jstring manual_pit_label, jboolean auto_reboot) noexcept {
  const jsize transport_count = transport_array != nullptr ? env->GetArrayLength(transport_array) : 0;
  if (transport_count <= 0) return brokkr::core::fail("No devices are ready");

  std::vector<std::unique_ptr<brokkr::core::IByteTransport>> transports;
  std::vector<brokkr::odin::Target> targets;
  std::vector<brokkr::odin::Target*> target_ptrs;
  std::vector<std::string> device_ids;

  transports.reserve(static_cast<std::size_t>(transport_count));
  targets.reserve(static_cast<std::size_t>(transport_count));
  target_ptrs.reserve(static_cast<std::size_t>(transport_count));
  device_ids.reserve(static_cast<std::size_t>(transport_count));

  for (jsize i = 0; i < transport_count; ++i) {
    jobject transport = env->GetObjectArrayElement(transport_array, i);
    if (transport == nullptr) continue;

    auto params = read_usb_open_params(env, transport);
    env->DeleteLocalRef(transport);
    if (!params) return brokkr::core::fail(params.error());

    if (session.usb_context == nullptr) {
      auto created = brokkr::android_platform::LibusbContext::create();
      if (!created) return brokkr::core::fail(created.error());
      session.usb_context = std::move(*created);
    }

    params->context = session.usb_context.get();
    params->external_cancel = &session.cancel;

    auto opened = brokkr::android_platform::LibusbUsbTransport::open(std::move(*params));
    if (!opened) return brokkr::core::fail(opened.error());

    auto* link = opened->get();
    device_ids.push_back(link->id());
    transports.push_back(std::move(*opened));
    targets.push_back({.id = link->id(), .link = link});
  }
  for (auto& t : targets) target_ptrs.push_back(&t);

  return execute_flash(env, session, std::move(transports), std::move(targets), std::move(target_ptrs),
                       std::move(device_ids), package_fds, package_labels, manual_pit_fd, manual_pit_label,
                       auto_reboot);
}

brokkr::core::Status run_wireless_flash(JNIEnv* env, NativeSession& session, jobject transport,
                                       jintArray package_fds, jobjectArray package_labels, jint manual_pit_fd,
                                       jstring manual_pit_label, jboolean auto_reboot) noexcept {
  if (transport == nullptr) return brokkr::core::fail("No wireless device is connected");

  auto link = std::make_unique<brokkr::android_platform::JavaTcpTransport>(g_vm, env, transport);
  std::string id = link->id();

  std::vector<std::unique_ptr<brokkr::core::IByteTransport>> transports;
  std::vector<brokkr::odin::Target> targets;
  std::vector<brokkr::odin::Target*> target_ptrs;
  std::vector<std::string> device_ids{id};

  targets.push_back({.id = std::move(id), .link = link.get()});
  target_ptrs.push_back(&targets.back());
  transports.push_back(std::move(link));

  return execute_flash(env, session, std::move(transports), std::move(targets), std::move(target_ptrs),
                       std::move(device_ids), package_fds, package_labels, manual_pit_fd, manual_pit_label,
                       auto_reboot);
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  g_vm = vm;
  spdlog::set_level(native_log_level());
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jlong JNICALL Java_com_oops_eros_BrokkrNativeBridge_nativeCreateSession(
    JNIEnv* env, jobject, jobject callback, jstring cache_dir) {
  if (callback == nullptr || cache_dir == nullptr) return 0;

  const char* chars = env->GetStringUTFChars(cache_dir, nullptr);
  if (chars == nullptr) return 0;

  auto proxy = std::make_unique<SessionCallbackProxy>(g_vm, env, callback, chars);
  env->ReleaseStringUTFChars(cache_dir, chars);

  auto* raw = new NativeSession(std::move(proxy));
  register_session(raw);
  return reinterpret_cast<jlong>(raw);
}

extern "C" JNIEXPORT void JNICALL Java_com_oops_eros_BrokkrNativeBridge_nativeDestroySession(
    JNIEnv*, jobject, jlong handle) {
  auto* session = as_session(handle);
  if (session == nullptr) return;
  if (session->callback != nullptr) session->callback->silence();
  unregister_session(session);
  delete session;
}

extern "C" JNIEXPORT void JNICALL Java_com_oops_eros_BrokkrNativeBridge_nativeRequestCancel(
    JNIEnv*, jobject, jlong handle) {
  auto* session = as_session(handle);
  if (session == nullptr) return;
  request_session_cancel(session);
}

extern "C" JNIEXPORT void JNICALL Java_com_oops_eros_BrokkrNativeBridge_nativeRunSession(
    JNIEnv* env, jobject, jlong handle, jobjectArray transport_array, jintArray package_fds, jobjectArray package_labels,
    jint manual_pit_fd, jstring manual_pit_label, jboolean auto_reboot) {
  auto* session = as_session(handle);
  if (session == nullptr || session->callback == nullptr) return;

  try {
    auto status = run_usb_flash(env, *session, transport_array, package_fds, package_labels, manual_pit_fd,
                                manual_pit_label, auto_reboot);
    if (!status) {
      session->callback->on_error(status.error());
      session->callback->on_finished(false, status.error());
      return;
    }
    session->callback->on_finished(true, {});
  } catch (const std::exception& error) {
    session->callback->on_error(error.what());
    session->callback->on_finished(false, error.what());
  } catch (...) {
    session->callback->on_error("Unknown native error");
    session->callback->on_finished(false, "Unknown native error");
  }
}

extern "C" JNIEXPORT void JNICALL Java_com_oops_eros_BrokkrNativeBridge_nativeRunWirelessSession(
    JNIEnv* env, jobject, jlong handle, jobject transport, jintArray package_fds, jobjectArray package_labels,
    jint manual_pit_fd, jstring manual_pit_label, jboolean auto_reboot) {
  auto* session = as_session(handle);
  if (session == nullptr || session->callback == nullptr) return;

  try {
    auto status = run_wireless_flash(env, *session, transport, package_fds, package_labels, manual_pit_fd,
                                     manual_pit_label, auto_reboot);
    if (!status) {
      session->callback->on_error(status.error());
      session->callback->on_finished(false, status.error());
      return;
    }
    session->callback->on_finished(true, {});
  } catch (const std::exception& error) {
    session->callback->on_error(error.what());
    session->callback->on_finished(false, error.what());
  } catch (...) {
    session->callback->on_error("Unknown native error");
    session->callback->on_finished(false, "Unknown native error");
  }
}