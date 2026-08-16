#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "FidoEngine.h"

// Official FIDO Alliance Bluetooth Low Energy GATT Service and Characteristic UUIDs
#define FIDO_BLE_SERVICE_UUID                NimBLEUUID((uint16_t)0xFFFD)
#define FIDO_BLE_CHAR_CONTROL_POINT_UUID     "F1D0FFF1-DEAA-ECEE-B42F-C9BA7ED623BB"
#define FIDO_BLE_CHAR_STATUS_UUID            "F1D0FFF2-DEAA-ECEE-B42F-C9BA7ED623BB"
#define FIDO_BLE_CHAR_CONTROL_POINT_LEN_UUID "F1D0FFF3-DEAA-ECEE-B42F-C9BA7ED623BB"
#define FIDO_BLE_CHAR_SERVICE_REV_BF_UUID    "F1D0FFF4-DEAA-ECEE-B42F-C9BA7ED623BB"
#define FIDO_BLE_CHAR_SERVICE_REV_UUID       NimBLEUUID((uint16_t)0x2A28)

class BLEFIDO : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
private:
  bool active;
  bool isConnected;
  uint16_t peerMtu;

  NimBLEServer *server;
  NimBLEService *fidoService;
  NimBLECharacteristic *charControlPoint;
  NimBLECharacteristic *charStatus;
  NimBLECharacteristic *charControlPointLen;
  NimBLECharacteristic *charServiceRev;
  NimBLECharacteristic *charServiceRevBitfield;

  // Packet reassembly buffer
  uint8_t currentCmd;
  uint16_t totalLen;
  uint8_t nextSeq;
  std::vector<uint8_t> rxBuffer;

  void processIncomingFrame(const uint8_t *data, size_t len);
  void handleCompleteMessage(uint8_t cmd, const uint8_t *data, size_t len);

  bool sendBlePacket(uint8_t cmd, const uint8_t *payload, size_t len);
  bool sendCborResponse(uint8_t statusCode, const uint8_t *cborData, size_t len);
  bool sendError(uint8_t errCode);

public:
  BLEFIDO();
  bool begin(const String &deviceName = "ESP32-S3 Passkey");
  void stop();
  bool isRunning() const { return active; }
  bool hasClient() const { return isConnected; }

  // NimBLE Callbacks
  void onConnect(NimBLEServer *pServer, ble_gap_conn_desc* desc) override;
  void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc* desc) override;
  void onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) override;
  void onWrite(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc) override;
  void onRead(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc) override;
  void onSubscribe(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc, uint16_t subValue) override;

  // Touch & Timeout (delegates directly to unified FidoEngine)
  bool isWaitingForTouch() const { return GlobalFidoEngine.isWaitingForTouch(); }
  void confirmTouch() { GlobalFidoEngine.confirmTouch(); }
  void cancelPending() { GlobalFidoEngine.cancelPending(); }
  void checkTimeout() { GlobalFidoEngine.checkTimeout(); }
};

extern BLEFIDO BleFido;
