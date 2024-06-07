#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define SERVICE_UUID "00001800-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a00-0000-1000-8000-00805f9b34fb"
#define DEVICE_NAME "ESP32_BLE_DEVICE"

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
bool authenticated = false;
uint8_t txValue = 0;

const int ledPin = 2;
Preferences preferences;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
      deviceConnected = true;
      uint8_t* addr = param->connect.remote_bda;

      // Вывод адреса подключающегося устройства
      Serial.print("Connected device address: ");
      for (int i = 0; i < 6; i++) {
        if (i != 0) {
          Serial.print(":");
        }
        Serial.print(addr[i], HEX);
      }
      Serial.println();

      preferences.begin("ble_whitelist", true);
      uint8_t peer_addr[6];
      if (preferences.getBytes("peer_addr", peer_addr, sizeof(peer_addr)) == sizeof(peer_addr)) {
        if (memcmp(addr, peer_addr, sizeof(peer_addr)) == 0) {
          authenticated = true;
          Serial.println("Device reconnected from whitelist");
        } else {
          authenticated = false;
          Serial.println("Connected device not in whitelist");
        }
      } else {
        authenticated = false;
        Serial.println("No devices in whitelist");
      }
      preferences.end();
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      authenticated = false;
      Serial.println("Device disconnected");
      pServer->getAdvertising()->start();
    }
};

class MySecurity : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() {
    Serial.println("PassKeyRequest");
    return 123456;
  }
  void onPassKeyNotify(uint32_t pass_key) {
    Serial.println("On passkey Notify number: " + String(pass_key));
  }
  bool onConfirmPIN(uint32_t pass_key) {
    Serial.println("onConfirmPIN");
    return true;
  }
  bool onSecurityRequest() {
    Serial.println("onSecurityRequest");
    return true;
  }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
    if(cmpl.success){
      Serial.println("Auth Complete");
      authenticated = true;
      esp_ble_gap_update_whitelist(ESP_BLE_WHITELIST_ADD, cmpl.bd_addr, BLE_WL_ADDR_TYPE_PUBLIC);
      preferences.begin("ble_whitelist", false);
      preferences.putBytes("peer_addr", cmpl.bd_addr, sizeof(cmpl.bd_addr));
      preferences.end();
    } else {
      Serial.println("Auth Failed");
      authenticated = false;
    }
  }
};

void notifyTask(void * parameter) {
  while (true) {
    if (deviceConnected && authenticated) {
      // pCharacteristic->setValue(&txValue, 1);
      // pCharacteristic->notify();
      // Serial.println(String(txValue));
      // txValue++;
      // if (txValue > 254) txValue = 0;
      digitalWrite(ledPin, HIGH);
      vTaskDelay(pdMS_TO_TICKS(500));
      digitalWrite(ledPin, LOW);
      vTaskDelay(pdMS_TO_TICKS(500));
    } else {
      digitalWrite(ledPin, LOW);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

void reconnectTask(void * parameter) {
  while (true) {
    if (!deviceConnected) {
      // Начинаем рекламировать устройство для попытки переподключения
      pServer->getAdvertising()->start();
      vTaskDelay(pdMS_TO_TICKS(5000));
    } else {
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);

  BLEDevice::init(DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  pSecurity->setCapability(ESP_IO_CAP_OUT);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  pSecurity->setKeySize(16);
  pSecurity->setStaticPIN(123456);
  BLEDevice::setSecurityCallbacks(new MySecurity());

  pService->start();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);

  esp_ble_gap_config_local_privacy(true);

  // Считываем адрес из памяти и добавляем в белый список
  preferences.begin("ble_whitelist", true);
  uint8_t peer_addr[6];
  if (preferences.getBytes("peer_addr", peer_addr, sizeof(peer_addr)) == sizeof(peer_addr)) {
    esp_ble_gap_update_whitelist(ESP_BLE_WHITELIST_ADD, peer_addr, BLE_WL_ADDR_TYPE_PUBLIC);
    Serial.print("Restored peer address from whitelist: ");
    for (int i = 0; i < 6; i++) {
      if (i != 0) {
        Serial.print(":");
      }
      Serial.print(peer_addr[i], HEX);
    }
    Serial.println();
  } else {
    Serial.println("No peer address found in whitelist");
  }
  preferences.end();

  pAdvertising->start();
  Serial.println("Waiting for a client connection to notify...");

  xTaskCreate(notifyTask, "notifyTask", 4096, NULL, 1, NULL);
  xTaskCreate(reconnectTask, "reconnectTask", 4096, NULL, 1, NULL);
}

void loop() {
  // Ничего не делаем в loop, вся работа выполняется в задачах
}
