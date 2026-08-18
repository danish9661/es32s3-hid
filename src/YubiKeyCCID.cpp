#include <Arduino.h>
#include "YubiKeyCCID.h"
#include "YubiKey.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

extern "C" {

#define CCID_EP_OUT 0x02
#define CCID_EP_IN  0x82
#define CCID_BUFSIZE 1024

static uint8_t ccid_rx_buf[CCID_BUFSIZE];
static uint8_t ccid_tx_buf[CCID_BUFSIZE];
static uint8_t ccid_ep_out = 0;
static uint8_t ccid_ep_in = 0;
static uint8_t ccid_rhport = 0;
static bool ccid_active = false;

static void ccid_init(void) {
  ccid_active = false;
}

static void ccid_reset(uint8_t rhport) {
  ccid_active = false;
  ccid_rhport = rhport;
}

static uint16_t ccid_open(uint8_t rhport, tusb_desc_interface_t const * itf_desc, uint16_t max_len) {
  if (itf_desc->bInterfaceClass != 0x0B) return 0;

  uint16_t const drv_len = sizeof(tusb_desc_interface_t) + 54 + (itf_desc->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
  if (max_len < drv_len) return 0;

  uint8_t const * p_desc = (uint8_t const *) itf_desc;
  p_desc += sizeof(tusb_desc_interface_t) + 54;

  for (int i = 0; i < itf_desc->bNumEndpoints; i++) {
    tusb_desc_endpoint_t const * desc_ep = (tusb_desc_endpoint_t const *) p_desc;
    if (desc_ep->bDescriptorType != TUSB_DESC_ENDPOINT) break;

    if (!usbd_edpt_open(rhport, desc_ep)) return 0;

    if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_OUT) {
      ccid_ep_out = desc_ep->bEndpointAddress;
    } else {
      ccid_ep_in = desc_ep->bEndpointAddress;
    }
    p_desc += sizeof(tusb_desc_endpoint_t);
  }

  ccid_rhport = rhport;
  ccid_active = true;

  if (ccid_ep_out) {
    usbd_edpt_xfer(rhport, ccid_ep_out, ccid_rx_buf, 64);
  }

  return drv_len;
}

static bool ccid_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request) {
  return true;
}

static bool ccid_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
  if (result != XFER_RESULT_SUCCESS) return true;

  if (ep_addr == ccid_ep_out) {
    std::vector<uint8_t> resp = YubiKey.processCcidMessage(ccid_rx_buf, xferred_bytes);

    if (!resp.empty() && ccid_ep_in) {
      size_t respLen = std::min(resp.size(), static_cast<size_t>(CCID_BUFSIZE));
      memcpy(ccid_tx_buf, resp.data(), respLen);
      usbd_edpt_xfer(rhport, ccid_ep_in, ccid_tx_buf, respLen);
    }

    usbd_edpt_xfer(rhport, ccid_ep_out, ccid_rx_buf, 64);
  }

  return true;
}

static usbd_class_driver_t const ccid_app_driver = {
  #if CFG_TUSB_DEBUG >= 2
  .name = "CCID",
  #endif
  .init = ccid_init,
  .reset = ccid_reset,
  .open = ccid_open,
  .control_xfer_cb = ccid_control_xfer_cb,
  .xfer_cb = ccid_xfer_cb,
  .sof = NULL
};

usbd_class_driver_t const * usbd_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1;
  return &ccid_app_driver;
}

void yubikey_ccid_init(void) {
  // Registered via TinyUSB
}

}
