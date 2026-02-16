#if defined(BROKKR_PLATFORM_LINUX)
#include "platform/linux/signal_shield.hpp"
#include "platform/linux/single_instance.hpp"
#include "platform/linux/sysfs_usb.hpp"
#include "platform/linux/tcp_transport.hpp"
#include "platform/linux/usbfs_device.hpp"
#include "platform/linux/usbfs_conn.hpp"
namespace brokkr::platform {
	using namespace linux;
}

#elif defined(BROKKR_PLATFORM_WINDOWS)
#include "platform/windows/signal_shield.hpp"
#include "platform/windows/single_instance.hpp"
#include "platform/windows/sysfs_usb.hpp"
#include "platform/windows/tcp_transport.hpp"
#include "platform/windows/usbfs_device.hpp"
#include "platform/windows/usbfs_conn.hpp"

namespace brokkr::platform {
	using namespace windows;
}
#else
#error "Unsupported platform"
#endif