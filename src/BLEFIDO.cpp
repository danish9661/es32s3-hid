#include "BLEFIDO.h"
#include <esp_bt.h>
#include <esp_log.h>

BLEFIDO BleFido;

BLEFIDO::BLEFIDO()
  : active(false), isConnected(false), peerMtu(23),
    server(nullptr), fidoService(nullptr),
    charControlPoint(nullptr), charStatus(nullptr),
    charControlPointLen(nullptr), charServiceRev(nullptr), charServiceRevBitfield(nullptr),
    currentCmd(0), totalLen(0), nextSeq(0) {}

class FidoSecurityCallbacks : public NimBLESecurityCallbacks {
  uint32_t onPassKeyRequest() override {
    Serial.println("[BLE-FIDO] Security: Passkey requested");
    return 0;
  }
  void onPassKeyNotify(uint32_t pass_key) override {
    Serial.printf("[BLE-FIDO] Security: Passkey notification: %06u\n", pass_key);
  }
  bool onConfirmPIN(uint32_t pin) override {
    Serial.printf("[BLE-FIDO] Security: Auto-confirming PIN: %u\n", pin);
    return true;
  }
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    if (desc->sec_state.encrypted) {
      Serial.printf("[BLE-FIDO] Security: Successfully Encrypted (Handle: %d, Bonded: %d)\n",
        desc->conn_handle, desc->sec_state.bonded);
    } else {
      Serial.printf("[BLE-FIDO] Security: Auth complete (Encrypted=%d, Bonded=%d)\n",
        desc->sec_state.encrypted, desc->sec_state.bonded);
    }
  }
  bool onSecurityRequest() override {
    Serial.println("[BLE-FIDO] Security: Security request accepted");
    return true;
  }
};

bool BLEFIDO::begin(const String &deviceName) {
  if (active) return true;

  String safeName = deviceName;
  if (safeName.isEmpty()) safeName = "ESP32-S3 Passkey";
  if (safeName.length() > 28) safeName = safeName.substring(0, 28);

  NimBLEDevice::init(safeName.c_str());
  // Enable NimBLE stack-level debug logging to diagnose CCCD writes at ATT layer.
  // This shows raw ATT_WRITE_REQ / BLE_GAP_EVENT_SUBSCRIBE events.
  esp_log_level_set("NimBLE", ESP_LOG_DEBUG);
  esp_log_level_set("NimBLEServer", ESP_LOG_DEBUG);
  // Bonding=true, SC=true: Windows stores LTK and reconnects without re-pairing.
  // This is required so the second connection (FIDO operation) can use the existing bond.
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityCallbacks(new FidoSecurityCallbacks());

  server = NimBLEDevice::createServer();
  server->setCallbacks(this);

  fidoService = server->createService(FIDO_BLE_SERVICE_UUID);

  // 1. FIDO Control Point (Write | Write Without Response)
  // Security enforced at link level by AES-128 SMP encryption, not per-attribute flags.
  charControlPoint = fidoService->createCharacteristic(
    FIDO_BLE_CHAR_CONTROL_POINT_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  charControlPoint->setCallbacks(this);

  // 2. FIDO Status (Notify | Read)
  charStatus = fidoService->createCharacteristic(
    FIDO_BLE_CHAR_STATUS_UUID,
    NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
  );
  charStatus->setCallbacks(this);
  uint8_t zeroStatus = 0x00;
  charStatus->setValue(&zeroStatus, 1);

  // 3. FIDO Control Point Length (Read: 512 bytes = 0x0200 big-endian)
  charControlPointLen = fidoService->createCharacteristic(
    FIDO_BLE_CHAR_CONTROL_POINT_LEN_UUID,
    NIMBLE_PROPERTY::READ
  );
  charControlPointLen->setCallbacks(this);
  uint8_t cpLen[2] = { 0x02, 0x00 }; // 512 bytes
  charControlPointLen->setValue(cpLen, 2);

  // 4. FIDO Service Revision (Read: "1.2")
  charServiceRev = fidoService->createCharacteristic(
    FIDO_BLE_CHAR_SERVICE_REV_UUID,
    NIMBLE_PROPERTY::READ
  );
  charServiceRev->setCallbacks(this);
  uint8_t sRev[3] = { '1', '.', '2' };
  charServiceRev->setValue(sRev, 3);

  // 5. FIDO Service Revision Bitfield (Read | Write | Write Without Response: FIDO 2.0 = 0x80)
  charServiceRevBitfield = fidoService->createCharacteristic(
    FIDO_BLE_CHAR_SERVICE_REV_BF_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  charServiceRevBitfield->setCallbacks(this);
  uint8_t revBf = 0x80; // FIDO 2.0 standard bit
  charServiceRevBitfield->setValue(&revBf, 1);

  fidoService->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(FIDO_BLE_SERVICE_UUID);
  uint8_t fidoFlags = 0x80; // Bit 7 = FIDO2 / CTAP2 capability flag
  adv->setServiceData(NimBLEUUID((uint16_t)0xFFFD), std::string(reinterpret_cast<char*>(&fidoFlags), 1));
  adv->setMinInterval(0x00A0); // 100ms
  adv->setMaxInterval(0x00F0); // 150ms
  adv->setScanResponse(true);
  adv->start();

  active = true;
  Serial.println("[BLE-FIDO] Bluetooth Low Energy Passkey Service Active (0xFFFD)");
  return true;
}

void BLEFIDO::stop() {
  if (!active) return;

  if (server) {
    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::deinit(true);
    server = nullptr;
    fidoService = nullptr;
  }

  esp_bt_controller_disable();
  esp_bt_controller_deinit();

  active = false;
  isConnected = false;
  rxBuffer.clear();
  Serial.println("[BLE-FIDO] Bluetooth Low Energy Passkey Service Stopped & Memory Released");
}

void BLEFIDO::onConnect(NimBLEServer *pServer, ble_gap_conn_desc* desc) {
  isConnected = true;
  peerMtu = 23;
  int subs = charStatus ? (int)charStatus->getSubscribedCount() : -1;
  Serial.printf("[BLE-FIDO] Peer connected! Handle: %d, Addr: %s, StatusSubscribers: %d\n",
    desc->conn_handle, NimBLEAddress(desc->peer_ota_addr).toString().c_str(), subs);
}

void BLEFIDO::onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc* desc) {
  isConnected = false;
  rxBuffer.clear();
  Serial.printf("[BLE-FIDO] Peer disconnected (Handle: %d). Resuming advertising...\n",
    desc->conn_handle);
  NimBLEDevice::startAdvertising();
}

void BLEFIDO::onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) {
  peerMtu = MTU;
  Serial.printf("[BLE-FIDO] Negotiated MTU: %u\n", peerMtu);
}

void BLEFIDO::onWrite(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc) {
  if (pCharacteristic == charControlPoint) {
    std::string val = pCharacteristic->getValue();
    if (!val.empty()) {
      Serial.printf("[BLE-FIDO] RX ControlPoint Write (%u bytes): ", val.size());
      for (size_t i = 0; i < std::min((size_t)16, val.size()); i++) {
        Serial.printf("%02X ", (uint8_t)val[i]);
      }
      if (val.size() > 16) Serial.print("...");
      Serial.println();
      processIncomingFrame(reinterpret_cast<const uint8_t*>(val.data()), val.size());
    }
  } else if (pCharacteristic == charServiceRevBitfield) {
    std::string val = pCharacteristic->getValue();
    if (!val.empty()) {
      uint8_t negotiated = static_cast<uint8_t>(val[0]);
      charServiceRevBitfield->setValue(&negotiated, 1);
      Serial.printf("[BLE-FIDO] Service Revision Bitfield negotiated: 0x%02X\n", negotiated);
    }
  }
}

void BLEFIDO::onRead(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc) {
  std::string val = pCharacteristic->getValue();
  Serial.printf("[BLE-FIDO] READ on Characteristic %s (%u bytes)\n",
    pCharacteristic->getUUID().toString().c_str(), val.size());
}

void BLEFIDO::onSubscribe(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc* desc, uint16_t subValue) {
  Serial.printf("[BLE-FIDO] SUBSCRIBE on %s -> Value: 0x%04X (Notifications %s)\n",
    pCharacteristic->getUUID().toString().c_str(), subValue, (subValue & 1) ? "ENABLED" : "DISABLED");
}

void BLEFIDO::processIncomingFrame(const uint8_t *data, size_t len) {
  if (len < 1) return;

  uint8_t cmdOrSeq = data[0];
  if ((cmdOrSeq & 0x80) != 0) {
    // Initialization Packet: CMD (1 byte) + LEN (2 bytes) + Payload
    if (len < 3) return;
    currentCmd = cmdOrSeq;
    totalLen = (static_cast<uint16_t>(data[1]) << 8) | data[2];
    nextSeq = 0;
    rxBuffer.clear();

    size_t payloadInPacket = std::min(static_cast<size_t>(totalLen), len - 3);
    rxBuffer.insert(rxBuffer.end(), data + 3, data + 3 + payloadInPacket);

    if (rxBuffer.size() >= totalLen) {
      handleCompleteMessage(currentCmd, rxBuffer.data(), rxBuffer.size());
      rxBuffer.clear();
    }
  } else {
    // Continuation Packet: SEQ (1 byte) + Payload
    if (cmdOrSeq != nextSeq) {
      sendError(0x04); // ERR_INVALID_SEQ
      return;
    }
    nextSeq++;
    size_t remaining = totalLen - rxBuffer.size();
    size_t payloadInPacket = std::min(remaining, len - 1);
    rxBuffer.insert(rxBuffer.end(), data + 1, data + 1 + payloadInPacket);

    if (rxBuffer.size() >= totalLen) {
      handleCompleteMessage(currentCmd, rxBuffer.data(), rxBuffer.size());
      rxBuffer.clear();
    }
  }
}

void BLEFIDO::handleCompleteMessage(uint8_t cmd, const uint8_t *data, size_t len) {
  uint8_t rawCmd = cmd & 0x7F;
  switch (rawCmd) {
    case 0x01: // PING
      sendBlePacket(cmd, data, len);
      break;
    case 0x03: // MSG / CBOR -> Delegate to unified GlobalFidoEngine!
      GlobalFidoEngine.processCbor(
        data,
        len,
        [this](uint8_t status, const uint8_t *cbor, size_t cborLen) {
          this->sendCborResponse(status, cbor, cborLen);
        },
        [this](uint8_t statusUp) {
          this->sendBlePacket(0x82, &statusUp, 1);
        }
      );
      break;
    case 0x3E: // CANCEL
      GlobalFidoEngine.cancelPending();
      break;
    default:
      sendError(0x01); // ERR_INVALID_CMD
      break;
  }
}

bool BLEFIDO::sendBlePacket(uint8_t cmd, const uint8_t *payload, size_t len) {
  if (!charStatus) return false;

  size_t maxPacket = std::max(20, peerMtu - 3);
  size_t sent = 0;

  // First packet: CMD (1 byte) + LEN (2 bytes) + Payload
  std::vector<uint8_t> pkt;
  pkt.push_back(cmd);
  pkt.push_back((len >> 8) & 0xFF);
  pkt.push_back(len & 0xFF);

  size_t chunk = std::min(len, maxPacket - 3);
  if (chunk > 0 && payload) {
    pkt.insert(pkt.end(), payload, payload + chunk);
  }
  sent += chunk;

  charStatus->setValue(pkt.data(), pkt.size());
  charStatus->notify();

  // Continuation packets
  uint8_t seq = 0;
  while (sent < len) {
    delay(4);
    pkt.clear();
    pkt.push_back(seq++);
    chunk = std::min(len - sent, maxPacket - 1);
    pkt.insert(pkt.end(), payload + sent, payload + sent + chunk);
    sent += chunk;

    charStatus->setValue(pkt.data(), pkt.size());
    charStatus->notify();
  }

  return true;
}

bool BLEFIDO::sendCborResponse(uint8_t statusCode, const uint8_t *cborData, size_t len) {
  std::vector<uint8_t> resp;
  resp.push_back(statusCode);
  if (cborData && len > 0) {
    resp.insert(resp.end(), cborData, cborData + len);
  }
  return sendBlePacket(0x83, resp.data(), resp.size());
}

bool BLEFIDO::sendError(uint8_t errCode) {
  return sendBlePacket(0xBF, &errCode, 1);
}
