/**
 * ASUS Zenbook UX31A (CPU) fan driver.
 * 
 * This driver is based on work by Felipe Contreras <felipe.contreras@gmail.com> (see https://gist.github.com/felipec/6169047)
 * 
 */
 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/dmi.h>

#include <linux/suspend.h> // Suspend notification
#include <linux/reboot.h>  // Power-off notification
#include <linux/mutex.h>   // Mutual exclusion

MODULE_AUTHOR("Daniel Hillerström <dhildotnet@gmail.com>");
MODULE_DESCRIPTION("ASUS fan driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");

// Convenient macros
#define GET_DEVICE_DATA(cdev) ((struct cdev_data*)(cdev->devdata))
#define ACQUIRE(cdev) (mutex_lock(GET_DEVICE_DATA(cdev)->lock))
#define RELEASE(cdev) (mutex_unlock(GET_DEVICE_DATA(cdev)->lock))
#define MAX_SPEED 0xFF
#define MIN_SPEED 0x00

// Auxiliary cooling device data structure
struct cdev_data {
  int id;
  int speed;
  enum { AUTO = 1, MANUAL = 2 } mode;
  enum { SUSPENDED = 1, ACTIVE = 2 } state;
  struct mutex *lock;
};

static struct thermal_cooling_device *cdev;

/** Auxiliary cooling device data structure initialisation and destruction **/
static struct cdev_data* make_cdev_data(int fan_id, int *error) {
  struct cdev_data *data = kmalloc(sizeof(struct cdev_data), GFP_KERNEL);
  if (!data) {
    printk(KERN_ERR "Out of memory.");
    *error = -ENOMEM;
    return NULL;
  }
  
  data->id    = fan_id;
  data->state = ACTIVE;
  data->mode  = AUTO;
  data->speed = 0;
  
  // Initialize device lock
  data->lock = kmalloc(sizeof(struct mutex), GFP_KERNEL);
  if (!data->lock) {
    printk(KERN_ERR "Out of memory.");
    *error = -ENOMEM;
    return NULL;
  }
  mutex_init(data->lock);
  return data;
}

static int cdev_data_destroy(struct cdev_data *data) {
  mutex_destroy(data->lock);
  kfree(data->lock);
  kfree(data);
  data = NULL;
  return 0;
}

/** Fan manipulation **/
static int fan_get_max_speed(struct thermal_cooling_device *cdev, unsigned long *max_speed) {
  *max_speed = MAX_SPEED;
  return 0;
}

/*
 * Sets the speed of @fan_id 
 * @fan_id = 1 sets the state of CPU fan, while @fan_id = 2 sets the state of GPU fan (if present).
 * @fan_id = 0 and @speed = 0 put both fans in AUTO mode.
 **/
static int fan_set(int fan_id, int speed) {
  struct acpi_object_list params;
  union acpi_object in_objs[2];
  unsigned long long value;
  
  params.count = ARRAY_SIZE(in_objs);
  params.pointer = in_objs;
  in_objs[0].type = ACPI_TYPE_INTEGER;
  in_objs[0].integer.value = fan_id;
  in_objs[1].type = ACPI_TYPE_INTEGER;
  in_objs[1].integer.value = speed;
  
  return acpi_evaluate_integer(NULL, "\\_SB.PCI0.LPCB.EC0.SFNV", &params, &value);
}

/* Monitored operations */
static int fan_get_cur_speed(struct thermal_cooling_device *cdev, unsigned long *cur_speed) {
  struct cdev_data *data = GET_DEVICE_DATA(cdev);
  struct acpi_object_list params;
  union acpi_object in_objs[1];
  unsigned long long value;
  acpi_status r;

  ACQUIRE(cdev);
  if (data->mode == AUTO && data->state == ACTIVE) {
    params.count = ARRAY_SIZE(in_objs);
    params.pointer = in_objs;
    in_objs[0].type = ACPI_TYPE_INTEGER;
    in_objs[0].integer.value = data->id-1;

    r = acpi_evaluate_integer(NULL, "\\_TZ.RFAN", &params, &value);
  } else if (data->mode == MANUAL && data->state == ACTIVE) {
    value = (unsigned long)data->speed;
    r = AE_OK;
  } else {
    r = -EAGAIN; // it is suspended.
    value = 0;
  }
  
  if (r == AE_OK)
    *cur_speed = value;
  RELEASE(cdev);
  return r != AE_OK ? r : 0;
}
 
static int fan_set_cur_speed(struct thermal_cooling_device *cdev, unsigned long speed) {
  struct cdev_data *data = GET_DEVICE_DATA(cdev);
  int r;

  // Sanitize input argument
  if (speed < MIN_SPEED || speed > MAX_SPEED)
    return -EINVAL;
  
  ACQUIRE(cdev);
  if (data->state == ACTIVE) {
    r = fan_set(data->id, speed);
    if (r == 0) {
      data->speed = (int)speed;
      if (data->mode == AUTO)
	data->mode = MANUAL;
    }
  } else {
    r = -EAGAIN;
  }
  RELEASE(cdev);
  return r;
}
 
static int fan_set_auto(void) {
  struct cdev_data *data = GET_DEVICE_DATA(cdev); // Global device
  int r;
  ACQUIRE(cdev);
  if (data->state == ACTIVE) {
    r = fan_set(0, 0);
    data->mode = AUTO;
  } else {
    r = -EAGAIN;
  }
  RELEASE(cdev);
  return r;
}
 
static const struct thermal_cooling_device_ops fan_cooling_ops = {
	.get_max_state = fan_get_max_speed,
	.get_cur_state = fan_get_cur_speed,
	.set_cur_state = fan_set_cur_speed,
};

/** Suspending, resuming and powering down **/
// Make sure the fan is powered down properly
static int prepare_suspend(void) {
  struct cdev_data *dev = GET_DEVICE_DATA(cdev);
  int r;
  ACQUIRE(cdev);
  if (dev->state == ACTIVE) {
    if (dev->mode == MANUAL)
      fan_set(0,0);
    dev->state = SUSPENDED;
    r = NOTIFY_OK;
  } else {
    r = NOTIFY_DONE;
  }
  RELEASE(cdev);
  return r;
}

// Return to pre-suspend state
static int prepare_resume(void) {
  struct cdev_data *dev = GET_DEVICE_DATA(cdev);
  int r;
  // Acquire lock
  ACQUIRE(cdev);
  if (dev->state == SUSPENDED) {
    if (dev->mode == MANUAL)
      fan_set(dev->id, dev->speed);
    dev->state = ACTIVE;
    r = NOTIFY_OK;
  } else {
    r = NOTIFY_DONE;
  }
  // Release lock
  RELEASE(cdev);
  return r;
}

// CPU notification handler
static int nb_suspend_handler(struct notifier_block *nb, unsigned long state, void *data) {
  if (state == PM_SUSPEND_PREPARE)
    return prepare_suspend();
  else if (state == PM_POST_SUSPEND)
    return prepare_resume();
  else
    return NOTIFY_DONE; // we do not care
}

static int nb_shutdown_handler(struct notifier_block *nb, unsigned long state, void *data) {
  if (state == SYS_DOWN || state == SYS_HALT || state == SYS_RESTART || state == SYS_POWER_OFF)
    return prepare_suspend();
  return NOTIFY_DONE;
}

static struct notifier_block suspend_handler = {
  .notifier_call = nb_suspend_handler,
};

static struct notifier_block shutdown_handler = {
  .notifier_call = nb_shutdown_handler,
};
 
static int __init fan_init(void) {
  struct cdev_data *fan_data;
  int err;
  int nb_suspend_reg_status, nb_shutdown_reg_status;

  if (strcmp(dmi_get_system_info(DMI_SYS_VENDOR), "ASUSTeK COMPUTER INC."))
    return -ENODEV;
  fan_data = make_cdev_data(1, &err);
  if (fan_data == NULL) {
    return err;
  }

  cdev = thermal_cooling_device_register("Fan", fan_data, &fan_cooling_ops);
  if (IS_ERR(cdev)) {
    cdev_data_destroy(fan_data);
    return PTR_ERR(cdev);
  }

  // Register notifier
  nb_suspend_reg_status = register_pm_notifier(&suspend_handler);
  if (nb_suspend_reg_status != 0) {
    fan_set_auto();
    cdev_data_destroy(fan_data);
    thermal_cooling_device_unregister(cdev);
    printk(KERN_CRIT "Failed to register CPU notification handler.\n");
    return -1;
  }
  
  nb_shutdown_reg_status = register_reboot_notifier(&shutdown_handler);
  if (nb_shutdown_reg_status != 0) {
    unregister_pm_notifier(&suspend_handler);
    fan_set_auto();
    cdev_data_destroy(fan_data);
    thermal_cooling_device_unregister(cdev);
    printk(KERN_CRIT "Failed to register CPU notification handler.\n");
    return -1;
  }
  
  fan_set_auto();
  return 0;
}
 
static void __exit fan_exit(void) {
  struct cdev_data *data = GET_DEVICE_DATA(cdev);
  unregister_pm_notifier(&suspend_handler);
  unregister_reboot_notifier(&shutdown_handler);
  fan_set_auto();
  cdev_data_destroy(data);
  thermal_cooling_device_unregister(cdev);
}
 
module_init(fan_init);
module_exit(fan_exit);
