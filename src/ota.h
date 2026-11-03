#pragma once

// Over-the-air update from the latest GitHub Release (tools: .github/workflows/release.yml).
namespace ota {

// Installs the latest release if its tag differs from FW_VERSION, then reboots.
// Does nothing in local ("dev") or CI test builds, so a USB-flashed device is never overwritten.
void check();

}  // namespace ota
