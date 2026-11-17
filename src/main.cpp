#include <Arduino.h>

#if defined(ESP32)
    #include <WiFi.h>
    #include <WiFiClient.h>
    #include <WebServer.h>
    #include <Update.h>
    using HttpServerType = WebServer;
#elif defined(ESP8266)
    #include <ESP8266WiFi.h>
    #include <ESP8266WebServer.h>
    #include <Updater.h>
    using HttpServerType = ESP8266WebServer;
#else
    #error "Unsupported platform!"
#endif

#include "config.h"

// Минималистичный интерфейс обновления (хранится во Flash)
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OTA</title>
<style>
body{background:#111;color:#eee;font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}
.box{background:#222;padding:24px;border-radius:8px;width:300px;text-align:center}
input[type=file]{margin-bottom:16px;width:100%;color:#aaa}
button{background:#007acc;color:#fff;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;width:100%;font-size:14px}
button:disabled{background:#444;cursor:not-allowed}
.bar{background:#333;height:8px;border-radius:4px;margin-top:16px;display:none;overflow:hidden}
.fill{background:#007acc;height:100%;width:0%}
#status{margin-top:12px;font-size:13px;color:#aaa}
</style>
</head>
<body>
<div class="box">
  <input type="file" id="f" accept=".bin" onchange="document.getElementById('b').disabled=!this.files.length">
  <button id="b" onclick="up()" disabled>Обновить</button>
  <div class="bar" id="pb"><div class="fill" id="pf"></div></div>
  <div id="status"></div>
</div>
<script>
function up(){
  var f=document.getElementById('f').files[0];
  if(!f)return;
  var b=document.getElementById('b'),pb=document.getElementById('pb'),pf=document.getElementById('pf'),s=document.getElementById('status');
  b.disabled=true;pb.style.display='block';s.innerText='0%';
  var x=new XMLHttpRequest();
  x.open('POST','/update',true);
  x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round((e.loaded/e.total)*100);pf.style.width=p+'%';s.innerText=p+'%';}};
  x.onload=function(){
    if(x.status===200){
      s.innerText='OK. Перезагрузка...';
      setTimeout(function(){location.reload();},8000);
    }else{
      s.innerText='Ошибка';b.disabled=false;
    }
  };
  x.onerror=function(){s.innerText='Ошибка';b.disabled=false;};
  var d=new FormData();d.append('u',f);x.send(d);
}
</script>
</body>
</html>)rawliteral";

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

WiFiServer tcpServer(TCP_PORT);
WiFiClient tcpClient;
HttpServerType httpServer(HTTP_PORT);

uint8_t bridgeBuffer[BRIDGE_BUFFER_SIZE];

void setup() {
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

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);

    IPAddress ip(AP_IP);
    IPAddress gateway(AP_GATEWAY);
    IPAddress subnet(AP_SUBNET);
    WiFi.softAPConfig(ip, gateway, subnet);
    WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL);

    tcpServer.begin();
    tcpServer.setNoDelay(true);

    httpServer.on("/", HTTP_GET, []() {
        httpServer.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });

    httpServer.on("/update", HTTP_GET, []() {
        httpServer.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });

    httpServer.on("/update", HTTP_POST, []() {
        bool ok = !Update.hasError();
        httpServer.sendHeader("Connection", "close");
        httpServer.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "FAIL");
        if (ok) {
            delay(200);
            ESP.restart();
        }
    }, []() {
        HTTPUpload& upload = httpServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
#if defined(ESP8266)
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            Update.begin(maxSketchSpace, U_FLASH);
#elif defined(ESP32)
            Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
#endif
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (!Update.hasError()) {
                Update.write(upload.buf, upload.currentSize);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (!Update.hasError()) {
                Update.end(true);
            }
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            Update.end();
        }
        yield();
    });

    httpServer.onNotFound([]() {
        httpServer.sendHeader("Location", "/");
        httpServer.send(302, "text/plain", "");
    });

    httpServer.begin();
}

void loop() {
    httpServer.handleClient();

    if (tcpServer.hasClient()) {
        WiFiClient newClient = tcpServer.accept();
        newClient.setNoDelay(true);

        if (tcpClient && tcpClient.connected()) {
            tcpClient.stop();
        }
        tcpClient = newClient;
    }

    if (tcpClient && tcpClient.connected()) {
        size_t availableTCP = tcpClient.available();
        if (availableTCP > 0) {
            size_t bytesToRead = (availableTCP < sizeof(bridgeBuffer)) ? availableTCP : sizeof(bridgeBuffer);
            size_t bytesRead = tcpClient.read(bridgeBuffer, bytesToRead);
            if (bytesRead > 0) {
                CNC_UART.write(bridgeBuffer, bytesRead);
            }
        }

        size_t availableSerial = CNC_UART.available();
        if (availableSerial > 0) {
            size_t bytesToRead = (availableSerial < sizeof(bridgeBuffer)) ? availableSerial : sizeof(bridgeBuffer);
            size_t bytesRead = CNC_UART.readBytes(bridgeBuffer, bytesToRead);
            if (bytesRead > 0) {
                tcpClient.write(bridgeBuffer, bytesRead);
                tcpClient.flush();
            }
        }
    } else {
        while (CNC_UART.available()) {
            CNC_UART.read();
        }
    }

    yield();
}
