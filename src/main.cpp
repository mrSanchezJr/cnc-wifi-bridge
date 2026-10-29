#include <Arduino.h>

#if defined(ESP32)
    #include <WiFi.h>
    #include <WiFiClient.h>
#elif defined(ESP8266)
    #include <ESP8266WiFi.h>
#else
    #error "Unsupported platform! Please use ESP32 or ESP8266 (ESP-01)."
#endif

#include "config.h"

#if ENABLE_OTA
    #include <ArduinoOTA.h>
#endif

// =================================================================
// НАСТРОЙКА АППАРАТНОГО UART ДЛЯ СТАНКА
// =================================================================
#if defined(ESP32)
    #if USE_HARDWARE_UART2
        HardwareSerial CncSerial(2);
        #define CNC_UART CncSerial
        #define DEBUG_OUT Serial
        #define HAS_DEBUG (DEBUG_TO_SERIAL && USE_HARDWARE_UART2)
    #else
        #define CNC_UART Serial
        #define HAS_DEBUG 0
    #endif
#elif defined(ESP8266)
    // На ESP-01 рабочий порт станка - Serial (GPIO1 TX, GPIO3 RX)
    #define CNC_UART Serial
    #if DEBUG_TO_SERIAL1
        #define DEBUG_OUT Serial1
        #define HAS_DEBUG 1
    #else
        #define HAS_DEBUG 0
    #endif
#endif

// Макросы безопасного отладочного вывода
#if HAS_DEBUG
    #define LOG_PRINT(x)        DEBUG_OUT.print(x)
    #define LOG_PRINTLN(x)      DEBUG_OUT.println(x)
    #define LOG_PRINTF(...)     DEBUG_OUT.printf(__VA_ARGS__)
#else
    #define LOG_PRINT(x)        ((void)0)
    #define LOG_PRINTLN(x)      ((void)0)
    #define LOG_PRINTF(...)     ((void)0)
#endif

// Глобальные объекты
WiFiServer server(TCP_PORT);
WiFiClient client;
uint8_t bridgeBuffer[BRIDGE_BUFFER_SIZE];
bool wasClientConnected = false;

void logTime() {
#if HAS_DEBUG
    unsigned long sec = millis() / 1000;
    unsigned long min = sec / 60;
    sec %= 60;
    LOG_PRINTF("[%02lu:%02lu] ", min, sec);
#endif
}

void setupOTA() {
#if ENABLE_OTA
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        logTime();
        LOG_PRINTLN(F("[OTA] Update started..."));
        if (client && client.connected()) {
            client.stop();
        }
    });

    ArduinoOTA.onEnd([]() {
        logTime();
        LOG_PRINTLN(F("\n[OTA] Update finished successfully! Rebooting..."));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        logTime();
        LOG_PRINTF("[OTA] Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) LOG_PRINTLN(F("Auth Failed"));
        else if (error == OTA_BEGIN_ERROR) LOG_PRINTLN(F("Begin Failed"));
        else if (error == OTA_CONNECT_ERROR) LOG_PRINTLN(F("Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR) LOG_PRINTLN(F("Receive Failed"));
        else if (error == OTA_END_ERROR) LOG_PRINTLN(F("End Failed"));
    });

    ArduinoOTA.begin();
    logTime();
    LOG_PRINTF("[OTA] Ready. Hostname: %s\n", OTA_HOSTNAME);
#endif
}

void connectWiFi() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);

#ifdef USE_STATIC_IP
    IPAddress ip(STATIC_IP);
    IPAddress gateway(STATIC_GATEWAY);
    IPAddress subnet(STATIC_SUBNET);
    IPAddress dns(STATIC_DNS);
    WiFi.config(ip, gateway, subnet, dns);
#endif

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    logTime();
    LOG_PRINTF("[WiFi] Connecting to SSID: '%s'", WIFI_SSID);

    unsigned long startAttemptTime = millis();
    unsigned long lastDot = 0;

    // Ждем подключения до истечения тайм-аута
    while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime < WIFI_CONNECT_TIMEOUT)) {
        delay(100);
        yield();
#if HAS_DEBUG
        if (millis() - lastDot > 500) {
            lastDot = millis();
            LOG_PRINT(F("."));
        }
#endif
    }

#if HAS_DEBUG
    LOG_PRINTLN();
#endif

    if (WiFi.status() == WL_CONNECTED) {
        logTime();
        LOG_PRINTLN(F("[WiFi] CONNECTED SUCCESSFULLY!"));
        logTime();
        LOG_PRINTF("[WiFi] IP Address : %s\n", WiFi.localIP().toString().c_str());
        logTime();
        LOG_PRINTF("[WiFi] Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
        logTime();
        LOG_PRINTF("[WiFi] Gateway    : %s\n", WiFi.gatewayIP().toString().c_str());
        logTime();
        LOG_PRINTF("[WiFi] Signal RSSI: %d dBm\n", WiFi.RSSI());
        logTime();
        LOG_PRINTF("[WiFi] MAC Address: %s\n", WiFi.macAddress().c_str());
    } else {
        // Резервная точка доступа
        logTime();
        LOG_PRINTLN(F("[WiFi] Could not connect to router! Starting Fallback AP..."));
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);

        logTime();
        LOG_PRINTLN(F("[WiFi] Access Point started!"));
        logTime();
        LOG_PRINTF("[WiFi] AP SSID    : %s\n", AP_SSID);
        logTime();
        LOG_PRINTF("[WiFi] AP Password: %s\n", AP_PASSWORD);
        logTime();
        LOG_PRINTF("[WiFi] AP IP      : %s\n", WiFi.softAPIP().toString().c_str());
    }
}

void setup() {
#if HAS_DEBUG
    #if defined(ESP32)
        DEBUG_OUT.begin(115200);
    #elif defined(ESP8266)
        DEBUG_OUT.begin(DEBUG_BAUD_RATE);
    #endif
    delay(200);
    LOG_PRINTLN();
    LOG_PRINTLN(F("=================================================="));
    #if defined(ESP32)
        LOG_PRINTLN(F("   Inectra MSC-3US WiFi Serial Bridge (ESP32)     "));
    #else
        LOG_PRINTLN(F("   Inectra MSC-3US WiFi Serial Bridge (ESP-01)    "));
    #endif
    LOG_PRINTLN(F("=================================================="));
    logTime();
    #if defined(ESP32)
        LOG_PRINTF("[SYSTEM] Chip Model   : %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
        LOG_PRINTF("[SYSTEM] CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
        LOG_PRINTF("[SYSTEM] Flash Size   : %d KB\n", ESP.getFlashChipSize() / 1024);
        LOG_PRINTF("[SYSTEM] Free Heap    : %d bytes\n", ESP.getFreeHeap());
        LOG_PRINTF("[UART2] RX Pin: %d, TX Pin: %d, Baud: %d\n", INECTRA_RX_PIN, INECTRA_TX_PIN, UART_BAUD_RATE);
    #elif defined(ESP8266)
        LOG_PRINTF("[SYSTEM] Chip ID      : 0x%08X\n", ESP.getChipId());
        LOG_PRINTF("[SYSTEM] CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
        LOG_PRINTF("[SYSTEM] Flash Size   : %d KB\n", ESP.getFlashChipRealSize() / 1024);
        LOG_PRINTF("[SYSTEM] Free Heap    : %d bytes\n", ESP.getFreeHeap());
        LOG_PRINTF("[UART0] Serial (GPIO1 TX, GPIO3 RX), Baud: %d\n", UART_BAUD_RATE);
    #endif
#endif

    // Инициализация порта ЧПУ
#if defined(ESP32)
    #if USE_HARDWARE_UART2
        CncSerial.setRxBufferSize(UART_RX_BUFFER_SIZE);
        CncSerial.begin(UART_BAUD_RATE, SERIAL_8N1, INECTRA_RX_PIN, INECTRA_TX_PIN);
    #else
        CNC_UART.setRxBufferSize(UART_RX_BUFFER_SIZE);
        CNC_UART.begin(UART_BAUD_RATE);
    #endif
#elif defined(ESP8266)
    CNC_UART.setRxBufferSize(UART_RX_BUFFER_SIZE);
    CNC_UART.begin(UART_BAUD_RATE);
#endif

    connectWiFi();
    setupOTA();

    // Запуск TCP сервера
    server.begin();
    server.setNoDelay(true);

    logTime();
    LOG_PRINTF("[TCP] Server listening on port: %d\n", TCP_PORT);
    LOG_PRINTLN(F("--------------------------------------------------"));
    logTime();
    LOG_PRINTLN(F("[READY] Bridge is active. Waiting for TCP client..."));
    LOG_PRINTLN(F("--------------------------------------------------"));
}

void loop() {
#if ENABLE_OTA
    ArduinoOTA.handle();
#endif

    // Проверка новых входящих TCP подключений
    if (server.hasClient()) {
        WiFiClient newClient = server.accept();
        newClient.setNoDelay(true);

        if (client && client.connected()) {
            client.stop();
            logTime();
            LOG_PRINTF("[TCP] Client replaced by %s:%d\n", 
                       newClient.remoteIP().toString().c_str(), 
                       newClient.remotePort());
        } else {
            logTime();
            LOG_PRINTF("[TCP] Client connected from %s:%d\n", 
                       newClient.remoteIP().toString().c_str(), 
                       newClient.remotePort());
        }
        client = newClient;
        wasClientConnected = true;
    }

    // Проверяем отключение клиента
    if (wasClientConnected && (!client || !client.connected())) {
        wasClientConnected = false;
        logTime();
        LOG_PRINTLN(F("[TCP] Client disconnected."));
    }

    // Если клиент активен - пересылаем данные
    if (client && client.connected()) {
        // 1. TCP (программа на ПК) -> UART (контроллер Inectra)
        size_t availableTCP = client.available();
        if (availableTCP > 0) {
            size_t bytesToRead = (availableTCP < sizeof(bridgeBuffer)) ? availableTCP : sizeof(bridgeBuffer);
            size_t bytesRead = client.read(bridgeBuffer, bytesToRead);
            if (bytesRead > 0) {
                CNC_UART.write(bridgeBuffer, bytesRead);
#if (DEBUG_DATA_TRAFFIC && HAS_DEBUG)
                logTime();
                LOG_PRINTF("[TCP -> CNC] (%u bytes)\n", bytesRead);
#endif
            }
        }

        // 2. UART (контроллер Inectra) -> TCP (программа на ПК)
        size_t availableSerial = CNC_UART.available();
        if (availableSerial > 0) {
            size_t bytesToRead = (availableSerial < sizeof(bridgeBuffer)) ? availableSerial : sizeof(bridgeBuffer);
            size_t bytesRead = CNC_UART.readBytes(bridgeBuffer, bytesToRead);
            if (bytesRead > 0) {
                client.write(bridgeBuffer, bytesRead);
                client.flush(); // Отправляем немедленно для минимальной задержки статуса ЧПУ
#if (DEBUG_DATA_TRAFFIC && HAS_DEBUG)
                logTime();
                LOG_PRINTF("[CNC -> TCP] (%u bytes)\n", bytesRead);
#endif
            }
        }
    } else {
        // Очищаем буфер UART, пока никто не подключен по TCP
        while (CNC_UART.available()) {
            CNC_UART.read();
        }
    }

    yield(); // Передаем управление системному стеку Wi-Fi и WDT
}
