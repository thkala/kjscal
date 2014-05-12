/*
 * kjscal - A module that provides automatic joystick calibration
 *
 * Copyright (c) 2005 Theodoros V. Kalamatianos <nyb@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */



#define DEBUG 0



#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/version.h>

#include "version.h"



MODULE_AUTHOR("Theodoros V. Kalamatianos <nyb@users.sourceforge.net>");
MODULE_DESCRIPTION("A module that provides automatic joystick calibration (Version " VERSION ")");
MODULE_LICENSE("GPL");



/* Verbosity level */
#if DEBUG
static int verbose = 1;
#else
static int verbose = 0;
#endif
module_param_named(verbose, verbose, int, 000);
MODULE_PARM_DESC(verbose, "Verbosity level (0-1)");

/* Initial per-axis events to ignore */
static int ignore = 0;
module_param_named(ignore, ignore, int, 000);
MODULE_PARM_DESC(ignore, "Initial per-axis events to ignore");

/* Per-axis recalibration interval */
static int recal = 0;
module_param_named(recal, recal, int, 000);
MODULE_PARM_DESC(recal, "Per-axis recalibration interval");

/* Require at least 1/minrange coverage of absolute range to normalise */
static int minrange = 0;
module_param_named(minrange, minrange, int, 000);
MODULE_PARM_DESC(minrange, "Require at least 1/minrange coverage of absolute range to normalise");

/* Ignore axis changes over 1/noskip of the absolute range */
static int noskip = 0;
module_param_named(noskip, noskip, int, 000);
MODULE_PARM_DESC(noskip, "Ignore axis changes over 1/noskip of the absolute range");

/* Apply noskip only for axis with absolute range greater than skiprange */
static int skiprange = 2;
module_param_named(skiprange, skiprange, int, 000);
MODULE_PARM_DESC(skiprange, "Apply noskip only for axis with absolute range greater than skiprange");



#define MAX_DEV	32
#define VDIR	"virtual/js/"



static const char *name = "kjscal";



typedef struct _kjscal_dev {
    int slot;

    char hname[32];		/* The device handler name */

    char vphys[32];		/* The virtual device phys field */

    struct input_handle handle;

    struct input_dev vdev;	/* The virtual output device */

    int ignr[ABS_MAX + 1];	/* Ignore events per axis down-counter */
    int rset[ABS_MAX + 1];	/* Setup complete per axis flag */
    int rmin[ABS_MAX + 1];	/* Minimum received per axis value */
    int rmax[ABS_MAX + 1];	/* Maximum received per axis value */

    int rccnt[ABS_MAX + 1];	/* Recalibration per axis down counter */
    int rcmin[ABS_MAX + 1];	/* Recalibration minimum received per axis value */
    int rcmax[ABS_MAX + 1];	/* Recalibration maximum received per axis value */

    int last[ABS_MAX + 1];	/* Last received value */
} kjscal_dev;



/* The device slot table */
static struct input_handle *kjscal_devs[MAX_DEV];



static void kjscal_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	kjscal_dev *kjsdev = handle->private;

	/* The absolute value range */
	int absrange = kjsdev->vdev.absmax[code] - kjsdev->vdev.absmin[code];

	/* Event translation */
	if (type == EV_ABS) {
		if (!kjsdev->rset[code]) {
			/* Ignore initial events */
			if (kjsdev->ignr[code] > 0) {
				--(kjsdev->ignr[code]);
				return;
			}

			if (kjsdev->rmin[code] == 0) {
				kjsdev->rmin[code] = value;
			} else {
				/* Skip protection */
				if ((noskip > 0) && (skiprange < absrange)) {
					if (labs((long)value -(long)(kjsdev->rmin[code])) * (long)noskip > (long)absrange)
						return;
					kjsdev->last[code] = value;
				}

				if (kjsdev->rmin[code] < value) {
					kjsdev->rmax[code] = value;
					kjsdev->rset[code] = 1;
				} else if (kjsdev->rmin[code] > value) {
					kjsdev->rmax[code] = kjsdev->rmin[code];
					kjsdev->rmin[code] = value;
					kjsdev->rset[code] = 1;
				}
			}
		} else {
			/* Skip protection */
			if ((noskip > 0) && (skiprange < absrange)) {
				if (labs((long)value -(long)(kjsdev->last[code])) * (long)noskip > (long)absrange)
					return;
				kjsdev->last[code] = value;
			}

			/* Recalibration support */
			if (recal > 0) {
				if (kjsdev->rccnt[code] <= recal) {
					--(kjsdev->rccnt[code]);

					if (value < kjsdev->rcmin[code])
						kjsdev->rcmin[code] = value;
					if (value > kjsdev->rcmax[code])
						kjsdev->rcmax[code] = value;

					/* Recalibrate! */
					if (kjsdev->rccnt[code] <= 0) {
						if ((minrange == 0) || ((long)(kjsdev->rcmax[code] - kjsdev->rcmin[code]) * (long)minrange >= (long)absrange)) {
#if DEBUG
							printk("kjscal%i: Recalibration for axis %i: %i -  %i\n", kjsdev->slot, code, kjsdev->rmin[code], kjsdev->rmax[code]);
#endif
							kjsdev->rmin[code] = kjsdev->rcmin[code];
							kjsdev->rmax[code] = kjsdev->rcmax[code];
							kjsdev->rccnt[code] = recal + 1;
							kjsdev->rcmin[code] = 0;
							kjsdev->rcmax[code] = 0;
						} else {
							kjsdev->rccnt[code] = 1;
						}
					}
				} else {
					if (kjsdev->rcmin[code] == 0) {
						kjsdev->rcmin[code] = value;
					} else {
						if (kjsdev->rcmin[code] < value) {
							kjsdev->rcmax[code] = value;
							kjsdev->rccnt[code] = recal;
						} else if (kjsdev->rcmin[code] > value) {
							kjsdev->rcmax[code] = kjsdev->rcmin[code];
							kjsdev->rcmin[code] = value;
							kjsdev->rccnt[code] = recal;
						}
					}
				}
			}

			if (value < kjsdev->rmin[code])
				kjsdev->rmin[code] = value;
			if (value > kjsdev->rmax[code])
				kjsdev->rmax[code] = value;

			/* Recalibrate if possible */
			if ((minrange == 0) || ((long)(kjsdev->rmax[code] - kjsdev->rmin[code]) * (long)minrange >= (long)absrange))
				value = ((absrange * (value - kjsdev->rmin[code])) / (kjsdev->rmax[code] - kjsdev->rmin[code])) + kjsdev->vdev.absmin[code];
		}
	}

	input_event(&(kjsdev->vdev), type, code, value);
}


static struct input_handle *kjscal_connect(struct input_handler *handler, struct input_dev *dev, struct input_device_id *id)
{
	int i, ret, slot;
	kjscal_dev *kjsdev;

	/* Avoid registering virtual devices */
	if (memcmp(dev->phys, VDIR, strlen(VDIR)) == 0) {
		if (verbose > 0)
			printk("kjscal: ignoring device %s (%s)\n", dev->name, dev->phys);
		return NULL;
	}

	/* Find an empty device slot */
	for (slot = 0; slot < MAX_DEV; ++slot)
		if (kjscal_devs[slot] == NULL) break;
	if (slot >= MAX_DEV) {
		printk(KERN_ERR "kjscal: Could not allocate an empty slot for %s (%s)\n", dev->name, dev->phys);
		return NULL;
	}

#if DEBUG
	printk("kjscal%i: allocating slot for %s (%s)\n", slot, dev->name, dev->phys);
#endif

	if (!(kjsdev = kmalloc(sizeof(kjscal_dev), GFP_KERNEL)))
		return NULL;
	memset(kjsdev, 0, sizeof(kjscal_dev));

#if DEBUG
	printk("kjscal%i: memory allocation for %s (%s) succeeded (%i bytes)\n", slot, dev->name, dev->phys, sizeof(kjscal_dev));
#endif

	/* kjscal_dev fields */
	kjsdev->slot = slot;

	sprintf(kjsdev->hname, "%s%i", name, slot);
	sprintf(kjsdev->vphys, "%s%s", VDIR, kjsdev->hname);

	kjsdev->handle.dev = dev;
	kjsdev->handle.name = kjsdev->hname;
	kjsdev->handle.handler = handler;
	kjsdev->handle.private = kjsdev;


	/* 
	 * WARNING: Black Magic ahead. This is a piece of code heavily
	 * influenced by coder laziness and lack of sleep. Changes in the
	 * kernel have broken it in the past and WILL break it again. Do
	 * not reuse in any other code unless you wish a slow and painfull
	 * death...
	 *
	 * This is definitely NOT the way to do this. The new input_device
	 * struct fields should be duplicated and properly adjusted one at
	 * a time, to avoid duplicating pointers to things that should not
	 * be shared. As it is, this code is full of race conditions, bugs
	 * waiting to happen and who knows what else. You have been warned...
	 */

	/* Copy the input_dev struct to the virtual device (Ouch!) */
	memcpy(&(kjsdev->vdev), dev, sizeof(struct input_dev));

	/* Alter the fields that should differ */
	kjsdev->vdev.private = kjsdev;
	kjsdev->vdev.phys = kjsdev->vphys;
	input_regs(&(kjsdev->vdev), NULL);
	kjsdev->vdev.grab = NULL;
	kjsdev->vdev.open = NULL;
	kjsdev->vdev.close = NULL;
	kjsdev->vdev.dev = NULL;

        /* Ugly fixes for an even uglier piece of sh^Wcode */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
        kjsdev->vdev.cdev.kobj.k_name = NULL;
#endif

	/* Set the kjscal_dev arrays */
	for (i = 0; i < ABS_MAX + 1; ++i) {
		kjsdev->ignr[i] = ignore;
		kjsdev->rccnt[i] = recal + 1;
	}

	/* Register the virtual device */
	input_register_device(&(kjsdev->vdev));
#if DEBUG
	printk("kjscal%i: virtual input device for %s (%s) registered\n", slot, dev->name, dev->phys);
#endif

	ret = input_open_device(&(kjsdev->handle));
	if (ret != 0) {
		printk(KERN_ERR "kjscal%i: Could not open device %s (%s)\n", slot, dev->name, dev->phys);

		/* Return to a sane state */
    		input_unregister_device(&(kjsdev->vdev));
		kjscal_devs[kjsdev->slot] = NULL;
		kfree(kjsdev);

		return NULL;
	}
#if DEBUG
	printk("kjscal%i: input device %s (%s) opened successfully\n", slot, dev->name, dev->phys);
#endif

	if (verbose > 0)
		printk("%s: activated for %s (%s)\n", kjsdev->hname, dev->name, dev->phys);

	return &(kjsdev->handle);
}


static void kjscal_disconnect(struct input_handle *handle)
{
	kjscal_dev *kjsdev = handle->private;

	input_close_device(handle);

	input_unregister_device(&(kjsdev->vdev));

	kjscal_devs[kjsdev->slot] = NULL;
	if (verbose > 0)
		printk("%s: deactivated for %s (%s)\n", kjsdev->hname, handle->dev->name, handle->dev->phys);

	kfree(handle->private);
}



/* Device detection shamelessly copied from the joydev module */
static struct input_device_id kjscal_blacklist[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT(EV_KEY) },
		.keybit = { [LONG(BTN_TOUCH)] = BIT(BTN_TOUCH) },
	}, 	/* Avoid touchpads, touchscreens and tablets */
	{ }, 	/* Terminating entry */
};

static struct input_device_id kjscal_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_ABS) },
		.absbit = { BIT(ABS_X) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_ABS) },
		.absbit = { BIT(ABS_WHEEL) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT(EV_ABS) },
		.absbit = { BIT(ABS_THROTTLE) },
	},
	{ }, 	/* Terminating entry */
};

MODULE_DEVICE_TABLE(input, kjscal_ids);



static struct input_handler kjscal_handler = {
	.event =	kjscal_event,
	.connect =	kjscal_connect,
	.disconnect =	kjscal_disconnect,
	.name =		"kjscal",
	.id_table =	kjscal_ids,
	.blacklist = 	kjscal_blacklist,
};



static int __init kjscal_init(void)
{
	int i;
	for (i = 0; i < MAX_DEV; ++i)
		kjscal_devs[i] = NULL;

	input_register_handler(&kjscal_handler);

	/* module parameter safeguards */
	if (verbose < 0)
		verbose = 0;
	if (ignore < 0)
		ignore = 0;
	if (recal < 0)
		recal = 0;
	if (recal > (INT_MAX - 1))
		recal = INT_MAX - 1;
	if (minrange < 0)
		minrange = 0;
	if (noskip < 0)
		noskip = 0;
	if (skiprange < 0)
		skiprange = 0;

	if (verbose > 0)
		printk("kjscal: Verbose logging enabled\n");

	return 0;
}

static void __exit kjscal_exit(void)
{
	input_unregister_handler(&kjscal_handler);
}



module_init(kjscal_init);
module_exit(kjscal_exit);
