#include <Arduino.h>
#include "YubiKeyCCID.h"
#include "YubiKey.h"
#include "esp32-hal-tinyusb.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

extern "C" {

#define CCID_DESC_LEN 77
#define CCID_BUFSIZE  1024

static uint8_t ccid_rx_buf[CCID_BUFSIZE];
static uint8_t ccid_tx_buf[CCID_BUFSIZE];
static uint8_t ccid_ep_out = 0;
static uint8_t ccid_ep_in = 0;
static uint8_t ccid_rhport = 0;
static bool ccid_active = false;

// --------------------------------------------------------------------------
// CCID SmartCard Interface Descriptor Loader (bInterfaceClass = 0x0B)
// --------------------------------------------------------------------------
static uint16_t ccid_load_descriptor(uint8_t *dst, uint8_t *itf) {
  uint8_t ep_num = tinyusb_get_free_duplex_endpoint();
  if (ep_num == 0) ep_num = 2;

  ccid_ep_out = ep_num;
  ccid_ep_in = 0x80 | ep_num;

  uint8_t const desc[] = {
    // 1. Interface Descriptor (9 bytes)
    0x09,        // bLength
    0x04,        // bDescriptorType (INTERFACE)
    *itf,        // bInterfaceNumber
    0x00,        // bAlternateSetting
    0x02,        // bNumEndpoints (Bulk OUT & Bulk IN)
    0x0B,        // bInterfaceClass (Smart Card / CCID: 0x0B)
    0x00,        // bInterfaceSubClass
    0x00,        // bInterfaceProtocol
    0x00,        // iInterface

    // 2. CCID Functional Descriptor (54 bytes)
    0x36,        // bLength (54 bytes)
    0x21,        // bDescriptorType (Functional)
    0x10, 0x01,  // bcdCCID (1.10)
    0x00,        // bMaxSlotIndex (1 slot)
    0x07,        // bVoltageSupport (5.0V, 3.0V, 1.8V)
    0x03, 0x00, 0x00, 0x00, // dwProtocols (T=0 and T=1)
    0xC4, 0x0E, 0x00, 0x00, // dwDefaultClock (3.78 MHz)
    0xC4, 0x0E, 0x00, 0x00, // dwMaximumClock (3.78 MHz)
    0x00,        // bNumClockSupported
    0x80, 0x25, 0x00, 0x00, // dwDataRate (9600 bps)
    0x00, 0xB0, 0x04, 0x00, // dwMaxDataRate (307200 bps)
    0x00,        // bNumDataRatesSupported
    0xFE, 0x00, 0x00, 0x00, // dwMaxIFSD (254 bytes)
    0x00, 0x00, 0x00, 0x00, // dwSynchProtocols
    0x00, 0x00, 0x00, 0x00, // dwMechanical
    0xB0, 0x00, 0x01, 0x00, // dwFeatures (Short APDU, auto-param)
    0x00, 0x04, 0x00, 0x00, // dwMaxCCIDMessageLength (1024 bytes)
    0xFF,        // bClassGetResponse
    0xFF,        // bClassEnvelope
    0x00, 0x00,  // wLcdLayout
    0x00,        // bPINSupport
    0x01,        // bMaxCCIDBusySlots

    // 3. Endpoint Bulk OUT (7 bytes)
    0x07,        // bLength
    0x05,        // bDescriptorType (ENDPOINT)
    ep_num,      // bEndpointAddress (OUT)
    0x02,        // bmAttributes (Bulk)
    0x40, 0x00,  // wMaxPacketSize (64)
    0x00,        // bInterval

    // 4. Endpoint Bulk IN (7 bytes)
    0x07,        // bLength
    0x05,        // bDescriptorType (ENDPOINT)
    (uint8_t)(0x80 | ep_num), // bEndpointAddress (IN)
    0x02,        // bmAttributes (Bulk)
    0x40, 0x00,  // wMaxPacketSize (64)
    0x00         // bInterval
  };

  *itf += 1;
  memcpy(dst, desc, sizeof(desc));
  return sizeof(desc);
}

// --------------------------------------------------------------------------
// TinyUSB Custom Class Driver Callbacks for CCID SmartCard
// --------------------------------------------------------------------------
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

void yubikey_ccid_begin(void) {
  tinyusb_enable_interface(USB_INTERFACE_CUSTOM, CCID_DESC_LEN, ccid_load_descriptor);
}

}
