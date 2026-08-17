#pragma once
#include <Arduino.h>
#include <functional>
#include "FidoCrypto.h"

enum FidoPendingAction {
  FIDO_ACTION_NONE = 0,
  FIDO_ACTION_MAKE_CREDENTIAL,
  FIDO_ACTION_GET_ASSERTION,
  FIDO_ACTION_RESET
};

struct FidoRequestContext {
  uint8_t ctap2Cmd;
  std::vector<uint8_t> clientDataHash;
  String rpId;
  String rpName;
  std::vector<uint8_t> userId;
  String userName;
  String userDisplayName;
  std::vector<std::vector<uint8_t>> allowList;
  bool upRequired;
  bool hasPinUvAuth;
  bool requestedCredProps;
  bool requestedCredProtect;
  uint8_t credProtectPolicy;
  int requestedAlgorithm;
  uint32_t requestTime;
};

typedef std::function<void(uint8_t statusCode, const uint8_t *cborData, size_t len)> FidoResponseCallback;
typedef std::function<void(uint8_t status)> FidoKeepaliveCallback;

class FidoEngine {
private:
  bool waitingForTouch;
  FidoPendingAction pendingAction;
  FidoRequestContext pendingReq;

  FidoResponseCallback activeResponseCb;
  FidoKeepaliveCallback activeKeepaliveCb;

  // Ephemeral ECDH key agreement state for Client PIN protocol
  std::vector<uint8_t> ecdhPrivKey;
  std::vector<uint8_t> ecdhPubX;
  std::vector<uint8_t> ecdhPubY;

  // Enumeration state for Credential Management
  size_t enumRpIndex;
  size_t enumCredIndex;
  uint8_t enumRpIdHash[32];

  void handleGetInfo();
  void handleClientPin(const uint8_t *data, size_t len);
  void handleCredentialManagement(const uint8_t *data, size_t len);
  void handleLargeBlob(const uint8_t *data, size_t len);

  void executeMakeCredential();
  void executeGetAssertion();
  void executeReset();

  void sendResponse(uint8_t statusCode, const uint8_t *cborData = nullptr, size_t len = 0);
  void sendKeepalive(uint8_t status);

public:
  FidoEngine();
  void processCbor(const uint8_t *data, size_t len, FidoResponseCallback respCb, FidoKeepaliveCallback keepaliveCb);

  bool isWaitingForTouch() const { return waitingForTouch; }
  FidoPendingAction getPendingAction() const { return pendingAction; }
  String getPendingRpId() const { return pendingReq.rpId; }

  void confirmTouch();
  void cancelPending();
  void checkTimeout();
};

extern FidoEngine GlobalFidoEngine;
