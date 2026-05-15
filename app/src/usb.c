#include "usb.h"

#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/usb/usb_dc.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>

/* By default, do not register the USB DFU class DFU mode instance. */
static const char *const blocklist[] = {
	"dfu_dfu",
	NULL,
};

USBD_DEVICE_DEFINE(farlock3_usbd, DEVICE_DT_GET(DT_NODELABEL(zehpyr_udc0)), CONFIG_FARLOCK3_USBD_VID, CONFIG_FARLOCK3_USBD_PID);

/* doc string instantiation start */
USBD_DESC_LANG_DEFINE(farlock3_lang);
USBD_DESC_MANUFACTURER_DEFINE(farlock3_mfr, CONFIG_SAMPLE_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(farlock3_product, CONFIG_SAMPLE_USBD_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(farlock3_sn)));

/* doc string instantiation end */

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");

/* doc configuration instantiation start */
static const uint8_t attributes = (IS_ENABLED(CONFIG_FARLOCK3_USBD_SELF_POWERED) ?
				   USB_SCD_SELF_POWERED : 0) |
				  (IS_ENABLED(CONFIG_FARLOCK3_USBD_REMOTE_WAKEUP) ?
				   USB_SCD_REMOTE_WAKEUP : 0);

USBD_CONFIGURATION_DEFINE(farlock3_fs_config,
			  attributes,
			  CONFIG_FARLOCK3_USBD_MAX_POWER, &fs_cfg_desc);

/* High speed configuration */
USBD_CONFIGURATION_DEFINE(farlock3_hs_config,
			  attributes,
			  CONFIG_FARLOCK3_USBD_MAX_POWER, &hs_cfg_desc);
/* doc configuration instantiation end */

#if CONFIG_FARLOCK3_USBD_20_EXTENSION_DESC
/*
 * This does not yet provide valuable information, but rather serves as an
 * example, and will be improved in the future.
 */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
	.bLength = sizeof(struct usb_bos_capability_lpm),
	.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
	.bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
	.bmAttributes = 0UL,
};

USBD_DESC_BOS_DEFINE(farlock3_usbext, sizeof(bos_cap_lpm), &bos_cap_lpm);
#endif

static void farlock3_fix_code_triple(struct usbd_context *uds_ctx,
				   const enum usbd_speed speed)
{
	/* Always use class code information from Interface Descriptors */
	if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_ECM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_NCM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_MIDI2_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_AUDIO2_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_VIDEO_CLASS)) {
		/*
		 * Class with multiple interfaces have an Interface
		 * Association Descriptor available, use an appropriate triple
		 * to indicate it.
		 */
		usbd_device_set_code_triple(uds_ctx, speed,
					    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	} else {
		usbd_device_set_code_triple(uds_ctx, speed, 0, 0, 0);
	}
}


struct usbd_context *farlock3_usbd_setup_device(usbd_msg_cb_t msg_cb)
{
	int err;

	/* doc add string descriptor start */
	err = usbd_add_descriptor(&farlock3_usbd, &farlock3_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&farlock3_usbd, &farlock3_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&farlock3_usbd, &farlock3_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return NULL;
	}

	IF_ENABLED(CONFIG_HWINFO, (
		err = usbd_add_descriptor(&farlock3_usbd, &farlock3_sn);
	))
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (%d)", err);
		return NULL;
	}
	/* doc add string descriptor end */

	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_caps_speed(&farlock3_usbd) == USBD_SPEED_HS) {
		err = usbd_add_configuration(&farlock3_usbd, USBD_SPEED_HS,
					     &farlock3_hs_config);
		if (err) {
			LOG_ERR("Failed to add High-Speed configuration");
			return NULL;
		}

		err = usbd_register_all_classes(&farlock3_usbd, USBD_SPEED_HS, 1,
						blocklist);
		if (err) {
			LOG_ERR("Failed to add register classes");
			return NULL;
		}

		farlock3_fix_code_triple(&farlock3_usbd, USBD_SPEED_HS);
	}

	/* doc configuration register start */
	err = usbd_add_configuration(&farlock3_usbd, USBD_SPEED_FS,
				     &farlock3_fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration");
		return NULL;
	}
	/* doc configuration register end */

	/* doc functions register start */
	err = usbd_register_all_classes(&farlock3_usbd, USBD_SPEED_FS, 1, blocklist);
	if (err) {
		LOG_ERR("Failed to add register classes");
		return NULL;
	}
	/* doc functions register end */

	sample_fix_code_triple(&farlock3_usbd, USBD_SPEED_FS);
	usbd_self_powered(&farlock3_usbd, attributes & USB_SCD_SELF_POWERED);

	if (msg_cb != NULL) {
		/* doc device init-and-msg start */
		err = usbd_msg_register_cb(&farlock3_usbd, msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback");
			return NULL;
		}
		/* doc device init-and-msg end */
	}

#if CONFIG_SAMPLE_USBD_20_EXTENSION_DESC
	(void)usbd_device_set_bcd_usb(&farlock3_usbd, USBD_SPEED_FS, 0x0201);
	(void)usbd_device_set_bcd_usb(&farlock3_usbd, USBD_SPEED_HS, 0x0201);

	err = usbd_add_descriptor(&farlock3_usbd, &farlock3_usbext);
	if (err) {
		LOG_ERR("Failed to add USB 2.0 Extension Descriptor");
		return NULL;
	}
#endif

	return &farlock3_usbd;
}

static struct usbd_context *farlock3_usbd_init_device(usbd_msg_cb_t msg_cb)
{
	int err;

	if (farlock3_usbd_setup_device(msg_cb) == NULL) {
		return NULL;
	}

	/* doc device init start */
	err = usbd_init(&farlock3_usbd);
	if (err) {
		LOG_ERR("Failed to initialize device support");
		return NULL;
	}
	/* doc device init end */

	return &farlock3_usbd;
}

int start_usb() {
    const struct device *const dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint32_t dtr = 0;

    if (usb_enable(NULL)) {
        return 0;
    }   

    usbd_enable();

    /* Poll if the DTR flag was set */
    while (!dtr) {
        uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
        /* Give CPU resources to low priority threads. */
        k_sleep(K_MSEC(100));
    }

    return 0;
}