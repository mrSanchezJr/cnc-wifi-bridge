#pragma once

#include <Arduino.h>

// =================================================================
// НАСТРОЙКИ СЕТИ WI-FI
// =================================================================
// Данные вашей домашней / рабочей Wi-Fi сети:
#define WIFI_SSID               "kyvartirad34"
#define WIFI_PASSWORD           "pa33-3e7"

// Резервная точка доступа (AP), если роутер недоступен:
#define AP_SSID                 "Inectra-CNC-Bridge"
#define AP_PASSWORD             "12345678"
#define WIFI_CONNECT_TIMEOUT    15000  // Тайм-аут подключения к роутеру (мс)

// Настройка статического IP (опционально):
//#define USE_STATIC_IP
#ifdef USE_STATIC_IP
    #define STATIC_IP           192, 168, 1, 150
    #define STATIC_GATEWAY      192, 168, 1, 1
    #define STATIC_SUBNET       255, 255, 255, 0
    #define STATIC_DNS          192, 168, 1, 1
#endif

// =================================================================
// НАСТРОЙКИ TCP СЕРВЕРА
// =================================================================
// TCP порт, к которому подключается C# приложение (G-write / gpellers)
#define TCP_PORT                8088

// =================================================================
// НАСТРОЙКИ UART (ПОДКЛЮЧЕНИЕ К ЧПУ INECTRA MSC-3US / GRBL)
// =================================================================
// Скорость порта контроллера ЧПУ (по умолчанию 115200)
#define UART_BAUD_RATE          115200

// -----------------------------------------------------------------
// НАСТРОЙКИ ДЛЯ ESP32
// -----------------------------------------------------------------
#if defined(ESP32)
    // 1 = Использовать Hardware Serial2 (ESP32 UART2, GPIO 16 RX2, GPIO 17 TX2).
    //     При этом USB Serial0 остается свободным для отладки и логов.
    #define USE_HARDWARE_UART2  1
    #define INECTRA_RX_PIN      16
    #define INECTRA_TX_PIN      17

    #define UART_RX_BUFFER_SIZE 2048
    #define BRIDGE_BUFFER_SIZE  2048

    // Вывод отладочных сообщений в USB Serial0
    #define DEBUG_TO_SERIAL     true
    #define DEBUG_DATA_TRAFFIC  false

// -----------------------------------------------------------------
// НАСТРОЙКИ ДЛЯ ESP-01 / ESP-01S (ESP8266)
// -----------------------------------------------------------------
#elif defined(ESP8266)
    // На ESP-01 единственный полнодуплексный аппаратный UART - это Serial (UART0):
    // TX = GPIO1, RX = GPIO3. Именно они подключаются к контроллеру Inectra!
    //
    // ВАЖНО: Во время работы моста отладочный вывод в Serial ОТКЛЮЧЕН,
    // чтобы контроллер Inectra не получал текстовые логи за команды G-кода!
    //
    // Если вам требуется аппаратная отладка, включите DEBUG_TO_SERIAL1:
    // логи будут выводиться в Serial1 (только TX на пине GPIO2).
    #define DEBUG_TO_SERIAL1    false
    #define DEBUG_BAUD_RATE     115200

    #define UART_RX_BUFFER_SIZE 1024
    #define BRIDGE_BUFFER_SIZE  1024
    #define DEBUG_DATA_TRAFFIC  false
#endif

// =================================================================
// ОБНОВЛЕНИЕ ПО ВОЗДУХУ (ArduinoOTA)
// =================================================================
#define ENABLE_OTA              true
#if defined(ESP32)
    #define OTA_HOSTNAME        "inectra-esp32"
#else
    #define OTA_HOSTNAME        "inectra-esp01"
#endif
#define OTA_PASSWORD            "admin"
