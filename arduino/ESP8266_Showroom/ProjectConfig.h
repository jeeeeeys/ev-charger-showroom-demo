#pragma once

#include <stdint.h>

namespace config {

// This value must match the charger_id returned by the API.
static constexpr char CHARGER_ID[] = "EVSE-01";

static constexpr uint32_t LIMITED_POWER_W = 5000;
static constexpr uint32_t FULL_POWER_W = 7000;

static constexpr uint32_t UART_BAUD = 115200;
static constexpr uint32_t DEBUG_BAUD = 115200;

// Keep disabled on the installed PCB. ESP UART0 is used by the ATmega link,
// while UART1 TX would appear on GPIO2, which is also a boot-strap pin.
static constexpr bool ENABLE_ESP_DEBUG_UART = false;

static constexpr uint32_t API_POLL_INTERVAL_MS = 5000;
static constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
static constexpr uint32_t ATMEGA_ACK_TIMEOUT_MS = 300;

// The ATmega returns to STANDBY if no valid API command reaches it in this time.
static constexpr uint32_t COMMAND_TIMEOUT_MS = 60000;

// Showroom-only convenience. Replace with CA validation before real deployment.
static constexpr bool ALLOW_INSECURE_HTTPS_FOR_DEMO = true;

}  // namespace config
