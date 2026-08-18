#include "USBHIDFIDO.h"
#include "YubiKey.h"

#if CONFIG_TINYUSB_HID_ENABLED

// FIDO / CTAP2 HID Report Descriptor (Usage Page: 0xF1D0 FIDO_USAGE_PAGE, Usage: 0x01 FIDO_USAGE_CTAPHID)
static const uint8_t fido_hid_report_descriptor[] = {
  0x06, 0xD0, 0xF1, // USAGE_PAGE (FIDO Alliance)
  0x09, 0x01,       // USAGE (U2F / CTAPHID Authenticator Device)
  0xA1, 0x01,       // COLLECTION (Application)
  
  // Input Report (Endpoint IN): 64 bytes
  0x09, 0x20,       //   USAGE (Data In)
  0x15, 0x00,       //   LOGICAL_MINIMUM (0)
  0x26, 0xFF, 0x00, //   LOGICAL_MAXIMUM (255)
  0x75, 0x08,       //   REPORT_SIZE (8 bits)
  0x95, 0x40,       //   REPORT_COUNT (64 bytes)
  0x81, 0x02,       //   INPUT (Data, Var, Abs)
  
  // Output Report (Endpoint OUT): 64 bytes
  0x09, 0x21,       //   USAGE (Data Out)
  0x15, 0x00,       //   LOGICAL_MINIMUM (0)
  0x26, 0xFF, 0x00, //   LOGICAL_MAXIMUM (255)
  0x75, 0x08,       //   REPORT_SIZE (8 bits)
  0x95, 0x40,       //   REPORT_COUNT (64 bytes)
  0x91, 0x02,       //   OUTPUT (Data, Var, Abs)
  
  0xC0              // END_COLLECTION
};

// CTAPHID Command Codes
constexpr uint8_t CTAPHID_PING      = 0x81;
constexpr uint8_t CTAPHID_MSG       = 0x83;
constexpr uint8_t CTAPHID_INIT      = 0x86;
constexpr uint8_t CTAPHID_WINK      = 0x88;
constexpr uint8_t CTAPHID_CBOR      = 0x90;
constexpr uint8_t CTAPHID_CANCEL    = 0x91;
constexpr uint8_t CTAPHID_KEEPALIVE = 0xBB;
constexpr uint8_t CTAPHID_ERROR     = 0xBF;

// CTAPHID Capability Flags
constexpr uint8_t CAPFLAG_WINK = 0x01;
constexpr uint8_t CAPFLAG_CBOR = 0x04;
constexpr uint8_t CAPFLAG_NMSG = 0x08;

static void fidoTaskWrapper(void *param) {
  static_cast<USBHIDFIDO *>(param)->taskLoop();
}

USBHIDFIDO::USBHIDFIDO()
  : hid(),
    isInitialized(false),
    currentRxCid(0),
    currentRxCmd(0),
    totalRxLen(0),
    nextRxSeq(0),
    rxQueue(nullptr),
    workerTaskHandle(nullptr),
    pureMode(false) {}

void USBHIDFIDO::begin(bool dedicatedMode) {
  pureMode = dedicatedMode;
  if (isInitialized) return;
  
  FidoStore::init();
  hid.addDevice(this, sizeof(fido_hid_report_descriptor));
  hid.begin();

  rxQueue = xQueueCreate(16, 64);
  xTaskCreatePinnedToCore(fidoTaskWrapper, "FidoWorker", 8192, this, 3, &workerTaskHandle, 1);

  isInitialized = true;
  Serial.printf("[FIDO] %s USB CTAPHID Device Started!\n", pureMode ? "Dedicated" : "Composite");
}

void USBHIDFIDO::end() {
  if (!isInitialized) return;
  if (workerTaskHandle) {
    vTaskDelete(workerTaskHandle);
    workerTaskHandle = nullptr;
  }
  if (rxQueue) {
    vQueueDelete(rxQueue);
    rxQueue = nullptr;
  }
  isInitialized = false;
}

uint16_t USBHIDFIDO::_onGetDescriptor(uint8_t *dst) {
  memcpy(dst, fido_hid_report_descriptor, sizeof(fido_hid_report_descriptor));
  return sizeof(fido_hid_report_descriptor);
}

void USBHIDFIDO::_onOutput(uint8_t report_id, const uint8_t *buffer, uint16_t len) {
  if (!rxQueue || len == 0) return;
  uint8_t pkt[64];
  memset(pkt, 0, 64);
  memcpy(pkt, buffer, std::min(static_cast<size_t>(len), static_cast<size_t>(64)));
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(rxQueue, pkt, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void USBHIDFIDO::_onSetFeature(uint8_t report_id, const uint8_t *buffer, uint16_t len) {
  _onOutput(report_id, buffer, len);
}

void USBHIDFIDO::taskLoop() {
  uint8_t pkt[64];
  while (true) {
    if (xQueueReceive(rxQueue, pkt, portMAX_DELAY) == pdTRUE) {
      processIncomingPacket(pkt, 64);
    }
  }
}

void USBHIDFIDO::processIncomingPacket(const uint8_t *pkt, uint16_t pktLen) {
  if (pktLen < 7) return;

  uint32_t cid = (static_cast<uint32_t>(pkt[0]) << 24) |
                 (static_cast<uint32_t>(pkt[1]) << 16) |
                 (static_cast<uint32_t>(pkt[2]) << 8)  |
                 (static_cast<uint32_t>(pkt[3]));
  uint8_t cmdOrSeq = pkt[4];

  if ((cmdOrSeq & 0x80) != 0) {
    // Initialization Packet
    currentRxCid = cid;
    currentRxCmd = cmdOrSeq;
    totalRxLen = (static_cast<uint16_t>(pkt[5]) << 8) | pkt[6];
    nextRxSeq = 0;
    rxPayload.clear();

    size_t payloadInPacket = std::min(static_cast<size_t>(totalRxLen), static_cast<size_t>(pktLen - 7));
    rxPayload.insert(rxPayload.end(), pkt + 7, pkt + 7 + payloadInPacket);

    if (rxPayload.size() >= totalRxLen) {
      processCompleteMessage(currentRxCid, currentRxCmd, rxPayload.data(), rxPayload.size());
      rxPayload.clear();
      totalRxLen = 0;
      currentRxCmd = 0;
    }
  } else {
    // Continuation Packet
    if (cid != currentRxCid || cmdOrSeq != nextRxSeq) {
      sendCtapError(cid, 0x04); // CTAP1_ERR_INVALID_SEQ
      return;
    }
    nextRxSeq++;
    size_t remaining = totalRxLen - rxPayload.size();
    size_t payloadInPacket = std::min(remaining, static_cast<size_t>(pktLen - 5));
    rxPayload.insert(rxPayload.end(), pkt + 5, pkt + 5 + payloadInPacket);

    if (rxPayload.size() >= totalRxLen) {
      processCompleteMessage(currentRxCid, currentRxCmd, rxPayload.data(), rxPayload.size());
      rxPayload.clear();
      totalRxLen = 0;
      currentRxCmd = 0;
    }
  }
}

bool USBHIDFIDO::sendCtapPacket(uint32_t cid, uint8_t cmd, const uint8_t *payload, size_t len) {
  uint8_t report[64];
  size_t sent = 0;

  // First packet (Initialization packet)
  memset(report, 0, sizeof(report));
  report[0] = (cid >> 24) & 0xFF;
  report[1] = (cid >> 16) & 0xFF;
  report[2] = (cid >> 8)  & 0xFF;
  report[3] = cid & 0xFF;
  report[4] = cmd;
  report[5] = (len >> 8) & 0xFF;
  report[6] = len & 0xFF;

  size_t chunk = std::min(len, static_cast<size_t>(57));
  if (chunk > 0 && payload) {
    memcpy(report + 7, payload, chunk);
  }
  sent += chunk;

  if (!hid.SendReport(0, report, sizeof(report))) {
    return false;
  }

  // Continuation packets if payload exceeds 57 bytes
  uint8_t seq = 0;
  while (sent < len) {
    vTaskDelay(pdMS_TO_TICKS(4));

    memset(report, 0, sizeof(report));
    report[0] = (cid >> 24) & 0xFF;
    report[1] = (cid >> 16) & 0xFF;
    report[2] = (cid >> 8)  & 0xFF;
    report[3] = cid & 0xFF;
    report[4] = seq++;

    chunk = std::min(len - sent, static_cast<size_t>(59));
    memcpy(report + 5, payload + sent, chunk);
    sent += chunk;

    if (!hid.SendReport(0, report, sizeof(report))) {
      return false;
    }
  }

  return true;
}

bool USBHIDFIDO::sendCtapError(uint32_t cid, uint8_t errCode) {
  return sendCtapPacket(cid, CTAPHID_ERROR, &errCode, 1);
}

bool USBHIDFIDO::sendCborResponse(uint32_t cid, uint8_t statusCode, const uint8_t *cborData, size_t len) {
  std::vector<uint8_t> resp;
  resp.push_back(statusCode);
  if (cborData && len > 0) {
    resp.insert(resp.end(), cborData, cborData + len);
  }
  return sendCtapPacket(cid, CTAPHID_CBOR, resp.data(), resp.size());
}

void USBHIDFIDO::processCompleteMessage(uint32_t cid, uint8_t cmd, const uint8_t *data, size_t len) {
  Serial.printf("[FIDO-USB] CMD: 0x%02X, CID: 0x%08X, Len: %u\n", cmd, cid, len);
  switch (cmd) {
    case CTAPHID_INIT:
      handleInit(cid, data, len);
      break;
    case CTAPHID_PING:
      handlePing(cid, data, len);
      break;
    case CTAPHID_WINK:
      handleWink(cid, data, len);
      break;
    case CTAPHID_CBOR:
      handleCbor(cid, data, len);
      break;
    case CTAPHID_MSG:
      handleMsg(cid, data, len);
      break;
    case CTAPHID_CANCEL:
      cancelPending();
      break;
    default:
      if (cmd >= 0xC0) { // CTAPHID Vendor Commands (Yubico Management 0xC0, OATH 0xC1, OTP 0xC2)
        std::vector<uint8_t> apduResp = YubiKey.processApdu(data, len);
        sendCtapPacket(cid, cmd, apduResp.data(), apduResp.size());
      } else {
        sendCtapError(cid, 0x01); // CTAP1_ERR_INVALID_CMD
      }
      break;
  }
}

void USBHIDFIDO::handleInit(uint32_t cid, const uint8_t *data, size_t len) {
  if (len < 8) {
    sendCtapError(cid, 0x03); // CTAP1_ERR_INVALID_LEN
    return;
  }

  static uint32_t nextChannel = 0x10203040;
  uint32_t assignedCid = (cid == 0xFFFFFFFF) ? nextChannel++ : cid;

  uint8_t resp[17];
  memcpy(resp, data, 8); // Echo 8-byte nonce
  resp[8]  = (assignedCid >> 24) & 0xFF;
  resp[9]  = (assignedCid >> 16) & 0xFF;
  resp[10] = (assignedCid >> 8)  & 0xFF;
  resp[11] = assignedCid & 0xFF;
  resp[12] = 2; // CTAPHID Protocol Version 2
  resp[13] = 2; // Device Version Major
  resp[14] = 0; // Device Version Minor
  resp[15] = 0; // Device Build
  resp[16] = CAPFLAG_WINK | CAPFLAG_CBOR | CAPFLAG_NMSG;

  sendCtapPacket(cid, CTAPHID_INIT, resp, sizeof(resp));
}

void USBHIDFIDO::handlePing(uint32_t cid, const uint8_t *data, size_t len) {
  sendCtapPacket(cid, CTAPHID_PING, data, len);
}

void USBHIDFIDO::handleWink(uint32_t cid, const uint8_t *data, size_t len) {
  sendCtapPacket(cid, CTAPHID_WINK, nullptr, 0);
}

void USBHIDFIDO::handleCbor(uint32_t cid, const uint8_t *data, size_t len) {
  GlobalFidoEngine.processCbor(
    data,
    len,
    [this, cid](uint8_t status, const uint8_t *cbor, size_t cborLen) {
      this->sendCborResponse(cid, status, cbor, cborLen);
    },
    [this, cid](uint8_t statusUp) {
      this->sendCtapPacket(cid, CTAPHID_KEEPALIVE, &statusUp, 1);
    }
  );
}

void USBHIDFIDO::handleMsg(uint32_t cid, const uint8_t *data, size_t len) {
  if (len < 4) {
    sendCtapError(cid, 0x03);
    return;
  }
  uint8_t ins = data[1];
  if (ins == 0x00) { // U2F_VERSION
    const char *ver = "U2F_V2";
    uint8_t resp[8];
    memcpy(resp, ver, 6);
    resp[6] = 0x90; // SW_NO_ERROR
    resp[7] = 0x00;
    sendCtapPacket(cid, CTAPHID_MSG, resp, 8);
  } else {
    // Return standard ISO 7816-4 SW_INS_NOT_SUPPORTED (0x6D00)
    uint8_t sw[2] = { 0x6D, 0x00 };
    sendCtapPacket(cid, CTAPHID_MSG, sw, sizeof(sw));
  }
}

#endif
