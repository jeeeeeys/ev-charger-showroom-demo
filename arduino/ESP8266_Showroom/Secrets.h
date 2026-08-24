#pragma once

// Local development copy. Replace the placeholder values before flashing.
// This file is ignored by Git so real credentials are not committed.
namespace secrets {

static constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
static constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
static constexpr char API_URL[] =
    "https://example.com/api/v1/chargers/EVSE-01/command";
static constexpr char API_BEARER_TOKEN[] = "YOUR_DEVICE_TOKEN";

}  // namespace secrets

