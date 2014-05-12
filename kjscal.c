/*
 * kjscal - A module that provides automatic joystick calibration
 *
 * This module creates for each joystick a virtual joystick with
 * automatic axis calibration.
 *
 * Copyright (c) 2005 Theodoros V. Kalamatianos <nyb@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */



//#define VERSION "0.1.0"
#define DEBUG 1



#include <linux/module.h>

MODULE_AUTHOR("Theodoros V. Kalamatianos <nyb@users.sourceforge.net>");
MODULE_DESCRIPTION("A module that provides automatic joystick calibration (Version " VERSION ")");
MODULE_LICENSE("GPL");



#include <linux/slab.h>
#include <linux/init.h>
#include <linux/input.h>



static int verbose = 0;
module_param_named(verbose, verbose, int, 0);
MODULE_PARM_DESC(verbose, "Verbosity level (0 / 1)");



#define MAX_DEV	32
#define VDIR	"virtual/js/"



const char *name = "kjscal";



typedef struct _kjscal_dev {
    int slot;

    char hname[32];		/* The device handler name */

    char vphys[32];		/* The virtual device phys field */

    struct input_handle handle;

    struct input_dev vdev;	/* The virtual output device */

    int rset[ABS_MAX + 1];	/* Setup complete flag per axis */
    int rmin[ABS_MAX + 1];	/* Minimum received value per axis */
    int rmax[ABS_MAX + 1];	/* Maximum received value per axis */
} kjscal_dev;



/* The device slot table */
struct input_handle *kjscal_devs[MAX_DEV];



static void kjscal_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	kjscal_dev *kjsdev = handle->private;

	/* Event translation */
	if (type == EV_ABS) {
	    if (!kjsdev->rset[code]) {
		if (kjsdev->rmin[code] == 0) {
		    kjsdev->rmin[code] = value;
		} else {
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
		if (value < kjsdev->rmin[code])
		    kjsdev->rmin[code] = value;
		if (value > kjsdev->rmax[code])
		    kjsdev->rmax[code] = value;
		value = ((((kjsdev->vdev.absmax[code]) - (kjsdev->vdev.absmin[code])) * (value - kjsdev->rmin[code])) / (kjsdev->rmax[code] - kjsdev->rmin[code])) + kjsdev->vdev.absmin[code];
	    }
	}

	input_event(&(kjsdev->vdev), type, code, value);
}


static struct input_handle *kjscal_connect(struct input_handler *handler, struct input_dev *dev, struct input_device_id *id)
{
	int slot;
	kjscal_dev *kjsdev;

	/* Avoid registering virtual devices */
	if (memcmp(dev->phys, VDIR, strlen(VDIR)) == 0) {
		if (verbose)
			printk("kjscal: ignoring device %s (%s)\n", dev->name, dev->phys);
		return NULL;
	}

	/* Find an empty device slot */
	for (slot = 0; slot < MAX_DEV; ++slot)
		if (kjscal_devs[slot] == NULL) break;
	if (slot >= MAX_DEV) {
		printk(KERN_ERR "Could not allocate an empty kjscal slot for %s (%s)\n", dev->name, dev->phys);
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

	/* Copy the input_dev struct to the virtual device */
	memcpy(&(kjsdev->vdev), dev, sizeof(struct input_dev));

	/* Alter the fields that should differ */
	kjsdev->vdev.private = kjsdev;
	kjsdev->vdev.phys = kjsdev->vphys;
	input_regs(&(kjsdev->vdev), NULL);
	kjsdev->vdev.grab = NULL;
	kjsdev->vdev.open = NULL;
	kjsdev->vdev.close = NULL;
	kjsdev->vdev.dev = NULL;

	input_register_device(&(kjsdev->vdev));
#if DEBUG
	printk("kjscal%i: virtual input device for %s (%s) registered\n", slot, dev->name, dev->phys);
#endif

	input_open_device(&(kjsdev->handle));

	if (verbose)
		printk("%s: activated for %s (%s)\n", kjsdev->hname, dev->name, dev->phys);

	return &(kjsdev->handle);
}


static void kjscal_disconnect(struct input_handle *handle)
{
	kjscal_dev *kjsdev = handle->private;

	input_close_device(handle);

	input_unregister_device(&(kjsdev->vdev));

	kjscal_devs[kjsdev->slot] = NULL;
	if (verbose)
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

	if (verbose)
		printk("kjscal: Verbose logging enabled\n");

	return 0;
}

static void __exit kjscal_exit(void)
{
	input_unregister_handler(&kjscal_handler);
}



module_init(kjscal_init);
module_exit(kjscal_exit);
