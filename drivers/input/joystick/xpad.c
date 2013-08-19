/*
 * Xbox gamepad driver with Xbox 360 wired/wireless support
 *
 * Last Modified:	2 March 2009
 *			Mike Murphy <mamurph@cs.clemson.edu>
 *
 * Copyright (c) 2002 Marko Friedemann <mfr@bmx-chemnitz.de>
 *               2004 Oliver Schwartz <Oliver.Schwartz@gmx.de>,
 *                    Steven Toth <steve@toth.demon.co.uk>,
 *                    Franz Lehner <franz@caos.at>,
 *                    Ivan Hawkes <blackhawk@ivanhawkes.com>
 *               2005 Dominic Cerquetti <binary1230@yahoo.com>
 *               2006 Adam Buchbinder <adam.buchbinder@gmail.com>
 *               2007 Jan Kratochvil <honza@jikos.cz>
 *               2009 Clemson University
 *		      (contact: Mike Murphy <mamurph@cs.clemson.edu>)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Please see xbox.h for the ChangeLog.
 */

#include "xpad.h"

/* This module parameter is something of a relic, but it remains for
 * compatibility. Importantly, the option to map the D-PAD buttons applies
 * only to controller *interfaces* (i.e. vendor and product codes) not
 * explicitly present in xpad_device[]. */

static int dpad_to_buttons;
module_param(dpad_to_buttons, bool, S_IRUGO);
MODULE_PARM_DESC(dpad_to_buttons,
	"Map D-PAD to buttons rather than axes for unknown pads");


/* Table of various device interfaces recognized by this driver. Each supported
 * device has a directional pad mapping, interface type, and controller type.
 * Note that wireless 360 devices have XCONTROLLER_TYPE_NONE, as the actual
 * type of the gaming controller is not known until the controller binds
 * wirelessly with the receiver
 */
static const struct xpad_device {
	u16 idVendor;
	u16 idProduct;
	char *name;
	u8 dpad_mapping;
	u8 xtype;
	u8 controller_type;
} xpad_device[] = {
	{ 0x045e, 0x0202, "Microsoft X-Box pad v1 (US)", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x045e, 0x0289, "Microsoft X-Box pad v2 (US)", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x045e, 0x0285, "Microsoft X-Box pad (Japan)", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x045e, 0x0287, "Microsoft Xbox Controller S", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x045e, 0x028e, "Microsoft X-Box 360 pad", MAP_DPAD_TO_AXES,
		XTYPE_XBOX360, XCONTROLLER_TYPE_PAD },
	{ 0x045e, 0x0719, "Xbox 360 Wireless Receiver", MAP_DPAD_TO_BUTTONS,
		XTYPE_XBOX360W, XCONTROLLER_TYPE_NONE },
	{ 0x0c12, 0x8809, "RedOctane Xbox Dance Pad", MAP_DPAD_TO_BUTTONS,
		XTYPE_XBOX, XCONTROLLER_TYPE_DANCE_PAD },
	{ 0x044f, 0x0f07, "Thrustmaster, Inc. Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x046d, 0xc242, "Logitech Chillstream Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX360, XCONTROLLER_TYPE_PAD },
	{ 0x046d, 0xca84, "Logitech Xbox Cordless Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x046d, 0xca88, "Logitech Compact Controller for Xbox",
		MAP_DPAD_TO_AXES, XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x05fd, 0x1007, "Mad Catz Controller (unverified)", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x05fd, 0x107a, "InterAct 'PowerPad Pro' X-Box pad (Germany)",
		MAP_DPAD_TO_AXES, XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0738, 0x4516, "Mad Catz Control Pad", MAP_DPAD_TO_AXES, XTYPE_XBOX,
		XCONTROLLER_TYPE_PAD },
	{ 0x0738, 0x4522, "Mad Catz LumiCON", MAP_DPAD_TO_AXES, XTYPE_XBOX,
		XCONTROLLER_TYPE_PAD },
	{ 0x0738, 0x4526, "Mad Catz Control Pad Pro", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0738, 0x4536, "Mad Catz MicroCON", MAP_DPAD_TO_AXES, XTYPE_XBOX,
		XCONTROLLER_TYPE_PAD },
	{ 0x0738, 0x4540, "Mad Catz Beat Pad", MAP_DPAD_TO_BUTTONS, XTYPE_XBOX,
		XCONTROLLER_TYPE_DANCE_PAD },
	{ 0x0738, 0x4556, "Mad Catz Lynx Wireless Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0738, 0x4716, "Mad Catz Wired Xbox 360 Controller",
		MAP_DPAD_TO_AXES, XTYPE_XBOX360, XCONTROLLER_TYPE_PAD },
	{ 0x0738, 0x4738, "Mad Catz Wired Xbox 360 Controller (SFIV)",
		MAP_DPAD_TO_AXES, XTYPE_XBOX360, XCONTROLLER_TYPE_PAD },
	{ 0x0738, 0x6040, "Mad Catz Beat Pad Pro", MAP_DPAD_TO_BUTTONS,
		XTYPE_XBOX, XCONTROLLER_TYPE_DANCE_PAD },
	{ 0x0c12, 0x8802, "Zeroplus Xbox Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0c12, 0x880a, "Pelican Eclipse PL-2023", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0c12, 0x8810, "Zeroplus Xbox Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0c12, 0x9902, "HAMA VibraX - *FAULTY HARDWARE*", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0e4c, 0x1097, "Radica Gamester Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0e4c, 0x2390, "Radica Games Jtech Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0e6f, 0x0003, "Logic3 Freebird wireless Controller",
		MAP_DPAD_TO_AXES, XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0e6f, 0x0005, "Eclipse wireless Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0e6f, 0x0006, "Edge wireless Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0e6f, 0x0006, "Pelican 'TSZ' Wired Xbox 360 Controller",
		MAP_DPAD_TO_AXES, XTYPE_XBOX360, XCONTROLLER_TYPE_PAD },
	{ 0x0e8f, 0x0201, "SmartJoy Frag Xpad/PS2 adaptor", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0f30, 0x0202, "Joytech Advanced Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0f30, 0x8888, "BigBen XBMiniPad Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0f0d, 0x0016, "Hori Real Arcade Pro.EX", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x102c, 0xff0c, "Joytech Wireless Advanced Controller",
		MAP_DPAD_TO_AXES, XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x12ab, 0x8809, "Xbox DDR dancepad", MAP_DPAD_TO_BUTTONS, XTYPE_XBOX,
		XCONTROLLER_TYPE_DANCE_PAD },
	{ 0x1430, 0x4748, "RedOctane Guitar Hero X-plorer", MAP_DPAD_TO_AXES,
		XTYPE_XBOX360, XCONTROLLER_TYPE_GUITAR },
	{ 0x1430, 0x8888, "TX6500+ Dance Pad (first generation)",
		MAP_DPAD_TO_BUTTONS, XTYPE_XBOX, XCONTROLLER_TYPE_DANCE_PAD },
	{ 0x146b, 0x0601, "BigBen Interactive XBOX 360 Controller",
		MAP_DPAD_TO_AXES, XTYPE_XBOX360, XCONTROLLER_TYPE_PAD },
	{ 0x1bad, 0x0003, "Harmonix Rock Band Drumkit",
		MAP_DPAD_TO_BUTTONS, XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0xffff, 0xffff, "Chinese-made Xbox Controller", MAP_DPAD_TO_AXES,
		XTYPE_XBOX, XCONTROLLER_TYPE_PAD },
	{ 0x0000, 0x0000, "Generic X-Box pad", MAP_DPAD_UNKNOWN, XTYPE_UNKNOWN,
		XCONTROLLER_TYPE_PAD }
};

/* buttons shared with xbox and xbox360 */
static const signed short xpad_common_btn[] = {
	BTN_A, BTN_B, BTN_X, BTN_Y,			/* "analog" buttons */
	BTN_START, BTN_BACK, BTN_THUMBL, BTN_THUMBR,	/* start/back/sticks */
	-1						/* terminating entry */
};

/* original xbox controllers only */
static const signed short xpad_btn[] = {
	BTN_C, BTN_Z,		/* "analog" buttons */
	-1			/* terminating entry */
};

/* only used if MAP_DPAD_TO_BUTTONS */
static const signed short xpad_btn_pad[] = {
	BTN_LEFT, BTN_RIGHT,		/* d-pad left, right */
	BTN_0, BTN_1,			/* d-pad up, down (XXX names??) */
	-1				/* terminating entry */
};

/* buttons for x360 controller */
static const signed short xpad360_btn[] = {
	BTN_TL, BTN_TR,		/* Button LB/RB */
	BTN_MODE,		/* The big X button */
	-1
};

/* sticks and triggers common to all devices */
static const signed short xpad_abs[] = {
	ABS_X, ABS_Y,		/* left stick */
	ABS_RX, ABS_RY,		/* right stick */
	ABS_Z, ABS_RZ,		/* triggers left/right */
	-1			/* terminating entry */
};

/* only used if MAP_DPAD_TO_AXES */
static const signed short xpad_abs_pad[] = {
	ABS_HAT0X, ABS_HAT0Y,	/* d-pad axes */
	-1			/* terminating entry */
};


static struct usb_device_id xpad_table[] = {
		/* X-Box USB-IF not approved class */
	{ USB_INTERFACE_INFO('X', 'B', 0) },
	XPAD_XBOX360_VENDOR(0x045e),	/* Microsoft X-Box 360 controllers */
	XPAD_XBOX360_VENDOR(0x046d),	/* Logitech 360 style controllers */
	XPAD_XBOX360_VENDOR(0x0738),	/* Mad Catz X-Box 360 controllers */
	XPAD_XBOX360_VENDOR(0x0e6f),	/* 0x0e6f X-Box 360 controllers */
	XPAD_XBOX360_VENDOR(0x0f0d),	/* Hori Controllers */
	XPAD_XBOX360_VENDOR(0x1430),	/* RedOctane X-Box 360 controllers */
	XPAD_XBOX360_VENDOR(0x146b),	/* BigBen Interactive Controllers */
	XPAD_XBOX360_VENDOR(0x1bad),	/* Rock Band Drums */
	{ }
};

MODULE_DEVICE_TABLE(usb, xpad_table);

static struct usb_driver xpad_driver = {
	.name		= "xpad",
	.probe		= xpad_probe,
	.disconnect	= xpad_disconnect,
	.id_table	= xpad_table,
};

/* Wireless 360 device identification.
 *
 * When a wireless controller connects, the 2nd packet it sends SEEMS to
 * be some kind of unique controller identification message. Using usbmon
 * (see Documentation/usb/usbmon.txt), I tried 4 gamepads and a guitar, and
 * I collected the following 5 ID packets from the 5 devices:
 *
 * 000f00f0 00ccfd27 0060e226 63700010 13e3201d 30034001 5001ffff ff
 * 000f00f0 f0ccfd27 0060d8c4 e9600009 13e7201d 30034001 5001ffff ff
 * 000f00f0 00ccfd27 0060578b 82f00010 13e3201d 30034001 5001ffff ff
 * 000f00f0 f0ccfd27 0060da1c b1500009 13e7201d 30034001 5001ffff ff
 * 000f00f0 f0ccfd27 006002d1 71d10000 13e3201d 30034430 5107ffff ff
 *
 * From this trace data, I concocted the following (potentially incorrect)
 * scheme for detecting type and unique ID:
 *
 * ******** xx****xx xxxxxxxx xxxx**xx **xx**** ****tttt tttt**** **
 *                |  unique id |                    |  type |
 *
 * It appears that some of the bytes in the first half of the message, noted
 * above as "unique id" are some sort of serial number, though I cannot work
 * out any correspondence between these bytes and the serial number printed
 * under the battery pack. Many of the bytes in this possibly unique field
 * are not unique across my controllers, and may not in fact be part of the
 * controller's unique identification, but I figured it was better to have
 * extra bytes on either end of the unique byte string instead of the
 * alternative. In addition, the packet appears to indicate the type of
 * the controller toward the end: the pads all send 4001 5001, while the
 * guitar sends 4430 5107.
 *
 * Further testing over a wider variety of devices is probably needed to
 * determine if changes need to be made to this scheme.
 */
static const struct w360_id {
	unsigned char id_bytes[4];
	u8 controller_type;
} w360_id[] = {
	{ {0x40, 0x01, 0x50, 0x01}, XCONTROLLER_TYPE_PAD },
	{ {0x44, 0x30, 0x51, 0x07}, XCONTROLLER_TYPE_GUITAR },
	{ {0x00, 0x00, 0x00, 0x00}, XCONTROLLER_TYPE_NONE }
};

/* The dead zone and stick limit both affect the behavior of the corresponding
 * analog stick, since the output values reported for the stick inputs will
 * be scaled onto [0,32767]. It is thus necessary to ensure that the dead zone
 * is never larger than the stick limit. In fact, a minimal amount of stick
 * travel space (1024) is maintained between the two values. In practice,
 * however, the stick limit should always be much greater than the dead zone.
 */

static void set_dead_zone(unsigned int new_size, unsigned int *dz,
		unsigned int stick_limit)
{
	*dz = min(new_size, stick_limit - 1024);
}

static void set_stick_limit(unsigned int new_size, unsigned int *sl,
		unsigned int dead_zone)
{
	*sl = min(max(new_size, dead_zone + 1024), 32767u);
}


/****************************************************************************/
/*
 * SysFs interface functions
 *
 * We use common functions, where possible, to implement the show/store
 * routines. This design saves on code and reduces the burden of adding to or
 * changing the interface.
 */


static ssize_t xpad_show_uint(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usb_xpad *xpad = to_xpad(dev);
	unsigned int value;
	if (attr == &dev_attr_left_dead_zone)
		value = xpad->left_dead_zone;
	else if (attr == &dev_attr_right_dead_zone)
		value = xpad->right_dead_zone;
	else if (attr == &dev_attr_left_stick_limit)
		value = xpad->left_stick_limit;
	else if (attr == &dev_attr_right_stick_limit)
		value = xpad->right_stick_limit;
	else
		return -EIO;
	return snprintf(buf, PAGE_SIZE, "%u\n", value);
}


static ssize_t xpad_store_uint(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_xpad *xpad = to_xpad(dev);
	unsigned int new_value;
	if (sscanf(buf, "%u", &new_value) != 1)
		return -EIO;

	if (attr == &dev_attr_left_dead_zone)
		set_dead_zone(new_value, &xpad->left_dead_zone,
				xpad->left_stick_limit);
	else if (attr == &dev_attr_right_dead_zone)
		set_dead_zone(new_value, &xpad->right_dead_zone,
				xpad->right_stick_limit);
	else if (attr == &dev_attr_left_stick_limit)
		set_stick_limit(new_value, &xpad->left_stick_limit,
				xpad->left_dead_zone);
	else if (attr == &dev_attr_right_stick_limit)
		set_stick_limit(new_value, &xpad->right_stick_limit,
				xpad->right_dead_zone);
	else
		return -EIO;
	return strnlen(buf, PAGE_SIZE);
}


static ssize_t xpad_store_bool(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_xpad *xpad = to_xpad(dev);
	int newvalue;
	if (sscanf(buf, "%d", &newvalue) != 1)
		return -EIO;

	if (attr == &dev_attr_rumble_enable)
		xpad->rumble_enable = (newvalue) ? 1 : 0;
	else if (attr == &dev_attr_left_trigger_full_axis)
		xpad->left_trigger_full_axis = (newvalue) ? 1 : 0;
	else if (attr == &dev_attr_right_trigger_full_axis)
		xpad->right_trigger_full_axis = (newvalue) ? 1 : 0;
	return strnlen(buf, PAGE_SIZE);
}


/* read-only attributes share a common store function that returns an error */
static ssize_t xpad_store_ro(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	return -EIO;
}


static ssize_t xpad_show_int(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct usb_xpad *xpad = to_xpad(dev);
	int value;
	if (attr == &dev_attr_rumble_enable)
		value = xpad->rumble_enable;
	else if (attr == &dev_attr_controller_number)
		value = xpad->controller_number;
	else if (attr == &dev_attr_controller_present)
		value = xpad->controller_present;
	else if (attr == &dev_attr_controller_type)
		value = xpad->controller_type;
	else if (attr == &dev_attr_left_trigger_full_axis)
		value = xpad->left_trigger_full_axis;
	else if (attr == &dev_attr_right_trigger_full_axis)
		value = xpad->right_trigger_full_axis;
	else
		return -EIO;
	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}


static ssize_t xpad_show_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usb_xpad *xpad = to_xpad(dev);
	return snprintf(buf, PAGE_SIZE, "%s\n", xpad->controller_unique_id);
}


/* end of sysfs interface */
/*****************************************************************************/

/* Input section */

/*	xpad_init_controller
 *
 *	Performs controller setup based on controller type.
 *
 *	NOTE: xpad->controller_data->controller_type needs to be set BEFORE
 *	calling this function!
 */

static void xpad_init_controller(struct usb_xpad *xpad)
{
	set_stick_limit(XSTICK_LIMIT_DEFAULT, &xpad->left_stick_limit,
			xpad->left_dead_zone);
	set_stick_limit(XSTICK_LIMIT_DEFAULT, &xpad->right_stick_limit,
			xpad->right_dead_zone);
	set_dead_zone(XDEAD_ZONE_DEFAULT, &xpad->left_dead_zone,
			xpad->left_stick_limit);
	set_dead_zone(XDEAD_ZONE_DEFAULT, &xpad->right_dead_zone,
			xpad->right_stick_limit);
	xpad->left_trigger_full_axis = XFULL_TRIGGER_AXIS_DEFAULT;
	xpad->right_trigger_full_axis = XFULL_TRIGGER_AXIS_DEFAULT;

	if (xpad->controller_type == XCONTROLLER_TYPE_GUITAR)
		xpad->rumble_enable = 0;
	else if (xpad->controller_type == XCONTROLLER_TYPE_DANCE_PAD)
		xpad->rumble_enable = 0;
	else
		xpad->rumble_enable = 1;
}


/*
 *	xpad_process_sticks
 *
 *	Handles stick input, accounting for dead zones and square axes. Based
 *	on the original handlers for the Xbox and Xbox 360 in
 *	xpad_process_packet and xpad360_process_packet, but unified to avoid
 *	duplication.
 *
 *	Whenever a dead zone is used, each axis is scaled so that moving the
 *	stick slightly out of the dead zone range results in a low axis
 *	value in jstest(1), while moving the stick to the maximum position
 *	along any axis still results in 32767.
 *
 *	In order to provide the ability to map inputs to a square axis (used
 *	by older games), the left_stick_limit and right_stick_limit can be
 *	set. These limits specify at what point in the raw input coordinates
 *	an axis is reported to be at maximum value (32767 or -32767).
 *
 *	Both the dead zone and stick limit algorithms are implemented
 *	together as a coordinate transformation from "effective coordinates"
 *	onto the output coordinates (which have absolute values from 0 to
 *	32767 and are positive or negative based on direction). Effective
 *	coordinates are defined as those input values that are greater than
 *	the dead zone but less than the stick limit on the axis in question.
 *
 *	DANGER: All denominator values in division operations MUST be checked
 *	for non-zero condition. Dividing by zero inside the kernel can cause
 *	a system deadlock.
 */

static void xpad_process_sticks(struct usb_xpad *xpad, __le16 *data)
{
	struct input_dev *dev = xpad->dev;
	s16 coords[4];    /* x, y, rx, ry */
	int c;
	int range;
	int abs_magnitude, adjusted_magnitude, difference, scale_fraction;
	int dead_zone[2], stick_limit[2];

	dead_zone[0] = xpad->left_dead_zone;
	dead_zone[1] = xpad->right_dead_zone;
	stick_limit[0] = xpad->left_stick_limit;
	stick_limit[1] = xpad->right_stick_limit;

	/* Stick input data starts at byte 12 (16-bit word 6) for the regular
	 * Xbox controller, byte 6 (16-bit word 3) for the 360 controllers */
	data += (xpad->xtype == XTYPE_XBOX) ? 6 : 3;

	coords[0] = le16_to_cpup(data);
	coords[1] = ~le16_to_cpup(data + 1);
	coords[2] = le16_to_cpup(data + 2);
	coords[3] = ~le16_to_cpup(data + 3);

	/* Adjustment for dead zone and square axis */
	for (c = 0; c < 4; c++) {
		abs_magnitude = abs(coords[c]);
		adjusted_magnitude = abs_magnitude;

		range = (stick_limit[c/2] - dead_zone[c/2]);

		if (abs_magnitude >= stick_limit[c/2]) {
			adjusted_magnitude = 32767;
		} else if (abs_magnitude <= dead_zone[c/2]) {
			adjusted_magnitude = 0;
		} else if (range > 0) {
			difference = 32767 - range;
			if (difference) {
				/* DIVISION: difference non-zero */
				scale_fraction = range / difference;
				adjusted_magnitude =
					abs_magnitude - dead_zone[c/2];

				/* Approximate floating-point division with a
				 * "catch-up" scaling algorithm that adds back
				 * to the adjusted_magnitude based on distance
				 * from the origin (0 in adjusted coordinates).
				 * If the range / difference is at least 1,
				 * then 1 needs to be added to the adjusted
				 * magnitude for every scale_fraction units
				 * from the origin. If the range / difference
				 * is less than 1 (0 in integer division),
				 * then divide the difference by the range to
				 * obtain the number of units to add per unit
				 * from the adjusted origin.
				 */
				if (scale_fraction) {
					/* DIVISION: scale_fraction non-zero */
					adjusted_magnitude +=
						adjusted_magnitude
						/ scale_fraction;
				} else {
					/* DIVISION: range non-zero */
					scale_fraction = difference / range;
					adjusted_magnitude +=
						adjusted_magnitude
						* scale_fraction;
				}
				if (adjusted_magnitude > 32767)
					adjusted_magnitude = 32767;
			}
		}
		coords[c] = (coords[c] < 0) ?
				-adjusted_magnitude : adjusted_magnitude;
	}

	input_report_abs(dev, ABS_X, coords[0]);
	input_report_abs(dev, ABS_Y, coords[1]);
	input_report_abs(dev, ABS_RX, coords[2]);
	input_report_abs(dev, ABS_RY, coords[3]);
}


/*
 *	xpad_process_packet
 *
 *	Completes a request by converting the data into events for the
 *	input subsystem.
 *
 *	The used report descriptor was taken from ITO Takayukis website:
 *	 http://euc.jp/periphs/xbox-controller.ja.html
 */

static void xpad_process_packet(struct usb_xpad *xpad, u16 cmd,
		unsigned char *data)
{
	struct input_dev *dev = xpad->dev;

	/* left and right sticks */
	xpad_process_sticks(xpad, (__le16 *) data);

	/* triggers left/right */
	input_report_abs(dev, ABS_Z, data[10]);
	input_report_abs(dev, ABS_RZ, data[11]);

	/* digital pad */
	if (xpad->dpad_mapping == MAP_DPAD_TO_AXES) {
		input_report_abs(dev, ABS_HAT0X,
				 !!(data[2] & 0x08) - !!(data[2] & 0x04));
		input_report_abs(dev, ABS_HAT0Y,
				 !!(data[2] & 0x02) - !!(data[2] & 0x01));
	} else /* xpad->dpad_mapping == MAP_DPAD_TO_BUTTONS */ {
		input_report_key(dev, BTN_LEFT,  data[2] & 0x04);
		input_report_key(dev, BTN_RIGHT, data[2] & 0x08);
		input_report_key(dev, BTN_0,     data[2] & 0x01); /* up */
		input_report_key(dev, BTN_1,     data[2] & 0x02); /* down */
	}

	/* start/back buttons and stick press left/right */
	input_report_key(dev, BTN_START,  data[2] & 0x10);
	input_report_key(dev, BTN_BACK,   data[2] & 0x20);
	input_report_key(dev, BTN_THUMBL, data[2] & 0x40);
	input_report_key(dev, BTN_THUMBR, data[2] & 0x80);

	/* "analog" buttons A, B, X, Y */
	input_report_key(dev, BTN_A, data[4]);
	input_report_key(dev, BTN_B, data[5]);
	input_report_key(dev, BTN_X, data[6]);
	input_report_key(dev, BTN_Y, data[7]);

	/* "analog" buttons black, white */
	input_report_key(dev, BTN_C, data[8]);
	input_report_key(dev, BTN_Z, data[9]);

	input_sync(dev);
}


/*
 *	xpad360_process_packet
 *
 *	Completes a request by converting the data into events for the
 *	input subsystem. It is version for xbox 360 controller
 *
 *	The used report descriptor was taken from:
 *		http://www.free60.org/wiki/Gamepad
 */

static void xpad360_process_packet(struct usb_xpad *xpad, u16 cmd,
		unsigned char *data)
{
	struct input_dev *dev = xpad->dev;
	int trigger;

	/* digital pad */
	if (xpad->dpad_mapping == MAP_DPAD_TO_AXES) {
		input_report_abs(dev, ABS_HAT0X,
				 !!(data[2] & 0x08) - !!(data[2] & 0x04));
		input_report_abs(dev, ABS_HAT0Y,
				 !!(data[2] & 0x02) - !!(data[2] & 0x01));
	} else if (xpad->dpad_mapping == MAP_DPAD_TO_BUTTONS) {
		/* dpad as buttons (right, left, down, up) */
		input_report_key(dev, BTN_LEFT, data[2] & 0x04);
		input_report_key(dev, BTN_RIGHT, data[2] & 0x08);
		input_report_key(dev, BTN_0, data[2] & 0x01);	/* up */
		input_report_key(dev, BTN_1, data[2] & 0x02);	/* down */
	}

	/* start/back buttons */
	input_report_key(dev, BTN_START,  data[2] & 0x10);
	input_report_key(dev, BTN_BACK,   data[2] & 0x20);

	/* stick press left/right */
	input_report_key(dev, BTN_THUMBL, data[2] & 0x40);
	input_report_key(dev, BTN_THUMBR, data[2] & 0x80);

	/* buttons A,B,X,Y,TL,TR and MODE */
	input_report_key(dev, BTN_A,	data[3] & 0x10);
	input_report_key(dev, BTN_B,	data[3] & 0x20);
	input_report_key(dev, BTN_X,	data[3] & 0x40);
	input_report_key(dev, BTN_Y,	data[3] & 0x80);
	input_report_key(dev, BTN_TL,	data[3] & 0x01);
	input_report_key(dev, BTN_TR,	data[3] & 0x02);
	input_report_key(dev, BTN_MODE,	data[3] & 0x04);

	/* left and right sticks */
	xpad_process_sticks(xpad, (__le16 *) data);

	/* triggers left/right: when full_axis is not enabled, report the
	 * absolute data value (0-255), which will be mapped onto [0,32767].
	 * If full axis is enabled, map the data value onto [-255:255], so
	 * that the input subsystem maps it onto [-32767:32767]. */
	trigger = data[4];
	if (xpad->left_trigger_full_axis)
		trigger = (2 * trigger) - 255;
	input_report_abs(dev, ABS_Z, trigger);
	trigger = data[5];
	if (xpad->right_trigger_full_axis)
		trigger = (2 * trigger) - 255;
	input_report_abs(dev, ABS_RZ, trigger);

	input_sync(dev);
}


static void xpad360w_identify_controller(struct usb_xpad *xpad)
{
	int i;
	unsigned char *data = xpad->id_packet;

	if (!data)
		return;

	snprintf(xpad->controller_unique_id, 17,
		"%02x%02x%02x%02x%02x%02x%02x%02x",
		data[8], data[9], data[10], data[11], data[12], data[13],
		data[14], data[15]);

	/* Identify controller type */
	xpad->controller_type = XCONTROLLER_TYPE_OTHER;
	for (i = 0; w360_id[i].id_bytes; i++) {
		if (!memcmp(data + 22, &w360_id[i].id_bytes, 4)) {
			xpad->controller_type =
				w360_id[i].controller_type;
			break;
		}
	}

	if (xpad->controller_type == XCONTROLLER_TYPE_OTHER)
		printk(KERN_INFO "xpad: unknown wireless controller: "
			"%02x%02x %02x%02x\n", data[22], data[23], data[24],
			data[25]);
}


/*
 *	xpad_work_controller
 *
 *	Submits command to set pad number on LED display of wireless 360
 *	controllers, as well as online/offline event. The shared workqueue
 *      is used for this purpose, so that the interrupt handler is kept short.
 */

static void xpad_work_controller(struct work_struct *w)
{
	struct usb_xpad *xpad = container_of(w, struct usb_xpad, work);
	if (xpad->controller_present) {
		xpad360w_identify_controller(xpad);
		xpad_init_controller(xpad);
#ifdef CONFIG_JOYSTICK_XPAD_LEDS
		xpad_send_led_command(xpad, xpad->controller_number + 1);
#endif
		kobject_uevent(&xpad->dev->dev.kobj, KOBJ_ONLINE);
	} else {
		kobject_uevent(&xpad->dev->dev.kobj, KOBJ_OFFLINE);
	}
}


/*
 * xpad360w_process_packet
 *
 * Completes a request by converting the data into events for the
 * input subsystem. It is version for xbox 360 wireless controller.
 *
 * Byte.Bit
 * 00.1 - Status change: The controller or headset has connected/disconnected
 *                       Bits 01.7 and 01.6 are valid
 * 01.f - Some kind of unique identifier message (see above)
 * 01.7 - Controller present
 * 01.6 - Headset present
 * 01.1 - Pad state (Bytes 4+) valid
 *
 */

static void xpad360w_process_packet(struct usb_xpad *xpad, u16 cmd,
		unsigned char *data)
{
	int padnum = 0;

	/* Presence change */
	if (data[0] & 0x08) {
		padnum = xpad->controller_number;
		if (data[1] & 0x80) {
			/* ignore duplicates */
			if (!xpad->controller_present) {
				xpad->controller_present = 1;
				/*schedule_work(&xpad->work);*/
				/* Wait for id packet before setting
				 * controller type and LEDs */
			}
		} else {
			xpad->controller_present = 0;
			xpad->controller_unique_id[0] = '\0';
			xpad->controller_type = XCONTROLLER_TYPE_NONE;
			/* We do NOT flush the shared workqueue here, because
			 * this function is called from an interrupt handler.
			 * If the controller has disconnected from the receiver,
			 * the worst that will happen from the work task running
			 * is that a packet will be transmitted from the
			 * receiver to a non-listening controller
			 */
		}
	}

	/* Process packets according to type */
	if (data[1] == 0x0f) {
		if (!xpad->controller_unique_id[0]) {
			if (xpad->id_packet) {
				memcpy(xpad->id_packet, data, 29);
				schedule_work(&xpad->work);
			}
		}
	} else if (data[1] & 0x1) {
		xpad360_process_packet(xpad, cmd, &data[4]);
	}
}


static void xpad_irq_in(struct urb *urb)
{
	struct usb_xpad *xpad = urb->context;
	int retval, status;

	status = urb->status;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
			__func__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
			__func__, status);
		goto exit;
	}

	switch (xpad->xtype) {
	case XTYPE_XBOX360:
		xpad360_process_packet(xpad, 0, xpad->idata);
		break;
	case XTYPE_XBOX360W:
		xpad360w_process_packet(xpad, 0, xpad->idata);
		break;
	default:
		xpad_process_packet(xpad, 0, xpad->idata);
	}

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err("%s - usb_submit_urb failed with result %d",
		     __func__, retval);
}

/* end input section */

/*****************************************************************************/
/* IRQ output section: present in object code only if the force feedback or
 * LED interface is enabled.
 */

#if defined(CONFIG_JOYSTICK_XPAD_FF) || defined(CONFIG_JOYSTICK_XPAD_LEDS)
static void xpad_irq_out(struct urb *urb)
{
	int retval, status;

	status = urb->status;

	switch (status) {
	case 0:
		/* success */
		return;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __func__, status);
		return;

	default:
		dbg("%s - nonzero urb status received: %d", __func__, status);
		goto exit;
	}

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err("%s - usb_submit_urb failed with result %d",
		    __func__, retval);
}


static int xpad_init_output(struct usb_interface *intf, struct usb_xpad *xpad)
{
	struct usb_endpoint_descriptor *ep_irq_out;
	int error = -ENOMEM;

	if ((xpad->xtype != XTYPE_XBOX360) && (xpad->xtype != XTYPE_XBOX360W))
		return 0;

	xpad->odata = usb_buffer_alloc(xpad->udev, XPAD_PKT_LEN,
				       GFP_KERNEL, &xpad->odata_dma);
	if (!xpad->odata)
		goto fail1;

	mutex_init(&xpad->odata_mutex);

	xpad->irq_out = usb_alloc_urb(0, GFP_KERNEL);
	if (!xpad->irq_out)
		goto fail2;

	ep_irq_out = &intf->cur_altsetting->endpoint[1].desc;
	usb_fill_int_urb(xpad->irq_out, xpad->udev,
			 usb_sndintpipe(xpad->udev,
				ep_irq_out->bEndpointAddress),
			 xpad->odata, XPAD_PKT_LEN,
			 xpad_irq_out, xpad, ep_irq_out->bInterval);
	xpad->irq_out->transfer_dma = xpad->odata_dma;
	xpad->irq_out->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return 0;

 fail2:	usb_buffer_free(xpad->udev, XPAD_PKT_LEN, xpad->odata, xpad->odata_dma);
 fail1:	return error;
}

static void xpad_stop_output(struct usb_xpad *xpad)
{
	if ((xpad->xtype == XTYPE_XBOX360) || (xpad->xtype == XTYPE_XBOX360W))
		usb_kill_urb(xpad->irq_out);
}

static void xpad_deinit_output(struct usb_xpad *xpad)
{
	if ((xpad->xtype == XTYPE_XBOX360) || (xpad->xtype == XTYPE_XBOX360W)) {
		usb_free_urb(xpad->irq_out);
		usb_buffer_free(xpad->udev, XPAD_PKT_LEN,
				xpad->odata, xpad->odata_dma);
	}
}
#else
/* Dummy implementations for xpad_probe and xpad_disconnect */
static int xpad_init_output(struct usb_interface *intf, struct usb_xpad *xpad)
	{ return 0; }
static void xpad_deinit_output(struct usb_xpad *xpad) {}
static void xpad_stop_output(struct usb_xpad *xpad) {}
#endif

/* end output section */

/*****************************************************************************/

/* Force feedback (rumble effect) section, depends on CONFIG_JOYSTICK_XPAD_FF */

#ifdef CONFIG_JOYSTICK_XPAD_FF

/* Rumble support for wireless controllers follows protocol description
 * from xboxdrv userspace driver:
 *       http://pingus.seul.org/~grumbel/xboxdrv/
 */
static int xpad_play_effect(struct input_dev *dev, void *data,
			    struct ff_effect *effect)
{
	struct usb_xpad *xpad = input_get_drvdata(dev);

	if (!xpad->rumble_enable)
		return 0;

	if (effect->type == FF_RUMBLE) {
		__u16 strong = effect->u.rumble.strong_magnitude;
		__u16 weak = effect->u.rumble.weak_magnitude;

		mutex_lock(&xpad->odata_mutex);
		if (xpad->xtype == XTYPE_XBOX360W) {
			xpad->odata[0] = 0x00;
			xpad->odata[1] = 0x01;
			xpad->odata[2] = 0x0f;
			xpad->odata[3] = 0xc0;
			xpad->odata[4] = 0x00;
			xpad->odata[5] = strong / 256;
			xpad->odata[6] = weak / 256;
			xpad->odata[7] = 0x00;
			xpad->odata[8] = 0x00;
			xpad->odata[9] = 0x00;
			xpad->odata[10] = 0x00;
			xpad->odata[11] = 0x00;
			xpad->irq_out->transfer_buffer_length = 12;
		} else {
			xpad->odata[0] = 0x00;
			xpad->odata[1] = 0x08;
			xpad->odata[2] = 0x00;
			xpad->odata[3] = strong / 256;
			xpad->odata[4] = weak / 256;
			xpad->odata[5] = 0x00;
			xpad->odata[6] = 0x00;
			xpad->odata[7] = 0x00;
			xpad->irq_out->transfer_buffer_length = 8;
		}
		/* FIXME: now atomic? */
		usb_submit_urb(xpad->irq_out, GFP_ATOMIC);
		mutex_unlock(&xpad->odata_mutex);
	}

	return 0;
}

static int xpad_init_ff(struct usb_xpad *xpad)
{
	if ((xpad->xtype != XTYPE_XBOX360) && (xpad->xtype != XTYPE_XBOX360W))
		return 0;

	input_set_capability(xpad->dev, EV_FF, FF_RUMBLE);

	return input_ff_create_memless(xpad->dev, NULL, xpad_play_effect);
}

#else
/* dummy implementation for xpad_probe */
static int xpad_init_ff(struct usb_xpad *xpad) { return 0; }
#endif


/* end force feedback section */

/*****************************************************************************/

/* LED handling section: provides support for the ring of LEDs on the 360
 * controllers. */

#ifdef CONFIG_JOYSTICK_XPAD_LEDS


/* XBox 360 wireless controller follows protocol from xboxdrv userspace
 * driver:
 *    http://pingus.seul.org/~grumbel/xboxdrv/
 */
static void xpad_send_led_command(struct usb_xpad *xpad, int command)
{
	if (command >= 0 && command < 14) {
		mutex_lock(&xpad->odata_mutex);
		if (xpad->xtype == XTYPE_XBOX360W) {
			xpad->odata[0] = 0x00;
			xpad->odata[1] = 0x00;
			xpad->odata[2] = 0x08;
			xpad->odata[3] = 0x40 + (command % 0x0e);
			xpad->odata[4] = 0x00;
			xpad->odata[5] = 0x00;
			xpad->odata[6] = 0x00;
			xpad->odata[7] = 0x00;
			xpad->odata[8] = 0x00;
			xpad->odata[9] = 0x00;
			xpad->odata[10] = 0x00;
			xpad->odata[11] = 0x00;
			xpad->irq_out->transfer_buffer_length = 12;
		} else {
			xpad->odata[0] = 0x01;
			xpad->odata[1] = 0x03;
			xpad->odata[2] = command;
			xpad->irq_out->transfer_buffer_length = 3;
		}
		usb_submit_urb(xpad->irq_out, GFP_KERNEL);
		mutex_unlock(&xpad->odata_mutex);
	}
}

static void xpad_led_set(struct led_classdev *led_cdev,
			 enum led_brightness value)
{
	struct xpad_led *xpad_led = container_of(led_cdev,
						 struct xpad_led, led_cdev);

	xpad_send_led_command(xpad_led->xpad, value);
}


static int xpad_led_probe(struct usb_xpad *xpad)
{
	static atomic_t led_seq	= ATOMIC_INIT(0);
	long led_no;
	struct xpad_led *led;
	struct led_classdev *led_cdev;
	int error;

	if ((xpad->xtype != XTYPE_XBOX360) && (xpad->xtype != XTYPE_XBOX360W))
		return 0;

	xpad->led = led = kzalloc(sizeof(struct xpad_led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led_no = (long)atomic_inc_return(&led_seq) - 1;

	snprintf(led->name, sizeof(led->name), "xpad%ld", led_no);
	led->xpad = xpad;

	led_cdev = &led->led_cdev;
	led_cdev->name = led->name;
	led_cdev->brightness_set = xpad_led_set;

	error = led_classdev_register(&xpad->udev->dev, led_cdev);
	if (error) {
		kfree(led);
		xpad->led = NULL;
		return error;
	}

	/*
	 * Light up the segment corresponding to controller number
	 */
	xpad_send_led_command(xpad, (led_no % 4) + 2);

	return 0;
}

static void xpad_led_disconnect(struct usb_xpad *xpad)
{
	struct xpad_led *xpad_led = xpad->led;

	if (xpad_led) {
		led_classdev_unregister(&xpad_led->led_cdev);
		kfree(xpad_led->name);
	}
}
#else
/* dummies for xpad_probe and xpad_disconnect */
static int xpad_led_probe(struct usb_xpad *xpad) { return 0; }
static void xpad_led_disconnect(struct usb_xpad *xpad) { }
#endif

/* end LED section */

/*****************************************************************************/

/* Module and device functions */

static int xpad_open(struct input_dev *dev)
{
	struct usb_xpad *xpad = input_get_drvdata(dev);

	/* URB was submitted in probe */
	if (xpad->xtype == XTYPE_XBOX360W)
		return 0;

	xpad->irq_in->dev = xpad->udev;
	if (usb_submit_urb(xpad->irq_in, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void xpad_close(struct input_dev *dev)
{
	struct usb_xpad *xpad = input_get_drvdata(dev);

	if (xpad->xtype != XTYPE_XBOX360W)
		usb_kill_urb(xpad->irq_in);
	xpad_stop_output(xpad);
}

static void xpad_set_up_abs(struct input_dev *input_dev, signed short abs)
{
	set_bit(abs, input_dev->absbit);

	switch (abs) {
	case ABS_X:
	case ABS_Y:
	case ABS_RX:
	case ABS_RY:	/* the two sticks */
		input_set_abs_params(input_dev, abs, -32768, 32767, 16, 128);
		break;
	case ABS_Z:
	case ABS_RZ:	/* the triggers */
		/* Triggers have a phony -255 to 255 range. Normally, only
		 * 0 to 255 will be reported (+ axis), unless full_trigger_axis
		 * is set, in which case -255 to 255 will be reported. */
		input_set_abs_params(input_dev, abs, -255, 255, 0, 0);
		break;
	case ABS_HAT0X:
	case ABS_HAT0Y:	/* the d-pad (only if MAP_DPAD_TO_AXES) */
		input_set_abs_params(input_dev, abs, -1, 1, 0, 0);
		break;
	}
}

static int xpad_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_xpad *xpad;
	struct input_dev *input_dev;
	struct usb_endpoint_descriptor *ep_irq_in;
	int controller_type;
	int i;
	int error = -ENOMEM;

	for (i = 0; xpad_device[i].idVendor; i++) {
		if ((le16_to_cpu(udev->descriptor.idVendor) ==
						xpad_device[i].idVendor) &&
				(le16_to_cpu(udev->descriptor.idProduct) ==
						xpad_device[i].idProduct))
			break;
	}

	xpad = kzalloc(sizeof(struct usb_xpad), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!xpad || !input_dev)
		goto fail1;

	xpad->idata = usb_buffer_alloc(udev, XPAD_PKT_LEN,
				       GFP_KERNEL, &xpad->idata_dma);
	if (!xpad->idata)
		goto fail1;

	xpad->irq_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!xpad->irq_in)
		goto fail2;

	xpad->udev = udev;
	xpad->dpad_mapping = xpad_device[i].dpad_mapping;
	xpad->xtype = xpad_device[i].xtype;
	controller_type = xpad_device[i].controller_type;
	if (xpad->dpad_mapping == MAP_DPAD_UNKNOWN)
		xpad->dpad_mapping = !dpad_to_buttons;
	if (xpad->xtype == XTYPE_UNKNOWN) {
		if (intf->cur_altsetting->desc.bInterfaceClass ==
					USB_CLASS_VENDOR_SPEC) {
			if (intf->cur_altsetting->desc.bInterfaceProtocol ==
					129)
				xpad->xtype = XTYPE_XBOX360W;
			else
				xpad->xtype = XTYPE_XBOX360;
		} else
			xpad->xtype = XTYPE_XBOX;
	}
	xpad->dev = input_dev;
	usb_make_path(udev, xpad->phys, sizeof(xpad->phys));
	strlcat(xpad->phys, "/input0", sizeof(xpad->phys));

	input_dev->name = xpad_device[i].name;
	input_dev->phys = xpad->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, xpad);

	input_dev->open = xpad_open;
	input_dev->close = xpad_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	/* set up buttons */
	for (i = 0; xpad_common_btn[i] >= 0; i++)
		set_bit(xpad_common_btn[i], input_dev->keybit);
	if ((xpad->xtype == XTYPE_XBOX360) || (xpad->xtype == XTYPE_XBOX360W))
		for (i = 0; xpad360_btn[i] >= 0; i++)
			set_bit(xpad360_btn[i], input_dev->keybit);
	else
		for (i = 0; xpad_btn[i] >= 0; i++)
			set_bit(xpad_btn[i], input_dev->keybit);
	if (xpad->dpad_mapping == MAP_DPAD_TO_BUTTONS) {
		for (i = 0; xpad_btn_pad[i] >= 0; i++)
			set_bit(xpad_btn_pad[i], input_dev->keybit);
	}

	/* set up axes */
	for (i = 0; xpad_abs[i] >= 0; i++)
		xpad_set_up_abs(input_dev, xpad_abs[i]);
	if (xpad->dpad_mapping == MAP_DPAD_TO_AXES) {
		for (i = 0; xpad_abs_pad[i] >= 0; i++)
			xpad_set_up_abs(input_dev, xpad_abs_pad[i]);
	}

	error = xpad_init_output(intf, xpad);
	if (error)
		goto fail2;

	error = xpad_init_ff(xpad);
	if (error)
		goto fail3;

	error = xpad_led_probe(xpad);
	if (error)
		goto fail3;

	ep_irq_in = &intf->cur_altsetting->endpoint[0].desc;
	usb_fill_int_urb(xpad->irq_in, udev,
			 usb_rcvintpipe(udev, ep_irq_in->bEndpointAddress),
			 xpad->idata, XPAD_PKT_LEN, xpad_irq_in,
			 xpad, ep_irq_in->bInterval);
	xpad->irq_in->transfer_dma = xpad->idata_dma;
	xpad->irq_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(xpad->dev);
	if (error)
		goto fail4;

	usb_set_intfdata(intf, xpad);

	xpad->controller_type = controller_type;
	if (controller_type != XCONTROLLER_TYPE_NONE)
		xpad_init_controller(xpad);

	/*
	 * Submit the int URB immediatly rather than waiting for open
	 * because we get status messages from the device whether
	 * or not any controllers are attached.  In fact, it's
	 * exactly the message that a controller has arrived that
	 * we're waiting for.
	 */
	if (xpad->xtype == XTYPE_XBOX360W) {
		xpad->controller_present = 0;
		xpad->controller_number =
			(intf->cur_altsetting->desc.bInterfaceNumber / 2) + 1;
		xpad->irq_in->dev = xpad->udev;
		error = usb_submit_urb(xpad->irq_in, GFP_KERNEL);
		if (error)
			goto fail5;
		xpad->id_packet = kzalloc(XPAD_PKT_LEN *
					sizeof(unsigned char), GFP_KERNEL);
		if (!xpad->id_packet)
			goto fail5;
	} else {
		xpad->controller_present = 1;
		xpad->controller_number = 0;
	}

	/* Set up device attributes */
	xpad->sysfs_ok = 1;
	xpad->controller_unique_id[0] = '\0';
	error = sysfs_create_group(&input_dev->dev.kobj,
						&xpad_default_attr_group);
	if (error) {
		/* Driver will work without the sysfs interface, but parameters
		 * will not be adjustable, so this failure is a warning. */
		printk(KERN_WARNING
			"xpad: sysfs_create_group failed with error %d\n",
			error);
		xpad->sysfs_ok = 0;
	}

	INIT_WORK(&xpad->work, &xpad_work_controller);

	return 0;

 fail5: usb_set_intfdata(intf, NULL);
	input_unregister_device(xpad->dev);
	xpad_led_disconnect(xpad);
 fail4:	usb_free_urb(xpad->irq_in);
 fail3:	xpad_deinit_output(xpad);
 fail2:	usb_buffer_free(udev, XPAD_PKT_LEN, xpad->idata, xpad->idata_dma);
 fail1:	input_free_device(input_dev);
	kfree(xpad);
	return error;

}

static void xpad_disconnect(struct usb_interface *intf)
{
	struct usb_xpad *xpad = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (xpad) {
		/* Ensure we don't have any pending work */
		flush_scheduled_work();

		if (xpad->sysfs_ok)
			sysfs_remove_group(&xpad->dev->dev.kobj,
						&xpad_default_attr_group);

		xpad_led_disconnect(xpad);
		input_unregister_device(xpad->dev);
		xpad_deinit_output(xpad);
		if (xpad->xtype == XTYPE_XBOX360W)
			usb_kill_urb(xpad->irq_in);
		usb_free_urb(xpad->irq_in);
		usb_buffer_free(xpad->udev, XPAD_PKT_LEN,
				xpad->idata, xpad->idata_dma);
		if (xpad->id_packet)
			kfree(xpad->id_packet);
		kfree(xpad);
	}
}



static int __init usb_xpad_init(void)
{
	int result = usb_register(&xpad_driver);
	if (result == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_DESC "\n");
	return result;
}

static void __exit usb_xpad_exit(void)
{
	usb_deregister(&xpad_driver);
}


module_init(usb_xpad_init);
module_exit(usb_xpad_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
