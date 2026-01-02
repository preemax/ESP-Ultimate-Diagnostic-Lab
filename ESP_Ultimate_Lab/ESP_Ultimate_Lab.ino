/*
 * ------------------------------------------------------------------------
 * PROJECT: ESP ULTIMATE DIAGNOSTIC LAB
 * VERSION: 3.1 (Gold Edition - All Features Included)
 * AUTHOR: Reza Nazari
 * ------------------------------------------------------------------------
 * ⚠️ نکته حیاتی برای آپلود (Partition Scheme):
 * به دلیل وجود همزمان بلوتوث و وای‌فای، حجم برنامه زیاد است.
 * در Arduino IDE حتماً مسیر زیر را تنظیم کنید:
 * Tools > Partition Scheme > "Huge APP (3MB No OTA/1MB SPIFFS)"
 * * اگر گزینه Huge APP را ندارید، گزینه "No OTA (Large APP)" را بزنید.
 * ------------------------------------------------------------------------
 */

#ifdef ESP32
  #include <WiFi.h>
  #include <WebServer.h>
  #include <BLEDevice.h>
  #include <BLEUtils.h>
  #include <BLEScan.h>
  #include <BLEAdvertisedDevice.h>
  #include "esp_chip_info.h"
  #include "esp_flash.h"
  #include <LittleFS.h> 
  #include <Wire.h>
  #include <EEPROM.h>
  WebServer server(80);
#else
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <LittleFS.h>
  #include <Wire.h>
  #include <EEPROM.h>
  ESP8266WebServer server(80);
#endif

const char* ssid = "sony3D";
const char* password = "re7460ZA";

// --- توابع کمکی ---

String checkManufacturer(String mac) {
    mac.toUpperCase();
    if(mac.startsWith("18:FE:34") || mac.startsWith("24:0A:C4") || mac.startsWith("30:AE:A4") || mac.startsWith("84:F3:EB")) 
        return "Espressif Original (Official)";
    if(mac.startsWith("54:43:B2") || mac.startsWith("D8:A0:1D") || mac.startsWith("24:62:AB")) 
        return "AI-Thinker (Licensed)";
    return "Generic / Unknown (Clone?)";
}

// تست سرعت شبکه
void handleNetSpeed() {
    // ارسال 50 کیلوبایت داده برای تست پهنای باند
    String junk = ""; junk.reserve(1024);
    for(int i=0; i<1024; i++) junk += "X"; 
    server.setContentLength(50 * 1024);
    server.send(200, "text/plain", "");
    for(int i=0; i<50; i++) server.sendContent(junk);
}

// اسکن I2C
String scanI2C() {
    Wire.begin();
    String res = "[";
    int c = 0;
    for(byte i=1; i<127; i++){
        Wire.beginTransmission(i);
        if(Wire.endTransmission() == 0){
            res += "\"" + String(i, HEX) + "\"";
            c++;
            if(i<126) res += ",";
        }
    }
    if(res.endsWith(",")) res.remove(res.length()-1);
    res += "]";
    return res;
}

// اسکن بلوتوث (فقط ESP32)
String scanBT() {
    #ifdef ESP32
    BLEDevice::init("");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    BLEScanResults* f = pBLEScan->start(2, false); // 2 ثانیه اسکن
    String res = "[";
    for (int i=0; i<f->getCount(); i++) {
        BLEAdvertisedDevice d = f->getDevice(i);
        String n = d.getName().c_str();
        if(n.length()==0) n="Unknown Device";
        String addr = d.getAddress().toString().c_str();
        res += "{\"n\":\""+n+"\",\"a\":\""+addr+"\",\"r\":"+String(d.getRSSI())+"}";
        if(i<f->getCount()-1) res+=",";
    }
    res += "]";
    pBLEScan->clearResults(); 
    return res;
    #else
    return "[]"; // ESP8266 بلوتوث ندارد
    #endif
}

// بنچمارک حافظه ذخیره‌سازی
String benchStorage() {
    File f = LittleFS.open("/tmp.dat", "w");
    if(!f) return "{\"w\":0,\"r\":0}";
    uint8_t b[512]; memset(b, 0xFF, 512);
    long s = micros();
    for(int i=0;i<20;i++) f.write(b, 512); // Write 10KB
    float w = 10.0 / ((micros()-s)/1000000.0/1000.0);
    f.close();
    
    f = LittleFS.open("/tmp.dat", "r");
    s = micros();
    while(f.available()) f.read(b, 512);
    float r = 10.0 / ((micros()-s)/1000000.0/1000.0);
    f.close(); LittleFS.remove("/tmp.dat");
    return "{\"w\":\""+String(w,1)+"\",\"r\":\""+String(r,1)+"\"}";
}

// --- دکتر پین (Pin Doctor) ---
String checkPinStatus(int p) {
    int val = digitalRead(p);
    bool safe = true;
    String warning = "";

    // تست اتصال کوتاه به زمین
    pinMode(p, INPUT_PULLUP);
    delay(2);
    if(digitalRead(p) == LOW) { safe = false; warning = "SHORT_GND"; }
    
    pinMode(p, INPUT); // بازگشت به حالت امن
    
    String color = (val == HIGH) ? "red" : "green"; // قرمز=High, سبز=Low
    return "{\"v\":" + String(val) + ",\"s\":" + String(safe) + ",\"w\":\"" + warning + "\",\"c\":\"" + color + "\"}";
}

// اطلاعات کامل سیستم
void handleFullInfo() {
    uint32_t flash_size = 0, flash_speed = 0;
    uint32_t heap_total = 0, heap_free = 0, psram_size = 0;
    
    #ifdef ESP32
        esp_flash_get_size(NULL, &flash_size);
        flash_speed = ESP.getFlashChipSpeed();
        heap_total = ESP.getHeapSize();
        heap_free = ESP.getFreeHeap();
        psram_size = ESP.getPsramSize();
    #else
        flash_size = ESP.getFlashChipRealSize();
        flash_speed = ESP.getFlashChipSpeed();
        heap_total = 81920; 
        heap_free = ESP.getFreeHeap();
    #endif

    String json = "{";
    json += "\"chip\":\"" + String(currChip()) + "\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"manu\":\"" + checkManufacturer(WiFi.macAddress()) + "\",";
    json += "\"flash_sz\":\"" + String(flash_size/1024/1024) + " MB\",";
    json += "\"flash_spd\":\"" + String(flash_speed/1000000) + " MHz\",";
    json += "\"ram_int\":\"" + String(heap_total/1024) + " KB\",";
    json += "\"ram_free\":\"" + String(heap_free/1024) + " KB\",";
    json += "\"psram\":\"" + (psram_size > 0 ? String(psram_size/1024/1024)+" MB" : "Not Installed") + "\",";
    json += "\"fs_sz\":\"" + String(LittleFS.totalBytes()/1024) + " KB\",";
    json += "\"eeprom\":\"" + String(EEPROM.length()) + " Bytes\""; 
    json += "}";
    server.send(200, "application/json", json);
}

String currChip() {
    #ifdef ESP32
    return String(ESP.getChipModel()) + " (Rev " + String(ESP.getChipRevision()) + ")";
    #else
    return "ESP8266EX";
    #endif
}

// --- هندلرهای سرور ---

void handleLive() {
    String json = "{\"t\":";
    #ifdef ESP32
    json += String(temperatureRead(), 1);
    #else
    json += "0";
    #endif
    json += ",\"r\":" + String(WiFi.RSSI()) + "}";
    server.send(200, "application/json", json);
}

void handleTests() {
    if(!server.hasArg("t")) return;
    String t = server.arg("t");
    
    if(t=="pin_chk") {
        server.send(200, "application/json", checkPinStatus(server.arg("p").toInt()));
    }
    else if(t=="pin_set") {
        int p = server.arg("p").toInt();
        int v = server.arg("v").toInt();
        pinMode(p, OUTPUT);
        digitalWrite(p, v);
        server.send(200, "text/plain", "OK");
    }
    else if(t=="str") server.send(200, "application/json", benchStorage());
    else if(t=="i2c") server.send(200, "application/json", "{\"d\":" + scanI2C() + "}");
    else if(t=="bt") server.send(200, "application/json", "{\"d\":" + scanBT() + "}");
    else if(t=="wifi") {
        int n = WiFi.scanNetworks();
        String j = "[";
        for(int i=0;i<n;i++) { j+="{\"s\":\""+WiFi.SSID(i)+"\",\"r\":"+String(WiFi.RSSI(i))+"}"; if(i<n-1)j+=","; }
        j+="]";
        server.send(200, "application/json", "{\"d\":"+j+"}");
    }
    else if(t=="mem") {
        long s = micros();
        // تست سرعت رم با اختصاص بلاک حافظه
        uint8_t* x = (uint8_t*)malloc(1024*30); 
        if(x){ free(x); server.send(200,"application/json","{\"v\":\""+String(30.0/((micros()-s)/1000000.0),2)+"\"}"); }
        else { server.send(200,"application/json","{\"v\":\"Fail (Low RAM)\"}"); }
    }
}

void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fa" dir="rtl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP Ultimate Lab</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Vazirmatn:wght@300;500;700&display=swap');
        body { background: #f4f7f6; font-family: 'Vazirmatn', sans-serif; }
        
        /* A4 Print Styles */
        @page { size: A4; margin: 0; }
        @media print {
            body { background: white; -webkit-print-color-adjust: exact; }
            .no-print, .btn, .navbar { display: none !important; }
            .main-container { width: 210mm; padding: 15mm; margin: 0 auto; box-shadow: none; }
            .card { border: 1px solid #333 !important; break-inside: avoid; box-shadow: none !important; margin-bottom: 10px; }
            .card-header { background: #eee !important; border-bottom: 1px solid #333 !important; }
            .print-header { display: flex !important; justify-content: space-between; border-bottom: 2px solid #000; margin-bottom: 20px; padding-bottom: 10px; }
            .print-footer { position: fixed; bottom: 0; left:0; width: 100%; text-align: center; border-top: 1px solid #000; padding: 5px; font-size: 10px; display: block !important; }
        }

        .main-container { max-width: 900px; margin: 20px auto; padding: 15px; background: white; border-radius: 12px; box-shadow: 0 5px 20px rgba(0,0,0,0.05); }
        .print-header, .print-footer { display: none; }
        
        /* Pin Doctor UI */
        .pin-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(50px, 1fr)); gap: 8px; }
        .pin-box { 
            height: 50px; border: 1px solid #ddd; border-radius: 6px; cursor: pointer; 
            display: flex; flex-direction: column; align-items: center; justify-content: center;
            font-size: 10px; position: relative; background: #fff;
        }
        .pin-box:hover { background: #f8f9fa; border-color: #aaa; }
        .pin-led { width: 8px; height: 8px; border-radius: 50%; margin-top: 4px; border: 1px solid #ddd; }
        .led-red { background: #dc3545; box-shadow: 0 0 4px #dc3545; border-color: #dc3545; }
        .led-green { background: #198754; box-shadow: 0 0 4px #198754; border-color: #198754; }
        .led-gray { background: #e9ecef; }
        
        .info-table th { width: 40%; background: #f8f9fa; font-weight: 500; font-size: 0.9rem; }
        .info-val { font-family: monospace; direction: ltr; }
    </style>
</head>
<body>
    <nav class="navbar navbar-dark bg-dark mb-4 no-print">
        <div class="container">
            <span class="navbar-brand mb-0 h1">🛠 ESP Ultimate Lab v3.1</span>
            <button class="btn btn-outline-light btn-sm" onclick="window.print()">🖨 چاپ A4</button>
        </div>
    </nav>

    <div class="main-container">
        <!-- Print Header -->
        <div class="print-header">
            <div>
                <h4>گزارش عیب‌یابی و تأیید سخت‌افزار</h4>
                <small>آزمایشگاه کنترل کیفیت</small>
            </div>
            <div style="text-align: left;">
                <div>تاریخ: <span id="p-date"></span></div>
                <div>کد دستگاه: <span id="p-id"></span></div>
            </div>
        </div>

        <!-- 1. Identity -->
        <div class="card mb-3">
            <div class="card-header">1. مشخصات و اصالت (Identity)</div>
            <div class="card-body p-0">
                <table class="table table-bordered mb-0 info-table">
                    <tbody id="tbl-id"><tr><td>در حال بارگذاری...</td></tr></tbody>
                </table>
            </div>
        </div>

        <!-- 2. Memory -->
        <div class="card mb-3">
            <div class="card-header">2. وضعیت حافظه (Memory Stats)</div>
            <div class="card-body p-0">
                <table class="table table-bordered mb-0 info-table table-striped">
                    <tbody id="tbl-mem"><tr><td>در حال آنالیز...</td></tr></tbody>
                </table>
            </div>
        </div>

        <!-- 3. Performance -->
        <div class="card mb-3 break-inside-avoid">
            <div class="card-header">3. تست عملکرد (Benchmarks)</div>
            <div class="card-body">
                <div class="row text-center">
                    <div class="col-4 border-end">
                        <small class="text-muted d-block">سرعت RAM</small>
                        <strong id="res-mem" class="fs-5">-</strong>
                        <button class="btn btn-sm btn-outline-primary no-print w-100 mt-1" onclick="runT('mem')">تست</button>
                    </div>
                    <div class="col-4 border-end">
                        <small class="text-muted d-block">سرعت Flash</small>
                        <strong id="res-str" class="fs-5">-</strong>
                        <button class="btn btn-sm btn-outline-success no-print w-100 mt-1" onclick="runT('str')">تست</button>
                    </div>
                    <div class="col-4">
                        <small class="text-muted d-block">پهنای باند WiFi</small>
                        <strong id="res-net" class="fs-5">-</strong>
                        <button class="btn btn-sm btn-outline-info no-print w-100 mt-1" onclick="runNet()">تست</button>
                    </div>
                </div>
            </div>
        </div>

        <!-- 4. Pin Doctor -->
        <div class="card mb-3 no-print border-warning">
            <div class="card-header bg-warning-subtle d-flex justify-content-between">
                <span>4. دکتر پین (GPIO Doctor)</span>
                <span id="pin-status-badge" class="badge bg-secondary">آماده</span>
            </div>
            <div class="card-body">
                <div class="d-flex justify-content-center gap-3 mb-2 small">
                    <span class="d-flex align-items-center"><div class="pin-led led-red me-1"></div> روشن (High)</span>
                    <span class="d-flex align-items-center"><div class="pin-led led-green me-1"></div> خاموش (Low)</span>
                </div>
                
                <div class="pin-grid" id="pin-grid"></div>

                <div id="pin-panel" class="mt-3 p-2 bg-light border rounded text-center" style="display:none">
                    <strong id="sel-pin-name" class="d-block mb-2">GPIO X</strong>
                    <div id="pin-btns" class="d-flex gap-2 justify-content-center">
                        <button class="btn btn-danger w-50 btn-sm" onmousedown="setPin(1)" onmouseup="setPin(0)">Hold HIGH</button>
                        <button class="btn btn-success w-50 btn-sm" onclick="setPin(0)">Set LOW</button>
                    </div>
                    <div id="pin-warn" class="text-danger small mt-2 fw-bold" style="display:none"></div>
                </div>
            </div>
        </div>

        <!-- 5. Connectivity -->
        <div class="card mb-3">
            <div class="card-header">5. اتصالات (Connectivity)</div>
            <div class="card-body">
                <div class="row">
                    <div class="col-6">
                        <h6>لیست I2C:</h6>
                        <ul id="lst-i2c" class="small text-muted ps-3 mb-1"><li>-</li></ul>
                        <button class="btn btn-sm btn-secondary no-print" onclick="runT('i2c')">اسکن I2C</button>
                    </div>
                    <div class="col-6 border-start">
                        <h6>محیط وایرلس:</h6>
                        <ul id="lst-env" class="small text-muted ps-3 mb-1"><li>-</li></ul>
                        <div class="no-print">
                            <button class="btn btn-sm btn-secondary" onclick="runT('wifi')">WiFi</button>
                            <button class="btn btn-sm btn-secondary" onclick="runT('bt')">Bluetooth</button>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Footer -->
        <div class="print-footer">
            تولید گزارش توسط سیستم عیب‌یابی هوشمند ESP Lab v3.1
        </div>
    </div>

    <script>
        document.getElementById('p-date').innerText = new Date().toLocaleDateString('fa-IR');
        document.getElementById('p-id').innerText = "DEV-" + Math.floor(1000 + Math.random() * 9000);

        // Load Full Info
        fetch('/full_info').then(r=>r.json()).then(d => {
            document.getElementById('tbl-id').innerHTML = `
                <tr><th>مدل چیپ</th><td>${d.chip}</td></tr>
                <tr><th>مک آدرس</th><td class="info-val">${d.mac}</td></tr>
                <tr><th>سازنده برد</th><td class="fw-bold ${d.manu.includes('Original')?'text-success':'text-danger'}">${d.manu}</td></tr>
            `;
            document.getElementById('tbl-mem').innerHTML = `
                <tr><th>حافظه Flash</th><td class="info-val">${d.flash_sz} (${d.flash_spd})</td></tr>
                <tr><th>حافظه RAM</th><td class="info-val">Total: ${d.ram_int} / Free: ${d.ram_free}</td></tr>
                <tr><th>PSRAM</th><td class="info-val">${d.psram}</td></tr>
                <tr><th>File System</th><td class="info-val">${d.fs_sz}</td></tr>
            `;
        });

        // Pin Doctor
        const pins = [0,2,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33]; 
        let selPin = -1;
        const grid = document.getElementById('pin-grid');
        
        pins.forEach(p => {
            grid.innerHTML += `<div class="pin-box" onclick="selectPin(${p})" id="pb-${p}"><span>G${p}</span><div class="pin-led led-gray" id="pl-${p}"></div></div>`;
        });

        // Live Monitor
        setInterval(() => {
            if(document.hidden) return;
            // Scan pins in round-robin or selected pin
            let target = (selPin !== -1) ? selPin : pins[Math.floor(Math.random()*pins.length)];
            
            fetch(`/tests?t=pin_chk&p=${target}`).then(r=>r.json()).then(d => {
                const el = document.getElementById(`pl-${target}`);
                el.className = `pin-led ${d.c=='red'?'led-red':'led-green'}`;
                
                if(selPin === target) {
                    const warn = document.getElementById('pin-warn');
                    const btns = document.getElementById('pin-btns');
                    const badge = document.getElementById('pin-status-badge');
                    
                    if(d.s) {
                        warn.style.display = 'none'; btns.style.opacity = '1'; btns.style.pointerEvents = 'auto';
                        badge.className='badge bg-success'; badge.innerText='امن (Safe)';
                    } else {
                        btns.style.opacity='0.5'; btns.style.pointerEvents='none';
                        warn.style.display='block'; warn.innerText=`⛔ اتصال کوتاه به ${d.w} !`;
                        badge.className='badge bg-danger'; badge.innerText='خطر (Short)';
                    }
                }
            });
        }, 600);

        function selectPin(p) {
            selPin = p;
            document.getElementById('pin-panel').style.display='block';
            document.getElementById('sel-pin-name').innerText=`کنترل GPIO ${p}`;
            document.getElementById('pin-status-badge').innerText='در حال بررسی...';
            document.getElementById('pin-status-badge').className='badge bg-secondary';
        }
        
        function setPin(v) { if(selPin!==-1) fetch(`/tests?t=pin_set&p=${selPin}&v=${v}`); }

        // Test Runners
        async function runNet() {
            document.getElementById('res-net').innerText = "...";
            const s = performance.now();
            await (await fetch('/net_speed')).text();
            const dur = (performance.now() - s) / 1000;
            document.getElementById('res-net').innerText = (50/dur).toFixed(0) + " KB/s";
        }

        function runT(t) {
            const el = document.getElementById(t.startsWith('i2c')||t=='wifi'||t=='bt' ? (t=='wifi'||t=='bt'?'lst-env':'lst-i2c') : `res-${t}`);
            el.innerHTML = '<span class="spinner-border spinner-border-sm"></span>';
            fetch(`/tests?t=${t}`).then(r=>r.json()).then(d => {
                if(t=='mem') el.innerText = d.v + " MB/s";
                else if(t=='str') el.innerText = `W:${d.w} | R:${d.r}`;
                else if(t=='i2c') el.innerHTML = d.d.map(x=>`<li>ADDR: 0x${x}</li>`).join('') || "<li>یافت نشد</li>";
                else if(t=='wifi'||t=='bt') document.getElementById('lst-env').innerHTML = d.d.map(x=>`<li>${x.n||x.s} (${x.r}dBm) <small>${x.a||''}</small></li>`).join('') || "<li>یافت نشد</li>";
            });
        }
    </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    LittleFS.begin(true);
    EEPROM.begin(512);
    WiFi.begin(ssid, password);
    while(WiFi.status() != WL_CONNECTED) delay(500);

    server.on("/", handleRoot);
    server.on("/full_info", handleFullInfo);
    server.on("/live", handleLive);
    server.on("/tests", handleTests);
    server.on("/net_speed", handleNetSpeed);
    
    server.begin();
    Serial.println("System Ready: " + WiFi.localIP().toString());
}

void loop() { server.handleClient(); }