/**
 * @file   led.c
 * @author Derek Molloy
 * @date   8 November 2015
 * @brief  A kernel module for controlling a simple LED (or any signal) that
 * is connected to a GPIO. It is threaded in order that it can flash the LED.
 * The sysfs entry appears at /sys/erpi/gpio17
 * @see http://www.derekmolloy.ie/
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>       // Required for the GPIO functions
#include <linux/kobject.h>    // Using kobjects for the sysfs bindings
#include <linux/kthread.h>    // Using kthreads for the flashing functionality
#include <linux/delay.h>      // Using this header for the msleep() function

#define LED1 5
#define LED2 6
#define LED3 13
#define LED4 19

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Derek Molloy");
MODULE_DESCRIPTION("A simple Linux LED driver LKM for the RPi");
MODULE_VERSION("0.1");

static unsigned int gpioLED = 17;           ///< Default GPIO for the LED is 17
//static unsigned int gpioLED22 = 22;           ///< Default GPIO for the LED is 17
module_param(gpioLED, uint, S_IRUGO);       ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioLED, " GPIO LED number (default=17)");     ///< parameter description

static unsigned int blinkPeriod = 1000;     ///< The blink period in ms
module_param(blinkPeriod, uint, S_IRUGO);   ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(blinkPeriod, " LED blink period in ms (min=1, default=1000, max=10000)");

static char ledName[7] = "ledXXX";          ///< Null terminated default string -- just in case
static bool ledOn = 0;                      ///< Is the LED on or off? Used for flashing
enum modes { CORRE, IZQ, DER };              ///< The available LED modes -- static not useful here
//enum modes { OFF, ON, FLASH };              ///< The available LED modes -- static not useful here
static enum modes mode = DER;             ///< Default mode is flashing

/** @brief A callback function to display the LED mode
 *  @param kobj represents a kernel object device that appears in the sysfs filesystem
 *  @param attr the pointer to the kobj_attribute struct
 *  @param buf the buffer to which to write the number of presses
 *  @return return the number of characters of the mode string successfully displayed
 */
static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   switch(mode){
      case CORRE:   return sprintf(buf, "corre\n");       // Display the state -- simplistic approach
      case IZQ:    return sprintf(buf, "izq\n");
      case DER: return sprintf(buf, "der\n");
      default:    return sprintf(buf, "LKM Error\n"); // Cannot get here
   }
}

/** @brief A callback function to store the LED mode using the enum above */
static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   // the count-1 is important as otherwise the \n is used in the comparison
   if (strncmp(buf,"izq",count-1)==0) { mode = IZQ; }   // strncmp() compare with fixed number chars
   else if (strncmp(buf,"corre",count-1)==0) { mode = CORRE; }
   else if (strncmp(buf,"der",count-1)==0) { mode = DER; }
   return count;
}

/** @brief A callback function to display the LED period */
static ssize_t period_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf){
   return sprintf(buf, "%d\n", blinkPeriod);
}

/** @brief A callback function to store the LED period value */
static ssize_t period_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){
   unsigned int period;                     // Using a variable to validate the data sent
   sscanf(buf, "%du", &period);             // Read in the period as an unsigned int
   if ((period>1)&&(period<=10000)){        // Must be 2ms or greater, 10secs or less
      blinkPeriod = period;                 // Within range, assign to blinkPeriod variable
   }
   return period;
}

/** Use these helper macros to define the name and access levels of the kobj_attributes
 *  The kobj_attribute has an attribute attr (name and mode), show and store function pointers
 *  The period variable is associated with the blinkPeriod variable and it is to be exposed
 *  with mode 0666 using the period_show and period_store functions above
 */
static struct kobj_attribute period_attr = __ATTR(blinkPeriod, 0664, period_show, period_store);
static struct kobj_attribute mode_attr = __ATTR(mode, 0664, mode_show, mode_store);

/** The erpi_attrs[] is an array of attributes that is used to create the attribute group below.
 *  The attr property of the kobj_attribute is used to extract the attribute struct
 */
static struct attribute *erpi_attrs[] = {
   &period_attr.attr,                       // The period at which the LED flashes
   &mode_attr.attr,                         // Is the LED on or off?
   NULL,
};

/** The attribute group uses the attribute array and a name, which is exposed on sysfs -- in this
 *  case it is gpio17, which is automatically defined in the erpi_LED_init() function below
 *  using the custom kernel parameter that can be passed when the module is loaded.
 */
static struct attribute_group attr_group = {
   .name  = ledName,                        // The name is generated in erpi_LED_init()
   .attrs = erpi_attrs,                     // The attributes array defined just above
};

static struct kobject *erpi_kobj;           // The pointer to the kobject
static struct task_struct *task;            // The pointer to the thread task

/** @brief The LED Flasher main kthread loop
 *
 *  @param arg A void pointer used in order to pass data to the thread
 *  @return returns 0 if successful
 */
static int flash(void *arg){
	static unsigned char state = 0;           //Variable de control de la máquina de estados
   printk(KERN_INFO "ERPi LED: Thread has started running \n");
   while(!kthread_should_stop()){           // Returns true when kthread_stop() is called
      set_current_state(TASK_RUNNING);
      if (mode==DER){
      	//ledOn = !ledOn;
      	switch (state){
      		case 0:
      		gpio_set_value(LED1, true);gpio_set_value(LED2, false);gpio_set_value(LED3, false);gpio_set_value(LED4, false);
      		state = 1;
      		break;
      		case 1:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, true);gpio_set_value(LED3, false);gpio_set_value(LED4, false);
      		state = 2;
      		break;
      		case 2:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, false);gpio_set_value(LED3, true);gpio_set_value(LED4, false);
      		state = 3;
      		break;
      		case 3:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, false);gpio_set_value(LED3, false);gpio_set_value(LED4, true);
      		state = 0;
      		break;
      		default:
      		state = 0;
      		break;
      	}
      }      // Invert the LED state
      else if (mode==IZQ){
      	//ledOn = true;
          switch (state){
      		case 0:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, false);gpio_set_value(LED3, false);gpio_set_value(LED4, true);
      		state = 1;
      		break;
      		case 1:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, false);gpio_set_value(LED3, true);gpio_set_value(LED4, false);
      		state = 2;
      		break;
      		case 2:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, true);gpio_set_value(LED3, false);gpio_set_value(LED4, false);
      		state = 3;
      		break;
      		case 3:
      		gpio_set_value(LED1, true);gpio_set_value(LED2, false);gpio_set_value(LED3, false);gpio_set_value(LED4, false);
      		state = 0;
      		break;
      		default:
      		state = 0;
      		break;
      	}
      }
      else{
      	//ledOn = false
          switch (state){
      		case 0:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, false);gpio_set_value(LED3, false);gpio_set_value(LED4, true);
      		state = 1;
      		break;
      		case 1:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, false);gpio_set_value(LED3, true);gpio_set_value(LED4, false);
      		state = 2;
      		break;
      		case 2:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, true);gpio_set_value(LED3, false);gpio_set_value(LED4, false);
      		state = 3;
      		break;
      		case 3:
      		gpio_set_value(LED1, true);gpio_set_value(LED2, false);gpio_set_value(LED3, false);gpio_set_value(LED4, false);
      		state = 4;
      		break;
      		case 4:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, true);gpio_set_value(LED3, false);gpio_set_value(LED4, false);
      		state = 5;
      		break;
      		case 5:
      		gpio_set_value(LED1, false);gpio_set_value(LED2, false);gpio_set_value(LED3, true);gpio_set_value(LED4, false);
      		state = 0;
      		break;
      		default:
      		state = 0;
      		break;
      		}
      	}
      ///gpio_set_value(LED1, ledOn);       // Use the LED state to light/turn off the LED
	 //gpioLED = 17;
      //gpio_set_value(gpioLED, ledOn);       // Use the LED state to light/turn off the LED
      ///gpio_set_value(LED2, ledOn);       // Use the LED state to light/turn off the LED
      //gpioLED = 22;
      //gpio_set_value(gpioLED, ledOn);       // Use the LED state to light/turn off the LED
      set_current_state(TASK_INTERRUPTIBLE);
      msleep(blinkPeriod/2);                // millisecond sleep for half of the period
   }
   printk(KERN_INFO "ERPi LED: Thread has run to completion \n");
   return 0;
}

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */
static int __init erpi_LED_init(void){
   int result = 0;

   printk(KERN_INFO "ERPi LED: Initializing the ERPi LED LKM\n");
   sprintf(ledName, "led%d", gpioLED);      // Create the gpio name for /sys/rpi/led17

   erpi_kobj = kobject_create_and_add("erpi", kernel_kobj->parent); // kernel_kobj points to /sys/kernel
   if(!erpi_kobj){
      printk(KERN_ALERT "ERPi LED: failed to create kobject\n");
      return -ENOMEM;
   }
   // add the attributes to /sys/erpi/ -- for example, /sys/rpi/led17/ledOn
   result = sysfs_create_group(erpi_kobj, &attr_group);
   if(result) {
      printk(KERN_ALERT "ERPi LED: failed to create sysfs group\n");
      kobject_put(erpi_kobj);                // clean up -- remove the kobject sysfs entry
      return result;
   }
   ledOn = true;
   gpio_request(LED1, "sysfs");          // gpioLED is 17 by default, request it
   gpio_request(LED2, "sysfs");          // gpioLED is 17 by default, request it
   gpio_request(LED3, "sysfs");          // gpioLED is 17 by default, request it
   gpio_request(LED4, "sysfs");          // gpioLED is 17 by default, request it
   gpio_direction_output(LED1, ledOn);   // Set the gpio to be in output mode and turn on
   gpio_direction_output(LED2, ledOn);   // Set the gpio to be in output mode and turn on
   gpio_direction_output(LED3, ledOn);   // Set the gpio to be in output mode and turn on
   gpio_direction_output(LED4, ledOn);   // Set the gpio to be in output mode and turn on
   gpio_export(LED1, false);  // causes gpio17 to appear in /sys/class/gpio
   gpio_export(LED2, false);  // causes gpio17 to appear in /sys/class/gpio the second argument prevents the direction from being changed
   gpio_export(LED3, false);  // causes gpio17 to appear in /sys/class/gpio
   gpio_export(LED4, false);  // causes gpio17 to appear in /sys/class/gpio
   
   task = kthread_run(flash, NULL, "LED_flash_thread");  // Start the LED flashing thread
   if(IS_ERR(task)){                                     // Kthread name is LED_flash_thread
      printk(KERN_ALERT "ERPi LED: failed to create the task\n");
      return PTR_ERR(task);
   }
   return result;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required.
 */
static void __exit erpi_LED_exit(void){
   kthread_stop(task);                      // Stop the LED flashing thread
   kobject_put(erpi_kobj);                  // clean up -- remove the kobject sysfs entry
   gpio_set_value(gpioLED, 0);              // Turn the LED off, indicates device was unloaded
   gpio_unexport(gpioLED);                  // Unexport the Button GPIO
   gpio_unexport(LED1);                  // Unexport the Button GPIO
   gpio_unexport(LED2);                  // Unexport the Button GPIO
   gpio_unexport(LED3);                  // Unexport the Button GPIO
   gpio_unexport(LED4);                  // Unexport the Button GPIO
   gpio_free(gpioLED);                      // Free the LED GPIO
   gpio_free(LED1);                      // Free the LED GPIO
   gpio_free(LED2);                      // Free the LED GPIO
   gpio_free(LED3);                      // Free the LED GPIO
   gpio_free(LED4);                      // Free the LED GPIO
   printk(KERN_INFO "ERPi LED: Goodbye from the ERPi LED LKM!\n");
}

/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(erpi_LED_init);
module_exit(erpi_LED_exit);
