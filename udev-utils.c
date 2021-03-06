/*
 * Copyright (c) 2015 Vladimir Kondratyev <wulf@cicgroup.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#include "libudev.h"
#include "udev-device.h"
#include "udev-list.h"
#include "udev-utils.h"
#include "utils.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include <fcntl.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_LINUX_INPUT_H
#include <linux/input.h>
#else
#define	BUS_PCI		0x01
#define	BUS_USB		0x03
#define	BUS_VIRTUAL	0x06
#define	BUS_ISA		0x10
#define	BUS_I8042	0x11
#endif

#define	PS2_KEYBOARD_VENDOR		0x001
#define	PS2_KEYBOARD_PRODUCT		0x001
#define	PS2_MOUSE_VENDOR		0x002
#define	PS2_MOUSE_GENERIC_PRODUCT	0x001

#ifdef HAVE_LINUX_INPUT_H
static const char *virtual_sysname = "uinput";
#endif

void create_evdev_handler(struct udev_device *udev_device);
void create_keyboard_handler(struct udev_device *udev_device);
void create_mouse_handler(struct udev_device *udev_device);
void create_joystick_handler(struct udev_device *udev_device);
void create_touchpad_handler(struct udev_device *udev_device);
void create_touchscreen_handler(struct udev_device *udev_device);
void create_sysmouse_handler(struct udev_device *udev_device);
void create_kbdmux_handler(struct udev_device *udev_device);

struct subsystem_config {
	char *subsystem;
	char *syspath;
	int flags; /* See SCFLAG_* below. */
	void (*create_handler)(struct udev_device *udev_device);
};

enum {
	IT_NONE,
	IT_KEYBOARD,
	IT_MOUSE,
	IT_TOUCHPAD,
	IT_TOUCHSCREEN,
	IT_JOYSTICK,
	IT_TABLET
};

/* Flag which in indicates a device should be skipped because it's
 * already exposed through EVDEV when it's enabled. */
#define	SCFLAG_SKIP_IF_EVDEV	0x01

struct subsystem_config subsystems[] = {
#ifdef HAVE_LINUX_INPUT_H
	{ "input", DEV_PATH_ROOT "/input/event[0-9]*",
		0,
		create_evdev_handler },
#endif
	{ "input", DEV_PATH_ROOT "/ukbd[0-9]*",
		SCFLAG_SKIP_IF_EVDEV,
		create_keyboard_handler },
	{ "input", DEV_PATH_ROOT "/atkbd[0-9]*",
		SCFLAG_SKIP_IF_EVDEV,
		create_keyboard_handler },
	{ "input", DEV_PATH_ROOT "/kbdmux[0-9]*",
		SCFLAG_SKIP_IF_EVDEV,
		create_kbdmux_handler },
	{ "input", DEV_PATH_ROOT "/ums[0-9]*",
		SCFLAG_SKIP_IF_EVDEV,
		create_mouse_handler },
	{ "input", DEV_PATH_ROOT "/psm[0-9]*",
		SCFLAG_SKIP_IF_EVDEV,
		create_mouse_handler },
	{ "input", DEV_PATH_ROOT "/joy[0-9]*",
		0,
		create_joystick_handler },
	{ "input", DEV_PATH_ROOT "/atp[0-9]*",
		0,
		create_touchpad_handler },
	{ "input", DEV_PATH_ROOT "/wsp[0-9]*",
		0,
		create_touchpad_handler },
	{ "input", DEV_PATH_ROOT "/uep[0-9]*",
		0,
		create_touchscreen_handler },
	{ "input", DEV_PATH_ROOT "/sysmouse",
		SCFLAG_SKIP_IF_EVDEV,
		create_sysmouse_handler },
	{ "input", DEV_PATH_ROOT "/vboxguest",
		0,
		create_mouse_handler },
};

static struct subsystem_config *
get_subsystem_config_by_syspath(const char *path)
{
	size_t i;

	for (i = 0; i < nitems(subsystems); i++)
		if (fnmatch(subsystems[i].syspath, path, 0) == 0)
			return (&subsystems[i]);

	return (NULL);
}

static bool
kernel_has_evdev_enabled()
{
	static int enabled = -1;
	size_t len;

	if (enabled != -1)
		return (enabled);

	if (sysctlbyname("kern.features.evdev_support", &enabled, &len, NULL, 0) < 0)
		return (0);

	TRC("() EVDEV enabled: %s", enabled ? "true" : "false");
	return (enabled);
}

const char *
get_subsystem_by_syspath(const char *syspath)
{
	struct subsystem_config *sc;

	sc = get_subsystem_config_by_syspath(syspath);
	if (sc == NULL)
		return (UNKNOWN_SUBSYSTEM);
	if (sc->flags & SCFLAG_SKIP_IF_EVDEV && kernel_has_evdev_enabled()) {
		TRC("(%s) EVDEV enabled -> skipping device", syspath);
		return (UNKNOWN_SUBSYSTEM);
	}

	return (sc->subsystem);
}

const char *
get_sysname_by_syspath(const char *syspath)
{

	return (strbase(syspath));
}

const char *
get_devpath_by_syspath(const char *syspath)
{

	return (syspath);
}

const char *
get_syspath_by_devpath(const char *devpath)
{

	return (devpath);
}

void
invoke_create_handler(struct udev_device *ud)
{
	const char *path;
	struct subsystem_config *sc;

	path = udev_device_get_syspath(ud);
	sc = get_subsystem_config_by_syspath(path);
	if (sc == NULL || sc->create_handler == NULL)
		return;
	if (sc->flags & SCFLAG_SKIP_IF_EVDEV && kernel_has_evdev_enabled()) {
		TRC("(%p) EVDEV enabled -> skipping device", ud);
		return;
	}

	sc->create_handler(ud);
}

static int
set_input_device_type(struct udev_device *ud, int input_type)
{
	struct udev_list *ul;

	ul = udev_device_get_properties_list(ud);
	if (udev_list_insert(ul, "ID_INPUT", "1") < 0)
		return (-1);
	switch (input_type) {
	case IT_KEYBOARD:
		udev_list_insert(ul, "ID_INPUT_KEY", "1");
		udev_list_insert(ul, "ID_INPUT_KEYBOARD", "1");
		break;
	case IT_MOUSE:
		udev_list_insert(ul, "ID_INPUT_MOUSE", "1");
		break;
	case IT_TOUCHPAD:
		udev_list_insert(ul, "ID_INPUT_MOUSE", "1");
		udev_list_insert(ul, "ID_INPUT_TOUCHPAD", "1");
		break;
	case IT_TOUCHSCREEN:
		udev_list_insert(ul, "ID_INPUT_TOUCHSCREEN", "1");
		break;
	case IT_JOYSTICK:
		udev_list_insert(ul, "ID_INPUT_JOYSTICK", "1");
		break;
	case IT_TABLET:
		udev_list_insert(ul, "ID_INPUT_TABLET", "1");
		break;
	}
	return (0);
}

static struct udev_device *
create_xorg_parent(struct udev_device *ud, const char* sysname,
    const char *name, const char *product, const char *pnp_id)
{
	struct udev_device *parent;
	struct udev *udev;
	struct udev_list *props, *sysattrs;

	/* xorg-server gets device name and vendor string from parent device */
	udev = udev_device_get_udev(ud);
	parent = udev_device_new_common(udev, sysname, UD_ACTION_NONE);
	if (parent == NULL)
		return NULL;

	props = udev_device_get_properties_list(parent);
	sysattrs = udev_device_get_sysattr_list(parent);
	udev_list_insert(props, "NAME", name);
	udev_list_insert(sysattrs, "name", name);
	if (product != NULL)
		udev_list_insert(props, "PRODUCT", product);
	if (pnp_id != NULL)
		udev_list_insert(sysattrs, "id", product);

	return (parent);
}

#ifdef HAVE_LINUX_INPUT_H

#define	LONG_BITS	(sizeof(long) * 8)
#define	NLONGS(x)	(((x) + LONG_BITS - 1) / LONG_BITS)

static inline bool
bit_is_set(const unsigned long *array, int bit)
{
	return !!(array[bit / LONG_BITS] & (1LL << (bit % LONG_BITS)));
}

static inline bool
bit_find(const unsigned long *array, int start, int stop)
{
	int i;

	for (i = start; i < stop; i++)
		if (bit_is_set(array, i))
			return true;

	return false;
}

void
create_evdev_handler(struct udev_device *ud)
{
	struct udev_device *parent;
	const char *sysname;
	char name[80], product[80], phys[80];
	int fd, input_type = IT_NONE;
	bool opened = false;
	bool has_keys, has_buttons, has_lmr;
	bool has_rel_axes, has_abs_axes, has_mt;
	unsigned long key_bits[NLONGS(KEY_CNT)];
	unsigned long rel_bits[NLONGS(REL_CNT)];
	unsigned long abs_bits[NLONGS(ABS_CNT)];
	struct input_id id;

	fd = path_to_fd(udev_device_get_devnode(ud));
	if (fd == -1) {
		fd = open(udev_device_get_devnode(ud), O_RDONLY | O_CLOEXEC);
		opened = true;
	}
	if (fd == -1)
		return;

	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
	    (ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys) < 0 && errno != ENOENT) ||
	    ioctl(fd, EVIOCGID, &id) < 0 ||
	    ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits) < 0 ||
	    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0 ||
	    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
		ERR("could not query evdev");
		goto bail_out;
	}

	/* Derived from EvdevProbe() function of xf86-input-evdev driver */
	has_keys = bit_find(key_bits, 0, BTN_MISC);
	has_buttons = bit_find(key_bits, BTN_MISC, BTN_JOYSTICK);
	has_lmr = bit_find(key_bits, BTN_LEFT, BTN_MIDDLE + 1);
	has_rel_axes = bit_find(rel_bits, 0, REL_CNT);
	has_abs_axes = bit_find(abs_bits, 0, ABS_CNT);
	has_mt = bit_find(abs_bits, ABS_MT_SLOT, ABS_CNT);

	if (has_abs_axes) {
		if (has_mt && !has_buttons) {
			/* TBD:Improve joystick detection */
			if (bit_is_set(key_bits, BTN_JOYSTICK)) {
				input_type = IT_JOYSTICK;
				goto detected;
			} else {
				has_buttons = true;
			}
		}

		if (bit_is_set(abs_bits, ABS_X) &&
		    bit_is_set(abs_bits, ABS_Y)) {
			if (bit_is_set(key_bits, BTN_TOOL_PEN) ||
			    bit_is_set(key_bits, BTN_STYLUS) ||
			    bit_is_set(key_bits, BTN_STYLUS2)) {
				input_type = IT_TABLET;
				goto detected;
			} else if (bit_is_set(abs_bits, ABS_PRESSURE) ||
			           bit_is_set(key_bits, BTN_TOUCH)) {
				if (has_lmr ||
				    bit_is_set(key_bits, BTN_TOOL_FINGER)) {
					input_type = IT_TOUCHPAD;
				} else {
					input_type = IT_TOUCHSCREEN;
				}
				goto detected;
			} else if (!(bit_is_set(rel_bits, REL_X) &&
			             bit_is_set(rel_bits, REL_Y)) &&
			             has_lmr) {
				/* some touchscreens use BTN_LEFT rather than BTN_TOUCH */
				input_type = IT_TOUCHSCREEN;
				goto detected;
			}
		}
	}

	if (has_keys)
		input_type = IT_KEYBOARD;
	else if (has_rel_axes || has_abs_axes || has_buttons)
		input_type = IT_MOUSE;

	if (input_type == IT_NONE)
		goto bail_out;

detected:
	set_input_device_type(ud, input_type);

	sysname = phys[0] == 0 ? virtual_sysname : phys;

	*(strchrnul(name, ',')) = '\0';	/* strip name */

	snprintf(product, sizeof(product), "%x/%x/%x/%x",
	    id.bustype, id.vendor, id.product, id.version);

	parent = create_xorg_parent(ud, sysname, name, product, NULL);
	if (parent != NULL)
		udev_device_set_parent(ud, parent);

bail_out:
	if (opened)
		close(fd);
}
#endif

size_t
syspathlen_wo_units(const char *path) {
	size_t len;

	len = strlen(path);
	while (len > 0) {
		if (path[len-1] < '0' || path[len-1] > '9')
			break;
		--len;
	}
	return len;
}

void
set_parent(struct udev_device *ud)
{
        struct udev_device *parent;
	char devname[DEV_PATH_MAX], mib[32], pnpinfo[1024];
	char name[80], product[80], parentname[80], *pnp_id;
	const char *sysname, *unit, *vendorstr, *prodstr, *devicestr;
	size_t len, vendorlen, prodlen, devicelen, pnplen;
	uint32_t bus, prod, vendor;

	sysname = udev_device_get_sysname(ud);
	len = syspathlen_wo_units(sysname);
	/* Check if device unit number found */
	if (strlen(sysname) == len)
		return;
	snprintf(devname, len + 1, "%s", sysname);
	unit = sysname + len;

	snprintf(mib, sizeof(mib), "dev.%s.%s.%%desc", devname, unit);
	len = sizeof(name);
	if (sysctlbyname(mib, name, &len, NULL, 0) < 0)
		return;
	*(strchrnul(name, ',')) = '\0';	/* strip name */

	snprintf(mib, sizeof(mib), "dev.%s.%s.%%pnpinfo", devname, unit);
	len = sizeof(pnpinfo);
	if (sysctlbyname(mib, pnpinfo, &len, NULL, 0) < 0)
		return;

	snprintf(mib, sizeof(mib), "dev.%s.%s.%%parent", devname, unit);
	len = sizeof(parentname);
	if (sysctlbyname(mib, parentname, &len, NULL, 0) < 0)
		return;

	vendorstr = get_kern_prop_value(pnpinfo, "vendor", &vendorlen);
	prodstr = get_kern_prop_value(pnpinfo, "product", &prodlen);
	devicestr = get_kern_prop_value(pnpinfo, "device", &devicelen);
	pnp_id = get_kern_prop_value(pnpinfo, "_HID", &pnplen);
	if (pnp_id != NULL && pnplen == 4 && strncmp(pnp_id, "none", 4) == 0)
		pnp_id = NULL;
	if (pnp_id != NULL)
		pnp_id[pnplen] = '\0';
	if (prodstr != NULL && vendorstr != NULL) {
		/* XXX: should parent be compared to uhub* to detect usb? */
		vendor = strtol(vendorstr, NULL, 0);
		prod = strtol(prodstr, NULL, 0);
		bus = BUS_USB;
	} else if (devicestr != NULL && vendorstr != NULL) {
		vendor = strtol(vendorstr, NULL, 0);
		prod = strtol(devicestr, NULL, 0);
		bus = BUS_PCI;
	} else if (strcmp(parentname, "atkbdc0") == 0) {
		if (strcmp(devname, "atkbd") == 0) {
			vendor = PS2_KEYBOARD_VENDOR;
			prod = PS2_KEYBOARD_PRODUCT;
		} else if (strcmp(devname, "psm") == 0) {
			vendor = PS2_MOUSE_VENDOR;
			prod = PS2_MOUSE_GENERIC_PRODUCT;
		} else {
			vendor = 0;
			prod = 0;
		}
		bus = BUS_I8042;
	} else {
		vendor = 0;
		prod = 0;
		bus = BUS_VIRTUAL;
	}
	snprintf(product, sizeof(product), "%x/%x/%x/0", bus, vendor, prod);
	parent = create_xorg_parent(ud, sysname, name, product, pnp_id);
	if (parent != NULL)
		udev_device_set_parent(ud, parent);

	return;
}

void
create_keyboard_handler(struct udev_device *ud)
{

	set_input_device_type(ud, IT_KEYBOARD);
	set_parent(ud);
}

void
create_mouse_handler(struct udev_device *ud)
{

	set_input_device_type(ud, IT_MOUSE);
	set_parent(ud);
}

void
create_kbdmux_handler(struct udev_device *ud)
{
	struct udev_device *parent;
	const char* sysname;

	set_input_device_type(ud, IT_KEYBOARD);
	sysname = udev_device_get_sysname(ud);
	parent = create_xorg_parent(ud, sysname,
	    "System keyboard multiplexor", "6/1/1/0", NULL);
	if (parent != NULL)
		udev_device_set_parent(ud, parent);
}

void
create_sysmouse_handler(struct udev_device *ud)
{
	struct udev_device *parent;
	const char* sysname;

	set_input_device_type(ud, IT_MOUSE);
	sysname = udev_device_get_sysname(ud);
	parent = create_xorg_parent(ud, sysname,
	    "System mouse", "6/2/1/0", NULL);
	if (parent != NULL)
		udev_device_set_parent(ud, parent);
}

void
create_joystick_handler(struct udev_device *ud)
{

	set_input_device_type(ud, IT_JOYSTICK);
	set_parent(ud);
}

void
create_touchpad_handler(struct udev_device *ud)
{

	set_input_device_type(ud, IT_TOUCHPAD);
	set_parent(ud);
}

void create_touchscreen_handler(struct udev_device *ud)
{

	set_input_device_type(ud, IT_TOUCHSCREEN);
	set_parent(ud);
}
