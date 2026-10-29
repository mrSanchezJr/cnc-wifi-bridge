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

// Настройка аппаратного UART контроллера ЧПУ
#if defined(ESP32)
    #if USE_HARDWARE_UART2
        HardwareSerial CncSerial(2);
        #define CNC_UART CncSerial
    #else
        #define CNC_UART Serial
    #endif
#elif defined(ESP8266)
    #define CNC_UART Serial
#endif

WiFiServer server(TCP_PORT);
WiFiClient client;
uint8_t bridgeBuffer[BRIDGE_BUFFER_SIZE];

void setup() {
    // 1. Инициализация аппаратного UART для контроллера ЧПУ
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

    // 2. Настройка Wi-Fi: строго режим автономной точки доступа (AP)
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);

    IPAddress ip(AP_IP);
    IPAddress gateway(AP_GATEWAY);
    IPAddress subnet(AP_SUBNET);
    WiFi.softAPConfig(ip, gateway, subnet);

    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);

    // 3. Запуск TCP сервера с минимальной задержкой
    server.begin();
    server.setNoDelay(true);
}

void loop() {
    // 1. Проверка новых входящих TCP подключений
    if (server.hasClient()) {
        WiFiClient newClient = server.accept();
        newClient.setNoDelay(true);

        // Если уже было соединение, замещаем старое новым (защита от зависших сессий)
        if (client && client.connected()) {
            client.stop();
        }
        client = newClient;
    }

    // 2. Двунаправленная пересылка данных при активном клиенте
    if (client && client.connected()) {
        // TCP (программа на ПК) -> UART (контроллер Inectra)
        size_t availableTCP = client.available();
        if (availableTCP > 0) {
            size_t bytesToRead = (availableTCP < sizeof(bridgeBuffer)) ? availableTCP : sizeof(bridgeBuffer);
            size_t bytesRead = client.read(bridgeBuffer, bytesToRead);
            if (bytesRead > 0) {
                CNC_UART.write(bridgeBuffer, bytesRead);
            }
        }

        // UART (контроллер Inectra) -> TCP (программа на ПК)
        size_t availableSerial = CNC_UART.available();
        if (availableSerial > 0) {
            size_t bytesToRead = (availableSerial < sizeof(bridgeBuffer)) ? availableSerial : sizeof(bridgeBuffer);
            size_t bytesRead = CNC_UART.readBytes(bridgeBuffer, bytesToRead);
            if (bytesRead > 0) {
                client.write(bridgeBuffer, bytesRead);
                client.flush(); // Немедленная отправка для минимального пинга
            }
        }
    } else {
        // Очищаем входной буфер UART, пока никто не подключен по TCP
        while (CNC_UART.available()) {
            CNC_UART.read();
        }
    }

    yield(); // Передача управления системному фоновому стеку Wi-Fi
}
