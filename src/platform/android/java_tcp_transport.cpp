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

#include "platform/android/java_tcp_transport.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace brokkr::android_platform {

namespace {

std::string jstring_to_string(JNIEnv* env, jstring value) {
  if (value == nullptr) return {};

  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (chars == nullptr) return {};

  std::string out(chars);
  env->ReleaseStringUTFChars(value, chars);
  return out;
}

} // namespace

JavaTcpTransport::JavaTcpTransport(JavaVM* vm, JNIEnv* env, jobject transport_object) : vm_(vm) {
  transport_ref_ = env->NewGlobalRef(transport_object);

  jclass local_class = env->GetObjectClass(transport_object);
  transport_class_ = static_cast<jclass>(env->NewGlobalRef(local_class));
  env->DeleteLocalRef(local_class);

  jmethodID get_id_method = env->GetMethodID(transport_class_, "getId", "()Ljava/lang/String;");
  is_connected_method_ = env->GetMethodID(transport_class_, "isConnected", "()Z");
  send_method_ = env->GetMethodID(transport_class_, "sendChunk", "([BIII)I");
  recv_method_ = env->GetMethodID(transport_class_, "receiveChunk", "([BIII)I");
  recv_zlp_method_ = env->GetMethodID(transport_class_, "receiveZlp", "(I)I");
  set_packet_size_hint_method_ = env->GetMethodID(transport_class_, "setPacketSizeHint", "(I)V");
  set_timeout_method_ = env->GetMethodID(transport_class_, "setTimeoutMs", "(I)V");
  close_method_ = env->GetMethodID(transport_class_, "close", "()V");

  if (send_method_ == nullptr || recv_method_ == nullptr || recv_zlp_method_ == nullptr ||
      close_method_ == nullptr || is_connected_method_ == nullptr) {
    spdlog::error("JavaTcpTransport: required JNI method missing");
    clear_exception(env, "GetMethodID(required)");
    connected_ = false;
  }

  if (get_id_method != nullptr) {
    auto id_string = static_cast<jstring>(env->CallObjectMethod(transport_ref_, get_id_method));
    if (env->ExceptionCheck()) {
      clear_exception(env, "getId");
      id_ = "device";
    } else {
      id_ = jstring_to_string(env, id_string);
      env->DeleteLocalRef(id_string);
    }
  }

  if (id_.empty()) id_ = "device";

  get_id_method_ = get_id_method;
}

JavaTcpTransport::~JavaTcpTransport() {
  close_java_transport();

  bool did_attach = false;
  JNIEnv* env = get_env(did_attach);
  if (env != nullptr) {
    if (send_buffer_ != nullptr) env->DeleteGlobalRef(send_buffer_);
    if (recv_buffer_ != nullptr) env->DeleteGlobalRef(recv_buffer_);
    if (transport_class_ != nullptr) env->DeleteGlobalRef(transport_class_);
    if (transport_ref_ != nullptr) env->DeleteGlobalRef(transport_ref_);
  }

  if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
}

bool JavaTcpTransport::connected() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return false;
  if (transport_ref_ == nullptr || is_connected_method_ == nullptr) return connected_;

  bool did_attach = false;
  JNIEnv* env = get_env(did_attach);
  if (env == nullptr) {
    connected_ = false;
    return false;
  }

  const jboolean is_connected = env->CallBooleanMethod(transport_ref_, is_connected_method_);
  if (env->ExceptionCheck()) {
    clear_exception(env, "isConnected");
    connected_ = false;
  } else if (is_connected == JNI_FALSE) {
    connected_ = false;
  }

  if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
  return connected_;
}

JNIEnv* JavaTcpTransport::get_env(bool& did_attach) const noexcept {
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

bool JavaTcpTransport::ensure_array(JNIEnv* env, jbyteArray& array_ref, std::size_t& capacity, std::size_t want) {
  if (capacity >= want && array_ref != nullptr) return true;

  if (array_ref != nullptr) {
    env->DeleteGlobalRef(array_ref);
    array_ref = nullptr;
    capacity = 0;
  }

  jbyteArray local = env->NewByteArray(static_cast<jsize>(want));
  if (local == nullptr) {
    clear_exception(env, "NewByteArray");
    return false;
  }

  array_ref = static_cast<jbyteArray>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);
  if (array_ref == nullptr) {
    clear_exception(env, "NewGlobalRef(byte[])");
    return false;
  }

  capacity = want;
  return true;
}

void JavaTcpTransport::clear_exception(JNIEnv* env, const char* context) const noexcept {
  if (!env->ExceptionCheck()) return;
  env->ExceptionDescribe();
  env->ExceptionClear();
  spdlog::error("JavaTcpTransport JNI exception during {}", context);
}

void JavaTcpTransport::close_java_transport() noexcept {
  if (!connected_) return;

  bool did_attach = false;
  JNIEnv* env = get_env(did_attach);
  if (env != nullptr && close_method_ != nullptr && transport_ref_ != nullptr) {
    env->CallVoidMethod(transport_ref_, close_method_);
    clear_exception(env, "close");
  }

  if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
  connected_ = false;
}

void JavaTcpTransport::set_packet_size_hint(std::size_t bytes) noexcept {
  if (bytes == 0 || set_packet_size_hint_method_ == nullptr) return;

  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return;

  bool did_attach = false;
  JNIEnv* env = get_env(did_attach);
  if (env == nullptr) return;

  const auto capped = std::min<std::size_t>(bytes, static_cast<std::size_t>(std::numeric_limits<jint>::max()));
  env->CallVoidMethod(transport_ref_, set_packet_size_hint_method_, static_cast<jint>(capped));
  clear_exception(env, "setPacketSizeHint");

  if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
}

void JavaTcpTransport::set_timeout_ms(int ms) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  timeout_ms_ = (ms <= 0) ? 1 : ms;
  if (!connected_ || set_timeout_method_ == nullptr) return;

  bool did_attach = false;
  JNIEnv* env = get_env(did_attach);
  if (env == nullptr) return;

  env->CallVoidMethod(transport_ref_, set_timeout_method_, static_cast<jint>(timeout_ms_));
  clear_exception(env, "setTimeoutMs");

  if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
}

int JavaTcpTransport::send(std::span<const std::uint8_t> data, unsigned retries) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return -1;

  bool did_attach = false;
  JNIEnv* env = get_env(did_attach);
  if (env == nullptr) return -1;

  const auto detach = [&] {
    if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
  };

  if (!ensure_array(env, send_buffer_, send_capacity_, data.size())) {
    detach();
    connected_ = false;
    return -1;
  }

  env->SetByteArrayRegion(send_buffer_, 0, static_cast<jsize>(data.size()),
                          reinterpret_cast<const jbyte*>(data.data()));
  if (env->ExceptionCheck()) {
    clear_exception(env, "SetByteArrayRegion(send)");
    detach();
    connected_ = false;
    return -1;
  }

  const jint rc = env->CallIntMethod(transport_ref_, send_method_, send_buffer_, static_cast<jint>(data.size()),
                                     static_cast<jint>(timeout_ms_), static_cast<jint>(retries));
  if (env->ExceptionCheck()) {
    clear_exception(env, "sendChunk");
    detach();
    connected_ = false;
    return -1;
  }

  detach();
  if (rc < 0) connected_ = false;
  return static_cast<int>(rc);
}

int JavaTcpTransport::recv(std::span<std::uint8_t> data, unsigned retries) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return -1;

  bool did_attach = false;
  JNIEnv* env = get_env(did_attach);
  if (env == nullptr) return -1;

  const auto detach = [&] {
    if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();
  };

  if (data.empty()) {
    const jint zlp_rc = env->CallIntMethod(transport_ref_, recv_zlp_method_, static_cast<jint>(retries));
    if (env->ExceptionCheck()) {
      clear_exception(env, "receiveZlp");
      detach();
      connected_ = false;
      return -1;
    }
    detach();
    if (zlp_rc < 0) connected_ = false;
    return static_cast<int>(zlp_rc);
  }

  if (!ensure_array(env, recv_buffer_, recv_capacity_, data.size())) {
    detach();
    connected_ = false;
    return -1;
  }

  const jint rc = env->CallIntMethod(transport_ref_, recv_method_, recv_buffer_, static_cast<jint>(data.size()),
                                     static_cast<jint>(timeout_ms_), static_cast<jint>(retries));
  if (env->ExceptionCheck()) {
    clear_exception(env, "receiveChunk");
    detach();
    connected_ = false;
    return -1;
  }

  if (rc > 0) {
    env->GetByteArrayRegion(recv_buffer_, 0, rc, reinterpret_cast<jbyte*>(data.data()));
    if (env->ExceptionCheck()) {
      clear_exception(env, "GetByteArrayRegion(recv)");
      detach();
      connected_ = false;
      return -1;
    }
  }

  detach();
  if (rc < 0) connected_ = false;
  return static_cast<int>(rc);
}

int JavaTcpTransport::recv_zlp(unsigned retries) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return -1;

  bool did_attach = false;
  JNIEnv* env = get_env(did_attach);
  if (env == nullptr) return -1;

  const jint rc = env->CallIntMethod(transport_ref_, recv_zlp_method_, static_cast<jint>(retries));
  if (env->ExceptionCheck()) {
    clear_exception(env, "receiveZlp");
    connected_ = false;
  }

  if (did_attach && vm_ != nullptr) vm_->DetachCurrentThread();

  if (rc < 0) connected_ = false;
  return static_cast<int>(rc);
}

} // namespace brokkr::android_platform
