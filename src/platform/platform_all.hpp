#if defined(BROKKR_PLATFORM_LINUX)
#include "platform/posix-common/signal_shield.hpp"
#include "platform/posix-common/single_instance.hpp"
#include "platform/posix-common/tcp_transport.hpp"
#include "platform/linux/sysfs_usb.hpp"
#include "platform/linux/usbfs_conn.hpp"
#include "platform/linux/usbfs_device.hpp"
namespace brokkr::platform {
using namespace linux;
using namespace posix_common;
}

#elif defined(BROKKR_PLATFORM_WINDOWS)
#include "platform/windows/signal_shield.hpp"
#include "platform/windows/single_instance.hpp"
#include "platform/windows/sysfs_usb.hpp"
#include "platform/windows/tcp_transport.hpp"
#include "platform/windows/usbfs_conn.hpp"
#include "platform/windows/usbfs_device.hpp"

namespace brokkr::platform {
using namespace windows;
}

#elif defined(BROKKR_PLATFORM_MACOS)
#include "platform/posix-common/signal_shield.hpp"
#include "platform/posix-common/single_instance.hpp"
#include "platform/posix-common/tcp_transport.hpp"
#include "platform/macos/sysfs_usb.hpp"
#include "platform/macos/usbfs_conn.hpp"
#include "platform/macos/usbfs_device.hpp"

namespace brokkr::platform {
using namespace macos;
using namespace posix_common;
}
#else
#error "Unsupported platform"
#endif