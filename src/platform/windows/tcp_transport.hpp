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

#include <cstdint>
#include <string>

#include <windows.h>
#include <WinSock2.h>

namespace brokkr::windows {

class TcpConnection final : public brokkr::core::IByteTransport {
public:
    Kind kind() const noexcept override { return Kind::TcpStream; }

    TcpConnection() = default;
    explicit TcpConnection(int fd, std::string peer_ip, std::uint16_t peer_port);

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    TcpConnection(TcpConnection&&) noexcept;
    TcpConnection& operator=(TcpConnection&&) noexcept;

    ~TcpConnection();

    bool connected() const noexcept override;

    void set_timeout_ms(int ms) noexcept override;
    int timeout_ms() const noexcept override { return timeout_ms_; }

    int send(std::span<const std::uint8_t> data, unsigned retries = 8) override;
    int recv(std::span<std::uint8_t> data, unsigned retries = 8) override;

    int recv_zlp(unsigned /*retries*/ = 0) override { return 0; } // not used on tcp

    std::string peer_label() const;

private:
    void close_() noexcept;
    bool set_sock_timeouts_() noexcept;

private:
    SOCKET fd_ = INVALID_SOCKET;
    int timeout_ms_ = 1000;

    std::string peer_ip_;
    std::uint16_t peer_port_ = 0;
};

class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    void bind_and_listen(std::string bind_ip, std::uint16_t port, int backlog = 4);

    TcpConnection accept_one();

private:
    SOCKET fd_ = INVALID_SOCKET;
    std::string bind_ip_;
    std::uint16_t port_ = 0;
};

} // namespace brokkr::windows
