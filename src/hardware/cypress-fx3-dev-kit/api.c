/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Sebastian Holzapfel <schnommus@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "protocol.h"

static const struct fx3_profile supported_fx3[] = {
	/*
	 * Default Cypress FX3
	 */
	{ 0x04B4, 0x00f1, "Cypress", "FX3", NULL,
                DEV_CAPS_16BIT, NULL, NULL },
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
};

static gboolean is_plausible(const struct libusb_device_descriptor *des)
{
	int i;

	for (i = 0; i != sizeof(supported_fx3)/sizeof(struct fx3_profile); i++) {
		if (des->idVendor != supported_fx3[i].vid)
			continue;
		if (des->idProduct == supported_fx3[i].pid) {
			sr_warn("PLAUSIBLE");
			return TRUE;
        }
	}

	return FALSE;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct sr_config *src;
	const struct fx3_profile *prof;
	GSList *l, *devices, *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	struct libusb_device_handle *hdl;
	int ret, i, j;
	int num_logic_channels = 0, num_analog_channels = 0;
	const char *conn;
	char manufacturer[64], product[64], serial_num[64], connection_id[64];
	char channel_name[16];

	drvc = di->context;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	/* Find all fx2lafw compatible devices and upload firmware to them. */
	devices = NULL;
	int count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; i != count; i++) {
		if (conn) {
			usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
					&& usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		libusb_get_device_descriptor( devlist[i], &des);

		if (!is_plausible(&des))
			continue;

		if ((ret = libusb_open(devlist[i], &hdl)) < 0) {
			sr_warn("Failed to open potential device with "
				"VID:PID %04x:%04x: %s.", des.idVendor,
				des.idProduct, libusb_error_name(ret));
			continue;
		}

		if (des.iManufacturer == 0) {
			manufacturer[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iManufacturer, (unsigned char *) manufacturer,
				sizeof(manufacturer))) < 0) {
			sr_warn("Failed to get manufacturer string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.iProduct == 0) {
			product[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, (unsigned char *) product,
				sizeof(product))) < 0) {
			sr_warn("Failed to get product string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.iSerialNumber == 0) {
			serial_num[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iSerialNumber, (unsigned char *) serial_num,
				sizeof(serial_num))) < 0) {
			sr_warn("Failed to get serial number string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));

		libusb_close(hdl);

		prof = NULL;
		for (j = 0; j != sizeof(supported_fx3)/sizeof(struct fx3_profile); j++) {
			if (des.idVendor == supported_fx3[j].vid &&
					des.idProduct == supported_fx3[j].pid &&
					(!supported_fx3[j].usb_manufacturer ||
					 !strcmp(manufacturer, supported_fx3[j].usb_manufacturer)) &&
					(!supported_fx3[j].usb_product ||
					 !strcmp(product, supported_fx3[j].usb_product))) {
				prof = &supported_fx3[j];
				break;
			}
		}

		if (!prof)
			continue;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup(prof->vendor);
		sdi->model = g_strdup(prof->model);
		sdi->version = g_strdup(prof->model_version);
		sdi->serial_num = g_strdup(serial_num);
		sdi->connection_id = g_strdup(connection_id);

		/* Fill in channellist according to this device's profile. */
		num_logic_channels = prof->dev_caps & DEV_CAPS_16BIT ? 16 : 8;
		num_analog_channels = prof->dev_caps & DEV_CAPS_AX_ANALOG ? 1 : 0;

		/* Logic channels, all in one channel group. */
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("Logic");
		for (j = 0; j < num_logic_channels; j++) {
			sprintf(channel_name, "D%d", j);
			ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC,
						TRUE, channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cg);

		for (j = 0; j < num_analog_channels; j++) {
			snprintf(channel_name, 16, "A%d", j);
			ch = sr_channel_new(sdi, j + num_logic_channels,
					SR_CHANNEL_ANALOG, TRUE, channel_name);

			/* Every analog channel gets its own channel group. */
			cg = g_malloc0(sizeof(struct sr_channel_group));
			cg->name = g_strdup(channel_name);
			cg->channels = g_slist_append(NULL, ch);
			sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
		}

		devc = fx3_dev_new();
		devc->profile = prof;
		sdi->priv = devc;
		devices = g_slist_append(devices, sdi);

        sdi->status = SR_ST_INACTIVE;
        sdi->inst_type = SR_INST_USB;
        sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
                libusb_get_device_address(devlist[i]), NULL);

        sr_warn("Found VID: %x.\n",des.idVendor);
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and open it. */

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and close it. */

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
        case SR_CONF_DEVICE_OPTIONS:
            return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	(void)sdi;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */

	(void)sdi;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver cypress_fx3_dev_kit_driver_info = {
	.name = "cypress-fx3-dev-kit",
	.longname = "Cypress FX3 Dev Kit",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(cypress_fx3_dev_kit_driver_info);
