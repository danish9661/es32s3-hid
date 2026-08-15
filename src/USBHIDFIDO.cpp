#include "USBHIDFIDO.h"

#if CONFIG_TINYUSB_HID_ENABLED

#define HID_REPORT_ID_FIDO 7

// Pure Dedicated FIDO Security Key Report Descriptor (No Report ID, 64-byte raw IN/OUT)
static const uint8_t fido_pure_report_desc[] = {
  0x06, 0xD0, 0xF1,                   // USAGE_PAGE (FIDO Alliance)
  0x09, 0x01,                         // USAGE (U2F / FIDO Authenticator)
  0xA1, 0x01,                         // COLLECTION (Application)
  0x09, 0x20,                         //   USAGE (Data In)
  0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
  0x26, 0xFF, 0x00,                   //   LOGICAL_MAXIMUM (255)
  0x75, 0x08,                         //   REPORT_SIZE (8)
  0x95, 0x40,                         //   REPORT_COUNT (64 bytes)
  0x81, 0x02,                         //   INPUT (Data,Var,Abs)
  0x09, 0x21,                         //   USAGE (Data Out)
  0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
  0x26, 0xFF, 0x00,                   //   LOGICAL_MAXIMUM (255)
  0x75, 0x08,                         //   REPORT_SIZE (8)
  0x95, 0x40,                         //   REPORT_COUNT (64 bytes)
  0x91, 0x02,                         //   OUTPUT (Data,Var,Abs)
  0xC0                                // END_COLLECTION
};

// Composite Mode Report Descriptor (Report ID 7)
static const uint8_t fido_report_desc[] = {
  0x06, 0xD0, 0xF1,                   // USAGE_PAGE (FIDO Alliance)
  0x09, 0x01,                         // USAGE (U2F / FIDO Authenticator)
  0xA1, 0x01,                         // COLLECTION (Application)
  0x85, HID_REPORT_ID_FIDO,           //   REPORT_ID (7)
  0x09, 0x20,                         //   USAGE (Data In)
  0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
  0x26, 0xFF, 0x00,                   //   LOGICAL_MAXIMUM (255)
  0x75, 0x08,                         //   REPORT_SIZE (8)
  0x95, 0x3F,                         //   REPORT_COUNT (63 bytes)
  0x81, 0x02,                         //   INPUT (Data,Var,Abs)
  0x09, 0x21,                         //   USAGE (Data Out)
  0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
  0x26, 0xFF, 0x00,                   //   LOGICAL_MAXIMUM (255)
  0x75, 0x08,                         //   REPORT_SIZE (8)
  0x95, 0x3F,                         //   REPORT_COUNT (63 bytes)
  0x91, 0x02,                         //   OUTPUT (Data,Var,Abs)
  0xC0                                // END_COLLECTION
};

// CTAPHID Command Constants
constexpr uint8_t CTAPHID_PING = 0x81;
constexpr uint8_t CTAPHID_MSG = 0x83;
constexpr uint8_t CTAPHID_LOCK = 0x84;
constexpr uint8_t CTAPHID_INIT = 0x86;
constexpr uint8_t CTAPHID_WINK = 0x88;
constexpr uint8_t CTAPHID_CBOR = 0x90;
constexpr uint8_t CTAPHID_CANCEL = 0x91;
constexpr uint8_t CTAPHID_ERROR = 0xBF;
constexpr uint8_t CTAPHID_KEEPALIVE = 0xBB;

// CTAPHID Capability Flags
constexpr uint8_t CAPFLAG_WINK = 0x01;
constexpr uint8_t CAPFLAG_CBOR = 0x04;
constexpr uint8_t CAPFLAG_NMSG = 0x08;

// FIDO global instance is declared in main.cpp to control registration order

// --- CBOR ENCODER HELPER ---
class CborWriter {
public:
  std::vector<uint8_t> buf;

  void writeTypeAndVal(uint8_t majorType, uint64_t val) {
    uint8_t type = majorType << 5;
    if (val < 24) {
      buf.push_back(type | static_cast<uint8_t>(val));
    } else if (val <= 0xFF) {
      buf.push_back(type | 24);
      buf.push_back(static_cast<uint8_t>(val));
    } else if (val <= 0xFFFF) {
      buf.push_back(type | 25);
      buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
      buf.push_back(static_cast<uint8_t>(val & 0xFF));
    } else if (val <= 0xFFFFFFFF) {
      buf.push_back(type | 26);
      buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
      buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
      buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
      buf.push_back(static_cast<uint8_t>(val & 0xFF));
    }
  }

  void writeInt(int64_t val) {
    if (val >= 0) {
      writeTypeAndVal(0, static_cast<uint64_t>(val));
    } else {
      writeTypeAndVal(1, static_cast<uint64_t>(-1 - val));
    }
  }

  void writeBytes(const uint8_t *data, size_t len) {
    writeTypeAndVal(2, len);
    buf.insert(buf.end(), data, data + len);
  }

  void writeText(const String &str) {
    writeTypeAndVal(3, str.length());
    buf.insert(buf.end(), str.begin(), str.end());
  }

  void writeArray(size_t size) { writeTypeAndVal(4, size); }
  void writeMap(size_t size) { writeTypeAndVal(5, size); }
  void writeBool(bool b) { buf.push_back(b ? 0xF5 : 0xF4); }
  void writeNull() { buf.push_back(0xF6); }
};

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
    waitingForTouch(false),
    pendingAction(FIDO_ACTION_NONE),
    rxQueue(nullptr),
    workerTaskHandle(nullptr)
{
  static bool registered = false;
  if (!registered) {
    registered = true;
    hid.addDevice(this, sizeof(fido_pure_report_desc));
  }
}

uint16_t USBHIDFIDO::_onGetDescriptor(uint8_t *dst) {
  memcpy(dst, fido_pure_report_desc, sizeof(fido_pure_report_desc));
  return sizeof(fido_pure_report_desc);
}

void USBHIDFIDO::begin(bool dedicatedMode) {
  if (isInitialized) return;
  isInitialized = true;
  FidoStore::init();
  if (!rxQueue) {
    rxQueue = xQueueCreate(16, 64);
  }
  if (!workerTaskHandle) {
    xTaskCreatePinnedToCore(fidoTaskWrapper, "FidoWorker", 8192, this, 2, &workerTaskHandle, 0);
  }
  hid.begin();
}

void USBHIDFIDO::end() {
  isInitialized = false;
}

void USBHIDFIDO::_onSetFeature(uint8_t report_id, const uint8_t *buffer, uint16_t len) {
  _onOutput(report_id, buffer, len);
}

void USBHIDFIDO::_onOutput(uint8_t report_id, const uint8_t *buffer, uint16_t len) {
  if (!rxQueue || len == 0) return;
  uint8_t pkt[64];
  memset(pkt, 0, sizeof(pkt));
  memcpy(pkt, buffer, std::min(static_cast<size_t>(len), static_cast<size_t>(64)));
  xQueueSend(rxQueue, pkt, 0);
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
                 static_cast<uint32_t>(pkt[3]);

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
    memset(report, 0, sizeof(report));
    report[0] = (cid >> 24) & 0xFF;
    report[1] = (cid >> 16) & 0xFF;
    report[2] = (cid >> 8)  & 0xFF;
    report[3] = cid & 0xFF;
    report[4] = seq++;

    chunk = std::min(len - sent, static_cast<size_t>(59));
    memcpy(report + 5, payload + sent, chunk);
    sent += chunk;

    delay(2);

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
  Serial.printf("[FIDO] CMD: 0x%02X, CID: 0x%08X, Len: %u\n", cmd, cid, len);
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
      sendCtapError(cid, 0x01); // CTAP1_ERR_INVALID_CMD
      break;
  }
}

void USBHIDFIDO::handleInit(uint32_t cid, const uint8_t *data, size_t len) {
  waitingForTouch = false;
  pendingAction = FIDO_ACTION_NONE;

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

void USBHIDFIDO::handleGetInfo(uint32_t cid) {
  CborWriter w;
  w.writeMap(6);

  // 01: versions -> ["FIDO_2_0"]
  w.writeInt(0x01);
  w.writeArray(1);
  w.writeText("FIDO_2_0");

  // 02: extensions -> ["credProps"]
  w.writeInt(0x02);
  w.writeArray(1);
  w.writeText("credProps");

  // 03: aaguid -> 16 zero bytes
  uint8_t aaguid[16];
  FidoStore::getAaguid(aaguid);
  w.writeInt(0x03);
  w.writeBytes(aaguid, 16);

  // 04: options -> { "rk": true, "up": true, "uv": true }
  w.writeInt(0x04);
  w.writeMap(3);
  w.writeText("rk"); w.writeBool(true);
  w.writeText("up"); w.writeBool(true);
  w.writeText("uv"); w.writeBool(true);

  // 05: maxMsgSize -> 1024
  w.writeInt(0x05);
  w.writeInt(1024);

  // 09: transports -> ["usb"]
  w.writeInt(0x09);
  w.writeArray(1);
  w.writeText("usb");

  sendCborResponse(cid, CTAP2_OK, w.buf.data(), w.buf.size());
}

void USBHIDFIDO::handleCbor(uint32_t cid, const uint8_t *data, size_t len) {
  if (len < 1) {
    sendCborResponse(cid, CTAP2_ERR_INVALID_LENGTH, nullptr, 0);
    return;
  }

  uint8_t ctap2Cmd = data[0];
  const uint8_t *cborPayload = data + 1;
  size_t cborLen = len - 1;

  if (ctap2Cmd == 0x04) { // authenticatorGetInfo
    handleGetInfo(cid);
    return;
  }

  if (ctap2Cmd == 0x01) { // authenticatorMakeCredential
    pendingReq.cid = cid;
    pendingReq.cmd = CTAPHID_CBOR;
    pendingReq.ctap2Cmd = ctap2Cmd;
    pendingReq.requestTime = millis();
    pendingReq.clientDataHash.assign(32, 0);
    pendingReq.userId.clear();
    pendingReq.userName = "";
    pendingReq.userDisplayName = "";
    pendingReq.rpId = "";

    // Extract RP ID from CBOR payload
    for (size_t i = 0; i + 4 < cborLen; i++) {
      if (cborPayload[i] == 0x62 && cborPayload[i+1] == 'i' && cborPayload[i+2] == 'd') { // "id"
        uint8_t t = cborPayload[i+3];
        if ((t & 0xE0) == 0x60) { // Text string
          size_t strLen = t & 0x1F;
          if (i + 4 + strLen <= cborLen) {
            pendingReq.rpId = String(reinterpret_cast<const char *>(cborPayload + i + 4)).substring(0, strLen);
          }
        }
        break;
      }
    }

    // Extract 32-byte ClientDataHash
    for (size_t i = 0; i + 34 <= cborLen; i++) {
      if (cborPayload[i] == 0x01 && cborPayload[i + 1] == 0x58 && cborPayload[i + 2] == 0x20) {
        pendingReq.clientDataHash.assign(cborPayload + i + 3, cborPayload + i + 3 + 32);
        break;
      }
    }

    // Await User Presence Touch (BOOT button)
    waitingForTouch = true;
    pendingAction = FIDO_ACTION_MAKE_CREDENTIAL;

    // Initial keepalive
    uint8_t statusUp = 0x01; // USER_PRESENCE_NEEDED
    sendCtapPacket(cid, CTAPHID_KEEPALIVE, &statusUp, 1);
    return;
  }

  if (ctap2Cmd == 0x02) { // authenticatorGetAssertion
    pendingReq.cid = cid;
    pendingReq.cmd = CTAPHID_CBOR;
    pendingReq.ctap2Cmd = ctap2Cmd;
    pendingReq.requestTime = millis();
    pendingReq.clientDataHash.assign(32, 0);
    pendingReq.allowList.clear();
    pendingReq.rpId = "";

    // 1. Extract rpId (Key 0x01: text string)
    for (size_t i = 0; i + 2 < cborLen; i++) {
      if (cborPayload[i] == 0x01 && (cborPayload[i+1] & 0xE0) == 0x60) {
        size_t strLen = cborPayload[i+1] & 0x1F;
        if (i + 2 + strLen <= cborLen) {
          pendingReq.rpId = String(reinterpret_cast<const char *>(cborPayload + i + 2)).substring(0, strLen);
        }
        break;
      }
    }

    // 2. Extract ClientDataHash (Key 0x02: 32-byte byte string -> 0x02 0x58 0x20)
    for (size_t i = 0; i + 34 <= cborLen; i++) {
      if (cborPayload[i] == 0x02 && cborPayload[i + 1] == 0x58 && cborPayload[i + 2] == 0x20) {
        pendingReq.clientDataHash.assign(cborPayload + i + 3, cborPayload + i + 3 + 32);
        break;
      }
    }

    // 3. Extract AllowList Credential IDs (search for "id" followed by 0x58 0x20)
    for (size_t i = 0; i + 37 <= cborLen; i++) {
      if (cborPayload[i] == 0x62 && cborPayload[i+1] == 'i' && cborPayload[i+2] == 'd' &&
          cborPayload[i+3] == 0x58 && cborPayload[i+4] == 0x20) {
        std::vector<uint8_t> credId(cborPayload + i + 5, cborPayload + i + 5 + 32);
        pendingReq.allowList.push_back(credId);
      }
    }

    // 4. Check if options.up == false (Silent presence check)
    pendingReq.upRequired = true;
    for (size_t i = 0; i + 3 < cborLen; i++) {
      if (cborPayload[i] == 0x62 && cborPayload[i+1] == 'u' && cborPayload[i+2] == 'p' && cborPayload[i+3] == 0xF4) {
        pendingReq.upRequired = false;
        break;
      }
    }

    if (!pendingReq.upRequired) {
      executeGetAssertion();
      return;
    }

    // Await User Presence Touch (BOOT button)
    waitingForTouch = true;
    pendingAction = FIDO_ACTION_GET_ASSERTION;

    // Initial keepalive
    uint8_t statusUp = 0x01; // USER_PRESENCE_NEEDED
    sendCtapPacket(cid, CTAPHID_KEEPALIVE, &statusUp, 1);
    return;
  }

  if (ctap2Cmd == 0x06) { // authenticatorReset
    pendingReq.cid = cid;
    pendingReq.cmd = CTAPHID_CBOR;
    pendingReq.ctap2Cmd = ctap2Cmd;
    pendingReq.requestTime = millis();
    waitingForTouch = true;
    pendingAction = FIDO_ACTION_RESET;

    // Send keepalive so Windows does not time out while waiting for touch
    uint8_t statusUp = 0x01; // USER_PRESENCE_NEEDED
    sendCtapPacket(cid, CTAPHID_KEEPALIVE, &statusUp, 1);
    return;
  }

  sendCborResponse(cid, CTAP2_ERR_INVALID_COMMAND, nullptr, 0);
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

void USBHIDFIDO::confirmTouch() {
  if (!waitingForTouch) return;
  waitingForTouch = false;

  if (pendingAction == FIDO_ACTION_MAKE_CREDENTIAL) {
    executeMakeCredential();
  } else if (pendingAction == FIDO_ACTION_GET_ASSERTION) {
    executeGetAssertion();
  } else if (pendingAction == FIDO_ACTION_RESET) {
    executeReset();
  }
  pendingAction = FIDO_ACTION_NONE;
}

void USBHIDFIDO::cancelPending() {
  if (waitingForTouch) {
    waitingForTouch = false;
    sendCborResponse(pendingReq.cid, CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
    pendingAction = FIDO_ACTION_NONE;
  }
}

void USBHIDFIDO::checkTimeout() {
  if (!waitingForTouch) return;

  static uint32_t lastKeepalive = 0;
  if (millis() - lastKeepalive > 150) {
    lastKeepalive = millis();
    uint8_t statusUp = 0x01; // USER_PRESENCE_NEEDED
    sendCtapPacket(pendingReq.cid, CTAPHID_KEEPALIVE, &statusUp, 1);
  }

  if (millis() - pendingReq.requestTime > 28000) {
    waitingForTouch = false;
    sendCborResponse(pendingReq.cid, CTAP2_ERR_USER_ACTION_TIMEOUT, nullptr, 0);
    pendingAction = FIDO_ACTION_NONE;
  }
}

void USBHIDFIDO::executeMakeCredential() {
  FidoCredential cred;
  String rp = pendingReq.rpId.isEmpty() ? "webauthn.io" : pendingReq.rpId;
  if (!FidoStore::createCredential(rp, pendingReq.userId, pendingReq.userName, pendingReq.userDisplayName, cred)) {
    sendCborResponse(pendingReq.cid, CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
    return;
  }

  // Construct Authenticator Data:
  // 1. RP ID Hash (32 bytes)
  uint8_t rpHash[32];
  FidoStore::sha256(reinterpret_cast<const uint8_t *>(cred.rpId.c_str()), cred.rpId.length(), rpHash);

  // 2. Flags (1 byte): UP (0x01) | UV (0x04) | AT (0x40) = 0x45
  uint8_t flags = FLAG_USER_PRESENT | FLAG_USER_VERIFIED | FLAG_ATTESTED_CRED_DATA;

  // 3. Signature Counter (4 bytes big-endian)
  uint32_t count = FidoStore::getNextGlobalCounter();
  uint8_t countBytes[4] = {
    static_cast<uint8_t>((count >> 24) & 0xFF),
    static_cast<uint8_t>((count >> 16) & 0xFF),
    static_cast<uint8_t>((count >> 8) & 0xFF),
    static_cast<uint8_t>(count & 0xFF)
  };

  // 4. Attested Credential Data:
  //    - AAGUID (16 bytes: all zero for standard self-attestation)
  //    - Credential ID Length (2 bytes big-endian)
  //    - Credential ID (32 bytes)
  //    - Credential Public Key (COSE_Key map in CBOR format)
  uint8_t aaguid[16];
  FidoStore::getAaguid(aaguid);

  CborWriter coseKey;
  coseKey.writeMap(5);
  coseKey.writeInt(1);  coseKey.writeInt(2);  // kty: 2 (EC2)
  coseKey.writeInt(3);  coseKey.writeInt(-7); // alg: -7 (ES256)
  coseKey.writeInt(-1); coseKey.writeInt(1);  // crv: 1 (P-256)
  coseKey.writeInt(-2); coseKey.writeBytes(cred.pubKeyX.data(), 32); // x
  coseKey.writeInt(-3); coseKey.writeBytes(cred.pubKeyY.data(), 32); // y

  std::vector<uint8_t> authData;
  authData.insert(authData.end(), rpHash, rpHash + 32);
  authData.push_back(flags);
  authData.insert(authData.end(), countBytes, countBytes + 4);
  authData.insert(authData.end(), aaguid, aaguid + 16);
  authData.push_back(0x00);
  authData.push_back(static_cast<uint8_t>(cred.credId.size()));
  authData.insert(authData.end(), cred.credId.begin(), cred.credId.end());
  authData.insert(authData.end(), coseKey.buf.begin(), coseKey.buf.end());

  // Sign: authData + clientDataHash
  std::vector<uint8_t> toSign = authData;
  toSign.insert(toSign.end(), pendingReq.clientDataHash.begin(), pendingReq.clientDataHash.end());

  std::vector<uint8_t> sig;
  FidoStore::signData(cred.privKey, toSign.data(), toSign.size(), sig);

  // Encode final CBOR response (fmt: "packed", authData: ..., attStmt: ..., unsignedExtensionOutputs: { "credProps": { "rk": true } })
  CborWriter resp;
  resp.writeMap(4);
  resp.writeInt(0x01); resp.writeText("packed"); // fmt: "packed"
  resp.writeInt(0x02); resp.writeBytes(authData.data(), authData.size()); // authData
  resp.writeInt(0x03); // attStmt
  resp.writeMap(2);
  resp.writeText("alg"); resp.writeInt(-7);      // len 3 ("alg") comes first
  resp.writeText("sig"); resp.writeBytes(sig.data(), sig.size()); // len 3 ("sig" > "alg")
  resp.writeInt(0x07); // unsignedExtensionOutputs
  resp.writeMap(1);
  resp.writeText("credProps");
  resp.writeMap(1);
  resp.writeText("rk");
  resp.writeBool(true);

  sendCborResponse(pendingReq.cid, CTAP2_OK, resp.buf.data(), resp.buf.size());
}

void USBHIDFIDO::executeGetAssertion() {
  FidoCredential *cred = nullptr;
  if (!pendingReq.allowList.empty()) {
    for (auto &id : pendingReq.allowList) {
      cred = FidoStore::findCredential(id);
      if (cred) break;
    }
  }
  if (!cred && !pendingReq.rpId.isEmpty()) {
    cred = FidoStore::findCredentialByRp(pendingReq.rpId);
  }
  if (!cred) {
    auto &all = FidoStore::getAllCredentials();
    if (!all.empty()) {
      cred = &all[0];
    }
  }

  if (!cred) {
    sendCborResponse(pendingReq.cid, CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
    return;
  }

  // Construct Authenticator Data
  uint8_t rpHash[32];
  String targetRp = pendingReq.rpId.isEmpty() ? cred->rpId : pendingReq.rpId;
  FidoStore::sha256(reinterpret_cast<const uint8_t *>(targetRp.c_str()), targetRp.length(), rpHash);
  uint8_t flags = pendingReq.upRequired ? (FLAG_USER_PRESENT | FLAG_USER_VERIFIED) : 0x00;

  uint32_t count = FidoStore::getNextGlobalCounter();
  cred->signCounter = count;
  FidoStore::saveToStorage();

  uint8_t countBytes[4] = {
    static_cast<uint8_t>((count >> 24) & 0xFF),
    static_cast<uint8_t>((count >> 16) & 0xFF),
    static_cast<uint8_t>((count >> 8) & 0xFF),
    static_cast<uint8_t>(count & 0xFF)
  };

  std::vector<uint8_t> authData;
  authData.insert(authData.end(), rpHash, rpHash + 32);
  authData.push_back(flags);
  authData.insert(authData.end(), countBytes, countBytes + 4);

  // Sign: authData + clientDataHash
  std::vector<uint8_t> toSign = authData;
  toSign.insert(toSign.end(), pendingReq.clientDataHash.begin(), pendingReq.clientDataHash.end());

  std::vector<uint8_t> sig;
  FidoStore::signData(cred->privKey, toSign.data(), toSign.size(), sig);

  // Encode final CBOR response
  CborWriter resp;
  bool hasUser = !cred->userId.empty() || !cred->userName.isEmpty();
  resp.writeMap(hasUser ? 4 : 3);
  resp.writeInt(0x01); // 01: credential descriptor
  resp.writeMap(2);
  resp.writeText("id"); resp.writeBytes(cred->credId.data(), cred->credId.size());
  resp.writeText("type"); resp.writeText("public-key");

  resp.writeInt(0x02); // 02: authData
  resp.writeBytes(authData.data(), authData.size());

  resp.writeInt(0x03); // 03: signature
  resp.writeBytes(sig.data(), sig.size());

  if (hasUser) {
    resp.writeInt(0x04); // 04: user
    size_t userKeys = (!cred->userId.empty() ? 1 : 0) + (!cred->userName.isEmpty() ? 1 : 0);
    resp.writeMap(userKeys);
    if (!cred->userId.empty()) {
      resp.writeText("id"); resp.writeBytes(cred->userId.data(), cred->userId.size());
    }
    if (!cred->userName.isEmpty()) {
      resp.writeText("name"); resp.writeText(cred->userName.c_str());
    }
  }

  sendCborResponse(pendingReq.cid, CTAP2_OK, resp.buf.data(), resp.buf.size());
}

void USBHIDFIDO::executeReset() {
  FidoStore::clearAll();
  sendCborResponse(pendingReq.cid, CTAP2_OK, nullptr, 0);
}

#endif
