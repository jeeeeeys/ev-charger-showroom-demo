#pragma once

namespace secrets {

static constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
static constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

// Replace this once the software team provides the endpoint.
static constexpr char API_URL[] =
    "https://example.com/api/v1/chargers/EVSE-01/command";

// Leave empty only when the mock endpoint does not require authentication.
static constexpr char API_KEY[] = "YOUR_API_KEY";

}  // namespace secrets
