/*****************************************************************************
 * lgnas_n4b2_setup.c
 ****************************************************************************/
#include "lgnas.h"
#include "lgnas_if.h"
#include "lgnas_sym.h"

/*****************************************************************************
 * N4B2 GLOBAL
 ****************************************************************************/
struct raid_state raidstate;
u8	key_cur, key_tmp, key_tmp_old, tmr_1000ms;
u32	tick_cnt, key_chk_valid_cnt;

u8	key_flag, key_proc, key_state, key_event, int_rsn, key_checked;
u8	main_status, disp_status, mnt_status, backup_status, burn_status, btn_cnt;

u8   com_state, com_flag;
u8   xbuf[8], rbuf[8], ebuf[8], lbuf[8], dbuf[8], ip[5], time[6], str_debug[8], title_name[9];
u8   netmask[4], gateway[4], ip_s[5], netmask_s[4], gateway_s[4];
u8   *pdbuf, *plbuf, *pebuf, *pxbuf, *prbuf, *pstr_debug, *pip, *ptime, *ptitle_name, *pserver_name;
u8   *pnetmask, *pgateway, *pip_s, *pnetmask_s, *pgateway_s;
u8   value_setup[16], value_pos;
u8   beep_cnt, beep_type, beep_buf[6], beep_prog, beep_cont_cnt, beep_int_cnt;
u16  beep_cnt_w;
u8   xidx, ridx, didx, dcnt, lcd_pos, lcd_len;
u8   hdd_usage_rate, mnt_dev, /* raid_sync_state,*/ prog_rate, prog_type, prog_type_ext;

u16  disp_return_to_main_cnt, boot_wait_cnt, key_idle_cnt;
u8   fan_rpm, bfw_update, lcd_bl_full, lcd_bl_half;

u8   file_num, total_file_num, file_name_size, file_name[MINFO_MAX_LCD_STR], file_name_shift, file_name_disp_cnt;
u8   err_msg_size, err_code, err_msg_shift, err_msg_disp_cnt, svc_code[2];
u8   wc_msg_size, wc_msg_shift, wc_msg_disp_cnt;

u8   bg_msg[MAX_MSG_SIZE], wc_msg[MAX_MSG_SIZE], err_msg[MAX_MSG_SIZE];
u8   server_name[12];

u8    serial[8], mn_fw_ver[12];

u8    test_type, type_0_seq, btest_run_stop;
u16   fan_timer_cnt, fan_timer_cnt_old, fan_rpm_fb, i2c_bsy_cnt, boot_to_cnt;

u8    boot_type, setup_type, bpower_key_pressed, aging_status, menu_seq[5], menu_idx, menu_top;
u8    power_fail_cnt, admin_pwd_init_cnt, power_fail_ccured_cnt, odd_boot_chk_cnt;

bit_struct	bit_str_0, bit_str_1, bit_str_2, bit_str_3;

static void gpio_init_ichx(void);
static void init_var(void);

/*****************************************************************************
 * Init Functions
 ****************************************************************************/
int __init gpio_lcd_led_btn_init(void)
{
	init_var();
	//gpio_init();
  gpio_init_ichx();
	lcd_init();

	init_gpio_interrupt();
  return 0;
}
__initcall(gpio_lcd_led_btn_init);


static void init_var(void)
{
  u8 bTmp;

  // Global Variable init
  //wAdcRslt0 = wAdcRslt1 = 0;
  key_cur = key_tmp = key_tmp_old = 0;
  tmr_1000ms = 0;
  tick_cnt = key_chk_valid_cnt = 0;

  key_flag = key_proc = key_state = key_event = int_rsn = 0; key_checked = 1;
  main_status = disp_status = mnt_status = backup_status = burn_status = btn_cnt = 0;
  com_state = com_flag = 0;
  value_pos = 0;
  xidx = ridx = didx = dcnt = lcd_pos = lcd_len = 0;
  hdd_usage_rate = 10;
  mnt_dev = prog_rate = prog_type = prog_type_ext = 0;
  /* raid_sync_state = 0 */
  raidstate.vol = RAID_STATUS_VOL_ALL;
  raidstate.state = RAID_STATUS_SYNC;
  raidstate.percent = 100;
  boot_wait_cnt = 0; boot_to_cnt = 0;
  bfw_update = 0;
  file_num = 1; file_name_shift = file_name_disp_cnt = 0;
  err_code = err_msg_shift = err_msg_disp_cnt = 0;
  svc_code[0] = svc_code[1] = 0;
  wc_msg_shift = wc_msg_disp_cnt = 0;

  beep_type = 1;
  beep_cnt_w = 0;

  // Set pointer
  pdbuf = &dbuf[0];
  plbuf = &lbuf[0];
  pebuf = &ebuf[0];
  pxbuf = &xbuf[0];
  prbuf = &rbuf[0];
  pstr_debug = &str_debug[0];

  pip_s = &ip_s[0];
  pnetmask_s = &netmask_s[0];
  pgateway_s = &gateway_s[0];

  pip = &ip[0];
  *(pip_s) = *(pip) = 150;
  *(pip_s+1) = *(pip+1) = 150;
  *(pip_s+2) = *(pip+2) = 1;
  *(pip_s+3) = *(pip+3) = 1;
  *(pip_s+4) = *(pip+4) = 0;

  pnetmask = &netmask[0];
  *(pnetmask_s) = *(pnetmask) = 255;
  *(pnetmask_s+1) = *(pnetmask+1) = 255;
  *(pnetmask_s+2) = *(pnetmask+2) = 255;
  *(pnetmask_s+3) = *(pnetmask+3) = 0;

  pgateway = &gateway[0];
  *(pgateway_s) = *(pgateway) = 150;
  *(pgateway_s+1) = *(pgateway+1) = 150;
  *(pgateway_s+2) = *(pgateway+2) = 1;
  *(pgateway_s+3) = *(pgateway+3) = 254;

  ptime = &time[0];
  *(ptime+0) = 20;
  *(ptime+1) = 9;
  *(ptime+2) = 1;
  *(ptime+3) = 1;
  *(ptime+4) = 10;
  *(ptime+5) = 10;

  disp_return_to_main_cnt = 0;

  *((unsigned char*) &bit_str_0) = 0;
  *((unsigned char*) &bit_str_1) = 0;
  *((unsigned char*) &bit_str_2) = 0;
  *((unsigned char*) &bit_str_3) = 0;

  for(bTmp=0; bTmp < 7; bTmp++)
  {
    *(pstr_debug + bTmp) = 0x20;
  }
  *(pstr_debug + 7) = 0x00;
  didx = 0;

  ptitle_name = &title_name[0];
  for(bTmp=0; bTmp < 8; bTmp++)
  {
    *(ptitle_name + bTmp) = 0x20;
  }
  *(ptitle_name + 8) = 0x00;

  pserver_name = &server_name[0];
  for(bTmp=0; bTmp < 12; bTmp++)
  {
    *(pserver_name + bTmp) = 0x20;
  }
  *(pserver_name + 0) = 'A';
  *(pserver_name + 1) = 'S';
  *(pserver_name + 2) = 'T';
  *(pserver_name + 3) = 'A';
  *(pserver_name + 4) = 'R';

  fan_rpm = 255;
  lcd_bl_full = LCD_BL_FULL;
  lcd_bl_half = LCD_BL_HALF;

  key_idle_cnt = KEY_IDLE_MAX_CNT;

  for(bTmp=0; bTmp < 7; bTmp++)
  {
    serial[bTmp] = 0x00;
  }

  boot_type = 0;  setup_type = 0xff;
  bpower_key_pressed = 0;
  aging_status = 1;
  btest_run_stop = 0;
  i2c_bsy_cnt = 0;

	for(bTmp=0; bTmp < 12; bTmp++)
	{
		mn_fw_ver[bTmp] = '*';
	}

  fan_timer_cnt = 0;
  fan_timer_cnt_old = 0;
  fan_rpm_fb = 0;

  for(bTmp=0; bTmp < MAX_MSG_SIZE; bTmp++)
  {
    bg_msg[bTmp] = wc_msg[bTmp] = 0x00;
  }
  wc_msg_size = strlen(str_welcome) - 1;
  memcpy(bg_msg, (unsigned char *) str_welcome, wc_msg_size);

  menu_seq[0] = MAIN_STATUS_IP_ENTRY;
  menu_seq[1] = MAIN_STATUS_ODD_BCK_ENTRY;
  menu_seq[2] = MAIN_STATUS_ODD_BN_ENTRY;
  menu_seq[3] = MAIN_STATUS_USB_BCK_ENTRY;
  menu_idx = 0;
  menu_top = MAIN_STATUS_IP_ENTRY;

  power_fail_cnt = power_fail_ccured_cnt = 0;

  odd_boot_chk_cnt = 0;

#if 1
	main_status = MAIN_STATUS_BOOT;
#else
	main_status = MAIN_STATUS_MAIN;
#endif
}

/*****************************************************************************
 * N4B2 GPIO Function
 ****************************************************************************/
#if 0
static void gpio_init(void)
{
  dprintk("%s is called\n", __FUNCTION__);
	//struct pci_dev *dev;
	//u16 temp = 0;
	//----------------------
	// SIO start
	//----------------------
	SIOEnter;

	SIOOutB(SIOInB(SIO_GLOBAL_MULTI2) | 0x03, SIO_GLOBAL_MULTI2);
	SIOOutB(SIOInB(SIO_GLOBAL_MULTI3) | 0x30, SIO_GLOBAL_MULTI3);
	//Sys Fan out
	SIOOutB(SIOInB(SIO_GLOBAL_MULTI3) & 0xfb, SIO_GLOBAL_MULTI3);
	SIOOutB(SIOInB(SIO_CONFIG_PORT_SEL) & 0xfe, SIO_CONFIG_PORT_SEL);
	//----------------------
	// 	fan 1 :[1:0] auto(01)
	// 	fan 2 :[3:2] mannual(11)
	//	fan 3 :[5:4] mannual(11)
	//----------------------
	SHWMOutB(0x3d, 0x96);
	//----------------------
	// SIO gpio drv push-pull
	//----------------------
	//open drain : 0 (default) Push Pull :1
	SIOSel(SIO_F71808E_LD_GPIO);
	SIOOutB(0xff,SIO_GPIO_DRVEN_1);
	SIOOutB(0xff,SIO_GPIO_DRVEN_2);
	SIOOutB(0xff,SIO_GPIO_DRVEN_3);
	//SIOOutB(0x00,SIO_GPIO_DRVEN_1);
	//SIOOutB(0x00,SIO_GPIO_DRVEN_2);
	//SIOOutB(0x00,SIO_GPIO_DRVEN_3);
	//----------------------
	// SIO gpio level
	//----------------------
#ifndef NS2EVT_BOARD
	SIOOutB(0,SIO_GPIO_DATA_1);
	SIOOutB(0,SIO_GPIO_DATA_2);
	SIOOutB(0,SIO_GPIO_DATA_3);
#else
	SIOOutB(MASKS1_LED_HDD,SIO_GPIO_DATA_1);
	SIOOutB(0,SIO_GPIO_DATA_2);
	SIOOutB(0,SIO_GPIO_DATA_3);
#endif
	//----------------------
	// SIO gpio select (1:ouput,    0:input)
	//----------------------
	SIOOutB(MASKS1_ALL,SIO_GPIO_OUTEN_1);
	SIOOutB(MASKS2_ALL,SIO_GPIO_OUTEN_2);
	SIOOutB(MASKS3_ALL,SIO_GPIO_OUTEN_3);
}
#endif
void gpio_init_ichx(void)
{
	//----------------------
	// ICh9R gpio use select (0:native, 	1:gpio)
	//----------------------
	GPIOUSOut1(GPIOUSIn1 | MASK1_ALL);
	GPIOUSOut2(GPIOUSIn2 | MASK2_ALL);
	//----------------------
	// ICh9R gpio level
	//----------------------
#ifndef NS2EVT_BOARD
	GPIODOut1(GPIODIn1 & ~MASK1_OUT_ALL);
#else
	GPIODOut1((GPIODIn1 & ~MASK1_OUT_ALL) | MASK1_LED_HDD4A);
	GPIODOut2((GPIODIn2 & ~MASK2_OUT_ALL) | MASK2_LED_ODD  );
#endif
	//----------------------
	// ICh9R gpio select (0:ouput,    1:input)
	//----------------------
	//GPIOSOut1((GPIOSIn1 & ~MASK1_ALL) | MASK1_IN_ALL);
	//GPIOSOut2((GPIOSIn2 & ~MASK2_ALL) | MASK2_IN_ALL);
	GPIOSOut1(GPIOSIn1 | MASK1_IN_ALL);
	GPIOSOut2(GPIOSIn2 | MASK2_IN_ALL);
	GPIOSOut1(GPIOSIn1 & ~MASK1_OUT_ALL);
#ifndef NS2EVT_BOARD
	GPIOSOut2(GPIOSIn2 & ~MASK2_OUT_ALL);
#endif

	//----------------------
	// ICh9R PMConfig 3 S4_State Pin Disable for gpio26(0:enable,1:disable,gpio)
	//----------------------
	//pci_read_config_word( dev, 0xa4, &temp);
}

