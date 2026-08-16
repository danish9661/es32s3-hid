#pragma once
#include <Arduino.h>
#include "USBHID.h"
#include "FidoEngine.h"

#if CONFIG_TINYUSB_HID_ENABLED

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

  // User presence touch interface (delegates directly to unified FidoEngine)
  bool isWaitingForTouch() const { return GlobalFidoEngine.isWaitingForTouch(); }
  FidoPendingAction getPendingAction() const { return GlobalFidoEngine.getPendingAction(); }
  String getPendingRpId() const { return GlobalFidoEngine.getPendingRpId(); }
  void confirmTouch() { GlobalFidoEngine.confirmTouch(); }
  void cancelPending() { GlobalFidoEngine.cancelPending(); }
  void checkTimeout() { GlobalFidoEngine.checkTimeout(); }
};

extern USBHIDFIDO FIDO;

#endif
