/* 
 * f71808e_gpio.c - GPIO interface for f71808E Super I/O chip 
 * 
 * Author: Denis Turischev <denis [at] compulab> 
 * 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License 2 as published 
 * by the Free Software Foundation. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; see the file COPYING. If not, write to 
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 */ 
  
#include <linux/init.h> 
#include <linux/kernel.h> 
#include <linux/module.h> 
#include <linux/io.h> 
#include <linux/errno.h> 
#include <linux/ioport.h> 
  
#include <linux/gpio.h> 
#include "../lgnas/mcu_emul/lgnas_if.h"
  
#define GPIO_NAME "f71808-gpio" 
  
#define CHIP_ID 0x0901 
#define VENDOR_ID 0x3219 
//#define VENDOR_ID 0x3119 
//#define VENDOR_ID 0x2019 
  
#define LDN 0x07 
#define CR_GPIO 0x06 
  
#define CHIP_ID_HIGH_BYTE 0x20 
#define CHIP_ID_LOW_BYTE 0x21 
#define VENDOR_ID_HIGH_BYTE 0x22 
#define VENDOR_ID_LOW_BYTE 0x23 
  
/*****************************************************************************
 * SIO Register Control
 ****************************************************************************/
#define SIO_BASE            0x2e
#define SIO_PCI_BASE        0xa00
#define SIO_F71808E_LD_UART	0x01		/* UART logical device */
#define SIO_F71808E_LD_HWM	0x04		/* Hardware monitor logical device */
#define SIO_F71808E_LD_KBC	0x05		/* KBC logical device */
#define SIO_F71808E_LD_GPIO	0x06		/* GPIO logical device */
#define SIO_F71808E_LD_WDT	0x07		/* WDT logical device */
#define SIO_F71808E_LD_PME_ACPI	0x0a	/* PME & ACPI logical device */

#define SIO_UNLOCK_KEY		  0x87		/* Key to enable Super-I/O */
#define SIO_LOCK_KEY        0xAA		/* Key to diasble Super-I/O */

#define SIO_REG_LDSEL		    0x07		/* Logical device select */
#define SIO_CONFIG_PORT_SEL	0x27
#define SIO_GLOBAL_MULTI1	  0x29
#define SIO_GLOBAL_MULTI2	  0x2a
#define SIO_GLOBAL_MULTI3	  0x2b
#define SIO_REG_ENABLE		  0x30		/* Logical device enable */
#define SIO_REG_ADDR		    0x60		/* Logical device address (2 bytes) */

#define SIO_GPIO_GPIRQ_SEL  0x70

#define SIO_GPIO_OUTEN_0  	0xF0
#define SIO_GPIO_DATA_0  	  0xF1
#define SIO_GPIO_STATUS_0  	0xF2
#define SIO_GPIO_DRVEN_0  	0xF3
#define SIO_GPIO_OUTEN_1  	0xE0
#define SIO_GPIO_DATA_1  	  0xE1
#define SIO_GPIO_STATUS_1  	0xE2
#define SIO_GPIO_DRVEN_1  	0xE3
#define SIO_GPIO_OUTEN_2  	0xD0
#define SIO_GPIO_DATA_2  	  0xD1
#define SIO_GPIO_STATUS_2  	0xD2
#define SIO_GPIO_DRVEN_2  	0xD3
#define SIO_GPIO_OUTEN_3  	0xC0
#define SIO_GPIO_DATA_3  	  0xC1
#define SIO_GPIO_STATUS_3  	0xC2
#define SIO_GPIO_DRVEN_3  	0xC3

#define ADDR_REG_OFFSET 5
#define DATA_REG_OFFSET 6
/*
 * NS2 LED Define
 */
// ACCESS LED
#define MASKS1_LED_HDD1F 	0x08		//13
#define MASKS1_LED_HDD2F 	0x04		//12
#define MASKS1_LED_HDD3F 	0x02		//11
#define MASKS1_LED_HDD4F 	0x01		//10
#define MASKS1_LED_HDD 		(MASKS1_LED_HDD4F|MASKS1_LED_HDD3F|MASKS1_LED_HDD2F|MASKS1_LED_HDD1F)
// LCD
#define MASKS2_LCD_RS  		0x10 		//24
#define MASKS2_LCD_EN  		0x20		//25
#define MASKS2_LCD_CON   	(MASKS2_LCD_RS|MASKS2_LCD_EN)
#define MASKS2_LCD_BL_EN  0x40		//26
// LCD DATA
#define MASKS3_LCD_D4  		0x08		//33
#define MASKS3_LCD_D5  		0x04 		//32
#define MASKS3_LCD_D6  		0x02 		//31
#define MASKS3_LCD_D7  		0x01 		//30
#define MASKS3_LCD_DA  		(MASKS3_LCD_D4|MASKS3_LCD_D5|MASKS3_LCD_D6|MASKS3_LCD_D7)
#define MASKS1_ALL			  (MASKS1_LED_HDD)
#define MASKS2_ALL			  (MASKS2_LCD_CON|MASKS2_LCD_BL_EN)
#define MASKS3_ALL			  (MASKS3_LCD_DA)

static inline void SHWMOutB( unsigned char data, unsigned char addr )
{
  outb(addr,SIO_PCI_BASE + ADDR_REG_OFFSET);
  outb(data,SIO_PCI_BASE + DATA_REG_OFFSET);
}
static inline u16 SHWMInW(int addr)
{
  u16 val;
  outb(addr++, SIO_PCI_BASE + ADDR_REG_OFFSET);
  val = inb(SIO_PCI_BASE + DATA_REG_OFFSET) << 8;
  outb(addr, SIO_PCI_BASE + ADDR_REG_OFFSET);
  val |= inb(SIO_PCI_BASE + DATA_REG_OFFSET);
  return val;
}

static u8 ports[2] = { 0x2e, 0x4e }; 
static u8 port; 
  
static DEFINE_SPINLOCK(sio_lock); 
  
  
static u8 read_reg(u8 addr, u8 port) 
{ 
  outb(addr, port); 
  return inb(port + 1); 
} 
  
static void write_reg(u8 data, u8 addr, u8 port) 
{ 
  outb(addr, port); 
  outb(data, port + 1); 
} 
  
static void enter_conf_mode(u8 port) 
{ 
  outb(0x87, port); 
  outb(0x87, port); 
} 
  
static void exit_conf_mode(u8 port) 
{ 
  return;
  //outb(0xaa, port); 
} 
  
static void enter_gpio_mode(u8 port) 
{ 
  write_reg(CR_GPIO, LDN, port); 
} 
  
static int f71808e_gpio_rename(unsigned gpio_num) 
{ 
  /* [0-7] -> [0-7] */ 
  if (gpio_num <= 7) 
    return gpio_num; 
  
  /* [8-12] -> [10-14]*/ 
  if (gpio_num <= 12) 
    return gpio_num + 2; 
  
  /* [13-20] -> [20-27] */ 
  if (gpio_num <= 20) 
    return gpio_num + 7; 
  
  /* [21-25] -> [30-34] */ 
  return gpio_num + 9; 
} 
  
static int f71808e_gpio_direction_in(struct gpio_chip *gc, unsigned _gpio_num) 
{ 
  unsigned gpio_num; 
  u8 curr_vals, reg, bit; 
  
  gpio_num = f71808e_gpio_rename(_gpio_num); 
  
  bit = gpio_num % 10; 
  reg = 0xf0 - (gpio_num / 10) * 0x10; 
  
  spin_lock(&sio_lock); 
  
  enter_conf_mode(port); 
  enter_gpio_mode(port); 
  
  curr_vals = read_reg(reg, port); 
  
  if (curr_vals & (1 << bit)) 
    write_reg(curr_vals & ~(1 << bit), reg, port); 
  
  exit_conf_mode(port); 
  
  spin_unlock(&sio_lock); 
  
  return 0; 
} 
  
static int f71808e_gpio_direction_out(struct gpio_chip *gc, 
  unsigned _gpio_num, int val) 
{ 
  unsigned gpio_num; 
  u8 curr_vals, reg, bit; 
  
  gpio_num = f71808e_gpio_rename(_gpio_num); 
  
  bit = gpio_num % 10; 
  reg = 0xf0 - (gpio_num / 10) * 0x10; 
  
  spin_lock(&sio_lock); 
  
  enter_conf_mode(port); 
  enter_gpio_mode(port); 
  
  curr_vals = read_reg(reg, port); 
  
  if (!(curr_vals & (1 << bit))) 
    write_reg(curr_vals | (1 << bit), reg, port); 
  
  exit_conf_mode(port); 
  
  spin_unlock(&sio_lock); 
  return 0; 
} 
  
static int f71808e_gpio_get(struct gpio_chip *gc, unsigned _gpio_num) 
{ 
  unsigned gpio_num, res; 
  u8 reg, bit; 
  
  gpio_num = f71808e_gpio_rename(_gpio_num); 
  
  bit = gpio_num % 10; 
  reg = 0xf2 - (gpio_num / 10) * 0x10; 
  
  spin_lock(&sio_lock); 
  
  enter_conf_mode(port); 
  enter_gpio_mode(port); 
  
  res = !!(read_reg(reg, port) & (1 << bit)); 
  
  exit_conf_mode(port); 
  
  spin_unlock(&sio_lock); 
  
  return res; 
} 
  
static void f71808e_gpio_set(struct gpio_chip *gc, 
  unsigned _gpio_num, int val) 
{ 
  unsigned gpio_num; 
  u8 curr_vals, reg, bit; 
  
  gpio_num = f71808e_gpio_rename(_gpio_num); 
  
  bit = gpio_num % 10; 
  reg = 0xf1 - (gpio_num / 10) * 0x10; 
  
  spin_lock(&sio_lock); 
  
  enter_conf_mode(port); 
  enter_gpio_mode(port); 
  
  curr_vals = read_reg(reg, port); 
  
  if (val) 
    write_reg(curr_vals | (1 << bit), reg, port); 
  else 
    write_reg(curr_vals & ~(1 << bit), reg, port); 
  
  exit_conf_mode(port); 
  
  spin_unlock(&sio_lock); 
} 
  
static struct gpio_chip f71808e_gpio_chip = { 
  .label = GPIO_NAME, 
  .owner = THIS_MODULE, 
  .get = f71808e_gpio_get, 
  .direction_input = f71808e_gpio_direction_in, 
  .set = f71808e_gpio_set, 
  .direction_output = f71808e_gpio_direction_out, 
}; 
  
static int __init f71808e_gpio_init(void) 
{ 
  int i, chip_id, vendor_id, err; 
  u8 io_reg, curr_vals; 
  
  printk("%s is called\n", __FUNCTION__);
  /* chip and port detection */ 
  for (i = 0; i < ARRAY_SIZE(ports); i++) { 
    spin_lock(&sio_lock); 
    enter_conf_mode(ports[i]); 
  
    chip_id = (read_reg(CHIP_ID_HIGH_BYTE, ports[i]) << 8) + 
    read_reg(CHIP_ID_LOW_BYTE, ports[i]); 
  
    vendor_id = (read_reg(VENDOR_ID_HIGH_BYTE, ports[i]) << 8) + 
    read_reg(VENDOR_ID_LOW_BYTE, ports[i]); 
  
    exit_conf_mode(ports[i]); 
    spin_unlock(&sio_lock); 
  
    //if ((chip_id == CHIP_ID) && (vendor_id == VENDOR_ID)) { 
    if (chip_id == CHIP_ID){
      port = ports[i]; 
      break; 
    } 
  } 
  
  if (!port) 
    return -ENODEV; 
  
  /* Enable all pins with GPIO capability */ 
  /* By default we enable all possible GPIOs on the chip */ 
  spin_lock(&sio_lock); 
  enter_conf_mode(port); 
  
  /* Enable GPIO30 and GPIO31 */ 
  io_reg = 0x27; 
  curr_vals = read_reg(io_reg, port); 
  write_reg(curr_vals & 0xfe, io_reg, port); 
  
  /*
   * Enable  GPIO
   */
  /* Enable GPIO[0-7], GPIO30, GPIO31, GPIO34 */ 
  /*
  io_reg = 0x29; 
  write_reg(0xff, io_reg, port); 
  */
  /* Enable GPIO[10-14], GPIO21, GPIO23 */ 
  io_reg = SIO_GLOBAL_MULTI2; 
  curr_vals = read_reg(io_reg, port); 
  write_reg(curr_vals | 0x03, io_reg, port); 
  /* Enable GPIO[20-27] */ 
  io_reg = SIO_GLOBAL_MULTI3; 
  curr_vals = read_reg(io_reg, port); 
  write_reg(curr_vals | 0x30, io_reg, port); 
  /* Sys Fan out */
  curr_vals = read_reg(io_reg, port); 
  write_reg(curr_vals & 0xfb, io_reg, port); 

  /*
   * Config port select
   */
  io_reg = SIO_CONFIG_PORT_SEL; 
  curr_vals = read_reg(io_reg, port); 
  write_reg(curr_vals & 0xfe, io_reg, port); 
  /*
	 * 	fan 1 :[1:0] auto(01)
	 * 	fan 2 :[3:2] mannual(11)
	 *	fan 3 :[5:4] mannual(11)
	 */
	SHWMOutB(0x3d, 0x96);
	/*
	 * SIO gpio drv push-pull
	 * open drain : 0 (default) Push Pull :1
	 */
  enter_gpio_mode(port); 
  write_reg(0xff, SIO_GPIO_DRVEN_1, port); 
  write_reg(0xff, SIO_GPIO_DRVEN_2, port); 
  write_reg(0xff, SIO_GPIO_DRVEN_3, port); 
	/*
	 * SIO gpio level
	 */
#ifdef NS2EVT_BOARD
  write_reg(0xf, SIO_GPIO_DATA_1, port); 
#else 
  write_reg(0x0, SIO_GPIO_DATA_1, port); 
#endif
  write_reg(0x0, SIO_GPIO_DATA_2, port); 
  write_reg(0x0, SIO_GPIO_DATA_3, port); 
  /*  
   * SIO gpio select (1:ouput, 0:input)
   */
  write_reg(MASKS1_ALL, SIO_GPIO_OUTEN_1, port); 
  write_reg(MASKS2_ALL, SIO_GPIO_OUTEN_2, port); 
  write_reg(MASKS3_ALL, SIO_GPIO_OUTEN_3, port); 

  exit_conf_mode(port); 
  spin_unlock(&sio_lock); 
  
  f71808e_gpio_chip.base = -1; 
  f71808e_gpio_chip.ngpio = 26; 
  
  err = gpiochip_add(&f71808e_gpio_chip); 
  if (err < 0) 
    goto gpiochip_add_err; 
  
  return 0; 
  
gpiochip_add_err: 
  return err; 
} 
  
static void __exit f71808e_gpio_exit(void) 
{ 
  int err;
  err = gpiochip_remove(&f71808e_gpio_chip); 
} 
module_init(f71808e_gpio_init); 
module_exit(f71808e_gpio_exit); 
  
MODULE_AUTHOR("Denis Turischev <denis [at] compulab>"); 
MODULE_DESCRIPTION("GPIO interface for F71808E Super I/O chip"); 
MODULE_LICENSE("GPL"); 

