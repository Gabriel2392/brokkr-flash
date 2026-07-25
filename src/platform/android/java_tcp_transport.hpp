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

#pragma once

#include "core/byte_transport.hpp"

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>

namespace brokkr::android_platform {

class JavaTcpTransport final : public brokkr::core::IByteTransport {
 public:
  JavaTcpTransport(JavaVM* vm, JNIEnv* env, jobject transport_object);
  ~JavaTcpTransport() override;

  JavaTcpTransport(const JavaTcpTransport&) = delete;
  JavaTcpTransport& operator=(const JavaTcpTransport&) = delete;

  Kind kind() const noexcept override { return Kind::TcpStream; }
  bool connected() const noexcept override;

  void set_packet_size_hint(std::size_t bytes) noexcept override;
  void set_timeout_ms(int ms) noexcept override;
  int timeout_ms() const noexcept override { return timeout_ms_; }

  int send(std::span<const std::uint8_t> data, unsigned retries = 8) override;
  int recv(std::span<std::uint8_t> data, unsigned retries = 8) override;
  int recv_zlp(unsigned retries = 0) override;

  const std::string& id() const noexcept { return id_; }

 private:
  JNIEnv* get_env(bool& did_attach) const noexcept;
  bool ensure_array(JNIEnv* env, jbyteArray& array_ref, std::size_t& capacity, std::size_t want);
  void close_java_transport() noexcept;
  void clear_exception(JNIEnv* env, const char* context) const noexcept;

  JavaVM* vm_ = nullptr;
  jobject transport_ref_ = nullptr;
  jclass transport_class_ = nullptr;

  jmethodID get_id_method_ = nullptr;
  jmethodID is_connected_method_ = nullptr;
  jmethodID send_method_ = nullptr;
  jmethodID recv_method_ = nullptr;
  jmethodID recv_zlp_method_ = nullptr;
  jmethodID set_packet_size_hint_method_ = nullptr;
  jmethodID set_timeout_method_ = nullptr;
  jmethodID close_method_ = nullptr;

  jbyteArray send_buffer_ = nullptr;
  std::size_t send_capacity_ = 0;
  jbyteArray recv_buffer_ = nullptr;
  std::size_t recv_capacity_ = 0;

  mutable std::mutex mutex_;
  std::string id_;
  int timeout_ms_ = 200;
  mutable bool connected_ = true;
};

} // namespace brokkr::android_platform
