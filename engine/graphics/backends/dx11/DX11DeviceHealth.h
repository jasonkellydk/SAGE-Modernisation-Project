#pragma once

#include <dxgi.h>

namespace Graphics::DX11Detail {

// Device loss is reported by GPU operations (notably Present and ResizeBuffers).
// Readiness checks must not enter the driver on the successful rendering path.
class DeviceHealth final {
public:
    bool Removed() const noexcept { return m_removed; }

    template<class RemovalReason>
    bool Check(HRESULT result, RemovalReason&& removal_reason) noexcept {
        if (SUCCEEDED(result)) return true;
        if (!m_removed) {
            m_removed = result == DXGI_ERROR_DEVICE_REMOVED
                || result == DXGI_ERROR_DEVICE_RESET
                || result == DXGI_ERROR_DEVICE_HUNG
                || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR
                || FAILED(removal_reason());
        }
        return false;
    }

private:
    bool m_removed = false;
};

}
