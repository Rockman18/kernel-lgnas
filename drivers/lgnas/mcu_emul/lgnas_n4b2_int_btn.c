
/*
	NS2 GPIO Interrupt, taking advantage of SCI interrupt. 
	for question, mail to mountaink@lge.com
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>

#include "lgnas.h"
#include "lgnas_if.h"
#include "lgnas_sym.h"
#include "lgnas_event.h"

//pci unique id
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x2916

//button gpio : just giving information
#define GPIO_OPEN         14
#define GPIO_MODE         13
#define GPIO_SELECT       12

#define SMI_ENABLE        0x01
#define SCI_ENABLE        0x10

//pci config registers
#define ICH9_PCI_CONF_VID			0x0
#define ICH9_PCI_CONF_DID			0x2
#define ICH9_PCI_CONF_PM_BASE		0x40
#define ICH9_PCI_CONF_GPIO_BASE	0x48
#define ICH9_PCI_CONF_GPI_ROUT  	0xB8  //pci configuration : 0xB8

//pmbase registers under pmbase address space
#define ICH9_PMBASE_PM1_CONTROL	0x4 
#define ICH9_PMBASE_PM1_TIMER		0x8  //0x8~0xB
#define ICH9_PMBASE_GPE0_STS 		0x20  //0x20~0x27
#define ICH9_PMBASE_GPE0_EN  		0x28  //0x28~0x2F
#define ICH9_PMBASE_GPGPE_CNTL 	0x42//0x32

//gpio registers under gpiobase address space
#define ICH9_GPIOBASE_USE_SEL	0x0
#define ICH9_GPIOBASE_IO_SEL	0x4
#define ICH9_GPIOBASE_LV	0xC
#define ICH9_GPIOBASE_INV	0x2C

//
#define GPIO_BTN_SET_OUTPUT (0xFFFF8FFF)
#define GPIO_BTN_SET_INPUT  (~GPIO_BTN_SET_OUTPUT)

#define GPIO_BTN_SIG_INV_LOW  (0x00007000)
#define GPIO_BTN_SIG_INV_HIGH (~GPIO_BTN_SIG_INV_LOW)

//value for GPI ROUT register: activating SCI even in S0 power status.
#define GPIO_OPEN_ROUT    (0x20000000)
#define GPIO_MODE_ROUT    (0x08000000)
#define GPIO_SELECT_ROUT  (0x02000000)
#define GPIO_BTN_ROUT_EN (GPIO_OPEN_ROUT | GPIO_MODE_ROUT | GPIO_SELECT_ROUT)
#define GPIO_BTN_ROUT_DIS (~GPIO_BTN_ROUT_EN)

//value for GPI GPE0 register: activating SCI interrupt invocation by gpio event.
#define GPIO_OPEN_GPE0_EN   (0x40000000)
#define GPIO_MODE_GPE0_EN   (0x20000000)
#define GPIO_SELECT_GPE0_EN (0x10000000)
#define GPIO_BTN_GPE0_EN    (GPIO_OPEN_GPE0_EN | GPIO_MODE_GPE0_EN | GPIO_SELECT_GPE0_EN)
#define GPIO_BTN_GPE0_DIS (~GPIO_BTN_GPE0_EN)
#define GPIO_BTN_GPE0_STS_MASK (GPIO_BTN_GPE0_EN) //note: to clear gpeo_sts register, write 1 to corresponding bit

static u32 pmbase = (u32)NULL;
static u32 gpiobase = (u32)NULL;
struct pci_dev* sb = NULL;


/*****************************************************************************
 * 20ms timer enabling SCI GPIO interrupt
 ****************************************************************************/
static void  post_func(struct work_struct *unused);
DECLARE_DELAYED_WORK(sci_int_post_handler,post_func);

static inline int pci_write_byte(struct pci_dev* dev,int where,u8 val){
	return pci_write_config_byte(dev,where,val);
}
static inline int pci_write_word(struct pci_dev* dev,int where,u16 val){
	return pci_write_config_word(dev,where,val);
}
static inline int pci_write_long(struct pci_dev* dev,int where,u32 val){
	return pci_write_config_dword(dev,where,val);
}
static inline u8 pci_read_byte(struct pci_dev* dev,int where){
	u8 ret;
	pci_read_config_byte(dev,where,&ret);
	return ret;
}
static inline u16 pci_read_word(struct pci_dev* dev,int where){
	u16 ret;
	pci_read_config_word(dev,where,&ret);
	return ret;
}
static inline u32 pci_read_long(struct pci_dev* dev,int where){
	u32 ret;
	pci_read_config_dword(dev,where,&ret);
	return ret;
}

void sci_gpio_enable(void){

	u32 gpe0_en;
	u32 gpio_rout;

	//enable GPIO 14/13/12 GPE0_EN
	gpe0_en = inl(pmbase+ICH9_PMBASE_GPE0_EN);
	outl((gpe0_en|GPIO_BTN_GPE0_EN),pmbase+ICH9_PMBASE_GPE0_EN); 

	//enable GPIO 14/13/12 ROUT 
	gpio_rout= pci_read_long(sb,ICH9_PCI_CONF_GPI_ROUT);
	pci_write_long(sb,ICH9_PCI_CONF_GPI_ROUT,(gpio_rout | GPIO_BTN_ROUT_EN) );
}
void sci_gpio_disable(void){

	u32 gpe0_en;
	u32 gpio_rout;
	//disable GPIO 14/13/12 ROUT 
	gpio_rout= pci_read_long(sb,ICH9_PCI_CONF_GPI_ROUT);
	pci_write_long(sb,ICH9_PCI_CONF_GPI_ROUT,(gpio_rout & GPIO_BTN_ROUT_DIS) );

	//disable GPIO 14/13/12 GPE0_EN
	gpe0_en = inl(pmbase+ICH9_PMBASE_GPE0_EN);
	outl((gpe0_en&GPIO_BTN_GPE0_DIS),pmbase+ICH9_PMBASE_GPE0_EN); 

}

static void post_func(struct work_struct *unused){

	//dprintk("%s is called %lu\n",__FUNCTION__, jiffies);
	sci_gpio_enable();
}

static void GetValue(unsigned char * pIn, unsigned char * pOut)
{
  unsigned char bTmp1;

  for(bTmp1=0; bTmp1 < 4; bTmp1++)
  {
    pOut[bTmp1] = (pIn[bTmp1*4 + 0] - '0')*100 + (pIn[bTmp1*4 + 1] - '0')*10
      + (pIn[bTmp1*4 + 2] - '0');
  }
}

void key_processing(struct nashal_data *data)
{
  if(!key_proc || !key_flag)
    return;

  if(!data->lock.buzzer)
    kd_mksound(0xfa3,HZ * 30/1000);

  if(!key_checked){
    com_make_rd_data(0x01);
    key_checked = 1;
  }
  btn_cnt++;

  if(key_cur == KEY_ENC_MODE)
  {
    if(!(main_status & 0x0f)
      && (main_status < MAIN_STATUS_SHUTDOWN)
      && (main_status > MAIN_STATUS_BOOT))
    {
      if(main_status == MAIN_STATUS_MAIN)
      {
        burn_list_toggle_f = 0;
        menu_idx = 0;
        main_status = menu_seq[menu_idx];
        menu_top = menu_seq[menu_idx];
      }
      else
      {
        if(++menu_idx >= MAIN_MODE_NUM)
        {
          menu_idx = 0;
          wc_msg_shift = wc_msg_disp_cnt = 0;
          main_status = MAIN_STATUS_MAIN;
        }
        else
        {
          main_status = menu_seq[menu_idx];
          menu_top = menu_seq[menu_idx];
        }
      }
      if(admin_pwd_init_f){
        wc_msg_shift = wc_msg_disp_cnt = 0;
    	  main_status = MAIN_STATUS_PWD_WAIT_INIT;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
        disp_request_f = 1;
        admin_pwd_init_f = 0;
      }else{
  	    disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT;
	      disp_request_f = 1;
      }
    }
	  else if((main_status & 0xf0) == MAIN_STATUS_MAIN)
	  {
	    wc_msg_shift = wc_msg_disp_cnt = 0;
	    main_status = MAIN_STATUS_MAIN;
	    disp_request_f = 1;
	  }
	  else if((main_status & 0xf0) == MAIN_STATUS_MODE_IP)
	  {
	    if(main_status == MAIN_STATUS_IP_DHCP_CF)
      {
     	    main_status++;
       	  disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
        disp_request_f = 1;
        value_pos = 0; value_edit_f = 1; make_value_f = 1;
      }
	    else if((main_status == MAIN_STATUS_IP_IP_SETUP)
        || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
        || (main_status == MAIN_STATUS_IP_GATEWAY_SETUP))
      {
        if((value_pos == 0) || (value_pos == 4) || (value_pos == 8) || (value_pos == 12))
        {
          if(value_setup[value_pos] < '2')
            value_setup[value_pos]++;
          else
            value_setup[value_pos] = '0';
        }
        else if((value_pos == 1) || (value_pos == 5) || (value_pos == 9) || (value_pos == 13))
        {
          if(value_setup[value_pos-1] == '2'){
            if(value_setup[value_pos] < '5')
              value_setup[value_pos]++;
            else
              value_setup[value_pos] = '0';
          }else{
            if(value_setup[value_pos] < '9')
              value_setup[value_pos]++;
            else
              value_setup[value_pos] = '0';
          }
        }
        else if((value_pos == 2) || (value_pos == 6) || (value_pos == 10) || (value_pos == 14))
        {
          if((value_setup[value_pos-2] == '2') && (value_setup[value_pos-1] == '5')){
            if(value_setup[value_pos] < '5')
              value_setup[value_pos]++;
            else
              value_setup[value_pos] = '0';
          }else{
            if(value_setup[value_pos] < '9')
              value_setup[value_pos]++;
            else
              value_setup[value_pos] = '0';
          }
        }
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
        disp_request_f = 1;
      }
	  }
    else if((main_status & 0xf0) == MAIN_STATUS_ODD_BN_ENTRY)
    {
      if(main_status == MAIN_STATUS_ODD_BN_PROG)
      {
        if((prog_type_ext != 0x01) && (prog_type_ext != 0x02)
          && (prog_type_ext != 0x04) && !(prog_type_ext&0x80) && !odd_cancel_f)
          main_status = MAIN_STATUS_ODD_BN_CANCEL;
      }
      else if(main_status == MAIN_STATUS_ODD_BN_CANCEL)
      {
        main_status = MAIN_STATUS_ODD_BN_PROG;
      }
      else if(main_status == MAIN_STATUS_ODD_BN_RW_CF)
      {
        main_status = MAIN_STATUS_MAIN;
      }
      disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
      disp_request_f = 1;
    }else if((main_status & 0xf0) == MAIN_STATUS_ODD_BCK_ENTRY){
      if(main_status == MAIN_STATUS_ODD_DATA_BCK_ENTRY){
        main_status = MAIN_STATUS_ODD_ISO_BCK_ENTRY;
      }else if(main_status == MAIN_STATUS_ODD_ISO_BCK_ENTRY){
        main_status = MAIN_STATUS_ODD_CANCEL_BCK_ENTRY;
      }else if(main_status == MAIN_STATUS_ODD_CANCEL_BCK_ENTRY){
        main_status = MAIN_STATUS_ODD_DATA_BCK_ENTRY;
      }else if(main_status == MAIN_STATUS_ODD_BCK_PROG){
        if(!odd_cancel_f)
          main_status = MAIN_STATUS_ODD_BCK_CANCEL;
      }else if(main_status == MAIN_STATUS_ODD_BCK_CANCEL){
        main_status = MAIN_STATUS_ODD_BCK_PROG;
      }
      disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
      disp_request_f = 1;
    }
    else if((main_status & 0xf0) == MAIN_STATUS_USB_BCK_ENTRY)
    {
      if(main_status == MAIN_STATUS_USB_BCK_PROG)
      {
        if(!usb_cancel_f)
          main_status = MAIN_STATUS_USB_BCK_CANCEL;
      }
      else if(main_status == MAIN_STATUS_USB_BCK_CANCEL)
      {
        main_status = MAIN_STATUS_USB_BCK_PROG;
      }
      disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
      disp_request_f = 1;
    }
    else if((main_status & 0xf0) == MAIN_STATUS_BOOT_ODD)
    {
      if(main_status == MAIN_STATUS_BOOT_ODD_SETUP)
      {
        main_status = MAIN_STATUS_BOOT_ODD_URAID;
      }
      disp_request_f = 1;
    }
  }
  else if(key_cur == KEY_ENC_ENTER)
  {
	  if((main_status & 0xf0) == MAIN_STATUS_MODE_DISP)
	  {
	    main_status++;
	    if((main_status&0x0f) > (DISP_MENU_NUM)) main_status = MAIN_STATUS_DISP_IP;
  	  disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT;
	    disp_request_f = 1;
	  }
	  else if((main_status & 0xf0) == MAIN_STATUS_MODE_IP)
	  {
	    if(main_status == MAIN_STATUS_MODE_IP)
      {
     	    main_status++;
  	      disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT;
        disp_request_f = 1;
        ;	//sort_menu_seq();
      }
	    else if(main_status == MAIN_STATUS_IP_DHCP_CF)
      {
        wc_msg_shift = wc_msg_disp_cnt = 0;
  	    main_status = MAIN_STATUS_IP_WAIT_SET;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT_DHCP;
        disp_request_f = 1;
      }
	    else if((main_status == MAIN_STATUS_IP_IP_SETUP)
        || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
        || (main_status == MAIN_STATUS_IP_GATEWAY_SETUP))
      {
        if(value_pos < 14)
        {
          if((value_pos == 2) || (value_pos == 6) || (value_pos == 10))
          {
            value_pos += 2;
          }
          else
          {
            value_pos++;
            if((value_pos == 1) || (value_pos == 5) || (value_pos == 9))
            {
              if((value_setup[value_pos - 1] == '2') && (value_setup[value_pos] > '5'))
              {
                value_setup[value_pos] = '5';
              }
            }
            else if((value_pos == 2) || (value_pos == 6) || (value_pos == 10))
            {
              if((value_setup[value_pos - 2] == '2') && (value_setup[value_pos - 1] == '5')
                && (value_setup[value_pos] > '5'))
              {
                value_setup[value_pos] = '5';
              }
            }
          }
          if(value_pos == 14) value_edit_f = 0;
          disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
          disp_request_f = 1;
        }
        else
        {
          value_pos = 0; value_edit_f = 1;
          if(main_status == MAIN_STATUS_IP_IP_SETUP){
            make_value_f = 1;
            GetValue(&value_setup[0], pip_s);
            *(pip_s + 4) = 0x00;
          }else if(main_status == MAIN_STATUS_IP_NETMASK_SETUP){
            make_value_f = 1;
            GetValue(&value_setup[0], pnetmask_s);
          }else if(main_status == MAIN_STATUS_IP_GATEWAY_SETUP){
            GetValue(&value_setup[0], pgateway_s);
          }
    	    main_status++;
    	    if(main_status > MAIN_STATUS_IP_GATEWAY_SETUP){
            disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
          }
	        disp_request_f = 1;
        }
      }
	  }
    else if((main_status & 0xf0) == MAIN_STATUS_ODD_BCK_ENTRY)
    {
      if(main_status == MAIN_STATUS_ODD_BCK_ENTRY){
        main_status = MAIN_STATUS_ODD_DATA_BCK_ENTRY;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
      }else if(  (main_status == MAIN_STATUS_ODD_BCK_ENTRY)
               ||(main_status == MAIN_STATUS_ODD_DATA_BCK_ENTRY)
               ||(main_status == MAIN_STATUS_ODD_ISO_BCK_ENTRY)){
        prog_type = 0;
        prog_rate = 0;
        prog_type_ext = 0;
        odd_cancel_f = 0;
 	      main_status=MAIN_STATUS_ODD_BCK_PROG;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT2D;
      }else if(main_status == MAIN_STATUS_ODD_CANCEL_BCK_ENTRY){
        main_status = MAIN_STATUS_MAIN;
      }else if(main_status == MAIN_STATUS_ODD_BCK_CANCEL){
        odd_cancel_f = 1;
        main_status = MAIN_STATUS_ODD_BCK_PROG;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_CANCEL;
      }else{
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
      }
      disp_request_f = 1;
    }
    else if((main_status & 0xf0) == MAIN_STATUS_ODD_BN_ENTRY)
    {
      if(main_status == MAIN_STATUS_ODD_BN_ENTRY)
      {
 	      main_status++;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
        disp_request_f = 1;
        ;	//sort_menu_seq();
      }
      else if(((main_status == MAIN_STATUS_ODD_BN_FILE_SEL) && !overwrite_warn_f)
        || (main_status == MAIN_STATUS_ODD_BN_RW_CF))
      {
        prog_type = 1;
        prog_rate = 0;
        prog_type_ext = 0;
        odd_cancel_f = 0;
 	      main_status = MAIN_STATUS_ODD_BN_PROG;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT2D;
        disp_request_f = 1;
      }
      else if(main_status == MAIN_STATUS_ODD_BN_FILE_SEL)
      {
 	      main_status++;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
        disp_request_f = 1;
      }
      else if(main_status == MAIN_STATUS_ODD_BN_CANCEL)
      {
        odd_cancel_f = 1;
        main_status = MAIN_STATUS_ODD_BN_PROG;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_CANCEL;
        disp_request_f = 1;
      }
    }
    else if((main_status & 0xf0) == MAIN_STATUS_USB_BCK_ENTRY)
    {
      if(main_status == MAIN_STATUS_USB_BCK_CANCEL)
      {
        usb_cancel_f = 1;
        main_status = MAIN_STATUS_USB_BCK_PROG;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_CANCEL;
      }
      else
      {
        prog_type = 2;
        prog_rate = 0;
        prog_type_ext = 0;
        usb_cancel_f = 0;
 	      main_status++;
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT2D;
        disp_request_f = 1;
        ;	//sort_menu_seq();
      }
    }
    else if((main_status & 0xf0) == MAIN_STATUS_BOOT_ODD)
    {
      if(main_status == MAIN_STATUS_BOOT_ODD_SETUP)
      {
        main_status = MAIN_STATUS_BOOT_ODD_DRAID;
      }
      disp_request_f = 1;
    }
    else if((main_status & 0xf0) == MAIN_STATUS_BOOT_AGING)
    {
      if(aging_status > 0) aging_status = 0;
      else aging_status = 1;

      disp_request_f = 1;
    }
  }
  else if(key_cur == KEY_ENC_EJECT)
  {
	  if((main_status & 0xf0) == MAIN_STATUS_MODE_IP)
    {
	    if((main_status == MAIN_STATUS_IP_IP_SETUP)
        || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
        || (main_status == MAIN_STATUS_IP_GATEWAY_SETUP))
      {
        if(value_pos)
        {
          if((value_pos == 4) || (value_pos == 8) || (value_pos == 12))
          {
            value_pos -= 2;
          }
          else
          {
            value_pos--;
          }
          if(value_pos == 13) value_edit_f = 1;
          disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
          disp_request_f = 1;
        }
      }
    }
  }
  key_proc = 0;
}

#define NS2_VKEY_ODD_BACKUP_DATA	 	0
#define NS2_VKEY_ODD_BACKUP_ISO			1
#define NS2_VKEY_ODD_BACKUP_CANCEL	2
#define NS2_VKEY_ODD_BURN 					3
#define NS2_VKEY_ODD_BURN_CANCEL 		4
#define NS2_VKEY_USB_BACKUP 				5
#define NS2_VKEY_USB_BACKUP_CANCEL 	6
#define NS2_VKEY_DHCP 							7
#define NS2_VKEY_STATIC							8
#define NS2_VKEY_ODD_EJECT  				9
#define NS2_VKEY_ODD_IMAGE_GET			10
#define NS2_VKEY_IP 			  				11
#define NS2_VKEY_TIME 							12
#define NS2_VKEY_CAPA 							13
#define NS2_VKEY_SYNC 							14
#define NS2_VKEY_SVCCODE						15
#define NS2_VKEY_PWD_INIT	  				16
#define NS2_VKEY_RSV 			  				17
#define WER 			0
extern void button_event(struct work_struct *work);
extern struct nashal_data *ns2data;
extern u8 int_rsn;
u8 process_item = 0;

void lgnas_event(u8 rsn, u8 key)
{
	dprintk("%s(INT_RSN:0x%02x KEY_STS:0x%02x)\n", __FUNCTION__, rsn, key);

	if(!rsn){ 
    /* power */
		if(key & 0x01)
      button_event(&ns2data->bdata[NS2_VKEY_RSV].work);
#if 0
    /* display */
		if(key & 0x02)
      button_event(&ns2data->bdata[NS2_VKEY_RSV].work);
#endif
    /* odd backup DATA */
		if(key & 0x02){
      button_event(&ns2data->bdata[NS2_VKEY_ODD_BACKUP_DATA].work);
      process_item = NS2_VKEY_ODD_BACKUP_DATA;
    }
    /* odd backup ISO */
		if(key & 0x04){
      button_event(&ns2data->bdata[NS2_VKEY_ODD_BACKUP_ISO].work);
      process_item = NS2_VKEY_ODD_BACKUP_ISO;
    }
    /* odd eject */
		if(key & 0x08)
      button_event(&ns2data->bdata[NS2_VKEY_ODD_EJECT].work);
    /* odd burn */
		if(key & 0x10){
      button_event(&ns2data->bdata[NS2_VKEY_ODD_BURN].work);
      process_item = NS2_VKEY_ODD_BURN;
    }
    /* usb backup */
		if(key & 0x20){
      button_event(&ns2data->bdata[NS2_VKEY_USB_BACKUP].work);
      process_item = NS2_VKEY_USB_BACKUP;
    }
    /* cancel */
		if(key & 0x40){
      switch(process_item){
        case NS2_VKEY_ODD_BACKUP_ISO:
        case NS2_VKEY_ODD_BACKUP_DATA:
          button_event(&ns2data->bdata[NS2_VKEY_ODD_BACKUP_CANCEL].work);
				  break;
        case NS2_VKEY_ODD_BURN:
          button_event(&ns2data->bdata[NS2_VKEY_ODD_BURN_CANCEL].work);
				  break;
        case NS2_VKEY_USB_BACKUP:
          button_event(&ns2data->bdata[NS2_VKEY_USB_BACKUP_CANCEL].work);
				  break;
        default:
          button_event(&ns2data->bdata[NS2_VKEY_RSV].work);
				  break;
      }
    }
	}else if(rsn == GET_IP_ADDR){
		if(key){
      /* dhcp */
      button_event(&ns2data->bdata[NS2_VKEY_DHCP].work);
		}else{
      /* static */
      button_event(&ns2data->bdata[NS2_VKEY_STATIC].work);
		}
	}else if(rsn == GET_FILE_NAME){
    if(!ns2data->mdata.file_init){
      ns2data->mdata.file_init = 1;
      /* for platformd initialize */
      ns2data->mdata.file_num = 1;
      ns2data->mdata.file_total = 0;
    }
      /* filename */
      button_event(&ns2data->bdata[NS2_VKEY_ODD_IMAGE_GET].work);
	}else{	
    switch(int_rsn){
      /* ipaddr */
      case 0x83:
        button_event(&ns2data->bdata[NS2_VKEY_IP].work);
        button_event(&ns2data->bdata[NS2_VKEY_TIME].work);
        break;
      /* time */
      case 0x84:
        //button_event(&ns2data->bdata[NS2_VKEY_TIME].work);
        button_event(&ns2data->bdata[NS2_VKEY_CAPA].work);
        break;
      /* capa */
      case 0x85:
        //button_event(&ns2data->bdata[NS2_VKEY_CAPA].work);
        button_event(&ns2data->bdata[NS2_VKEY_SYNC].work);
        break;
      /* sync */
      case 0x88:
        //button_event(&ns2data->bdata[NS2_VKEY_SYNC].work);
        button_event(&ns2data->bdata[NS2_VKEY_SVCCODE].work);
        break;
      /* svc-code */
      case 0x87:
        //button_event(&ns2data->bdata[NS2_VKEY_SVCCODE].work);
        button_event(&ns2data->bdata[NS2_VKEY_IP].work);
        break;
      /* adm-pwd-init */
      case SET_PWD_INIT:
        button_event(&ns2data->bdata[NS2_VKEY_PWD_INIT].work);
        break;
      default:
        button_event(&ns2data->bdata[NS2_VKEY_RSV].work);
        break;
    }
	}
}

int _sci_btn_handler(void)
{
	key_chk_valid();

  /* for line assemble diag */
	if(ns2data->lock.micom)
    return 0;

  key_processing(ns2data);


	if(key_event && key_checked){
		key_event--;
		if(key_event == 6){
		  //set_int(1);
		  //dprintk("Event Trg: %02x\n", int_rsn);
		  lgnas_event(int_rsn, key_state);
		  // issue trigger only one time for the key_event
			if(!int_rsn || ((int_rsn == 1) && key_state)) 
				key_state = 0;
		}else if(!key_event){
	 		;	//set_int(0);
	 	}
	}  
	
	return 0;
}

/*
 * SCI Interrupt handler called by acpi_ev_gpe_dispatch(), sci gpe handler, 
 * at drivers/acpi/acpica/evgpe.c
 */
int sci_btn_handler(void){

	//dprintk("%s is called\n",__FUNCTION__);
	/* disable the interrupt */
	sci_gpio_disable();
  /* 
   * post a timer task which enables the sci interrupt 20ms later. 
	 * please note that posting timer task precedes doing handler to meet 20ms period.
   */
	schedule_delayed_work(&sci_int_post_handler,LGNAS_BTN_CHECK_TIME/*=5, assumes HZ=250, meaning 20ms*/);
  if(ns2data->lock.button)
    return 0; 
	/* check key and enable the interrupt */
	_sci_btn_handler();

	return 0;
}
int key_acc_cnt = 0;
inline void key_chk_valid(void)
{
	u32 gpio_tmp=0, gpio_tmp2;
	gpio_tmp2 = (MASK1_KEY_SEL| MASK1_KEY_EJECT |MASK1_KEY_MODE) & GPIODIn1;
	if(!(gpio_tmp2&MASK1_KEY_MODE )) gpio_tmp |= MASK1_KEY_MODE;
	if(!(gpio_tmp2&MASK1_KEY_SEL  )) gpio_tmp |= MASK1_KEY_SEL;
	if(!(gpio_tmp2&MASK1_KEY_EJECT)) gpio_tmp |= MASK1_KEY_EJECT;

  if(!gpio_tmp){
		key_tmp = KEY_ENC_NONE;
		key_cur = KEY_ENC_NONE;
		key_tmp_old = KEY_ENC_NONE;
		key_chk_valid_cnt = 0;
    key_acc_cnt = 0;
		ip_set_cancel_f = 0;
		burn_list_down_f = 0;
		burn_list_chg_f = 0;
		key_flag = 0;
    return;
  }

	if(key_tmp != key_tmp_old){
		key_tmp_old = key_tmp;
		key_chk_valid_cnt = 1;
	}else{
    key_chk_valid_cnt++;
  }

	if(gpio_tmp == MASK1_KEY_MODE)
    key_tmp = KEY_ENC_MODE;
	else if(gpio_tmp == MASK1_KEY_SEL)
    key_tmp = KEY_ENC_ENTER;
	else if(gpio_tmp == MASK1_KEY_EJECT)
    key_tmp = KEY_ENC_EJECT;
	else
    return;

	if(key_flag){
    /* back of ip setting position */
    if(  (key_chk_valid_cnt > KEY_CHK_PERSIST_LIMIT_CNT) 
      && ( (key_cur == KEY_ENC_MODE) || (key_cur == KEY_ENC_EJECT) )
      && ((main_status == MAIN_STATUS_IP_IP_SETUP)
      || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
      || (main_status == MAIN_STATUS_IP_GATEWAY_SETUP))
      && !ip_set_cancel_f){

      ip_set_cancel_f = 1;
      if(value_pos){
        if(  (value_pos == 4) 
          || (value_pos == 8) 
          || (value_pos == 12)){
          value_pos -= 2;
        }else{
          value_pos--;
        }
      }
      if(value_pos == 13) value_edit_f = 1;
      disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
      disp_request_f = 1;
    /* change direction of index count */
    }else if((key_chk_valid_cnt > KEY_CHK_PERSIST_LIMIT_CNT)
      && (key_cur == KEY_ENC_MODE)
      && (main_status == MAIN_STATUS_ODD_BN_FILE_SEL)
      && !burn_list_chg_f){

      if(burn_list_toggle_f) 
        burn_list_toggle_f = 0;
      else 
        burn_list_toggle_f = 1;

      disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT;
      disp_request_f = 1;
      burn_list_chg_f = 1;
    /* cancel of odd burnlist */
    }else if((key_chk_valid_cnt > (KEY_CHK_PERSIST_LIMIT_CNT*2))
      && (key_cur == KEY_ENC_MODE)
      && (main_status == MAIN_STATUS_ODD_BN_FILE_SEL)
      && burn_list_chg_f){

      main_status = MAIN_STATUS_MAIN;
      disp_return_to_main_cnt = 0;
      disp_request_f = 1;
    /* cancel of ip setting */
    }else if((key_chk_valid_cnt > (KEY_CHK_PERSIST_LIMIT_CNT*2))
      && ( (key_cur == KEY_ENC_MODE) || (key_cur == KEY_ENC_EJECT) )
      && ((main_status == MAIN_STATUS_IP_IP_SETUP)
      || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
      || (main_status == MAIN_STATUS_IP_GATEWAY_SETUP))
      && ip_set_cancel_f){
      main_status = MAIN_STATUS_MAIN;
      disp_return_to_main_cnt = 0;
      disp_request_f = 1;
    }

    /* [function key] Accelation of IP set button */
    key_acc_cnt++;
    if(  (key_chk_valid_cnt <= (KEY_CHK_PERSIST_LIMIT_CNT - 10)) 
      && (key_cur == KEY_ENC_MODE) 
      && ((main_status == MAIN_STATUS_IP_IP_SETUP)
      || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
      || (main_status == MAIN_STATUS_IP_GATEWAY_SETUP)
      || (main_status == MAIN_STATUS_ODD_BN_FILE_SEL)) ){
      if(make_value_f || (key_chk_valid_cnt < 30/*600ms*/)) 
        return;

      if(key_acc_cnt >=10 /*200ms*/)  
        key_acc_cnt = 0;
      else 
        return;
    }else{
      key_acc_cnt = 0;
		  return;
    }
	}

  lcd_backlight(ns2data->lcd[0].brightness[LCD_BRIGHT_FULL]);

  /* for line assemble diag */
	if(ns2data->lock.micom){
    key_flag = 1;
    if(!ns2data->lock.buzzer)
      kd_mksound(0xfa3,HZ * 30/1000);
    if(key_tmp == KEY_ENC_EJECT)
      button_event(&ns2data->bdata[NS2_VKEY_TIME].work);
    if(key_tmp == KEY_ENC_MODE)
      button_event(&ns2data->bdata[NS2_VKEY_CAPA].work);
    if(key_tmp == KEY_ENC_ENTER)
      button_event(&ns2data->bdata[NS2_VKEY_SYNC].work);
		return;
	}

  if(!(main_status&0x0f) /* Main Mode Shift */
    && (main_status > MAIN_STATUS_MODE_DISP)
    && (main_status < MAIN_STATUS_SHUTDOWN)){
    key_cur = key_tmp;
    key_proc = key_flag = 1;
    key_checked = 0; 
    key_event = 7;  
    beep_cnt = BEEP_CNT;
    int_rsn = 0x00;

    if(key_cur == KEY_ENC_MODE){
      key_event = 0; key_checked = 1;
      if((main_status == menu_seq[MAIN_MODE_NUM-1]) 
        && (mnt_status == MNT_STATUS_OPENED)){
        if(++admin_pwd_init_cnt >= 5){
          key_checked = 0; 
          key_event = 7;
          int_rsn = 0x0d;
          admin_pwd_init_f = 1;
          admin_pwd_init_cnt = 0;
        }
      }
    }else if(key_cur == KEY_ENC_ENTER){
      if(main_status == MAIN_STATUS_ODD_BN_ENTRY){
        file_num = 1;
        ns2data->mdata.file_num = file_num; 
        file_name_shift = 0;
        valid_file_name_f = 0;
        int_rsn = 0x06;
        disp_shift_occurred_f = 1;
      }else if(main_status == MAIN_STATUS_MODE_USB_BCK){
        key_checked = 0; 
        key_event = 7;
        int_rsn = 0x00;
        key_state = 0x20;
      }else{
        key_event = 0; 
        key_checked = 1;
      }
    }else if(key_cur == KEY_ENC_EJECT){
      key_state = 0x08;
    }
  }else if((main_status&0xf0) == MAIN_STATUS_MODE_DISP){
    key_cur = key_tmp;
    key_proc = key_flag = 1;
    key_checked = 0; key_event = 7;  beep_cnt = BEEP_CNT;
    int_rsn = 0x00;

    if(key_cur == KEY_ENC_MODE){
      key_event = 0; key_checked = 1;
    }else if(key_cur == KEY_ENC_ENTER){
      if((main_status == (MAIN_STATUS_DISP_IP-1))
        || (main_status == (MAIN_STATUS_DISP_ERR))){
        int_rsn = 0x83;
        disp_shift_occurred_f = 1;
      //key_event = 0; key_checked = 1;
      }else if(main_status == (MAIN_STATUS_DISP_TIME-1)){
        int_rsn = 0x84;
        disp_shift_occurred_f = 1;
      }else if(main_status == (MAIN_STATUS_DISP_CAPA-1)){
        int_rsn = 0x85;
        disp_shift_occurred_f = 1;
      }else if(main_status == (MAIN_STATUS_DISP_RAID-1)){
        int_rsn = 0x88;
        disp_shift_occurred_f = 1;
      }else if(main_status == (MAIN_STATUS_DISP_ERR-1)){
        int_rsn = 0x87;
        disp_shift_occurred_f = 1;
      /* 
      }else if(main_status == (MAIN_STATUS_DISP_FW_VER-1)){
        key_event = 0; key_checked = 1;
        disp_shift_occurred_f = 1;
      */
      }else if(main_status == (MAIN_STATUS_DISP_MN_FW_VER-1)){
        int_rsn = 0x8d;
        disp_shift_occurred_f = 1;
      }else if(main_status == (MAIN_STATUS_DISP_FAN_RPM-1)){
        key_event = 0; key_checked = 1;
        disp_shift_occurred_f = 1;
      }else{
        key_event = 0; key_checked = 1;
      }
    }else if(key_cur == KEY_ENC_EJECT){
      key_state = 0x08;
    }
  }else if((main_status&0xf0) == MAIN_STATUS_MODE_IP){
    key_cur = key_tmp;
    key_proc = key_flag = 1;
    key_checked = 0; key_event = 7;  beep_cnt = BEEP_CNT;
    int_rsn = 0x00;

    if(key_cur == KEY_ENC_MODE){
      if(main_status == (MAIN_STATUS_IP_IP_SETUP-1)){
        disp_shift_occurred_f = 1;
        key_event = 0; key_checked = 1;
      }else{
        key_event = 0; key_checked = 1;
      }
    }else if(key_cur == KEY_ENC_ENTER){
      key_state = 0x00;
      if(main_status == (MAIN_STATUS_IP_DHCP_CF)){
        *(pip_s + 4) = 0x01;
        key_state = 0x01; /* dhcp */
        int_rsn = 0x03;
        disp_shift_occurred_f = 1;
      }else if((main_status == (MAIN_STATUS_IP_NETMASK_SETUP-1)) 
        && !value_edit_f){
        disp_shift_occurred_f = 1;
        key_event = 0; key_checked = 1;
      }else if((main_status == (MAIN_STATUS_IP_GATEWAY_SETUP-1)) 
        && !value_edit_f){
        disp_shift_occurred_f = 1;
        key_event = 0; key_checked = 1;
      }else if((main_status == (MAIN_STATUS_IP_GATEWAY_SETUP)) 
        && !value_edit_f){
        int_rsn = 0x03;
        disp_shift_occurred_f = 1;
      }else{
        key_event = 0; key_checked = 1;
      }
    }else if(key_cur == KEY_ENC_EJECT){
      key_event = 0; key_checked = 1;
    }
  }else if((main_status&0xf0) == MAIN_STATUS_ODD_BCK_ENTRY){
    key_cur = key_tmp;
    key_proc = key_flag = 1;
    key_checked = 0; key_event = 7;  beep_cnt = BEEP_CNT;
    int_rsn = 0x00;
    odd_rip_f = 0;

    if(key_cur == KEY_ENC_ENTER){
      if(main_status == MAIN_STATUS_ODD_DATA_BCK_ENTRY){
        key_state = 0x02;
      }else if(main_status == MAIN_STATUS_ODD_ISO_BCK_ENTRY){
        key_state = 0x04;
      }else if(main_status == MAIN_STATUS_ODD_BCK_CANCEL){
        key_state = 0x40;
        backup_status = BACKUP_STATUS_CANCELED;
      }else{
        key_checked = 1; key_event = 0;
      }
    }else{
      key_checked = 1; key_event = 0;
    }
  }else if((main_status&0xf0) == MAIN_STATUS_ODD_BN_ENTRY){
    key_cur = key_tmp;
    key_proc = key_flag = 1;
    key_checked = 0; key_event = 7;  beep_cnt = BEEP_CNT;
    int_rsn = 0x00;

    if(key_cur == KEY_ENC_MODE){
      if(main_status == MAIN_STATUS_ODD_BN_FILE_SEL){
        if(burn_list_toggle_f){
          if(file_num == 1) file_num = total_file_num;
          else if(file_num > 1) file_num--;
        }else{
          if(++file_num > total_file_num) file_num = 1;
        }
        ns2data->mdata.file_num = file_num; 
        file_name_shift = 0;
        valid_file_name_f = 0;
        int_rsn = 0x06;
        disp_shift_occurred_f = 1;
      }else{
        key_checked = 1; key_event = 0;
      }
    }else if(key_cur == KEY_ENC_ENTER){
      if(((main_status == MAIN_STATUS_ODD_BN_FILE_SEL) && !overwrite_warn_f)
        || (main_status == MAIN_STATUS_ODD_BN_RW_CF)){
        key_checked = 0; key_event = 7;
        int_rsn = 0x00;
        key_state = 0x10;
      }else if(main_status == MAIN_STATUS_ODD_BN_CANCEL){
        key_state = 0x40;
        burn_status = BURN_STATUS_CANCELED;
      }else{
        key_checked = 1; key_event = 0;
      }
    }else if(key_cur == KEY_ENC_EJECT){
      if(main_status == MAIN_STATUS_ODD_BN_FILE_SEL){
        key_state = 0x08;
      }else{
        key_event = 0; key_checked = 1;
      }
    }
  }else if((main_status&0xf0) == MAIN_STATUS_USB_BCK_ENTRY){
    key_cur = key_tmp;
    key_proc = key_flag = 1;
    key_checked = 0; key_event = 7;  beep_cnt = BEEP_CNT;
    int_rsn = 0x00;

    if(key_cur == KEY_ENC_ENTER){
      if(main_status == MAIN_STATUS_USB_BCK_CANCEL){
        key_state = 0x40;
        backup_status = BACKUP_STATUS_CANCELED;
      }else{
        key_checked = 1; key_event = 0;
      }
    }else{
      key_checked = 1; key_event = 0;
    }
  }else if((main_status&0xf0) == MAIN_STATUS_BOOT_ODD){
    key_cur = key_tmp;
    key_proc = key_flag = 1;
    key_checked = 0; key_event = 7;  beep_cnt = BEEP_CNT;
    int_rsn = 0x00;

    if(main_status == MAIN_STATUS_BOOT_ODD_SETUP){
      if(key_cur == KEY_ENC_ENTER){// SYS&VOL
        key_event = 0; key_checked = 1;
        setup_type = 0x00;
      }else if(key_cur == KEY_ENC_MODE){// SYS Only
        key_event = 0; key_checked = 1;
        setup_type = 0x01;
      }
    }else if(main_status == MAIN_STATUS_BOOT_ODD){
      if(key_cur == KEY_ENC_EJECT){
        key_state = 0x08;
      }
    }
  }else if((main_status&0xf0) == MAIN_STATUS_BOOT_AGING){
    key_cur = key_tmp;
    key_proc = key_flag = 1;
    key_checked = 1;
  }

  if((main_status == MAIN_STATUS_WAIT) 
    && (key_tmp == KEY_ENC_ENTER)) 
    odd_boot_chk_cnt = 30;

  key_idle_cnt = KEY_IDLE_MAX_CNT;
}

//PCI register setting for SCI interrupt 
int init_gpio_interrupt(void){

	u16 vendor_id;
	u16 device_id;
	u32 gpio_usb_sel;
	u32 gpio_inv;
	u32 gpio_rout;
	u32 gpe0_en;

	sb = pci_get_device(VENDOR_ID,DEVICE_ID,sb);
	if(sb == 0){
			printk("fail to find proper PCI bus\n");
	}	
	pci_read_config_word(sb,ICH9_PCI_CONF_VID,&vendor_id);
	pci_read_config_word(sb,ICH9_PCI_CONF_DID,&device_id);

	dprintk("systemID = %04x %04x\n",vendor_id,device_id);

	if(VENDOR_ID != vendor_id || DEVICE_ID != device_id){
		printk("WARNING: PCI bus authentication failed\n");
	}
	
	pmbase = pci_read_long(sb,ICH9_PCI_CONF_PM_BASE) & 0xFFFFFFFE;
	gpiobase = pci_read_long(sb,ICH9_PCI_CONF_GPIO_BASE) & 0xFFFFFFFE;

	dprintk("GPIOBASE = 0x%04x\n\n", gpiobase);
	dprintk("pmbase = 0x%x\n\n", pmbase);

	//check GPIO expliot
	dprintk("GPIO_USE_SEL\n");
	dprintk("%08x\n",inl(gpiobase+ICH9_GPIOBASE_USE_SEL));

	//check GPIO usage : input mode
	dprintk("GPIO_IO_SEL\n");
	gpio_usb_sel = inl(gpiobase+ICH9_GPIOBASE_IO_SEL);
	//outl((gpio_usb_sel & GPIO_BTN_SET_OUTPUT),gpiobase+0x4); //output
	outl((gpio_usb_sel | GPIO_BTN_SET_INPUT),gpiobase+ICH9_GPIOBASE_IO_SEL); //input
	dprintk("%08x\n",inl(gpiobase+ICH9_GPIOBASE_IO_SEL));

	//set to low active
	dprintk("GPIO_INV\n");
	gpio_inv = inl(gpiobase+ICH9_GPIOBASE_INV);
	outl((gpio_inv | GPIO_BTN_SIG_INV_LOW),gpiobase+ICH9_GPIOBASE_INV); //low active
	//outl((gpio_inv & GPIO_BTN_SIG_INV_HIGH),gpiobase+ICH9_GPIOBASE_INV); //high active
	dprintk("%08x\n",inl(gpiobase+ICH9_GPIOBASE_INV));

	//check if SCI interrupt enabled
	dprintk("PM1 control\n");
	dprintk("%08x\n",inl(pmbase+ICH9_PMBASE_PM1_CONTROL)); //0bit: 0=SMI, 1=SCI occur

	//enable GPIO 14/13/12 ROUT
	gpio_rout= pci_read_long(sb,ICH9_PCI_CONF_GPI_ROUT);
	dprintk("GPIO_ROUT 0xb8=%08x\n",gpio_rout);
	pci_write_long(sb,ICH9_PCI_CONF_GPI_ROUT,(gpio_rout | GPIO_BTN_ROUT_EN) );
	dprintk("GPIO_ROUT 0xb8=%08x\n",pci_read_long(sb,ICH9_PCI_CONF_GPI_ROUT));

	//enable GPIO 14/13/12 GPE0_EN
	printk("PMR_GPE0_EN\n");
	gpe0_en = inl(pmbase+ICH9_PMBASE_GPE0_EN);
	dprintk("%08x\n",gpe0_en);
	//outl((gpe0_en&GPIO_BTN_GPE0_DIS),pmbase+ICH9_PMBASE_GPE0_EN); //GPE0 disable
	outl((gpe0_en|GPIO_BTN_GPE0_EN),pmbase+ICH9_PMBASE_GPE0_EN); //GPE0 enable 
	dprintk("%08x\n",inl(pmbase+ICH9_PMBASE_GPE0_EN));

#if 0
	//test code : monitering btn status 
	while(1){
	      //clear GPIO 14/13/12 status 
	      u32 gpeo_sts = inl(pmbase+ICH9_PMBASE_GPE0_STS);
	      outl((gpeo_sts | GPIO_BTN_GPE0_STS_MASK),pmbase+ICH9_PMBASE_GPE0_STS);

	      printk("PMR_GPE0_STS  GPIO_LV\n");
	      printk("%08x  %08x\n",inl(pmbase+ICH9_PMBASE_GPE0_STS),inl(gpiobase+0xC));
	      msleep(500);

	    break;
	}
#endif 	
	return 0;

}
