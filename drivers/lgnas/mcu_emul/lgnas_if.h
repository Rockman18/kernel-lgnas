#ifndef _LGNAS_IF_H_
#define _LGNAS_IG_H_

#include "../hal/model_info.h"

/*
 * NS2EVT_BOARD = DVT + LED Inverted
 */
//#define NS2EVT_BOARD

#define LGNAS_BTN_CHECK_TIME 	  			5 	/* HZ=250, 5 x  4ms = 20ms  */
#define KEY_CHK_PERSIST_LIMIT_CNT   	125 /*       125 x 20ms = 2.5s  */
#define BEEP_CNT                      10   
#define KEY_IDLE_MAX_CNT							150 /* 30 sec */
#define LCD_DEF_STR1			      			"*LG N/W Storage "
#define LCD_DEF_STR2			      			" Loading kernel "
#define LCDMSG_TIMEOUT(dst, count)    ((count)? (count*5):dst)
#define MAX_MSG_SIZE    							MINFO_MAX_LCD_STR
#define LCD_BL_FULL     							NAS_INIT_LCD_BRIGHT_FULL
#define LCD_BL_HALF     							NAS_INIT_LCD_BRIGHT_HALF

/*
----------------------------------------------------------------------
NS2DVT Board N4B2 GPIO I/F definition
----------------------------------------------------------------------
	Function				Port			Etc.
HDD
	HDD1 	access	  ICH9 	GPIO09		Blue LED
	HDD2 	access	  ICH9 	GPIO26		Blue LED
	HDD3 	access	  ICH9 	GPIO25		Blue LED
	HDD4 	access	  ICH9 	GPIO15		Blue LED
	ODD 	access		ICH9  GPIO57    Blue LED

	HDD1 	fail	  	SIO 	GPIO10		Red LED
	HDD2 	fail	  	SIO 	GPIO11		Red LED
	HDD3 	fail	  	SIO 	GPIO12		Red LED
	HDD4 	fail	  	SIO 	GPIO13		Red LED

  NS2EVT Board N4B2 GPIO I/F definition
HDD
	HDD1 	access	  ICH9 	GPIO09		Blue LED
	HDD2 	access	  ICH9 	GPIO26		Blue LED
	HDD3 	access	  ICH9 	GPIO25		Blue LED
	HDD4 	access	  ICH9 	GPIO15		Blue LED  LED Board Intert
	ODD 	access		ICH9  GPIO57    Blue LED  LED Board Intert

	HDD1 	fail	  	SIO 	GPIO10		Red LED   LED Board Intert
	HDD2 	fail	  	SIO 	GPIO11		Red LED   LED Board Intert
	HDD3 	fail	  	SIO 	GPIO12		Red LED   LED Board Intert
	HDD4 	fail	  	SIO 	GPIO13		Red LED   LED Board Intert

LCD
	LCD 	RS			  SIO 	GPIO24
	LCD 	E			    SIO 	GPIO25
	LCD 	D[4]		  SIO 	GPIO30
	LCD 	D[5]		  SIO 	GPIO31
	LCD 	D[6]		  SIO 	GPIO32
	LCD 	D[7]		  SIO 	GPIO33
	LCD 	BL Enable	SIO 	GPIO26
	LCD 	BL      	SIO 	SYSFANOUT2	PWM output
FAN
	CPU FAN			    SIO 	CPUFANOUT  	PWM output
	SYS FAN			    SIO  	SYSFANOUT1	PWM output
Key
	Power				    ICH9 	PWRBTN#
	Mode				    ICH9 	GPIO12    SMI/SCI support
	Sel					    ICH9 	GPIO13		SMI/SCI support
	Eject				    ICH9 	GPIO14		SMI/SCI support
Jig
	Jig mode check	ICH9 	GPIO55		Bottom size Jig TPi

----------------------------------------------------------------------
2009.10.19 Pegatron history of CMOS Control
----------------------------------------------------------------------
- WOL : enable/disable
  CMOS address: 58h, 0 = Disabled, 1 = Enabled

- AC power loss : always power on/off 
  CMOS address: 59h, 0 = always off, 1 = always on

- RTC wake up : enable/disable
  CMOS address:        
  1.Second alarm: 01h
  2.Minute alarm: 03h
  3.Hour alarm : 05h
  4.Date alarm: 0Dh
    (There are 8 bits digits. 
     Bit7 must be “1”; 
     Bit6 = 1 means “Every day”; 
     Bit5 and Bit4 means “Date”;
     Bit3 ~ 0 means “Date”)

  ex) Date Alarm example: (if we want to set alarm date is “11” in every month)
     1. 0Dh Bit7 must be set “1”
            Bit6 = 0 (not every day)
            Bit5,4 = 01
            Bit3~0 = 0001
        
        And than 0Dh = 10010001 = 91h

----------------------------------------------------------------------
2009.12.15 Pegatron history
----------------------------------------------------------------------
Because of Intel’s setting limitation, we change the address as below.
- WOL : enable/disable
  CMOS address: 58h, xxxxxxx0 = Disabled, xxxxxxx1 = Enabled

- AC power loss : always power on/off 
  CMOS address: 59h, xxxxxxx0 = always off, xxxxxxx1 = always on, xxxxxx10 = last state

- RTC wake up : enable/disable
  CMOS address: 5Ah, xxxxxxx0 = Disabled, xxxxxxx1 = Enabled
  
  CMOS address:        
  1.Second alarm: 6Bh, key in value with hexadecimal
  2.Minute alarm: 6Ah, key in value with hexadecimal
  3.Hour alarm : 69h, key in value with hexadecimal
  4.Date alarm: 68h, key in value with hexadecimal
    Every day = 00000000
    1th day = 00000001, 
    2th day = 00000010,…etc.

----------------------------------------------------------------------
 AC Power Loss without reboot (2010.1.27)
----------------------------------------------------------------------
	WOL		POWER	QLAST	SIO	Result
  B1 B0
1) 0  1(on)	2		0			4		OK	
2) 0  1(on)	0		1			4		OK	
3) 0  0(off)2		0			4		OK	
4) 0  0(off)0		0			6		NG BIOS116B	
5) 1  1(off)0		1			4		OK	
  So case 4) convert to 5) and do not on wol of NIC 
*/

/*
 * N4B2 GPIO I/F definition
 */
/* Key */
#define MASK1_KEY_MODE		0x00001000 	//12
#define MASK1_KEY_SEL		  0x00002000	//13
#define MASK1_KEY_EJECT 	0x00004000 	//14
#define MASK2_JIG_MODE		0x00800000 	//55
/* Access Led */
#define MASK2_LED_ODD     0x02000000  //57
#define MASK1_LED_HDD1A 	0x00008000	//15
#define MASK1_LED_HDD2A 	0x02000000	//25
#define MASK1_LED_HDD3A 	0x04000000	//26
#define MASK1_LED_HDD4A 	0x00000200	//9
#define MASK1_LED_HDDA 		(MASK1_LED_HDD4A|MASK1_LED_HDD3A|MASK1_LED_HDD2A|MASK1_LED_HDD1A)
#define MASK1_ALL			    (MASK1_KEY_SEL  | MASK1_KEY_EJECT | MASK1_KEY_MODE | MASK1_LED_HDDA)
#define MASK2_ALL			    (MASK2_JIG_MODE | MASK2_LED_ODD  )
#define MASK1_IN_ALL		  (MASK1_KEY_SEL  | MASK1_KEY_EJECT | MASK1_KEY_MODE)
#define MASK2_IN_ALL		   MASK2_JIG_MODE
#define MASK1_OUT_ALL		   MASK1_LED_HDDA
#define MASK2_OUT_ALL      MASK2_LED_ODD
/* Fail Led */
#define MASKS1_LED_HDD1F 	0x08		//13
#define MASKS1_LED_HDD2F 	0x04		//12
#define MASKS1_LED_HDD3F 	0x02		//11
#define MASKS1_LED_HDD4F 	0x01		//10
#define MASKS1_LED_HDD 		(MASKS1_LED_HDD4F|MASKS1_LED_HDD3F|MASKS1_LED_HDD2F|MASKS1_LED_HDD1F)
/* LCD */
#define MASKS2_LCD_RS  		0x10 		//24
#define MASKS2_LCD_EN  		0x20		//25
#define MASKS2_LCD_CON   	(MASKS2_LCD_RS|MASKS2_LCD_EN)
#define MASKS2_LCD_BL_EN  0x40		//26
#define MASKS3_LCD_D4  		0x08		//33
#define MASKS3_LCD_D5  		0x04 		//32
#define MASKS3_LCD_D6  		0x02 		//31
#define MASKS3_LCD_D7  		0x01 		//30
#define MASKS3_LCD_DA  		(MASKS3_LCD_D4|MASKS3_LCD_D5|MASKS3_LCD_D6|MASKS3_LCD_D7)

#define MASKS1_ALL			  (MASKS1_LED_HDD)
#define MASKS2_ALL			  (MASKS2_LCD_CON|MASKS2_LCD_BL_EN)
#define MASKS3_ALL			  (MASKS3_LCD_DA)

/*
 * SIO Register Control
 */
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

#define SIO_KEEPLASTSTATE   0xf4

#define ADDR_REG_OFFSET 5
#define DATA_REG_OFFSET 6

/*
 * Super IO Ctrl Function
 */
#define SIOEnter			{ outb(SIO_UNLOCK_KEY,SIO_BASE); outb(SIO_UNLOCK_KEY,SIO_BASE); }
#define SIOExit				  outb(SIO_LOCK_KEY,SIO_BASE)
#define SIOSel(id)			SIOOutB(id,SIO_REG_LDSEL)
#define SIOInW(x)			{ ((int)SIOInB(x) << 8) | SIOInB(x+1) }
/*
 * CMOS Register Control
 */
#define CMOS_ADD_BASE       0x70
#define CMOS_ADD_QLAST      0x56
#define CMOS_ADD_WOL        0x58
#define CMOS_ADD_POWER      0x59
#define CMOS_ADD_RTC        0x5a
#define CMOS_ADD_RTC_DATE   0x68
#define CMOS_ADD_RTC_HOUR   0x69
#define CMOS_ADD_RTC_MIN    0x6a
#define CMOS_ADD_RTC_SEC    0x6b

#define ON  1
#define OFF 0

#define CMOS_ITEM_RTC    1
#define CMOS_ITEM_WOL    2
#define CMOS_ITEM_POWER  3
#define CMOS_ITEM_SIO    4
#define CMOS_ITEM_QLAST  5

/*
 * AC Power Loss without reboot
 */
#define PM_BASE 0x800
#define PM1_EN  0x02
#define PM1_EN_WAKE_ENABLE  0x4000
#define PMOUT(x)	outl( x, PM_BASE + PM1_EN )
#define PMIN  	  inl( PM_BASE + PM1_EN )

/*
 * ICH9R Register Control
 */
#define ICH9_GPIO_BASE		  0x480
#define ICH9_GPIO_USE_SEL1  0x0
#define ICH9_GPIO_USE_SEL2  0x30
#define ICH9_GPIO_IO_LVL1  	0xc
#define ICH9_GPIO_IO_LVL2  	0x38
#define ICH9_GPIO_IO_SEL1	  0x4
#define ICH9_GPIO_IO_SEL2	  0x34
#define ICH9_GEN_PMCON_3	  0xa4

#define GPIOUSIn1           inl( 	ICH9_GPIO_BASE + ICH9_GPIO_USE_SEL1)
#define GPIOUSIn2           inl(  	ICH9_GPIO_BASE + ICH9_GPIO_USE_SEL2)
#define GPIOUSOut1(x)       outl( x,ICH9_GPIO_BASE + ICH9_GPIO_USE_SEL1)
#define GPIOUSOut2(x)       outl( x,ICH9_GPIO_BASE + ICH9_GPIO_USE_SEL2)

#define GPIOSIn1            inl(  	ICH9_GPIO_BASE + ICH9_GPIO_IO_SEL1)
#define GPIOSIn2            inl(  	ICH9_GPIO_BASE + ICH9_GPIO_IO_SEL2)
#define GPIOSOut1(x)        outl( x,ICH9_GPIO_BASE + ICH9_GPIO_IO_SEL1)
#define GPIOSOut2(x)        outl( x,ICH9_GPIO_BASE + ICH9_GPIO_IO_SEL2)

#define GPIODIn1            inl(  	ICH9_GPIO_BASE + ICH9_GPIO_IO_LVL1)
#define GPIODIn2            inl(  	ICH9_GPIO_BASE + ICH9_GPIO_IO_LVL2)
#define GPIODOut1(x)        outl( x,ICH9_GPIO_BASE + ICH9_GPIO_IO_LVL1)
#define GPIODOut2(x)        outl( x,ICH9_GPIO_BASE + ICH9_GPIO_IO_LVL2)

/*****************************************************************************
 * LCD
 ****************************************************************************/
#define LCD_LOW_RW_RS_EN 	  SIOOutB(( SIOInB(SIO_GPIO_DATA_2) & ~MASKS2_LCD_CON),SIO_GPIO_DATA_2)
#define LCD_HIGH_RS        	SIOOutB(( SIOInB(SIO_GPIO_DATA_2) | MASKS2_LCD_RS),SIO_GPIO_DATA_2)
#define LCD_LOW_RS        	SIOOutB(( SIOInB(SIO_GPIO_DATA_2) & ~MASKS2_LCD_RS),SIO_GPIO_DATA_2)
#define LCD_HIGH_EN         SIOOutB(( SIOInB(SIO_GPIO_DATA_2) | MASKS2_LCD_EN),SIO_GPIO_DATA_2)
#define LCD_LOW_EN        	SIOOutB(( SIOInB(SIO_GPIO_DATA_2) & ~MASKS2_LCD_EN),SIO_GPIO_DATA_2)
#define LCD_LOW_DA 			    SIOOutB(( SIOInB(SIO_GPIO_DATA_3) & ~MASKS3_LCD_DA),SIO_GPIO_DATA_3)

#define LCD_W_U_NIBBLE(x)   SIOOutB(((SIOInB(SIO_GPIO_DATA_3)&0xf0)|((x & 0xf0) >> 4)),SIO_GPIO_DATA_3)
#define LCD_W_L_NIBBLE(x)   SIOOutB(((SIOInB(SIO_GPIO_DATA_3)&0xf0)| (x & 0x0f)      ),SIO_GPIO_DATA_3)

#define LCD_DELAY_50US      udelay(50)
#define LCD_DELAY_250NS     udelay(1)
#define LCD_DELAY_2MS       mdelay(2)
#define LCD_DELAY_100MS     mdelay(100)
#define LCD_DELAY_200MS     mdelay(200)
#define LCD_STROBE 			    LCD_HIGH_EN; \
							              LCD_DELAY_250NS; \
							              LCD_LOW_EN;

#define DBG_GPIO		      	printf("GPIODIn1 = 0x%08x\n",GPIODIn1); \
					              		printf("GPIODIn2 = 0x%08x\n",GPIODIn2);

/*****************************************************************************
 * Button and LCD Status definition
 ****************************************************************************/
#define KEY_ENC_NONE	0
#define KEY_ENC_POWER	1
#define KEY_ENC_MODE	2
#define KEY_ENC_ENTER	3
#define KEY_ENC_EJECT	4

#define MAIN_STATUS_WAIT            0x00
#define MAIN_STATUS_BOOT            0x10
#define MAIN_STATUS_MAIN            0x20    // Main Display
#define MAIN_STATUS_MODE_DISP       0x20    // Display
#define MAIN_STATUS_MODE_IP         0x30    // IP Set
#define MAIN_STATUS_MODE_ODD_BCK    0x40    // ODD Backup
#define MAIN_STATUS_MODE_ODD_BURN	  0x50    // ODD Burn
#define MAIN_STATUS_MODE_USB_BCK    0x60    // USB Backup

#define MAIN_STATUS_DISP_IP         0x21    // server name, ip
#define MAIN_STATUS_DISP_TIME       0x22    // date, time
#define MAIN_STATUS_DISP_CAPA       0x23    // capa usage
#define MAIN_STATUS_DISP_RAID       0x24    // raid state
#define MAIN_STATUS_DISP_ERR        0x25    // error code
#define MAIN_STATUS_DISP_MN_FW_VER  0x26    // Main fw version
#define MAIN_STATUS_DISP_FAN_RPM    0x27    // Fan RPM
#define MAIN_STATUS_IP_ENTRY        0x30    // IP Main
#define MAIN_STATUS_IP_DHCP_CF      0x31    // IP - DHCP Confirm
#define MAIN_STATUS_IP_IP_SETUP     0x32    // IP - IP Setup
#define MAIN_STATUS_IP_NETMASK_SETUP	0x33    // IP - NET MASK Setup
#define MAIN_STATUS_IP_GATEWAY_SETUP	0x34    // IP - GATE WAY Setup
#define MAIN_STATUS_IP_WAIT_SET     0x35    // IP - Wait until setting is end
#define MAIN_STATUS_PWD_WAIT_INIT   0x36    // IP - Wait until PWD-init ends
#define MAIN_STATUS_ODD_BCK_ENTRY        0x40    // ODD Backup Main
#define MAIN_STATUS_ODD_DATA_BCK_ENTRY   0x41    // ODD Backup DATA Menu
#define MAIN_STATUS_ODD_ISO_BCK_ENTRY    0x42    // ODD Backup ISO Menu
#define MAIN_STATUS_ODD_CANCEL_BCK_ENTRY 0x43    // ODD Backup Cancel Menu
#define MAIN_STATUS_ODD_BCK_PROG         0x44    // ODD Backup Progress
#define MAIN_STATUS_ODD_BCK_CANCEL       0x45    // ODD Backup Cancel
#define MAIN_STATUS_ODD_BN_ENTRY    0x50    // ODD Burn Main
#define MAIN_STATUS_ODD_BN_FILE_SEL 0x51    // ODD Burn File Select
#define MAIN_STATUS_ODD_BN_RW_CF    0x52    // ODD Burn RW Disc Confirm
#define MAIN_STATUS_ODD_BN_PROG     0x53    // ODD Burn Progress
#define MAIN_STATUS_ODD_BN_CANCEL   0x54    // ODD Burn Cancel
#define MAIN_STATUS_USB_BCK_ENTRY   0x60    // USB Backup Main
#define MAIN_STATUS_USB_BCK_PROG    0x61    // USB Backup Progress
#define MAIN_STATUS_USB_BCK_CANCEL  0x62    // USB Backup Cancel

#define MAIN_STATUS_SHUTDOWN        0x70
#define MAIN_STATUS_BOOT_ODD        0x80
#define MAIN_STATUS_BOOT_ODD_SETUP  0x81
#define MAIN_STATUS_BOOT_ODD_DRAID  0x83
#define MAIN_STATUS_BOOT_ODD_URAID  0x84
#define MAIN_STATUS_BOOT_AGING      0x90

#define MNT_STATUS_UNLOADED     0x00
#define MNT_STATUS_OPENED       0x01
#define MNT_STATUS_LOADED       0x02
#define MNT_STATUS_LOADING      0x03
#define MNT_STATUS_MOUNTED      0x04

#define BACKUP_STATUS_NONE      0x00
#define BACKUP_STATUS_STARTED   0x01
#define BACKUP_STATUS_ENDED     0x02
#define BACKUP_STATUS_CANCELED  0x03
#define BACKUP_STATUS_FAILED    0x04

#define BURN_STATUS_NONE        0x00
#define BURN_STATUS_STARTED     0x01
#define BURN_STATUS_ENDED       0x02
#define BURN_STATUS_CANCELED    0x03
#define BURN_STATUS_FAILED      0x04

#define RAID_STATUS_VOL_ALL     0x10
#define RAID_STATUS_VOL_SYS     0x20
#define RAID_STATUS_VOL_SWP     0x30
#define RAID_STATUS_VOL_USR     0x40

#define RAID_STATUS_SYNC        0x10
#define RAID_STATUS_MIGRATE     0x11
#define RAID_STATUS_EXPAND      0x12
#define RAID_STATUS_FORMAT      0x13
#define RAID_STATUS_ERROR       0x20
#define RAID_STATUS_DEGRADE     0x21
#define RAID_STATUS_NOVOLUME    0x22

#define MAIN_MODE_NUM		4
#define DISP_MENU_NUM		7

#define DATA_FRM_NUM    8

#define main_disp_toggle_f    	bit_str_0.b0
#define disp_request_f        	bit_str_0.b1
#define disp_shift_occurred_f	bit_str_0.b2
#define value_edit_f        	bit_str_0.b3
#define valid_ip_f          	bit_str_0.b4
#define long_data_f         	bit_str_0.b5
#define valid_file_name_f   	bit_str_0.b6
#define test_f              	bit_str_0.b7

#define make_value_f        	bit_str_1.b0
#define valid_err_msg_f   		bit_str_1.b1
#define persist_err_msg_f   	bit_str_1.b2
#define msg_check_flag_f    	bit_str_1.b3
#define beep_task_flag_f    	bit_str_1.b4
#define boot_timeover_f    		bit_str_1.b5
#define ip_set_cancel_f     	bit_str_1.b6
#define odd_cancel_f        	bit_str_1.b7

#define burn_list_down_f    	bit_str_2.b0
#define burn_list_toggle_f  	bit_str_2.b1
#define burn_list_chg_f  		bit_str_2.b2
#define power_fail_f    		bit_str_2.b3
#define power_on_state_f    	bit_str_2.b4
#define bg_err_msg_f    		bit_str_2.b5
#define odd_rip_f   			bit_str_2.b6
#define overwrite_warn_f    	bit_str_2.b7

#define forced_power_down_f    	bit_str_3.b0
#define admin_pwd_init_f    	bit_str_3.b1
#define usb_cancel_f        	bit_str_3.b2
#endif
