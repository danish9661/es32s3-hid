#pragma once
#include <Arduino.h>
#include "USBHID.h"
#include "FidoCrypto.h"

#if CONFIG_TINYUSB_HID_ENABLED

enum FidoPendingAction {
  FIDO_ACTION_NONE = 0,
  FIDO_ACTION_MAKE_CREDENTIAL,
  FIDO_ACTION_GET_ASSERTION,
  FIDO_ACTION_RESET
};

struct FidoRequestContext {
  uint32_t cid;
  uint8_t cmd;
  uint8_t ctap2Cmd;
  std::vector<uint8_t> clientDataHash;
  String rpId;
  String rpName;
  std::vector<uint8_t> userId;
  String userName;
  String userDisplayName;
  std::vector<std::vector<uint8_t>> allowList;
  bool upRequired;
  uint32_t requestTime;
};

class USBHIDFIDO : public USBHIDDevice {
private:
  USBHID hid;
  bool isInitialized;
  
  // Reassembly buffer for incoming CTAPHID packets
  uint32_t currentRxCid;
  uint8_t currentRxCmd;
  uint16_t totalRxLen;
  uint8_t nextRxSeq;
  std::vector<uint8_t> rxPayload;

  // Pending User Presence Touch State
  bool waitingForTouch;
  FidoPendingAction pendingAction;
  FidoRequestContext pendingReq;

  // Queue and worker task for asynchronous CTAP processing
  QueueHandle_t rxQueue;
  TaskHandle_t workerTaskHandle;

  void processIncomingPacket(const uint8_t *pkt, uint16_t pktLen);
  void processCompleteMessage(uint32_t cid, uint8_t cmd, const uint8_t *data, size_t len);
  void handleInit(uint32_t cid, const uint8_t *data, size_t len);
  void handlePing(uint32_t cid, const uint8_t *data, size_t len);
  void handleWink(uint32_t cid, const uint8_t *data, size_t len);
  void handleCbor(uint32_t cid, const uint8_t *data, size_t len);
  void handleMsg(uint32_t cid, const uint8_t *data, size_t len);

  // Ephemeral ECDH key agreement state for Client PIN protocol
  std::vector<uint8_t> ecdhPrivKey;
  std::vector<uint8_t> ecdhPubX;
  std::vector<uint8_t> ecdhPubY;

  // Enumeration state for Credential Management
  size_t enumRpIndex;
  size_t enumCredIndex;
  uint8_t enumRpIdHash[32];

  void handleGetInfo(uint32_t cid);
  void handleClientPin(uint32_t cid, const uint8_t *data, size_t len);
  void handleCredentialManagement(uint32_t cid, const uint8_t *data, size_t len);
  void handleLargeBlob(uint32_t cid, const uint8_t *data, size_t len);
  void executeMakeCredential();
  void executeGetAssertion();
  void executeReset();

  bool pureMode;

public:
  USBHIDFIDO();
  void begin(bool dedicatedMode = false);
  void end();
  void taskLoop();
  bool isDedicatedMode() const { return pureMode; }

  // USBHIDDevice overrides
  uint16_t _onGetDescriptor(uint8_t *dst) override;
  void _onOutput(uint8_t report_id, const uint8_t *buffer, uint16_t len) override;
  void _onSetFeature(uint8_t report_id, const uint8_t *buffer, uint16_t len) override;

  bool sendCtapPacket(uint32_t cid, uint8_t cmd, const uint8_t *payload, size_t len);
  bool sendCtapError(uint32_t cid, uint8_t errCode);
  bool sendCborResponse(uint32_t cid, uint8_t statusCode, const uint8_t *cborData, size_t len);

  // User presence touch interface
  bool isWaitingForTouch() const { return waitingForTouch; }
  FidoPendingAction getPendingAction() const { return pendingAction; }
  String getPendingRpId() const { return pendingReq.rpId; }
  void confirmTouch();
  void cancelPending();
  void checkTimeout();
};

extern USBHIDFIDO FIDO;

#endif
