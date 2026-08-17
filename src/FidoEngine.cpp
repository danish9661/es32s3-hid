#include "FidoEngine.h"

FidoEngine GlobalFidoEngine;

FidoEngine::FidoEngine()
  : waitingForTouch(false),
    pendingAction(FIDO_ACTION_NONE),
    enumRpIndex(0),
    enumCredIndex(0) {
  memset(enumRpIdHash, 0, sizeof(enumRpIdHash));
}

void FidoEngine::sendResponse(uint8_t statusCode, const uint8_t *cborData, size_t len) {
  if (activeResponseCb) {
    activeResponseCb(statusCode, cborData, len);
  }
}

void FidoEngine::sendKeepalive(uint8_t status) {
  if (activeKeepaliveCb) {
    activeKeepaliveCb(status);
  }
}

void FidoEngine::processCbor(const uint8_t *data, size_t len, FidoResponseCallback respCb, FidoKeepaliveCallback keepaliveCb) {
  activeResponseCb = respCb;
  activeKeepaliveCb = keepaliveCb;

  if (len < 1) {
    sendResponse(CTAP2_ERR_INVALID_LENGTH, nullptr, 0);
    return;
  }

  uint8_t ctap2Cmd = data[0];
  const uint8_t *cborPayload = data + 1;
  size_t cborLen = len - 1;

  if (ctap2Cmd == 0x04) { // authenticatorGetInfo
    handleGetInfo();
    return;
  }

  if (ctap2Cmd == 0x01) { // authenticatorMakeCredential
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

    // Extract requested extensions
    pendingReq.requestedCredProps = false;
    pendingReq.requestedCredProtect = false;
    pendingReq.credProtectPolicy = 1;
    for (size_t i = 0; i + 9 <= cborLen; i++) {
      if (memcmp(cborPayload + i, "credProps", 9) == 0) {
        pendingReq.requestedCredProps = true;
      }
      if (i + 11 <= cborLen && memcmp(cborPayload + i, "credProtect", 11) == 0) {
        pendingReq.requestedCredProtect = true;
        uint8_t nextVal = (i + 11 < cborLen) ? cborPayload[i + 11] : 1;
        if (nextVal >= 1 && nextVal <= 3) {
          pendingReq.credProtectPolicy = nextVal;
        } else if (i + 12 < cborLen && cborPayload[i + 12] >= 1 && cborPayload[i + 12] <= 3) {
          pendingReq.credProtectPolicy = cborPayload[i + 12];
        }
      }
    }

    // Extract requested algorithm (-7 = ES256, -8 = EdDSA)
    pendingReq.requestedAlgorithm = -7;
    for (size_t i = 0; i + 4 < cborLen; i++) {
      if (cborPayload[i] == 0x63 && cborPayload[i+1] == 'a' && cborPayload[i+2] == 'l' && cborPayload[i+3] == 'g') { // "alg"
        if (i + 4 < cborLen && cborPayload[i+4] == 0x27) { // CBOR negative int -8
          pendingReq.requestedAlgorithm = -8;
          break;
        }
      }
    }

    // Extract User entity (Key 0x03)
    for (size_t i = 0; i + 4 < cborLen; i++) {
      if (cborPayload[i] == 0x64 && memcmp(cborPayload + i + 1, "name", 4) == 0) { // "name"
        uint8_t t = (i + 5 < cborLen) ? cborPayload[i + 5] : 0;
        if ((t & 0xE0) == 0x60) {
          size_t sLen = t & 0x1F;
          if (i + 6 + sLen <= cborLen) {
            pendingReq.userName = String(reinterpret_cast<const char *>(cborPayload + i + 6)).substring(0, sLen);
          }
        }
      }
      if (cborPayload[i] == 0x62 && cborPayload[i+1] == 'i' && cborPayload[i+2] == 'd') { // "id"
        if (i + 3 < cborLen && (cborPayload[i + 3] & 0xE0) == 0x40 && pendingReq.userId.empty()) {
          size_t bLen = cborPayload[i + 3] & 0x1F;
          if (cborPayload[i + 3] == 0x58 && i + 4 < cborLen) {
            bLen = cborPayload[i + 4];
            if (i + 5 + bLen <= cborLen) {
              pendingReq.userId.assign(cborPayload + i + 5, cborPayload + i + 5 + bLen);
            }
          } else if (i + 4 + bLen <= cborLen) {
            pendingReq.userId.assign(cborPayload + i + 4, cborPayload + i + 4 + bLen);
          }
        }
      }
    }

    // Detect if PIN / UV auth token param is present (Key 0x08)
    pendingReq.hasPinUvAuth = false;
    for (size_t i = 0; i + 18 <= cborLen; i++) {
      if (cborPayload[i] == 0x08 && cborPayload[i+1] == 0x50) {
        pendingReq.hasPinUvAuth = true;
        break;
      }
    }

    // Await User Presence Touch (BOOT button)
    waitingForTouch = true;
    pendingAction = FIDO_ACTION_MAKE_CREDENTIAL;

    // Initial keepalive
    sendKeepalive(0x01); // USER_PRESENCE_NEEDED
    return;
  }

  if (ctap2Cmd == 0x02) { // authenticatorGetAssertion
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

    // Detect if PIN / UV auth token param is present (Key 0x06)
    pendingReq.hasPinUvAuth = false;
    for (size_t i = 0; i + 18 <= cborLen; i++) {
      if (cborPayload[i] == 0x06 && cborPayload[i+1] == 0x50) {
        pendingReq.hasPinUvAuth = true;
        break;
      }
    }

    // 4. Check if options.up == false (Silent presence check)
    pendingReq.upRequired = true;
    for (size_t i = 0; i + 3 < cborLen; i++) {
      if (cborPayload[i] == 0x62 && cborPayload[i+1] == 'u' && cborPayload[i+2] == 'p' && cborPayload[i+3] == 0xF4) { // "up": false
        pendingReq.upRequired = false;
        break;
      }
    }

    if (!pendingReq.upRequired) {
      // Probe / silent check: execute immediately without requiring physical touch
      executeGetAssertion();
      return;
    }

    // Await User Presence Touch (BOOT button)
    waitingForTouch = true;
    pendingAction = FIDO_ACTION_GET_ASSERTION;

    sendKeepalive(0x01); // USER_PRESENCE_NEEDED
    return;
  }

  if (ctap2Cmd == 0x04) { // authenticatorGetInfo
    handleGetInfo();
    return;
  }

  if (ctap2Cmd == 0x06) { // authenticatorClientPIN
    handleClientPin(cborPayload, cborLen);
    return;
  }

  if (ctap2Cmd == 0x07) { // authenticatorReset
    pendingReq.ctap2Cmd = ctap2Cmd;
    pendingReq.requestTime = millis();
    waitingForTouch = true;
    pendingAction = FIDO_ACTION_RESET;
    sendKeepalive(0x01);
    return;
  }

  if (ctap2Cmd == 0x0A) { // authenticatorCredentialManagement (CTAP2 standard 0x0A)
    handleCredentialManagement(cborPayload, cborLen);
    return;
  }

  if (ctap2Cmd == 0x0C || ctap2Cmd == 0x0B) { // authenticatorLargeBlobs (CTAP2 standard 0x0C)
    handleLargeBlob(cborPayload, cborLen);
    return;
  }

  // Unsupported command
  sendResponse(CTAP2_ERR_UNSUPPORTED_OPTION, nullptr, 0);
}

void FidoEngine::handleGetInfo() {
  CborWriter w;
  w.writeMap(11);

  // 01: versions -> ["U2F_V2", "FIDO_2_0", "FIDO_2_1"]
  w.writeInt(0x01);
  w.writeArray(3);
  w.writeText("U2F_V2");
  w.writeText("FIDO_2_0");
  w.writeText("FIDO_2_1");

  // 02: extensions -> ["credProps", "hmac-secret", "largeBlobKey", "credProtect"]
  w.writeInt(0x02);
  w.writeArray(4);
  w.writeText("credProps");
  w.writeText("hmac-secret");
  w.writeText("largeBlobKey");
  w.writeText("credProtect");

  // 03: aaguid -> 16 zero bytes
  uint8_t aaguid[16];
  FidoStore::getAaguid(aaguid);
  w.writeInt(0x03);
  w.writeBytes(aaguid, 16);

  // 04: options -> { "rk": true, "up": true, "uv": getEmulateUv(), "plat": false, "credMgmt": true, "clientPin": isPinSet(), "largeBlobs": true }
  w.writeInt(0x04);
  w.writeMap(7);
  w.writeText("rk"); w.writeBool(true);
  w.writeText("up"); w.writeBool(true);
  w.writeText("uv"); w.writeBool(FidoStore::getEmulateUv());
  w.writeText("plat"); w.writeBool(false);
  w.writeText("credMgmt"); w.writeBool(true);
  w.writeText("clientPin"); w.writeBool(FidoStore::isPinSet());
  w.writeText("largeBlobs"); w.writeBool(true);

  // 05: maxMsgSize -> 1024
  w.writeInt(0x05);
  w.writeInt(1024);

  // 06: pinUvAuthProtocols -> [1]
  w.writeInt(0x06);
  w.writeArray(1);
  w.writeInt(1);

  // 08: maxCredentialIdLength -> 128
  w.writeInt(0x08);
  w.writeInt(128);

  // 09: transports -> ["usb", "ble"]
  w.writeInt(0x09);
  w.writeArray(2);
  w.writeText("usb");
  w.writeText("ble");

  // 0A (10): algorithms -> [{ "alg": -7, "type": "public-key" }, { "alg": -8, "type": "public-key" }]
  w.writeInt(10);
  w.writeArray(2);
  w.writeMap(2);
  w.writeText("alg"); w.writeInt(-7);
  w.writeText("type"); w.writeText("public-key");
  w.writeMap(2);
  w.writeText("alg"); w.writeInt(-8);
  w.writeText("type"); w.writeText("public-key");

  // 0B (11): maxLargeBlob -> 2048
  w.writeInt(11);
  w.writeInt(2048);

  // 0D (13): minPinLength -> 4
  w.writeInt(13);
  w.writeInt(4);

  sendResponse(CTAP2_OK, w.buf.data(), w.buf.size());
}

void FidoEngine::executeMakeCredential() {
  FidoCredential cred;
  String rp = pendingReq.rpId.isEmpty() ? "webauthn.io" : pendingReq.rpId;
  if (!FidoStore::createCredential(rp, pendingReq.userId, pendingReq.userName, pendingReq.userDisplayName, cred, pendingReq.requestedAlgorithm)) {
    sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
    return;
  }
  cred.credProtect = pendingReq.credProtectPolicy;
  FidoStore::saveToStorage();

  // Construct Authenticator Data
  uint8_t rpHash[32];
  FidoStore::sha256(reinterpret_cast<const uint8_t *>(cred.rpId.c_str()), cred.rpId.length(), rpHash);

  // Extension Data (embedded in authData ONLY when extensions were explicitly requested by client)
  CborWriter extWriter;
  size_t extCount = 0;
  if (pendingReq.requestedCredProps) extCount++;
  if (pendingReq.requestedCredProtect) extCount++;

  bool hasExtensions = (extCount > 0);
  if (hasExtensions) {
    extWriter.writeMap(extCount);
    // Canonical CBOR order: "credProps" (length 9) before "credProtect" (length 11)
    if (pendingReq.requestedCredProps) {
      extWriter.writeText("credProps");
      extWriter.writeMap(1);
      extWriter.writeText("rk");
      extWriter.writeBool(true);
    }
    if (pendingReq.requestedCredProtect) {
      extWriter.writeText("credProtect");
      extWriter.writeInt(pendingReq.credProtectPolicy);
    }
  }

  bool isUv = FidoStore::getEmulateUv() || pendingReq.hasPinUvAuth || FidoStore::isPinTokenValid() || FidoStore::isPinSet();
  uint8_t flags = FLAG_USER_PRESENT | (isUv ? FLAG_USER_VERIFIED : 0) | FLAG_ATTESTED_CRED_DATA | (hasExtensions ? FLAG_EXTENSION_DATA : 0);

  uint32_t count = FidoStore::getNextGlobalCounter();
  uint8_t countBytes[4] = {
    static_cast<uint8_t>((count >> 24) & 0xFF),
    static_cast<uint8_t>((count >> 16) & 0xFF),
    static_cast<uint8_t>((count >> 8) & 0xFF),
    static_cast<uint8_t>(count & 0xFF)
  };

  uint8_t aaguid[16];
  FidoStore::getAaguid(aaguid);

  CborWriter coseKey;
  if (cred.algorithm == -8) { // EdDSA (Ed25519)
    coseKey.writeMap(4);
    coseKey.writeInt(1);  coseKey.writeInt(1);  // kty: 1 (OKP)
    coseKey.writeInt(3);  coseKey.writeInt(-8); // alg: -8 (EdDSA)
    coseKey.writeInt(-1); coseKey.writeInt(6);  // crv: 6 (Ed25519)
    coseKey.writeInt(-2); coseKey.writeBytes(cred.pubKeyX.data(), 32); // x (32-byte public key)
  } else { // ES256 (NIST P-256)
    coseKey.writeMap(5);
    coseKey.writeInt(1);  coseKey.writeInt(2);  // kty: 2 (EC2)
    coseKey.writeInt(3);  coseKey.writeInt(-7); // alg: -7 (ES256)
    coseKey.writeInt(-1); coseKey.writeInt(1);  // crv: 1 (P-256)
    coseKey.writeInt(-2); coseKey.writeBytes(cred.pubKeyX.data(), 32); // x
    coseKey.writeInt(-3); coseKey.writeBytes(cred.pubKeyY.data(), 32); // y
  }

  std::vector<uint8_t> authData;
  authData.insert(authData.end(), rpHash, rpHash + 32);
  authData.push_back(flags);
  authData.insert(authData.end(), countBytes, countBytes + 4);
  authData.insert(authData.end(), aaguid, aaguid + 16);
  authData.push_back(0x00);
  authData.push_back(static_cast<uint8_t>(cred.credId.size()));
  authData.insert(authData.end(), cred.credId.begin(), cred.credId.end());
  authData.insert(authData.end(), coseKey.buf.begin(), coseKey.buf.end());
  if (hasExtensions) {
    authData.insert(authData.end(), extWriter.buf.begin(), extWriter.buf.end());
  }

  // Sign: authData + clientDataHash
  std::vector<uint8_t> toSign = authData;
  toSign.insert(toSign.end(), pendingReq.clientDataHash.begin(), pendingReq.clientDataHash.end());

  std::vector<uint8_t> sig;
  if (cred.algorithm == -8) {
    FidoStore::signEd25519(cred.privKey, toSign.data(), toSign.size(), sig);
  } else {
    FidoStore::signData(cred.privKey, toSign.data(), toSign.size(), sig);
  }

  // Encode standard CTAP2 MakeCredential CBOR response (fmt, authData, attStmt)
  CborWriter resp;
  resp.writeMap(3);
  resp.writeInt(0x01); resp.writeText("packed");
  resp.writeInt(0x02); resp.writeBytes(authData.data(), authData.size());
  resp.writeInt(0x03);
  resp.writeMap(2);
  resp.writeText("alg"); resp.writeInt(cred.algorithm);
  resp.writeText("sig"); resp.writeBytes(sig.data(), sig.size());

  sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
}

void FidoEngine::executeGetAssertion() {
  if (pendingReq.allowList.empty()) {
    if (FidoStore::getAllCredentials().empty()) {
      sendResponse(CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
      return;
    }
  }

  // Find matching credential
  FidoCredential *cred = nullptr;
  if (!pendingReq.allowList.empty()) {
    for (const auto &id : pendingReq.allowList) {
      cred = FidoStore::findCredential(id);
      if (cred) break;
    }
    if (!cred) {
      sendResponse(CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
      return;
    }
  } else if (!pendingReq.rpId.isEmpty()) {
    auto all = FidoStore::getAllCredentials();
    for (auto &c : all) {
      if (c.rpId.equalsIgnoreCase(pendingReq.rpId)) {
        cred = FidoStore::findCredential(c.credId);
        break;
      }
    }
  }

  if (!cred) {
    sendResponse(CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
    return;
  }

  // Enforce credProtect policy if required
  if (cred->credProtect == 2 || cred->credProtect == 3) {
    if (!FidoStore::getEmulateUv() && FidoStore::isPinSet() && !FidoStore::isPinTokenValid()) {
      sendResponse(CTAP2_ERR_PIN_REQUIRED, nullptr, 0);
      return;
    }
  }

  // Construct Authenticator Data
  uint8_t rpHash[32];
  String targetRp = pendingReq.rpId.isEmpty() ? cred->rpId : pendingReq.rpId;
  FidoStore::sha256(reinterpret_cast<const uint8_t *>(targetRp.c_str()), targetRp.length(), rpHash);
  bool isUv = FidoStore::getEmulateUv() || pendingReq.hasPinUvAuth || FidoStore::isPinTokenValid() || FidoStore::isPinSet();
  uint8_t flags = (pendingReq.upRequired ? FLAG_USER_PRESENT : 0) | (isUv ? FLAG_USER_VERIFIED : 0);

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
  if (cred->algorithm == -8) {
    FidoStore::signEd25519(cred->privKey, toSign.data(), toSign.size(), sig);
  } else {
    FidoStore::signData(cred->privKey, toSign.data(), toSign.size(), sig);
  }

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

  sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
}

static bool skipCborValue(const uint8_t *cbor, size_t cborLen, size_t &pos) {
  if (pos >= cborLen) return false;
  uint8_t initial = cbor[pos++];
  uint8_t major = initial >> 5;
  uint8_t info = initial & 0x1F;
  size_t valLen = 0;

  if (info < 24) {
    valLen = info;
  } else if (info == 24) {
    if (pos >= cborLen) return false;
    valLen = cbor[pos++];
  } else if (info == 25) {
    if (pos + 2 > cborLen) return false;
    valLen = (static_cast<size_t>(cbor[pos]) << 8) | cbor[pos+1];
    pos += 2;
  } else if (info == 26) {
    if (pos + 4 > cborLen) return false;
    valLen = (static_cast<size_t>(cbor[pos]) << 24) | (static_cast<size_t>(cbor[pos+1]) << 16) | (static_cast<size_t>(cbor[pos+2]) << 8) | cbor[pos+3];
    pos += 4;
  } else if (info >= 28) {
    return true; // simple value (true/false/null)
  }

  if (major == 0 || major == 1 || major == 7) {
    return true;
  } else if (major == 2 || major == 3) {
    if (pos + valLen > cborLen) return false;
    pos += valLen;
    return true;
  } else if (major == 4) {
    for (size_t i = 0; i < valLen; i++) {
      if (!skipCborValue(cbor, cborLen, pos)) return false;
    }
    return true;
  } else if (major == 5) {
    for (size_t i = 0; i < valLen; i++) {
      if (!skipCborValue(cbor, cborLen, pos)) return false;
      if (!skipCborValue(cbor, cborLen, pos)) return false;
    }
    return true;
  }
  return true;
}

void FidoEngine::handleClientPin(const uint8_t *cbor, size_t cborLen) {
  if (cborLen < 1) {
    sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
    return;
  }

  uint8_t protocol = 1;
  uint8_t subCmd = 0;
  std::vector<uint8_t> peerPubX;
  std::vector<uint8_t> peerPubY;
  std::vector<uint8_t> pinAuth;
  std::vector<uint8_t> newPinEnc;
  std::vector<uint8_t> pinHashEnc;
  std::vector<uint8_t> currentPinHashEnc;

  if (cborLen > 0 && (cbor[0] & 0xE0) == 0xA0) {
    size_t mapSize = cbor[0] & 0x1F;
    size_t pos = 1;
    for (size_t k = 0; k < mapSize && pos < cborLen; k++) {
      uint8_t key = cbor[pos++];
      if (key == 0x01 && pos < cborLen) { // 1: pinUvAuthProtocol (uint)
        protocol = cbor[pos++];
      } else if (key == 0x02 && pos < cborLen) { // 2: subCommand (uint)
        subCmd = cbor[pos++];
      } else if (key == 0x03 && pos < cborLen) { // 3: keyAgreement (COSE map)
        if ((cbor[pos] & 0xE0) == 0xA0) {
          size_t innerMap = cbor[pos++] & 0x1F;
          for (size_t ik = 0; ik < innerMap && pos < cborLen; ik++) {
            uint8_t ikey = cbor[pos++];
            if (ikey == 0x21 && pos + 33 <= cborLen) { // -2: x (32 bytes)
              if (cbor[pos] == 0x58 && cbor[pos+1] == 0x20) {
                peerPubX.assign(cbor + pos + 2, cbor + pos + 34);
                pos += 34;
              } else {
                skipCborValue(cbor, cborLen, pos);
              }
            } else if (ikey == 0x22 && pos + 33 <= cborLen) { // -3: y (32 bytes)
              if (cbor[pos] == 0x58 && cbor[pos+1] == 0x20) {
                peerPubY.assign(cbor + pos + 2, cbor + pos + 34);
                pos += 34;
              } else {
                skipCborValue(cbor, cborLen, pos);
              }
            } else {
              skipCborValue(cbor, cborLen, pos);
            }
          }
        } else {
          skipCborValue(cbor, cborLen, pos);
        }
      } else if (key == 0x04 && pos < cborLen) { // 4: pinUvAuthParam (16 bytes)
        if (cbor[pos] == 0x50 && pos + 17 <= cborLen) {
          pinAuth.assign(cbor + pos + 1, cbor + pos + 17);
          pos += 17;
        } else if (cbor[pos] == 0x58 && cbor[pos+1] == 0x10 && pos + 18 <= cborLen) {
          pinAuth.assign(cbor + pos + 2, cbor + pos + 18);
          pos += 18;
        } else {
          skipCborValue(cbor, cborLen, pos);
        }
      } else if (key == 0x05 && pos < cborLen) { // 5: newPinEnc (64 bytes)
        if (cbor[pos] == 0x58 && cbor[pos+1] == 0x40 && pos + 66 <= cborLen) {
          newPinEnc.assign(cbor + pos + 2, cbor + pos + 66);
          pos += 66;
        } else {
          skipCborValue(cbor, cborLen, pos);
        }
      } else if (key == 0x06 && pos < cborLen) { // 6: pinHashEnc / currentPinHashEnc (16 bytes)
        if (cbor[pos] == 0x50 && pos + 17 <= cborLen) {
          if (subCmd == 0x04) currentPinHashEnc.assign(cbor + pos + 1, cbor + pos + 17);
          else pinHashEnc.assign(cbor + pos + 1, cbor + pos + 17);
          pos += 17;
        } else if (cbor[pos] == 0x58 && cbor[pos+1] == 0x10 && pos + 18 <= cborLen) {
          if (subCmd == 0x04) currentPinHashEnc.assign(cbor + pos + 2, cbor + pos + 18);
          else pinHashEnc.assign(cbor + pos + 2, cbor + pos + 18);
          pos += 18;
        } else {
          skipCborValue(cbor, cborLen, pos);
        }
      } else {
        skipCborValue(cbor, cborLen, pos);
      }
    }
  }

  if (protocol != 1) {
    sendResponse(CTAP2_ERR_PIN_AUTH_INVALID, nullptr, 0);
    return;
  }

  if (subCmd == 0x01) { // getPinRetries
    CborWriter resp;
    resp.writeMap(2);
    resp.writeInt(0x03); resp.writeInt(FidoStore::getPinRetries());
    resp.writeInt(0x04); resp.writeBool(false);
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x07) { // getUVRetries
    CborWriter resp;
    resp.writeMap(1);
    resp.writeInt(0x05); resp.writeInt(8);
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x02) { // getKeyAgreement
    if (!FidoStore::generateKeyPair(ecdhPrivKey, ecdhPubX, ecdhPubY)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    CborWriter resp;
    resp.writeMap(1);
    resp.writeInt(0x01);
    resp.writeMap(5);
    resp.writeInt(1);  resp.writeInt(2);   // kty: 2 (EC2)
    resp.writeInt(3);  resp.writeInt(-25); // alg: -25 (ECDH-ES w/ HKDF-256 per CTAP2 spec)
    resp.writeInt(-1); resp.writeInt(1);   // crv: 1 (P-256)
    resp.writeInt(-2); resp.writeBytes(ecdhPubX.data(), 32);
    resp.writeInt(-3); resp.writeBytes(ecdhPubY.data(), 32);
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x03) { // setPIN
    if (FidoStore::isPinSet()) {
      sendResponse(CTAP2_ERR_PIN_AUTH_INVALID, nullptr, 0);
      return;
    }
    if (peerPubX.size() != 32 || peerPubY.size() != 32 || pinAuth.size() != 16 || newPinEnc.size() != 64) {
      sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
      return;
    }
    std::vector<uint8_t> sharedSecret;
    if (!FidoStore::computeSharedSecret(ecdhPrivKey, peerPubX, peerPubY, sharedSecret)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    uint8_t expectedAuth[32];
    FidoStore::hmacSha256(sharedSecret.data(), 32, newPinEnc.data(), newPinEnc.size(), expectedAuth);
    if (memcmp(expectedAuth, pinAuth.data(), 16) != 0) {
      sendResponse(CTAP2_ERR_PIN_AUTH_INVALID, nullptr, 0);
      return;
    }
    std::vector<uint8_t> decryptedPin;
    if (!FidoStore::aes256CbcDecrypt(sharedSecret.data(), nullptr, newPinEnc.data(), 64, decryptedPin)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    size_t pinLen = 0;
    while (pinLen < 64 && decryptedPin[pinLen] != 0) {
      pinLen++;
    }
    if (pinLen < 4) {
      sendResponse(CTAP2_ERR_PIN_POLICY_VIOLATION, nullptr, 0);
      return;
    }
    FidoStore::setPin(decryptedPin.data(), pinLen);
    sendResponse(CTAP2_OK, nullptr, 0);
    return;
  }

  if (subCmd == 0x04) { // changePIN
    if (!FidoStore::isPinSet()) {
      sendResponse(CTAP2_ERR_PIN_NOT_SET, nullptr, 0);
      return;
    }
    if (peerPubX.size() != 32 || peerPubY.size() != 32 || pinAuth.size() != 16 || newPinEnc.size() != 64 || currentPinHashEnc.size() != 16) {
      sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
      return;
    }
    std::vector<uint8_t> sharedSecret;
    if (!FidoStore::computeSharedSecret(ecdhPrivKey, peerPubX, peerPubY, sharedSecret)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    std::vector<uint8_t> decCurrentPinHash;
    if (!FidoStore::aes256CbcDecrypt(sharedSecret.data(), nullptr, currentPinHashEnc.data(), 16, decCurrentPinHash)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    if (!FidoStore::verifyPinHash(decCurrentPinHash.data())) {
      sendResponse(CTAP2_ERR_PIN_INVALID, nullptr, 0);
      return;
    }
    std::vector<uint8_t> toAuth = newPinEnc;
    toAuth.insert(toAuth.end(), currentPinHashEnc.begin(), currentPinHashEnc.end());
    uint8_t expectedAuth[32];
    FidoStore::hmacSha256(sharedSecret.data(), 32, toAuth.data(), toAuth.size(), expectedAuth);
    if (memcmp(expectedAuth, pinAuth.data(), 16) != 0) {
      sendResponse(CTAP2_ERR_PIN_AUTH_INVALID, nullptr, 0);
      return;
    }
    std::vector<uint8_t> decryptedPin;
    if (!FidoStore::aes256CbcDecrypt(sharedSecret.data(), nullptr, newPinEnc.data(), 64, decryptedPin)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    size_t pinLen = 0;
    while (pinLen < 64 && decryptedPin[pinLen] != 0) {
      pinLen++;
    }
    if (pinLen < 4) {
      sendResponse(CTAP2_ERR_PIN_POLICY_VIOLATION, nullptr, 0);
      return;
    }
    FidoStore::setPin(decryptedPin.data(), pinLen);
    sendResponse(CTAP2_OK, nullptr, 0);
    return;
  }

  if (subCmd == 0x05 || subCmd == 0x06 || subCmd == 0x09) { // getPinToken / getPinUvAuthTokenUsingPinWithPermissions
    if (!FidoStore::isPinSet()) {
      sendResponse(CTAP2_ERR_PIN_NOT_SET, nullptr, 0);
      return;
    }
    if (peerPubX.size() != 32 || peerPubY.size() != 32 || pinHashEnc.size() != 16) {
      sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
      return;
    }
    std::vector<uint8_t> sharedSecret;
    if (!FidoStore::computeSharedSecret(ecdhPrivKey, peerPubX, peerPubY, sharedSecret)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    std::vector<uint8_t> decPinHash;
    if (!FidoStore::aes256CbcDecrypt(sharedSecret.data(), nullptr, pinHashEnc.data(), 16, decPinHash)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    if (!FidoStore::verifyPinHash(decPinHash.data())) {
      sendResponse(CTAP2_ERR_PIN_INVALID, nullptr, 0);
      return;
    }
    uint8_t pinToken[32];
    FidoStore::getPinToken(pinToken);
    std::vector<uint8_t> pinTokenEnc;
    if (!FidoStore::aes256CbcEncrypt(sharedSecret.data(), nullptr, pinToken, 32, pinTokenEnc)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    CborWriter resp;
    resp.writeMap(1);
    resp.writeInt(0x02);
    resp.writeBytes(pinTokenEnc.data(), pinTokenEnc.size());
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x08) { // getPinUvAuthTokenUsingUvWithPermissions
    if (peerPubX.size() != 32 || peerPubY.size() != 32) {
      sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
      return;
    }
    std::vector<uint8_t> sharedSecret;
    if (!FidoStore::computeSharedSecret(ecdhPrivKey, peerPubX, peerPubY, sharedSecret)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    uint8_t pinToken[32];
    FidoStore::getPinToken(pinToken);
    std::vector<uint8_t> pinTokenEnc;
    if (!FidoStore::aes256CbcEncrypt(sharedSecret.data(), nullptr, pinToken, 32, pinTokenEnc)) {
      sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
      return;
    }
    CborWriter resp;
    resp.writeMap(1);
    resp.writeInt(0x02);
    resp.writeBytes(pinTokenEnc.data(), pinTokenEnc.size());
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  sendResponse(CTAP2_ERR_UNSUPPORTED_OPTION, nullptr, 0);
}

void FidoEngine::handleLargeBlob(const uint8_t *cbor, size_t cborLen) {
  if (cborLen < 1) {
    sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
    return;
  }

  uint32_t getLen = 0;
  bool isGet = false;
  uint32_t offset = 0;
  uint32_t length = 0;
  std::vector<uint8_t> setToWrite;

  if (cborLen > 0 && (cbor[0] & 0xE0) == 0xA0) {
    size_t mapSize = cbor[0] & 0x1F;
    size_t pos = 1;
    for (size_t k = 0; k < mapSize && pos < cborLen; k++) {
      uint8_t key = cbor[pos++];
      if (key == 0x01 && pos < cborLen) { // get (uint)
        isGet = true;
        uint8_t b = cbor[pos++];
        if ((b & 0xE0) == 0x00) {
          if (b <= 23) getLen = b;
          else if (b == 0x18 && pos < cborLen) getLen = cbor[pos++];
          else if (b == 0x19 && pos + 1 < cborLen) { getLen = (static_cast<uint32_t>(cbor[pos]) << 8) | cbor[pos+1]; pos += 2; }
        }
      } else if (key == 0x02 && pos < cborLen) { // set (bytes)
        uint8_t b = cbor[pos++];
        if ((b & 0xE0) == 0x40) {
          size_t bLen = b & 0x1F;
          if (b == 0x58 && pos < cborLen) bLen = cbor[pos++];
          else if (b == 0x59 && pos + 1 < cborLen) { bLen = (static_cast<size_t>(cbor[pos]) << 8) | cbor[pos+1]; pos += 2; }
          if (pos + bLen <= cborLen) {
            setToWrite.assign(cbor + pos, cbor + pos + bLen);
            pos += bLen;
          }
        }
      } else if (key == 0x03 && pos < cborLen) { // offset (uint)
        uint8_t b = cbor[pos++];
        if ((b & 0xE0) == 0x00) {
          if (b <= 23) offset = b;
          else if (b == 0x18 && pos < cborLen) offset = cbor[pos++];
          else if (b == 0x19 && pos + 1 < cborLen) { offset = (static_cast<uint32_t>(cbor[pos]) << 8) | cbor[pos+1]; pos += 2; }
        }
      } else if (key == 0x04 && pos < cborLen) { // length (uint)
        uint8_t b = cbor[pos++];
        if ((b & 0xE0) == 0x00) {
          if (b <= 23) length = b;
          else if (b == 0x18 && pos < cborLen) length = cbor[pos++];
          else if (b == 0x19 && pos + 1 < cborLen) { length = (static_cast<uint32_t>(cbor[pos]) << 8) | cbor[pos+1]; pos += 2; }
        }
      } else {
        skipCborValue(cbor, cborLen, pos);
      }
    }
  }

  if (isGet) {
    std::vector<uint8_t> blob;
    FidoStore::getLargeBlob(blob);
    std::vector<uint8_t> chunk;
    if (offset < blob.size()) {
      size_t avail = blob.size() - offset;
      size_t toRead = (getLen > 0) ? std::min(static_cast<size_t>(getLen), avail) : avail;
      chunk.assign(blob.begin() + offset, blob.begin() + offset + toRead);
    }
    CborWriter resp;
    resp.writeMap(1);
    resp.writeInt(0x01);
    resp.writeBytes(chunk.data(), chunk.size());
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (!setToWrite.empty()) {
    std::vector<uint8_t> blob;
    if (offset > 0) {
      FidoStore::getLargeBlob(blob);
    }
    size_t targetSize = (offset == 0 && length > 0) ? length : (offset + setToWrite.size());
    if (blob.size() < targetSize) {
      blob.resize(targetSize);
    }
    memcpy(blob.data() + offset, setToWrite.data(), setToWrite.size());
    if (offset == 0 && length > 0 && blob.size() > length) {
      blob.resize(length);
    }
    FidoStore::setLargeBlob(blob.data(), blob.size());
    CborWriter resp;
    resp.writeMap(0);
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  CborWriter resp;
  resp.writeMap(0);
  sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
}

void FidoEngine::handleCredentialManagement(const uint8_t *cbor, size_t cborLen) {
  if (cborLen < 1) {
    sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
    return;
  }

  uint8_t subCmd = 0;
  uint8_t targetRpHash[32];
  bool hasRpHash = false;
  std::vector<uint8_t> targetCredId;

  if (cborLen > 0 && (cbor[0] & 0xE0) == 0xA0) {
    size_t mapSize = cbor[0] & 0x1F;
    size_t pos = 1;
    for (size_t k = 0; k < mapSize && pos < cborLen; k++) {
      uint8_t key = cbor[pos++];
      if (key == 0x01 && pos < cborLen) {
        subCmd = cbor[pos++];
      } else {
        skipCborValue(cbor, cborLen, pos);
      }
    }
  }

  for (size_t i = 0; i + 34 <= cborLen; i++) {
    if (cbor[i] == 0x01 && cbor[i+1] == 0x58 && cbor[i+2] == 0x20) {
      memcpy(targetRpHash, cbor + i + 3, 32);
      hasRpHash = true;
      break;
    }
  }

  for (size_t i = 0; i + 37 <= cborLen; i++) {
    if (cbor[i] == 0x62 && cbor[i+1] == 'i' && cbor[i+2] == 'd' &&
        cbor[i+3] == 0x58 && cbor[i+4] == 0x20) {
      targetCredId.assign(cbor + i + 5, cbor + i + 5 + 32);
      break;
    }
  }

  auto &allCreds = FidoStore::getAllCredentials();

  if (subCmd == 0x01) { // getCredsMetadata
    CborWriter resp;
    resp.writeMap(2);
    resp.writeInt(0x01); resp.writeInt(allCreds.size());
    resp.writeInt(0x02); resp.writeInt(50 - std::min(static_cast<size_t>(50), allCreds.size()));
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x02) { // enumerateRPsBegin
    if (allCreds.empty()) {
      sendResponse(CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
      return;
    }
    std::vector<String> uniqueRps;
    for (auto &c : allCreds) {
      bool exists = false;
      for (auto &r : uniqueRps) { if (r == c.rpId) { exists = true; break; } }
      if (!exists) uniqueRps.push_back(c.rpId);
    }
    enumRpIndex = 0;
    String rp = uniqueRps[0];
    uint8_t rHash[32];
    FidoStore::sha256(reinterpret_cast<const uint8_t*>(rp.c_str()), rp.length(), rHash);

    CborWriter resp;
    resp.writeMap(3);
    resp.writeInt(0x03);
    resp.writeMap(1);
    resp.writeText("id"); resp.writeText(rp.c_str());
    resp.writeInt(0x04);
    resp.writeBytes(rHash, 32);
    resp.writeInt(0x05);
    resp.writeInt(uniqueRps.size());
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x03) { // enumerateRPsGetNextRP
    std::vector<String> uniqueRps;
    for (auto &c : allCreds) {
      bool exists = false;
      for (auto &r : uniqueRps) { if (r == c.rpId) { exists = true; break; } }
      if (!exists) uniqueRps.push_back(c.rpId);
    }
    enumRpIndex++;
    if (enumRpIndex >= uniqueRps.size()) {
      sendResponse(CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
      return;
    }
    String rp = uniqueRps[enumRpIndex];
    uint8_t rHash[32];
    FidoStore::sha256(reinterpret_cast<const uint8_t*>(rp.c_str()), rp.length(), rHash);

    CborWriter resp;
    resp.writeMap(2);
    resp.writeInt(0x03);
    resp.writeMap(1);
    resp.writeText("id"); resp.writeText(rp.c_str());
    resp.writeInt(0x04);
    resp.writeBytes(rHash, 32);
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x04) { // enumerateCredentialsBegin
    if (!hasRpHash) {
      sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
      return;
    }
    memcpy(enumRpIdHash, targetRpHash, 32);
    enumCredIndex = 0;

    std::vector<FidoCredential*> rpCreds;
    for (auto &c : allCreds) {
      uint8_t rHash[32];
      FidoStore::sha256(reinterpret_cast<const uint8_t*>(c.rpId.c_str()), c.rpId.length(), rHash);
      if (memcmp(rHash, enumRpIdHash, 32) == 0) {
        rpCreds.push_back(&c);
      }
    }
    if (rpCreds.empty()) {
      sendResponse(CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
      return;
    }
    auto *c = rpCreds[0];
    CborWriter resp;
    resp.writeMap(4);
    resp.writeInt(0x06);
    resp.writeMap((c->userId.empty() ? 0 : 1) + (c->userName.isEmpty() ? 0 : 1));
    if (!c->userId.empty()) { resp.writeText("id"); resp.writeBytes(c->userId.data(), c->userId.size()); }
    if (!c->userName.isEmpty()) { resp.writeText("name"); resp.writeText(c->userName.c_str()); }

    resp.writeInt(0x07);
    resp.writeMap(2);
    resp.writeText("id"); resp.writeBytes(c->credId.data(), c->credId.size());
    resp.writeText("type"); resp.writeText("public-key");

    resp.writeInt(0x08);
    resp.writeMap(5);
    resp.writeInt(1);  resp.writeInt(2);
    resp.writeInt(3);  resp.writeInt(-7);
    resp.writeInt(-1); resp.writeInt(1);
    resp.writeInt(-2); resp.writeBytes(c->pubKeyX.data(), 32);
    resp.writeInt(-3); resp.writeBytes(c->pubKeyY.data(), 32);

    resp.writeInt(0x09);
    resp.writeInt(rpCreds.size());

    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x05) { // enumerateCredentialsGetNextCredential
    std::vector<FidoCredential*> rpCreds;
    for (auto &c : allCreds) {
      uint8_t rHash[32];
      FidoStore::sha256(reinterpret_cast<const uint8_t*>(c.rpId.c_str()), c.rpId.length(), rHash);
      if (memcmp(rHash, enumRpIdHash, 32) == 0) {
        rpCreds.push_back(&c);
      }
    }
    enumCredIndex++;
    if (enumCredIndex >= rpCreds.size()) {
      sendResponse(CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
      return;
    }
    auto *c = rpCreds[enumCredIndex];
    CborWriter resp;
    resp.writeMap(3);
    resp.writeInt(0x06);
    resp.writeMap((c->userId.empty() ? 0 : 1) + (c->userName.isEmpty() ? 0 : 1));
    if (!c->userId.empty()) { resp.writeText("id"); resp.writeBytes(c->userId.data(), c->userId.size()); }
    if (!c->userName.isEmpty()) { resp.writeText("name"); resp.writeText(c->userName.c_str()); }

    resp.writeInt(0x07);
    resp.writeMap(2);
    resp.writeText("id"); resp.writeBytes(c->credId.data(), c->credId.size());
    resp.writeText("type"); resp.writeText("public-key");

    resp.writeInt(0x08);
    resp.writeMap(5);
    resp.writeInt(1);  resp.writeInt(2);
    resp.writeInt(3);  resp.writeInt(-7);
    resp.writeInt(-1); resp.writeInt(1);
    resp.writeInt(-2); resp.writeBytes(c->pubKeyX.data(), 32);
    resp.writeInt(-3); resp.writeBytes(c->pubKeyY.data(), 32);

    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  if (subCmd == 0x06) { // deleteCredential
    if (targetCredId.size() != 32) {
      sendResponse(CTAP2_ERR_INVALID_PARAMETER, nullptr, 0);
      return;
    }
    if (!FidoStore::deleteCredential(targetCredId)) {
      sendResponse(CTAP2_ERR_NO_CREDENTIALS, nullptr, 0);
      return;
    }
    CborWriter resp;
    resp.writeMap(0);
    sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
    return;
  }

  sendResponse(CTAP2_ERR_UNSUPPORTED_OPTION, nullptr, 0);
}

void FidoEngine::executeReset() {
  FidoStore::clearAll();
  CborWriter resp;
  resp.writeMap(0);
  sendResponse(CTAP2_OK, resp.buf.data(), resp.buf.size());
}

void FidoEngine::confirmTouch() {
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

void FidoEngine::cancelPending() {
  if (waitingForTouch) {
    waitingForTouch = false;
    sendResponse(CTAP2_ERR_OPERATION_DENIED, nullptr, 0);
    pendingAction = FIDO_ACTION_NONE;
  }
}

void FidoEngine::checkTimeout() {
  if (!waitingForTouch) return;

  static uint32_t lastKeepalive = 0;
  if (millis() - lastKeepalive > 150) {
    lastKeepalive = millis();
    sendKeepalive(0x01); // USER_PRESENCE_NEEDED
  }

  if (millis() - pendingReq.requestTime > 28000) {
    waitingForTouch = false;
    sendResponse(CTAP2_ERR_USER_ACTION_TIMEOUT, nullptr, 0);
    pendingAction = FIDO_ACTION_NONE;
  }
}
