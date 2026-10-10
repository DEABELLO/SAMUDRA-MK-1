#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>

#include <Adafruit_ADS1X15.h>
#include <QMC5883LCompass.h>

// =====================================================
// SAMUDRA MARK 1
// UNDERWATER METAL DETECTION SYSTEM
// MPU6050 REMOVED
// BLUE LED ADDED
// PROMETHEUS METAL CLASSIFICATION ADDED
// =====================================================


// ================= WIFI =================

const char* WIFI_SSID = "VSAI";
const char* WIFI_PASSWORD = "sai_141107";


// ================= ESP32 PINS ===========

#define TX_DRIVER_PIN 5
#define LED_PIN 2

#define I2C_SDA 21
#define I2C_SCL 22


// ================= I2C ADDRESSES =========

#define ADS1115_ADDR 0x48
#define QMC5883L_ADDR 0x0D


// ================= OBJECTS ===============

Adafruit_ADS1115 ads;
QMC5883LCompass compass;

WebServer server(80);


// ================= STATUS ================

bool adsOK = false;
bool qmcOK = false;
bool wifiOK = false;


// ================= LED STATUS ============

unsigned long previousLedMillis = 0;
bool ledState = false;


// ================= RX DATA ===============

int16_t rxRaw = 0;

float rxVoltage = 0.0;
float rxBaseline = 0.0;
float rxDeviation = 0.0;


// ================= MAGNETIC DATA =========

int magX = 0;
int magY = 0;
int magZ = 0;

float magneticMagnitude = 0.0;
float magneticBaseline = 0.0;
float magneticDeviation = 0.0;

float heading = 0.0;


// ================= CLASSIFICATION =========

String detectedMaterial = "NO TARGET";
String detectionStrength = "NONE";

float confidence = 0.0;


// =====================================================
// CHECK I2C DEVICE
// =====================================================

bool i2cDevicePresent(uint8_t address) {

  Wire.beginTransmission(address);

  return Wire.endTransmission() == 0;
}


// =====================================================
// WIFI CONNECTION
// =====================================================

void connectWiFi() {

  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);

  WiFi.disconnect();

  delay(500);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 40) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    wifiOK = true;

    Serial.println("WiFi connected!");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

  } else {

    wifiOK = false;

    Serial.println("WiFi connection failed.");

    Serial.print("Status code: ");
    Serial.println(WiFi.status());

    Serial.println("Continuing without WiFi...");
  }
}


// =====================================================
// WIFI BLUE LED
// =====================================================

void updateLED() {

  // Blink blue LED when WiFi is connected

  if (WiFi.status() == WL_CONNECTED) {

    unsigned long currentMillis = millis();

    if (currentMillis - previousLedMillis >= 500) {

      previousLedMillis = currentMillis;

      ledState = !ledState;

      digitalWrite(LED_PIN, ledState);
    }

  } else {

    // Turn LED OFF when WiFi is disconnected

    digitalWrite(LED_PIN, LOW);

    ledState = false;
  }
}


// =====================================================
// INITIALIZE SENSORS
// =====================================================

void initializeSensors() {

  Serial.println();
  Serial.println("Initializing sensors...");
  Serial.println("--------------------------------");


  // -------------------------------
  // ADS1115
  // -------------------------------

  if (i2cDevicePresent(ADS1115_ADDR)) {

    Serial.println("ADS1115 found at 0x48");

    if (ads.begin(ADS1115_ADDR, &Wire)) {

      adsOK = true;

      ads.setGain(GAIN_ONE);

      Serial.println("ADS1115 initialized.");

    } else {

      adsOK = false;

      Serial.println("ADS1115 initialization failed.");
    }

  } else {

    adsOK = false;

    Serial.println("ADS1115 not found.");
  }


  // -------------------------------
  // QMC5883L
  // -------------------------------

  if (i2cDevicePresent(QMC5883L_ADDR)) {

    Serial.println("QMC5883L found at 0x0D");

    compass.init();

    qmcOK = true;

    Serial.println("QMC5883L initialized.");

  } else {

    qmcOK = false;

    Serial.println("QMC5883L not found.");
  }


  Serial.println("--------------------------------");

  Serial.print("ADS1115 : ");
  Serial.println(adsOK ? "OK" : "FAILED");

  Serial.print("QMC5883L: ");
  Serial.println(qmcOK ? "OK" : "FAILED");

  Serial.println("MPU6050 : REMOVED");
}


// =====================================================
// READ RX COIL
// =====================================================

void readRX() {

  if (!adsOK) {
    return;
  }

  rxRaw = ads.readADC_SingleEnded(0);

  rxVoltage = ads.computeVolts(rxRaw);

  rxDeviation = abs(rxVoltage - rxBaseline);
}


// =====================================================
// READ QMC5883L
// =====================================================

void readMagnetometer() {

  if (!qmcOK) {
    return;
  }

  compass.read();

  magX = compass.getX();
  magY = compass.getY();
  magZ = compass.getZ();

  magneticMagnitude =
    sqrt(
      (float)magX * magX +
      (float)magY * magY +
      (float)magZ * magZ
    );

  magneticDeviation =
    abs(magneticMagnitude - magneticBaseline);

  heading =
    atan2(
      (float)magY,
      (float)magX
    ) * 180.0 / PI;

  if (heading < 0) {
    heading += 360.0;
  }
}


// =====================================================
// METAL CLASSIFICATION
// =====================================================

void classifyMetal() {

  // -------------------------------
  // NO TARGET
  // -------------------------------

  if (rxDeviation < 0.008 &&
      magneticDeviation < 20) {

    detectedMaterial = "NO TARGET";

    detectionStrength = "NONE";

    confidence = 0;

    return;
  }


  // -------------------------------
  // IRON / STEEL
  // -------------------------------

  if (rxDeviation >= 0.100 &&
      magneticDeviation >= 150) {

    detectedMaterial = "IRON / STEEL-LIKE";

    detectionStrength = "VERY HIGH";

    confidence = 90;

    return;
  }


  // -------------------------------
  // IRON
  // -------------------------------

  if (rxDeviation >= 0.060 &&
      magneticDeviation >= 80) {

    detectedMaterial = "IRON-LIKE";

    detectionStrength = "HIGH";

    confidence = 80;

    return;
  }


  // -------------------------------
  // NICKEL
  // -------------------------------

  if (rxDeviation >= 0.040) {

    detectedMaterial = "NICKEL-LIKE";

    detectionStrength = "MEDIUM";

    confidence = 65;

    return;
  }


  // -------------------------------
  // COPPER
  // -------------------------------

  if (rxDeviation >= 0.020) {

    detectedMaterial = "COPPER-LIKE";

    detectionStrength = "LOW";

    confidence = 50;

    return;
  }


  // -------------------------------
  // ALUMINIUM
  // -------------------------------

  if (rxDeviation >= 0.008) {

    detectedMaterial = "ALUMINIUM-LIKE";

    detectionStrength = "VERY LOW";

    confidence = 35;

    return;
  }
}


// =====================================================
// SENSOR CALIBRATION
// =====================================================

void calibrateSensors() {

  Serial.println();
  Serial.println("================================");
  Serial.println("CALIBRATING SAMUDRA");
  Serial.println("Keep metal away from RX coil.");
  Serial.println("================================");

  delay(2000);


  // -------------------------------
  // RX BASELINE
  // -------------------------------

  if (adsOK) {

    float totalRX = 0;

    for (int i = 0; i < 100; i++) {

      int16_t value =
        ads.readADC_SingleEnded(0);

      totalRX += ads.computeVolts(value);

      delay(10);
    }

    rxBaseline = totalRX / 100.0;

    Serial.print("RX Baseline = ");
    Serial.println(rxBaseline, 6);
  }


  // -------------------------------
  // MAGNETIC BASELINE
  // -------------------------------

  if (qmcOK) {

    float totalMag = 0;

    for (int i = 0; i < 100; i++) {

      compass.read();

      int x = compass.getX();
      int y = compass.getY();
      int z = compass.getZ();

      float magnitude =
        sqrt(
          (float)x * x +
          (float)y * y +
          (float)z * z
        );

      totalMag += magnitude;

      delay(10);
    }

    magneticBaseline =
      totalMag / 100.0;

    Serial.print("Magnetic Baseline = ");
    Serial.println(magneticBaseline, 2);
  }

  Serial.println("Calibration complete.");
}


// =====================================================
// JSON API
// =====================================================

void handleJSON() {

  readRX();

  readMagnetometer();

  classifyMetal();

  String json = "{";

  json += "\"wifi\":";
  json += wifiOK ? "true" : "false";

  json += ",\"ads1115\":";
  json += adsOK ? "true" : "false";

  json += ",\"qmc5883l\":";
  json += qmcOK ? "true" : "false";

  json += ",\"rx_raw\":";
  json += String(rxRaw);

  json += ",\"rx_voltage\":";
  json += String(rxVoltage, 6);

  json += ",\"rx_baseline\":";
  json += String(rxBaseline, 6);

  json += ",\"rx_deviation\":";
  json += String(rxDeviation, 6);

  json += ",\"mag_x\":";
  json += String(magX);

  json += ",\"mag_y\":";
  json += String(magY);

  json += ",\"mag_z\":";
  json += String(magZ);

  json += ",\"magnetic_magnitude\":";
  json += String(magneticMagnitude, 2);

  json += ",\"magnetic_deviation\":";
  json += String(magneticDeviation, 2);

  json += ",\"heading\":";
  json += String(heading, 2);

  json += ",\"material\":\"";
  json += detectedMaterial;
  json += "\"";

  json += ",\"strength\":\"";
  json += detectionStrength;
  json += "\"";

  json += ",\"confidence\":";
  json += String(confidence, 1);

  json += "}";

  server.send(
    200,
    "application/json",
    json
  );
}


// =====================================================
// PROMETHEUS /metrics
// =====================================================

void handleMetrics() {

  readRX();
  readMagnetometer();
  classifyMetal();

  // -----------------------------------------
  // Convert classification to metric label
  // -----------------------------------------

  String metalLabel = "NONE";

  if (detectedMaterial == "IRON / STEEL-LIKE") {

    metalLabel = "IRON_STEEL";

  }
  else if (detectedMaterial == "IRON-LIKE") {

    metalLabel = "IRON";

  }
  else if (detectedMaterial == "NICKEL-LIKE") {

    metalLabel = "NICKEL";

  }
  else if (detectedMaterial == "COPPER-LIKE") {

    metalLabel = "COPPER";

  }
  else if (detectedMaterial == "ALUMINIUM-LIKE") {

    metalLabel = "ALUMINIUM";
  }


  String metrics = "";


  // -----------------------------------------
  // RX RAW
  // -----------------------------------------

  metrics += "# HELP samudra_rx_raw Raw RX ADC value\n";
  metrics += "# TYPE samudra_rx_raw gauge\n";

  metrics +=
    "samudra_rx_raw " +
    String(rxRaw) +
    "\n";


  // -----------------------------------------
  // RX VOLTAGE
  // -----------------------------------------

  metrics += "# HELP samudra_rx_voltage RX voltage\n";
  metrics += "# TYPE samudra_rx_voltage gauge\n";

  metrics +=
    "samudra_rx_voltage " +
    String(rxVoltage, 6) +
    "\n";


  // -----------------------------------------
  // RX DEVIATION
  // -----------------------------------------

  metrics +=
    "# HELP samudra_rx_deviation RX deviation from baseline\n";

  metrics +=
    "# TYPE samudra_rx_deviation gauge\n";

  metrics +=
    "samudra_rx_deviation " +
    String(rxDeviation, 6) +
    "\n";


  // -----------------------------------------
  // MAGNETIC MAGNITUDE
  // -----------------------------------------

  metrics +=
    "# HELP samudra_magnetic_magnitude Magnetic field magnitude\n";

  metrics +=
    "# TYPE samudra_magnetic_magnitude gauge\n";

  metrics +=
    "samudra_magnetic_magnitude " +
    String(magneticMagnitude, 2) +
    "\n";


  // -----------------------------------------
  // MAGNETIC DEVIATION
  // -----------------------------------------

  metrics +=
    "# HELP samudra_magnetic_deviation Magnetic field deviation\n";

  metrics +=
    "# TYPE samudra_magnetic_deviation gauge\n";

  metrics +=
    "samudra_magnetic_deviation " +
    String(magneticDeviation, 2) +
    "\n";


  // -----------------------------------------
  // HEADING
  // -----------------------------------------

  metrics +=
    "# HELP samudra_heading Magnetic heading\n";

  metrics +=
    "# TYPE samudra_heading gauge\n";

  metrics +=
    "samudra_heading " +
    String(heading, 2) +
    "\n";


  // -----------------------------------------
  // CONFIDENCE
  // -----------------------------------------

  metrics +=
    "# HELP samudra_confidence Detection confidence\n";

  metrics +=
    "# TYPE samudra_confidence gauge\n";

  metrics +=
    "samudra_confidence " +
    String(confidence, 1) +
    "\n";


  // -----------------------------------------
  // METAL CLASSIFICATION
  // -----------------------------------------

  metrics +=
    "# HELP samudra_metal_detected Estimated metal classification\n";

  metrics +=
    "# TYPE samudra_metal_detected gauge\n";

  metrics +=
    "samudra_metal_detected{metal=\"" +
    metalLabel +
    "\"} 1\n";


  // -----------------------------------------
  // SEND TO PROMETHEUS
  // -----------------------------------------

  server.send(
    200,
    "text/plain",
    metrics
  );
}


// =====================================================
// WEB PAGE
// =====================================================

void handleRoot() {

  String page = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
      content="width=device-width,initial-scale=1">

<title>SAMUDRA MARK 1</title>

<style>

body {
  font-family: Arial;
  background: #08111f;
  color: white;
  text-align: center;
  padding: 20px;
}

.card {
  background: #111d2e;
  padding: 20px;
  margin: 15px auto;
  border-radius: 12px;
  max-width: 500px;
}

.value {
  font-size: 28px;
  font-weight: bold;
}

</style>

</head>


<body>

<h1>SAMUDRA MARK 1</h1>

<h2>METAL DETECTION SYSTEM</h2>


<div class="card">

<h2>Detected Material</h2>

<div id="material"
     class="value">

WAITING...

</div>

<p>
Strength:
<span id="strength">---</span>
</p>

<p>
Confidence:
<span id="confidence">---</span>%
</p>

</div>


<div class="card">

<h2>RX SENSOR</h2>

<p>
Raw:
<span id="rxraw">---</span>
</p>

<p>
Voltage:
<span id="rxvoltage">---</span> V
</p>

<p>
Deviation:
<span id="rxdev">---</span>
</p>

</div>


<div class="card">

<h2>MAGNETIC SENSOR</h2>

<p>
X:
<span id="mx">---</span>
</p>

<p>
Y:
<span id="my">---</span>
</p>

<p>
Z:
<span id="mz">---</span>
</p>

<p>
Magnitude:
<span id="mm">---</span>
</p>

<p>
Heading:
<span id="heading">---</span>°
</p>

</div>


<div class="card">

<h2>SYSTEM STATUS</h2>

<p>
WiFi:
<span id="wifi">---</span>
</p>

<p>
ADS1115:
<span id="ads">---</span>
</p>

<p>
QMC5883L:
<span id="qmc">---</span>
</p>

</div>


<script>

async function updateData() {

  try {

    const response =
      await fetch('/json');

    const d =
      await response.json();


    document.getElementById("material")
      .innerText = d.material;


    document.getElementById("strength")
      .innerText = d.strength;


    document.getElementById("confidence")
      .innerText = d.confidence;


    document.getElementById("rxraw")
      .innerText = d.rx_raw;


    document.getElementById("rxvoltage")
      .innerText =
      d.rx_voltage.toFixed(6);


    document.getElementById("rxdev")
      .innerText =
      d.rx_deviation.toFixed(6);


    document.getElementById("mx")
      .innerText = d.mag_x;


    document.getElementById("my")
      .innerText = d.mag_y;


    document.getElementById("mz")
      .innerText = d.mag_z;


    document.getElementById("mm")
      .innerText =
      d.magnetic_magnitude.toFixed(2);


    document.getElementById("heading")
      .innerText =
      d.heading.toFixed(2);


    document.getElementById("wifi")
      .innerText =
      d.wifi ? "CONNECTED" : "OFFLINE";


    document.getElementById("ads")
      .innerText =
      d.ads1115 ? "OK" : "FAILED";


    document.getElementById("qmc")
      .innerText =
      d.qmc5883l ? "OK" : "FAILED";

  }

  catch(error) {

    console.log(error);

  }

}


setInterval(updateData, 1000);

updateData();

</script>

</body>

</html>

)rawliteral";


  server.send(
    200,
    "text/html",
    page
  );
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1500);


  Serial.println();
  Serial.println("================================");
  Serial.println("       SAMUDRA MARK 1");
  Serial.println("     METAL DETECTION SYSTEM");
  Serial.println("================================");


  // ===============================
  // TX DRIVER
  // ===============================

  pinMode(
    TX_DRIVER_PIN,
    OUTPUT
  );

  digitalWrite(
    TX_DRIVER_PIN,
    LOW
  );


  // ===============================
  // BLUE LED
  // ===============================

  pinMode(
    LED_PIN,
    OUTPUT
  );

  digitalWrite(
    LED_PIN,
    LOW
  );


  // ===============================
  // I2C
  // ===============================

  Serial.println();
  Serial.println("Starting I2C...");

  Wire.begin(
    I2C_SDA,
    I2C_SCL
  );

  Wire.setClock(100000);

  delay(200);


  // ===============================
  // SENSORS
  // ===============================

  initializeSensors();


  // ===============================
  // CALIBRATION
  // ===============================

  calibrateSensors();


  // ===============================
  // WIFI
  // ===============================

  connectWiFi();


  // ===============================
  // WEB SERVER
  // ===============================

  server.on(
    "/",
    handleRoot
  );


  server.on(
    "/json",
    handleJSON
  );


  server.on(
    "/metrics",
    handleMetrics
  );


  server.begin();


  Serial.println();
  Serial.println("Web server started.");


  if (wifiOK) {

    Serial.print("Dashboard: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");

    Serial.print("JSON: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/json");

    Serial.print("Prometheus: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/metrics");
  }


  Serial.println();
  Serial.println("SAMUDRA READY");
  Serial.println("================================");
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  server.handleClient();


  // ===============================
  // SENSOR READINGS
  // ===============================

  readRX();

  readMagnetometer();

  classifyMetal();


  // ===============================
  // WIFI BLUE LED
  // ===============================

  updateLED();


  delay(100);
}