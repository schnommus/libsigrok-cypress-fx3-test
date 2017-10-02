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

#ifndef LIBSIGROK_HARDWARE_CYPRESS_FX3_DEV_KIT_PROTOCOL_H
#define LIBSIGROK_HARDWARE_CYPRESS_FX3_DEV_KIT_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <stdio.h>
#include <string.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "cypress-fx3-dev-kit"

#define DEV_CAPS_16BIT_POS	0
#define DEV_CAPS_AX_ANALOG_POS	1

#define DEV_CAPS_16BIT		(1 << DEV_CAPS_16BIT_POS)
#define DEV_CAPS_AX_ANALOG	(1 << DEV_CAPS_AX_ANALOG_POS)

struct fx3_profile {
	uint16_t vid;
	uint16_t pid;

	const char *vendor;
	const char *model;
	const char *model_version;

	uint32_t dev_caps;

	const char *usb_manufacturer;
	const char *usb_product;
};

struct dev_context {
	const struct fx3_profile *profile;
};

SR_PRIV int cypress_fx3_dev_kit_receive_data(int fd, int revents, void *cb_data);

SR_PRIV struct dev_context *fx3_dev_new(void);

#endif
