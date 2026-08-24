#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecureBearSSL.h>

#include "ChargerProtocol.h"
#include "ProjectConfig.h"
#include "Secrets.h"

#include <string.h>

namespace {

// UART0 is reserved for machine-readable messages to the ATmega2560.
HardwareSerial& atmegaLink = Serial;

// UART1 is transmit-only on ESP8266 GPIO2 and is used only for optional debug.
HardwareSerial& debugPort = Serial1;

uint32_t lastWifiAttemptAtMs = 0;
uint32_t lastPollAtMs = 0;
bool pollImmediately = true;
bool wifiWasConnected = false;

void debugLine(const char* text) {
  if (config::ENABLE_ESP_DEBUG_UART) {
    debugPort.println(text);
  }
}

void debugHttpCode(int code) {
  if (config::ENABLE_ESP_DEBUG_UART) {
    debugPort.print(F("HTTP status: "));
    debugPort.println(code);
  }
}

bool copyJsonString(JsonVariantConst value, char* destination, size_t size) {
  if (!value.is<const char*>()) {
    return false;
  }

  const char* text = value.as<const char*>();
  if (text == nullptr) {
    return false;
  }

  const size_t length = strlen(text);
  if (length == 0 || length >= size) {
    return false;
  }

  memcpy(destination, text, length + 1);
  return true;
}

charger::ValidationError parseApiResponse(const String& payload,
                                          charger::Command& command) {
  StaticJsonDocument<512> document;
  const DeserializationError jsonError = deserializeJson(document, payload);
  if (jsonError) {
    return charger::ValidationError::INVALID_FORMAT;
  }

  charger::Command parsed = {};

  if (!copyJsonString(document["charger_id"], parsed.chargerId,
                      sizeof(parsed.chargerId))) {
    return charger::ValidationError::INVALID_CHARGER_ID;
  }

  JsonVariantConst commandId = document["command_id"];
  if (!commandId.is<unsigned long>()) {
    return charger::ValidationError::INVALID_COMMAND_ID;
  }
  parsed.commandId = static_cast<uint32_t>(commandId.as<unsigned long>());

  char targetState[16] = {};
  if (!copyJsonString(document["target_state"], targetState,
                      sizeof(targetState)) ||
      !charger::parseTargetState(targetState, parsed.targetState)) {
    return charger::ValidationError::INVALID_TARGET_STATE;
  }

  JsonVariantConst powerLimit = document["power_limit_w"];
  if (!powerLimit.is<unsigned long>()) {
    return charger::ValidationError::INVALID_POWER_LIMIT;
  }
  parsed.powerLimitW = static_cast<uint32_t>(powerLimit.as<unsigned long>());

  if (!copyJsonString(document["timestamp"], parsed.timestamp,
                      sizeof(parsed.timestamp))) {
    return charger::ValidationError::INVALID_TIMESTAMP;
  }

  const charger::ValidationError validation = charger::validateCommand(
      parsed, config::CHARGER_ID, config::LIMITED_POWER_W,
      config::FULL_POWER_W);
  if (validation != charger::ValidationError::NONE) {
    return validation;
  }

  command = parsed;
  return charger::ValidationError::NONE;
}

bool waitForAtmegaResponse(uint32_t expectedCommandId) {
  char response[64] = {};
  size_t length = 0;
  const uint32_t startedAt = millis();

  while (static_cast<uint32_t>(millis() - startedAt) <
         config::ATMEGA_ACK_TIMEOUT_MS) {
    while (atmegaLink.available() > 0) {
      const char incoming = static_cast<char>(atmegaLink.read());
      if (incoming == '\n') {
        response[length] = '\0';
        if (config::ENABLE_ESP_DEBUG_UART) {
          debugPort.print(F("ATmega response: "));
          debugPort.println(response);
        }

        char expected[32] = {};
        snprintf(expected, sizeof(expected), "ACK|%lu",
                 static_cast<unsigned long>(expectedCommandId));
        return strcmp(response, expected) == 0;
      }

      if (incoming != '\r' && length < sizeof(response) - 1) {
        response[length++] = incoming;
      }
    }
    delay(1);
  }

  debugLine("ATmega acknowledgement timeout");
  return false;
}

bool forwardCommandToAtmega(const charger::Command& command) {
  char line[charger::MAX_UART_LINE_LENGTH] = {};
  const int written = snprintf(
      line, sizeof(line), "CMD|%s|%lu|%s|%lu|%s", command.chargerId,
      static_cast<unsigned long>(command.commandId),
      charger::targetStateToString(command.targetState),
      static_cast<unsigned long>(command.powerLimitW), command.timestamp);

  if (written <= 0 || static_cast<size_t>(written) >= sizeof(line)) {
    debugLine("UART command formatting failed");
    return false;
  }

  atmegaLink.println(line);
  atmegaLink.flush();
  return waitForAtmegaResponse(command.commandId);
}

template <typename ClientType>
void executeApiRequest(ClientType& client) {
  HTTPClient http;
  http.setTimeout(config::HTTP_TIMEOUT_MS);

  if (!http.begin(client, secrets::API_URL)) {
    debugLine("Could not begin HTTP request");
    return;
  }

  http.addHeader("Accept", "application/json");
  if (strlen(secrets::API_KEY) > 0) {
    http.addHeader("X-API-Key", secrets::API_KEY);
  }

  const int statusCode = http.GET();
  debugHttpCode(statusCode);

  if (statusCode != HTTP_CODE_OK) {
    http.end();
    return;
  }

  const int responseSize = http.getSize();
  if (responseSize > 1024) {
    debugLine("API response is larger than the MVP limit");
    http.end();
    return;
  }

  const String payload = http.getString();
  http.end();

  charger::Command command = {};
  const charger::ValidationError validation =
      parseApiResponse(payload, command);
  if (validation != charger::ValidationError::NONE) {
    if (config::ENABLE_ESP_DEBUG_UART) {
      debugPort.print(F("Rejected API response: "));
      debugPort.println(charger::validationErrorToString(validation));
    }
    return;
  }

  if (config::ENABLE_ESP_DEBUG_UART) {
    debugPort.print(F("Valid command received: "));
    debugPort.println(command.commandId);
  }
  forwardCommandToAtmega(command);
}

void fetchLatestCommand() {
  const String url(secrets::API_URL);
  if (url.startsWith("https://")) {
    BearSSL::WiFiClientSecure secureClient;
    if (!config::ALLOW_INSECURE_HTTPS_FOR_DEMO) {
      debugLine("Secure CA validation is not configured");
      return;
    }
    secureClient.setInsecure();
    executeApiRequest(secureClient);
    return;
  }

  WiFiClient client;
  executeApiRequest(client);
}

void startWifiConnection() {
  lastWifiAttemptAtMs = millis();
  debugLine("Connecting to Wi-Fi...");
  WiFi.disconnect();
  WiFi.begin(secrets::WIFI_SSID, secrets::WIFI_PASSWORD);
}

void maintainWifi() {
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      pollImmediately = true;
      if (config::ENABLE_ESP_DEBUG_UART) {
        debugPort.print(F("Wi-Fi connected, IP: "));
        debugPort.println(WiFi.localIP());
      }
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    debugLine("Wi-Fi disconnected");
  }

  if (static_cast<uint32_t>(millis() - lastWifiAttemptAtMs) >=
      config::WIFI_RETRY_INTERVAL_MS) {
    startWifiConnection();
  }
}

}  // namespace

void setup() {
  atmegaLink.begin(config::UART_BAUD);
  if (config::ENABLE_ESP_DEBUG_UART) {
    debugPort.begin(config::DEBUG_BAUD);
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  debugLine("\nEV charger showroom Wi-Fi client");
  startWifiConnection();
}

void loop() {
  maintainWifi();

  if (WiFi.status() != WL_CONNECTED) {
    delay(1);
    return;
  }

  if (pollImmediately ||
      static_cast<uint32_t>(millis() - lastPollAtMs) >=
          config::API_POLL_INTERVAL_MS) {
    pollImmediately = false;
    lastPollAtMs = millis();
    fetchLatestCommand();
  }

  delay(1);
}
