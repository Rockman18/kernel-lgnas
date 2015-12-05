#include "lgnas.h"
#include "lgnas_if.h"
#include "lgnas_sym.h"

const u8* str_welcome = "* Enjoy LG-NAS with built-in Blu-ray drive";
const u8* str_welcome_dvd = "* Enjoy LG-NAS with built-in DVD rewriter";

static int f_main_wait = 0;

void disp_processing(struct nashal_data *data);
extern int lgnas_sysfan_rpm(void);

/*****************************************************************************
 * N4B2 front main (period 200ms)
 ****************************************************************************/
void task_200ms(struct nashal_data *data)
{
  if(data->lock.micom)
		return;

	switch(main_status&0xf0)
	{
		case MAIN_STATUS_MAIN:
		case MAIN_STATUS_IP_ENTRY:
		case MAIN_STATUS_ODD_BCK_ENTRY:
		case MAIN_STATUS_ODD_BN_ENTRY:
		case MAIN_STATUS_USB_BCK_ENTRY:
		  disp_processing(data);
		  break;

  	default:
			break;
	}

  if( (main_status&0xf0) != MAIN_STATUS_ODD_BN_ENTRY)
    data->mdata.file_init = 0;

	if(key_idle_cnt){ //<- 75 * 200ms = 15s
		key_idle_cnt--;
    f_main_wait = 0;
	}else{
		if(!persist_err_msg_f){
		  lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_HALF]);
      if(!f_main_wait){
        wc_msg_shift = 0 ;
        f_main_wait = 1;
      }
			valid_err_msg_f = 0;
			err_code = 0;
			// clear status
			backup_status = burn_status = 0;
			admin_pwd_init_cnt = 0;
		}
	}

	return;
}

/*****************************************************************************
 * N4B2 front timer2 (period : 1s)
 ****************************************************************************/
void task_1000ms(void)
{
	// Display Control
  //dprintk("%s is called\n", __FUNCTION__);

	if(main_disp_toggle_f) main_disp_toggle_f = 0x00;
	else main_disp_toggle_f = 0x01;

	disp_request_f = 1;
	fan_rpm_fb = lgnas_sysfan_rpm();

  if(valid_file_name_f && (main_status == MAIN_STATUS_ODD_BN_FILE_SEL)){
    if(file_name_size > MINFO_MAX_LCD_STR) file_name_size = MINFO_MAX_LCD_STR;
    if(!file_name_shift){
      if(file_name_disp_cnt++){
        if(file_name_shift < file_name_size) file_name_shift++;
        else file_name_shift = 0;
        file_name_disp_cnt = 0;
      }
    }else{
      if(file_name_shift < (file_name_size+4)) file_name_shift++;
      else file_name_shift = 0;
      file_name_disp_cnt = 0;
    }

    disp_request_f = 1;
  }

  if((valid_err_msg_f || persist_err_msg_f) && ((main_status == MAIN_STATUS_MAIN)
    || (main_status == MAIN_STATUS_BOOT_ODD)
    || (main_status == MAIN_STATUS_BOOT)
    || (main_status == MAIN_STATUS_BOOT_AGING))){
    if(err_msg_size > MAX_MSG_SIZE) err_msg_size = MAX_MSG_SIZE;
    if(err_msg_size > 16){
      if(!err_msg_shift){
        if(err_msg_disp_cnt++){
          if(err_msg_shift < err_msg_size) 
            err_msg_shift++;
          else 
            err_msg_shift = 0;
          err_msg_disp_cnt = 0;
        }
      }else{
        if(err_msg_shift < (err_msg_size+4)) 
					err_msg_shift++;
        else err_msg_shift = 0;
        	err_msg_disp_cnt = 0;
      }
    }else{
      err_msg_shift = 0;
      err_msg_disp_cnt = 0;
    }

    disp_request_f = 1;
  }else if(!valid_err_msg_f && !persist_err_msg_f && (main_status == MAIN_STATUS_MAIN)){
    if(wc_msg_size > 16){
      if(!wc_msg_shift){
        if(wc_msg_disp_cnt++){
          if(wc_msg_shift < wc_msg_size) 
            wc_msg_shift++;
          else 
            wc_msg_shift = 0;
          wc_msg_disp_cnt = 0;
        }
      }else{
        if(wc_msg_shift < (wc_msg_size+4)) wc_msg_shift++;
        else wc_msg_shift = 0;
        wc_msg_disp_cnt = 0;
      }
    }else{
      wc_msg_shift = 0;
      wc_msg_disp_cnt = 0;
    }

    disp_request_f = 1;
  }

  if(disp_return_to_main_cnt)
    disp_return_to_main_cnt -= 1;
  else{
    if((main_status > MAIN_STATUS_MAIN)
      && (main_status < MAIN_STATUS_SHUTDOWN))
    {
      wc_msg_shift = wc_msg_disp_cnt = 0;
      main_status = MAIN_STATUS_MAIN;
    }
  }

}

static void MakeValue(unsigned char * pIn, unsigned char * pOut)
{
  unsigned char bTmp1, bTmp2;

  for(bTmp1=0; bTmp1 < 4; bTmp1++)
  {
    pOut[bTmp1*4 + 0] = pIn[bTmp1]/100;
    bTmp2 = pIn[bTmp1] - pOut[bTmp1*4 + 0]*100;
    pOut[bTmp1*4 + 1] = bTmp2/10;
    pOut[bTmp1*4 + 2] = pIn[bTmp1] - pOut[bTmp1*4 + 0]*100 - pOut[bTmp1*4 + 1]*10;
    pOut[bTmp1*4 + 0] += '0';
    pOut[bTmp1*4 + 1] += '0';
    pOut[bTmp1*4 + 2] += '0';
    pOut[bTmp1*4 + 3] = '.';
    if(bTmp1 == 3) pOut[bTmp1*4 + 3] = ' ';
  }
}


void disp_processing(struct nashal_data *data)
{
  char temp[2];
  if(!disp_request_f)
    return;

  //dprintk("%s is called. main_status is %x\n", __FUNCTION__, main_status);

  switch(main_status&0xf0)
  {
  case MAIN_STATUS_WAIT:
    lcd_put_s(0x40, "* Ready to ON !*", 16);

    if(!main_disp_toggle_f)
      lcd_put_s(0, "*", 1);
    else
      lcd_put_s(0, " ", 1);

    lcd_put_s(1, "LG N/W Storage ", 15);
    break;

  case MAIN_STATUS_BOOT:

    if(!main_disp_toggle_f)
    {
      lcd_put_s(0, "*", 1);
    }
    else
    {
      lcd_put_s(0, " ", 1);
    }
    lcd_put_s(1, "System Booting ", 15);
    if(valid_err_msg_f || persist_err_msg_f)
    {
      if(err_msg_size <= 16)
      {
        lcd_put_s(0x40, (char*)&err_msg[0], err_msg_size);
        lcd_fill_blank(0x40+err_msg_size, 16-err_msg_size);
      }
      else
      {
        if(err_msg_size > err_msg_shift)
        {
          if((err_msg_size - err_msg_shift) >= 16)
          {
            lcd_put_s(0x40, (char*)&err_msg[err_msg_shift], 16);  //err_msg_size);
          }
          else
          {
            lcd_len = err_msg_size - err_msg_shift;
            lcd_put_s(0x40, (char*)&err_msg[err_msg_shift], lcd_len);

            if(lcd_len >= 12)
            {
              lcd_fill_blank(0x40+lcd_len, 16-lcd_len);
            }
            else
            {
              lcd_fill_blank(0x40+lcd_len, 4);
              lcd_put_s(0x44+lcd_len, (char*)&err_msg[0], 12 - lcd_len);
            }
          }
        }
        else
        {
          lcd_fill_blank(0x40, err_msg_size+4-err_msg_shift);
          lcd_put_s(0x40+err_msg_size+4-err_msg_shift, (char*)&err_msg[0], 16 - (err_msg_size+4-err_msg_shift));
        }
      }
    }
    else
    {
      lcd_put_s(0x40, " Turn On Power..", 16);
    }

    break;

  case MAIN_STATUS_MAIN:

    switch(main_status)
    {
    case MAIN_STATUS_MODE_DISP:
      if(disp_shift_occurred_f) 
        disp_shift_occurred_f = 0;

      if(!main_disp_toggle_f)
        lcd_put_s(0, "*", 1);
      else
        lcd_put_s(0, " ", 1);

      lcd_put_s(1, "LG N/W Storage ", 15);

      if(valid_err_msg_f || persist_err_msg_f){
        if(err_msg_size <= 16){
          lcd_put_s(0x40, (char*)&err_msg[0], err_msg_size);
          lcd_fill_blank(0x40+err_msg_size, 16-err_msg_size);
        }else{
          if(err_msg_size > err_msg_shift){
            if((err_msg_size - err_msg_shift) >= 16)
              lcd_put_s(0x40, (char*)&err_msg[err_msg_shift], 16);  //err_msg_size);
            else{
              lcd_len = err_msg_size - err_msg_shift;
              lcd_put_s(0x40, (char*)&err_msg[err_msg_shift], lcd_len);

              if(lcd_len >= 12)
                lcd_fill_blank(0x40+lcd_len, 16-lcd_len);
              else{
                lcd_fill_blank(0x40+lcd_len, 4);
                lcd_put_s(0x44+lcd_len, (char*)&err_msg[0], 12 - lcd_len);
              }
            }
          }else{
            lcd_fill_blank(0x40, err_msg_size+4-err_msg_shift);
            lcd_put_s(0x40+err_msg_size+4-err_msg_shift, (char*)&err_msg[0], 16 - (err_msg_size+4-err_msg_shift));
          }
        }
      }else if(mnt_status){
        if(mnt_status == MNT_STATUS_OPENED)
          lcd_put_s(0x40, "* Tray Open    *", 16);
        else if(mnt_status == MNT_STATUS_LOADED){
          //if(main_disp_toggle_f)
          //{
            if(!mnt_dev){
		          lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_FULL]);
              lcd_put_s(0x40, "* Tray Loaded  *", 16);
            }else
              lcd_put_s(0x40, "* Usb  Loaded  *", 16);
          //}
        }else if(mnt_status == MNT_STATUS_LOADING){
          //lcd_put_s(0x40, "* Disc Loading *", 16);
		      lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_FULL]);
          lcd_put_s(0x40, "* Tray Loading *", 16);
        }else if(mnt_status == MNT_STATUS_MOUNTED){
          lcd_put_s(0x40, "* Disc Loading *", 16);
        }
      }else{
        if(wc_msg_size <= 16){
          lcd_put_s(0x40, (char*)(bg_msg), wc_msg_size);
          lcd_fill_blank(0x40+wc_msg_size, 16-wc_msg_size);
        }else{
          if(wc_msg_size > wc_msg_shift){
            if((wc_msg_size - wc_msg_shift) >= 16)
              lcd_put_s(0x40, (char*)&bg_msg[wc_msg_shift], 16);  //err_msg_size);
            else{
              lcd_len = wc_msg_size - wc_msg_shift;
              lcd_put_s(0x40, (char*)&bg_msg[wc_msg_shift], lcd_len);

              if(lcd_len >= 12)
                lcd_fill_blank(0x40+lcd_len, 16-lcd_len);
              else{
                lcd_fill_blank(0x40+lcd_len, 4);
                lcd_put_s(0x44+lcd_len, (char*)&bg_msg[0], 12 - lcd_len);
              }
            }
          }else{
            lcd_fill_blank(0x40, wc_msg_size+4-wc_msg_shift);
            lcd_put_s(0x40+wc_msg_size+4-wc_msg_shift, (char*)&bg_msg[0], 16 - (wc_msg_size+4-wc_msg_shift));
          }
        }
      }

      lcd_cursor_on(0x00, 0x00);

      break;

    case MAIN_STATUS_DISP_IP:  // Server Name & IP
      if(disp_shift_occurred_f)
      {
       // lcd_put_s(0x00, "ASTAR           ", 16);
       // lcd_put_s(0x40, "150.150.146.xxx ", 16);
        disp_shift_occurred_f = 0;
      }

      //if(valid_ip_f)
      //{
        lcd_put_s(0x00, (char*) pserver_name, 12);
        lcd_put_s(0x0c, "    ", 4);
        //lcd_put_s(0x40, "150.150.146.    ", 16);

        //check link down
        if( 
            (*(pip+0) == 0) &&
            (*(pip+1) == 0) &&
            (*(pip+2) == 0) &&
            (*(pip+3) == 0)  ){
          lcd_put_s(0x40, "Link-down       ", 16);
        } 
        else{
          lcd_disp_b2(0x40, *(pip+0));
          lcd_put_s(0x43, ".", 1);
          lcd_disp_b2(0x44, *(pip+1));
          lcd_put_s(0x47, ".", 1);
          lcd_disp_b2(0x48, *(pip+2));
          lcd_put_s(0x4b, ".", 1);
          lcd_disp_b2(0x4c, *(pip+3));
        }

        if(*(pip+4))
        {
          lcd_put_s(0x4f, "D", 1);
        }
        else
        {
          lcd_put_s(0x4f, "S", 1);
        }
      //}

      break;

    case MAIN_STATUS_DISP_TIME:  // TIME & DATE
      if(disp_shift_occurred_f)
      {
        lcd_put_s(0x00, "DATE        TIME", 16);
        lcd_put_s(0x40, "2008/ 2/ 4 xx:xx", 16);
        disp_shift_occurred_f = 0;
      }
      lcd_disp_b(0x40, *(ptime+0));
      lcd_disp_b(0x42, *(ptime+1));
      lcd_disp_b(0x45, *(ptime+2));
      lcd_disp_b(0x48, *(ptime+3));
      lcd_disp_b(0x4b, *(ptime+4));
      lcd_disp_b(0x4e, *(ptime+5));
      break;

    case MAIN_STATUS_DISP_CAPA:  // CAPA USAGE
      if(disp_shift_occurred_f)
      {
        lcd_put_s(0x00, "STORAGE CAPACITY", 16);
        lcd_put_s(0x40, "[USAGE:       %]", 16);
        disp_shift_occurred_f = 0;
      }
      if(hdd_usage_rate == 0xff)
      {
        lcd_put_s(0x4b, "---",3);
      }
      else
      {
        lcd_disp_b2(0x4b, hdd_usage_rate);
      }

      break;
    case MAIN_STATUS_DISP_RAID:  // RAID
      if(disp_shift_occurred_f){
        lcd_put_s(0x00, "RAID STATE:     ", 16);
        lcd_put_s(0x40, "[SYNC          ]", 16);
        disp_shift_occurred_f = 0;
      }
      /* for old ns2 fw */
      if (raidstate.vol == 0x00){
        lcd_put_s(0x45, " DONE     ", 10); break;
      }else if(raidstate.vol == 0x01){
        lcd_put_s(0x45, "ING       ", 10); break;
      }else
        ;

      switch(raidstate.vol & 0xf0){
        case RAID_STATUS_VOL_ALL:
          lcd_put_s(0xC, "ALL ", 4); break;
        case RAID_STATUS_VOL_SYS:
          lcd_put_s(0xC, "SYS ", 4); break;
        case RAID_STATUS_VOL_SWP:
          lcd_put_s(0xC, "SWP ", 4); break;
        case RAID_STATUS_VOL_USR:
          lcd_put_s(0xC, "VOL", 3); 
          temp[0] = (raidstate.vol % 0x10) + '0';
          temp[1] = 0x00;
          lcd_put_s(0xF, temp, 1);
          break;
        default : 
          lcd_put_s(0xC, "ALL ", 4); break;
      } 
      switch(raidstate.state){
        case RAID_STATUS_SYNC:
          lcd_put_s(0x41, "SYNC     :", 10); break;
        case RAID_STATUS_MIGRATE:
          lcd_put_s(0x41, "MIGRATE  :", 10); break;
        case RAID_STATUS_EXPAND:
          lcd_put_s(0x41, "EXPAND   :", 10); break;
        case RAID_STATUS_FORMAT:
          lcd_put_s(0x41, "FORMAT   :", 10); break;
        case RAID_STATUS_ERROR:
          lcd_put_s(0x41, "ERROR     ", 10); break;
        case RAID_STATUS_DEGRADE:
          lcd_put_s(0x41, "DEGRADE   ", 10); break;
        case RAID_STATUS_NOVOLUME:
          lcd_put_s(0x41, "No Volume ", 10); break;
        default : 
          lcd_put_s(0x41, "SYNC      ", 10); break;
      } 
      
      if((raidstate.state & 0xf0) == 0x10){
        lcd_disp_b2(0x4b, raidstate.percent);
        lcd_put_s(0x4e, "%", 1);
      }else{
        lcd_put_s(0x4b, "    ", 4);
      }

      break;

    case MAIN_STATUS_DISP_ERR:  // SYSTEM ERROR STATE
      if(disp_shift_occurred_f)
      {
        lcd_put_s(0x00, "SYSTEM STATE    ", 16);
        disp_shift_occurred_f = 0;
      }

      if(svc_code[0] || svc_code[1])
      {
        lcd_put_s(0x40, "[SVC_CODE:  _  ]", 16);
        lcd_disp_h(0x4a, svc_code[0]);
        lcd_disp_h(0x4d, svc_code[1]);
      }
      else
      {
        lcd_put_s(0x40, "[SVC_CODE:00_00]", 16);
      }
      break;
/*
    case MAIN_STATUS_DISP_FW_VER:  // FW Version
      if(disp_shift_occurred_f)
      {
        lcd_put_s(0x00, "*IO uCom Version", 16);
        lcd_put_s(0x40, "                ", 16);
        //lcd_disp_b(0x46, *(char*)(FW_VER_ADDR+0x00));
        //lcd_disp_b(0x48, *(char*)(FW_VER_ADDR+0x01));
        //lcd_disp_b(0x4a, *(char*)(FW_VER_ADDR+0x02));
        //lcd_disp_b(0x4c, *(char*)(FW_VER_ADDR+0x03));

        if(!power_fail_ccured_cnt)
        {
          ;	//lcd_disp_b(0x4e, *(char*)(FW_VER_ADDR+0x04));
        }
        else
        {
          lcd_disp_h(0x4e, (power_fail_ccured_cnt&0x0f) + 0xa0);
        }
        disp_shift_occurred_f = 0;
      }

      break;
*/
    case MAIN_STATUS_DISP_MN_FW_VER:  // Main FW Version
      if(disp_shift_occurred_f)
      {
        lcd_put_s(0x00, "*Main FW Version", 16);
        lcd_put_s(0x40, "                ", 16);
        lcd_put_s(0x42, (char*) mn_fw_ver, 12);
        disp_shift_occurred_f = 0;
      }

      break;

    case MAIN_STATUS_DISP_FAN_RPM:  // Fan RPM
      if(disp_shift_occurred_f)
      {
        lcd_put_s(0x00, "* Fan RPM       ", 16);
        lcd_put_s(0x40, "  [0000] RPM    ", 16);
        disp_shift_occurred_f = 0;
      }
      lcd_disp(0x43, fan_rpm_fb);

      break;
    }
    break;

  case MAIN_STATUS_MODE_IP:
    if(!main_disp_toggle_f)
      lcd_put_s(0, "*", 1);
    else
      lcd_put_s(0, " ", 1);

    switch(main_status)
    {
    case MAIN_STATUS_IP_ENTRY:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      lcd_put_s(0x01, " IP Setup ?    ", 15);
      break;

    case MAIN_STATUS_IP_DHCP_CF:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      lcd_put_s(0x01, " Use DHCP ?    ", 15);
      break;

    case MAIN_STATUS_IP_IP_SETUP:
      if(make_value_f){
        MakeValue(pip, &value_setup[0]);

        lcd_put_s(0x01, " IP Setup      ", 15);
        lcd_put_s(0x40, (char*) &value_setup[0], 16);
        lcd_cursor_on(0x01, 0x40+value_pos);

        make_value_f = 0;
      }else{
        lcd_cursor_on(0x00, 0x40+value_pos);
        lcd_data(value_setup[value_pos]);
        lcd_cursor_on(0x01, 0x40+value_pos);
      }
      break;

    case MAIN_STATUS_IP_NETMASK_SETUP:
      if(make_value_f){
        MakeValue(pnetmask, &value_setup[0]);

        lcd_put_s(0x00, "* NETMASK Setup ", 16);
        lcd_put_s(0x40, (char*) &value_setup[0], 16);

        lcd_cursor_on(0x01, 0x40+value_pos);

        make_value_f = 0;
      }else{
        lcd_cursor_on(0x00, 0x40+value_pos);
        lcd_data(value_setup[value_pos]);
        lcd_cursor_on(0x01, 0x40+value_pos);
      }
      break;

    case MAIN_STATUS_IP_GATEWAY_SETUP:
      if(make_value_f){
        gateway_s[0] = ip_s[0]&netmask_s[0];
        gateway_s[1] = ip_s[1]&netmask_s[1];
        gateway_s[2] = ip_s[2]&netmask_s[2];
        if(!gateway_s[2]) gateway_s[2] = 1;
        gateway_s[3] = ip_s[3]&netmask_s[3];
        if(!gateway_s[3]) gateway_s[3] = 1;
        MakeValue(pgateway_s, &value_setup[0]);

        lcd_put_s(0x00, "* GATEWAY Setup ", 16);
        lcd_put_s(0x40, (char*) &value_setup[0], 16);

        lcd_cursor_on(0x01, 0x40+value_pos);

        make_value_f = 0;
      }else{
        lcd_cursor_on(0x00, 0x40+value_pos);
        lcd_data(value_setup[value_pos]);
        lcd_cursor_on(0x01, 0x40+value_pos);
      }
      break;

    case MAIN_STATUS_IP_WAIT_SET:
      lcd_put_s(0x40, " Please Wait... ", 16);
      lcd_put_s(0x01, " IP setting now", 15);
      break;

    case MAIN_STATUS_PWD_WAIT_INIT:
      lcd_put_s(0x40, " Please Wait... ", 16);
      lcd_put_s(0x01, " Init Password ", 15);
      break;

    default:
      break;
    }

    break;

  case MAIN_STATUS_MODE_ODD_BCK:
    if(!main_disp_toggle_f)
      lcd_put_s(0, "*", 1);
    else
      lcd_put_s(0, " ", 1);

    switch(main_status)
    {
    case MAIN_STATUS_ODD_BCK_ENTRY:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      lcd_put_s(0x01, " ODD Backup ?  ", 15);
      break;
    case MAIN_STATUS_ODD_DATA_BCK_ENTRY:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      lcd_put_s(0x01, " [1] Data ?    ", 15);
      break;
    case MAIN_STATUS_ODD_ISO_BCK_ENTRY:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      lcd_put_s(0x01, " [2] Image ?   ", 15);
      break;
    case MAIN_STATUS_ODD_CANCEL_BCK_ENTRY:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      lcd_put_s(0x01, " [3] Cancel ?  ", 15);
      break;

    case MAIN_STATUS_ODD_BCK_PROG:
      lcd_put_s(0x01, " ODD Backup now", 15);
      if((prog_type_ext&0x80) == 0x80)
      {
        ;
/*
        if(prog_type_ext == 0x80)
        {
          lcd_put_s(0x40, " SCSI Open Error", 16);
        }
        else if(prog_type_ext == 0x81)
        {
          lcd_put_s(0x40, " Buffer Open Err", 16);
        }
        else if(prog_type_ext == 0x82)
        {
          lcd_put_s(0x40, " Check Drv State", 16);
        }
        else if(prog_type_ext == 0x83)
        {
          lcd_put_s(0x40, " Check Disc     ", 16);
        }
        else if(prog_type_ext == 0x84)
        {
          lcd_put_s(0x40, " Protected Disc ", 16);
        }
        else if(prog_type_ext == 0x85)
        {
          lcd_put_s(0x40, " Not Proper Disc", 16);
        }
        else if(prog_type_ext == 0x86)
        {
          lcd_put_s(0x40, " File Open Error", 16);
        }
        else if(prog_type_ext == 0x87)
        {
          lcd_put_s(0x40, " Disc Read Error", 16);
        }
        else if(prog_type_ext == 0x89)
        {
          lcd_put_s(0x40, " File Write Err ", 16);
        }
        else if(prog_type_ext == 0x8a)
        {
          lcd_put_s(0x40, " Close tray plz ", 16);
        }
        else if(prog_type_ext == 0x8b)
        {
          lcd_put_s(0x40, " Not Proper Disc", 16);
        }
        else
        {
          lcd_put_s(0x40, " Unknown Error  ", 16);
        }
*/
      }
      else
      {
        if(odd_cancel_f)
          lcd_put_s(0x40, "Backup Canceled ", 16);
        else{
          if((prog_rate) == 0x64)
            lcd_put_s(0x40, " Backup Done    ", 16);
          else{
            lcd_put_s(0x40, " Processing 000%", 16);
            lcd_disp_b2(0x4c, prog_rate);
            if((prog_rate&0x07) == 0x05) lcd_cursor_on(0x00, 0x00);
          }
        }
      }

      break;

    case MAIN_STATUS_ODD_BCK_CANCEL:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      if(!main_disp_toggle_f)
        lcd_put_s(0, "*", 1);
      else
        lcd_put_s(0, " ", 1);
      lcd_put_s(0x01, " Cancel backup?", 15);
      break;

    default:
      break;
    }

    break;

  case MAIN_STATUS_MODE_ODD_BURN:

    switch(main_status)
    {
    case MAIN_STATUS_ODD_BN_ENTRY:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);

      if(!main_disp_toggle_f)
      {
        lcd_put_s(0, "*", 1);
      }
      else
      {
        lcd_put_s(0, " ", 1);
      }
      lcd_put_s(0x01, " ODD Burn ?    ", 15);
      break;

    case MAIN_STATUS_ODD_BN_FILE_SEL:
      if(valid_file_name_f)
      {
        if(!main_disp_toggle_f)
        {
          lcd_put_s(0x00, "* Select", 8);
        }
        else
        {
          lcd_put_s(0x00, "  Select", 8);
        }
        if(burn_list_toggle_f) lcd_put_s(0x08, "<", 1);
        else lcd_put_s(0x08, ">", 1);

        lcd_put_s(0x09, "[", 1);

        if(!total_file_num) // pch_081027
        {
          lcd_disp_b(0x0a, 0);
        }
        else
        {
          lcd_disp_b(0x0a, file_num);
        }
        lcd_put_s(0x0c, "/", 1);
        lcd_disp_b(0x0d, total_file_num);
        lcd_put_s(0x0f, "]", 1);

        lcd_command(0x80|0x40);
        if(file_name_size <= 16)
        {
          lcd_put_s(0x40, (char*)&file_name[0], file_name_size);
          lcd_fill_blank(0x40+file_name_size, 16-file_name_size);
        }
        else
        {
          if(file_name_size > file_name_shift)
          {
            if((file_name_size - file_name_shift) >= 16)
            {
              lcd_put_s(0x40, (char*)&file_name[file_name_shift], 16);
            }
            else
            {
              lcd_put_s(0x40, (char*)&file_name[file_name_shift], file_name_size - file_name_shift);

              lcd_len = (file_name_size - file_name_shift);
              if(lcd_len >= 12)
              {
                lcd_fill_blank(0x40+lcd_len, 16-lcd_len);
              }
              else
              {
                lcd_fill_blank(0x40+lcd_len, 4);
                lcd_put_s(0x44+lcd_len, (char*)&file_name[0], 12 - lcd_len);
              }
            }
          }
          else
          {
            lcd_fill_blank(0x40, file_name_size+4-file_name_shift);
            lcd_put_s(0x40+file_name_size+4-file_name_shift, (char*)&file_name[0], 16 - (file_name_size+4-file_name_shift));
          }

        }
      }
      else
      {
        if(!main_disp_toggle_f)
        {
          lcd_put_s(0x00, "* Select", 8);
        }
        else
        {
          lcd_put_s(0x00, "  Select", 8);
        }
        if(burn_list_toggle_f) lcd_put_s(0x08, "<", 1);
        else lcd_put_s(0x08, ">", 1);

        lcd_put_s(0x09, "[  /  ]", 7);
        lcd_put_s(0x40, "                ", 16);
      }
      break;

    case MAIN_STATUS_ODD_BN_RW_CF:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      if(!main_disp_toggle_f)
      {
        lcd_put_s(0, "*", 1);
      }
      else
      {
        lcd_put_s(0, " ", 1);
      }
      lcd_put_s(0x01, "Overwrite Disc?", 15);
      break;

    case MAIN_STATUS_ODD_BN_PROG:
      if(!main_disp_toggle_f)
      {
        lcd_put_s(0, "*", 1);
      }
      else
      {
        lcd_put_s(0, " ", 1);
      }
      lcd_put_s(0x01, " ODD Burn now  ", 15);
      if((prog_type_ext&0x80) == 0x80)
      {
        ;
/*
        if(prog_type_ext == 0x80)
        {
          lcd_put_s(0x40, " SCSI Open Error", 16);
        }
        else if(prog_type_ext == 0x81)
        {
          lcd_put_s(0x40, " Buffer Open Err", 16);
        }
        else if(prog_type_ext == 0x82)
        {
          lcd_put_s(0x40, " Check Drv State", 16);
        }
        else if(prog_type_ext == 0x83)
        {
          lcd_put_s(0x40, " Not Blank Disc ", 16);
        }
        else if(prog_type_ext == 0x84)
        {
          lcd_put_s(0x40, " No Enough Space", 16);
        }
        else if(prog_type_ext == 0x85)
        {
          lcd_put_s(0x40, " Invalid Cue    ", 16);
        }
        else if(prog_type_ext == 0x86)
        {
          lcd_put_s(0x40, " File Open Error", 16);
        }
        else if(prog_type_ext == 0x87)
        {
          lcd_put_s(0x40, " Disc Write Err ", 16);
        }
        else if(prog_type_ext == 0x88)
        {
          lcd_put_s(0x40, " File Access Err", 16);
        }
        else if(prog_type_ext == 0x89)
        {
          lcd_put_s(0x40, " Format Error   ", 16);
        }
        else if(prog_type_ext == 0x8a)
        {
          lcd_put_s(0x40, " Close tray plz ", 16);
        }
        else if(prog_type_ext == 0x8b)
        {
          lcd_put_s(0x40, " Not Proper Disc", 16);
        }
        else
        {
          lcd_put_s(0x40, " Unknown Error  ", 16);
        }
*/
      }
      else if(prog_type_ext == 0x02)
      {
        lcd_put_s(0x40, "Session Close...", 16);
      }
      else if(prog_type_ext == 0x01)
      {
        lcd_put_s(0x40, "Track Close...  ", 16);
      }
      else if(prog_type_ext == 0x04)
      {
        lcd_put_s(0x40, "Initialize....  ", 16);
      }
      else
      {
        if(odd_cancel_f)
        {
          lcd_put_s(0x40, "Burn Canceled   ", 16);
        }
        else
        {
          if((prog_rate) == 0x64)
            lcd_put_s(0x40, " Burn Done      ", 16);
          else{
            lcd_put_s(0x40, " Processing 000%", 16);
            lcd_disp_b2(0x4c, prog_rate);
            if((prog_rate&0x07) == 0x05) lcd_cursor_on(0x00, 0x00);
          }
        }
      }
      break;

    case MAIN_STATUS_ODD_BN_CANCEL:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      if(!main_disp_toggle_f)
      {
        lcd_put_s(0, "*", 1);
      }
      else
      {
        lcd_put_s(0, " ", 1);
      }
      lcd_put_s(0x01, " Cancel burn?  ", 15);
      break;

    default:
      break;
    }

    break;

  case MAIN_STATUS_MODE_USB_BCK:
    if(!main_disp_toggle_f)
      lcd_put_s(0, "*", 1);
    else
      lcd_put_s(0, " ", 1);

    switch(main_status)
    {
    case MAIN_STATUS_USB_BCK_ENTRY:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      lcd_put_s(0x01, " USB Backup ?  ", 15);
      break;

    case MAIN_STATUS_USB_BCK_PROG:
      if(usb_cancel_f){
        lcd_put_s(0x40, " Backup Canceled", 16);
      }else{
        if((prog_rate) == 0x64)
          lcd_put_s(0x40, " Backup Done    ", 16);
        else{
          lcd_put_s(0x40, " Processing 000%", 16);
          lcd_disp_b2(0x4c, prog_rate);
          if((prog_rate&0x07) == 0x05) lcd_cursor_on(0x00, 0x00);
        }
      }
      lcd_put_s(0x01, " USB Backup now", 15);
      break;

    case MAIN_STATUS_USB_BCK_CANCEL:
      lcd_put_s(0x40, " M:No   S:Yes   ", 16);
      lcd_put_s(0x01, " Cancel backup?", 15);
      break;

    default:
      break;
    }

    break;

  case MAIN_STATUS_BOOT_ODD:
    switch(main_status)
    {
    case MAIN_STATUS_BOOT_ODD:
    case MAIN_STATUS_BOOT_ODD_DRAID:
    case MAIN_STATUS_BOOT_ODD_URAID:
      if(!main_disp_toggle_f)
      {
        lcd_put_s(0, "*", 1);
      }
      else
      {
        lcd_put_s(0, " ", 1);
      }
      if(main_status == MAIN_STATUS_BOOT_ODD)
      {
        lcd_put_s(0x01, "ODD Boot - MAIN", 15);
      }
      else if(main_status == MAIN_STATUS_BOOT_ODD_DRAID)
      {
        lcd_put_s(0x01, "Default Setup !", 15);
      }
      else if(main_status == MAIN_STATUS_BOOT_ODD_URAID)
      {
        lcd_put_s(0x01, "Custom Setup ! ", 15);
      }

      if((main_status == MAIN_STATUS_BOOT_ODD)
        || (main_status == MAIN_STATUS_BOOT_ODD_DRAID)
        || (main_status == MAIN_STATUS_BOOT_ODD_URAID))
      {
        if(valid_err_msg_f || persist_err_msg_f)
        {
          if(err_msg_size <= 16)
          {
            lcd_put_s(0x40, (char*)&err_msg[0], err_msg_size);
            lcd_fill_blank(0x40+err_msg_size, 16-err_msg_size);
          }
          else
          {
            if(err_msg_size > err_msg_shift)
            {
              if((err_msg_size - err_msg_shift) >= 16)
              {
                lcd_put_s(0x40, (char*)&err_msg[err_msg_shift], 16);  //err_msg_size);
              }
              else
              {
                lcd_len = err_msg_size - err_msg_shift;
                lcd_put_s(0x40, (char*)&err_msg[err_msg_shift], lcd_len);

                if(lcd_len >= 12)
                {
                  lcd_fill_blank(0x40+lcd_len, 16-lcd_len);
                }
                else
                {
                  lcd_fill_blank(0x40+lcd_len, 4);
                  lcd_put_s(0x44+lcd_len, (char*)&err_msg[0], 12 - lcd_len);
                }
              }
            }
            else
            {
              lcd_fill_blank(0x40, err_msg_size+4-err_msg_shift);
              lcd_put_s(0x40+err_msg_size+4-err_msg_shift, (char*)&err_msg[0], 16 - (err_msg_size+4-err_msg_shift));
            }
          }
        }
        else
        {
          if(main_status == MAIN_STATUS_BOOT_ODD)
          {
            lcd_put_s(0x40, " Processing...  ", 16);
          }
          else if(main_status == MAIN_STATUS_BOOT_ODD_DRAID)
          {
            lcd_put_s(0x40, " Processing...  ", 16);
          }
          else if(main_status == MAIN_STATUS_BOOT_ODD_URAID)
          {
            lcd_put_s(0x40, " Processing...  ", 16);
          }

        }
      }
      break;

    case MAIN_STATUS_BOOT_ODD_SETUP:
      if(!main_disp_toggle_f)
      {
        lcd_put_s(0, "*", 1);
      }
      else
      {
        lcd_put_s(0, " ", 1);
      }
      lcd_put_s(0x01, "S:Default Setup", 15);
      lcd_put_s(0x40, " M:Custom Setup ", 16);

      break;

    default:
      break;
    }
    break;

  case MAIN_STATUS_BOOT_AGING:
    if(!main_disp_toggle_f)
    {
      lcd_put_s(0, "*", 1);
    }
    else
    {
      lcd_put_s(0, " ", 1);
    }
    lcd_put_s(0x01, "Aging Mode ", 11);
    if(aging_status) lcd_put_s(0x0c, "RUN ", 4);
    else lcd_put_s(0x0c, "STOP", 4);

    if(main_status == MAIN_STATUS_BOOT_AGING)
    {
      if(valid_err_msg_f || persist_err_msg_f)
      {
        if(err_msg_size <= 16)
        {
          lcd_put_s(0x40, (char*)&err_msg[0], err_msg_size);
          lcd_fill_blank(0x40+err_msg_size, 16-err_msg_size);
        }
        else
        {
          if(err_msg_size > err_msg_shift)
          {
            if((err_msg_size - err_msg_shift) >= 16)
            {
              lcd_put_s(0x40, (char*)&err_msg[err_msg_shift], 16);  //err_msg_size);
            }
            else
            {
              lcd_len = err_msg_size - err_msg_shift;
              lcd_put_s(0x40, (char*)&err_msg[err_msg_shift], lcd_len);

              if(lcd_len >= 12)
              {
                lcd_fill_blank(0x40+lcd_len, 16-lcd_len);
              }
              else
              {
                lcd_fill_blank(0x40+lcd_len, 4);
                lcd_put_s(0x44+lcd_len, (char*)&err_msg[0], 12 - lcd_len);
              }
            }
          }
          else
          {
            lcd_fill_blank(0x40, err_msg_size+4-err_msg_shift);
            lcd_put_s(0x40+err_msg_size+4-err_msg_shift, (char*)&err_msg[0], 16 - (err_msg_size+4-err_msg_shift));
          }
        }
      }
      else
      {
        lcd_put_s(0x40, " Processing...  ", 16);
      }
    }

    break;

  case MAIN_STATUS_SHUTDOWN:
    if(!main_disp_toggle_f)
    {
      lcd_put_s(0, "*", 1);
    }
    else
    {
      lcd_put_s(0, " ", 1);
    }
    lcd_put_s(0x01, "Shutting Down  ", 15);
    lcd_put_s(0x40, " Please Wait... ", 16);

    break;

  default:
    break;
  }
  disp_request_f = 0;
}

void com_make_rd_data(unsigned char raddr)
{
  unsigned char bTmp1=0, bTmp2=0;

  switch(raddr)
  {
    case 0x00:  // don't set any data except key_state
      *(pebuf+0) = 0x00;
      *(pebuf+1) = key_state;
      *(pebuf+2) = (key_state&0x10) ? file_num : 0x00;
      if(key_state&0x40)  // cancel key
      {
        if(main_status == MAIN_STATUS_ODD_BCK_PROG)
        {
          bTmp1 = 0x01;
        }
        else if(main_status == MAIN_STATUS_ODD_BN_PROG)
        {
          bTmp1 = 0x02;
        }
        else if(main_status == MAIN_STATUS_USB_BCK_PROG)
        {
          bTmp1 = 0x03;
        }
        else
        {
          bTmp1 = 0x00;
        }
      }
      *(pebuf+3) = bTmp1;
      *(pebuf+4) = 0x00;
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x01:
      *(pebuf+0) = 0x01;
      *(pebuf+1) = (int_rsn&0x80) ? 0 : 1;
      *(pebuf+2) = int_rsn;
      if(int_rsn)
      {
        *(pebuf+3) = 0x00;
        *(pebuf+4) = 0x00;
        *(pebuf+5) = 0x00;
        *(pebuf+6) = 0x00;
      }
      else
      {
        *(pebuf+3) = key_state;
        *(pebuf+4) = (key_state&0x10) ? file_num : 0x00;
        if(key_state&0x40)
        {
          if(main_status == MAIN_STATUS_ODD_BCK_PROG)
          {
            bTmp1 = 0x01;
          }
          else if(main_status == MAIN_STATUS_ODD_BN_PROG)
          {
            bTmp1 = 0x02;
          }
          else if((main_status&0xf0) == MAIN_STATUS_USB_BCK_ENTRY)
          {
            bTmp1 = 0x03;
          }
          else
          {
            bTmp1 = 0x00;
          }
        }
        *(pebuf+5) = bTmp1;
        *(pebuf+6) = 0x00;
      }
      break;

    case 0x02:
      *(pebuf+0) = 0x02;
      //*(pebuf+1) = *(unsigned char*)(FW_VER_ADDR);
      //*(pebuf+2) = *(unsigned char*)(FW_VER_ADDR+1);
      //*(pebuf+3) = *(unsigned char*)(FW_VER_ADDR+2);
      //*(pebuf+4) = *(unsigned char*)(FW_VER_ADDR+3);
      //*(pebuf+5) = *(unsigned char*)(FW_VER_ADDR+4);
      //*(pebuf+6) = *(unsigned char*)(FW_VER_ADDR+5);
      break;

    case 0x03:
      *(pebuf+0) = 0x03;
      *(pebuf+1) = *(pip_s+0);
      *(pebuf+2) = *(pip_s+1);
      *(pebuf+3) = *(pip_s+2);
      *(pebuf+4) = *(pip_s+3);
      *(pebuf+5) = *(pip_s+4);
      *(pebuf+6) = 0x00;
      break;

    case 0x04:
      *(pebuf+0) = 0x04;
      *(pebuf+1) = *(pnetmask_s+0);
      *(pebuf+2) = *(pnetmask_s+1);
      *(pebuf+3) = *(pnetmask_s+2);
      *(pebuf+4) = *(pnetmask_s+3);
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x05:
      *(pebuf+0) = 0x05;
      *(pebuf+1) = *(pgateway_s+0);
      *(pebuf+2) = *(pgateway_s+1);
      *(pebuf+3) = *(pgateway_s+2);
      *(pebuf+4) = *(pgateway_s+3);
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x06:
      *(pebuf+0) = 0x06;
      *(pebuf+1) = file_num;
      *(pebuf+2) = 0x00;
      *(pebuf+3) = 0x00;
      *(pebuf+4) = 0x00;
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x07:
      *(pebuf+0) = 0x07;
      *(pebuf+1) = 0x00;
      *(pebuf+2) = serial[0];
      *(pebuf+3) = serial[1];
      *(pebuf+4) = serial[2];
      *(pebuf+5) = serial[3];
      *(pebuf+6) = 0x00;
      break;

    case 0x08:
      *(pebuf+0) = 0x08;
      *(pebuf+1) = 0x01;
      *(pebuf+2) = serial[4];
      *(pebuf+3) = serial[5];
      *(pebuf+4) = serial[6];
      *(pebuf+5) = serial[7];
      *(pebuf+6) = 0x00;
      break;

    case 0x09:
      *(pebuf+0) = 0x09;
      *(pebuf+1) = boot_type;
      *(pebuf+2) = (boot_type == 0x02) ? aging_status : setup_type;
      *(pebuf+3) = 0x00;
      *(pebuf+4) = 0x00;
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x0a:
      *(pebuf+0) = 0x0a;
      *(pebuf+1) = test_type;
      *(pebuf+2) = btest_run_stop;
      *(pebuf+3) = 0x00;
      *(pebuf+4) = 0x00;
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x0b:
      *(pebuf+0) = 0x0b;
      *(pebuf+1) = (unsigned char) (fan_rpm_fb >> 8)&0x00ff;
      *(pebuf+2) = (unsigned char) (fan_rpm_fb >> 0)&0x00ff;
      *(pebuf+3) = 0x00;
      *(pebuf+4) = 0x00;
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x0c:
      *(pebuf+0) = 0x0c;
      //*(pebuf+1) = (unsigned char) (wAdTmp0 >> 8)&0x00ff;
      //*(pebuf+2) = (unsigned char) (wAdTmp0 >> 0)&0x00ff;
      //*(pebuf+3) = (unsigned char) (wAdTmp1 >> 8)&0x00ff;
      //*(pebuf+4) = (unsigned char) (wAdTmp1 >> 0)&0x00ff;
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x0d:
      *(pebuf+0) = 0x0d;
      *(pebuf+1) = 0x01;
      *(pebuf+2) = 0x00;
      *(pebuf+3) = 0x00;
      *(pebuf+4) = 0x00;
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x7e:
      *(pebuf) = 0x7e;
      *(pebuf+1) = *(plbuf+0);
      *(pebuf+2) = *(plbuf+1);
      *(pebuf+3) = *(plbuf+2);
      *(pebuf+4) = 0x00;
      *(pebuf+5) = 0x00;
      *(pebuf+6) = 0x00;
      break;

    case 0x7f:
      *(pebuf+0) = 0x7f;
      *(pebuf+1) = 'L';
      *(pebuf+2) = 'G';
      *(pebuf+3) = 'D';
      *(pebuf+4) = 'S';
      *(pebuf+5) = '3';
      *(pebuf+6) = 'S';
      break;

    default:
      break;
  }
  bTmp1 = 0x00;
  for(bTmp2=0; bTmp2 <= (DATA_FRM_NUM-2); bTmp2++) bTmp1 ^= *(pebuf+bTmp2);
  *(pebuf+DATA_FRM_NUM-1) = bTmp1;
}

void com_processing(struct nashal_data *data)
{
  unsigned char bTmp1=0, bTmp2=0;	//, bTmp3=0;

  //dprintk("%s is called\n", __FUNCTION__);

  pdbuf = &dbuf[0];
  plbuf = &lbuf[0];

  if(!com_flag)
    return;

	//dprintk("Comm write data : %02x %02x %02x %02x %02x %02x %02x %02x\n",
	//	pdbuf[0], pdbuf[1], pdbuf[2], pdbuf[3], pdbuf[4], pdbuf[5], pdbuf[6], pdbuf[7]);

  if(long_data_f)
  {
    for(bTmp2=0; bTmp2 <= 3; bTmp2++) bTmp1 ^= *(pdbuf + bTmp2);
    if(*pdbuf == 0xd0)
    {
      for(bTmp2=0; bTmp2 < (*(pdbuf + 1) - 2); bTmp2++)
      {
        bTmp1 ^= wc_msg[bTmp2];
      }
    }
    else if((*pdbuf == 0xd1) || (*pdbuf == 0xd2) || (*pdbuf == 0xd3))
    {
      for(bTmp2=0; bTmp2 < (*(pdbuf + 1) - 2); bTmp2++) bTmp1 ^= wc_msg[bTmp2];
    }
  }
  else
  {
    for(bTmp2=0; bTmp2 <= (DATA_FRM_NUM-2); bTmp2++) bTmp1 ^= *(pdbuf + bTmp2);
  }

  if(bTmp1 == *(pdbuf + DATA_FRM_NUM - 1))
  {
    if((*pdbuf) != 0x80)
    {
      *plbuf = *pdbuf;
      *(plbuf+1) = 0x01;
      *(plbuf+2) = *(pdbuf + DATA_FRM_NUM - 1);
    }
		//dprintk("Comm write_data : %02x %02x %02x %02x %02x %02x %02x %02x\n",
		//	pdbuf[0], pdbuf[1], pdbuf[2], pdbuf[3], pdbuf[4], pdbuf[5], pdbuf[6], pdbuf[7]);
    switch(*pdbuf)
    {
      case 0x80:
        com_make_rd_data(*(pdbuf+1));
        break;

      case 0x81:  // server_name
        *(pserver_name + 0) = (*(pdbuf + 1) >= 0x20) ? *(pdbuf + 1) : 0x20;
        *(pserver_name + 1) = (*(pdbuf + 2) >= 0x20) ? *(pdbuf + 2) : 0x20;
        *(pserver_name + 2) = (*(pdbuf + 3) >= 0x20) ? *(pdbuf + 3) : 0x20;
        *(pserver_name + 3) = (*(pdbuf + 4) >= 0x20) ? *(pdbuf + 4) : 0x20;
        *(pserver_name + 4) = (*(pdbuf + 5) >= 0x20) ? *(pdbuf + 5) : 0x20;
        *(pserver_name + 5) = (*(pdbuf + 6) >= 0x20) ? *(pdbuf + 6) : 0x20;
        break;

      case 0x82:  // server_name
        *(pserver_name + 6) = (*(pdbuf + 1) >= 0x20) ? *(pdbuf + 1) : 0x20;
        *(pserver_name + 7) = (*(pdbuf + 2) >= 0x20) ? *(pdbuf + 2) : 0x20;
        *(pserver_name + 8) = (*(pdbuf + 3) >= 0x20) ? *(pdbuf + 3) : 0x20;
        *(pserver_name + 9) = (*(pdbuf + 4) >= 0x20) ? *(pdbuf + 4) : 0x20;
        *(pserver_name + 10) = (*(pdbuf + 5) >= 0x20) ? *(pdbuf + 5) : 0x20;
        *(pserver_name + 11) = (*(pdbuf + 6) >= 0x20) ? *(pdbuf + 6) : 0x20;
        break;

      case 0x83:  // ip
        if((main_status == MAIN_STATUS_IP_GATEWAY_SETUP)
          || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
          || ((main_status == MAIN_STATUS_IP_IP_SETUP) && value_pos))
        {
          *(pip + 0) = *(pdbuf + 1);
          *(pip + 1) = *(pdbuf + 2);
          *(pip + 2) = *(pdbuf + 3);
          *(pip + 3) = *(pdbuf + 4);
          *(pip + 4) = *(pdbuf + 5);
        }
        else
        {
          *(pip_s + 0) = *(pip + 0) = *(pdbuf + 1);
          *(pip_s + 1) = *(pip + 1) = *(pdbuf + 2);
          *(pip_s + 2) = *(pip + 2) = *(pdbuf + 3);
          *(pip_s + 3) = *(pip + 3) = *(pdbuf + 4);
          *(pip_s + 4) = *(pip + 4) = *(pdbuf + 5);

          valid_ip_f = 1;
          disp_request_f = 1; make_value_f = 1;
          if(main_status == MAIN_STATUS_IP_WAIT_SET)
          {
            wc_msg_shift = wc_msg_disp_cnt = 0;
            main_status = MAIN_STATUS_MAIN;
          }
        }
        break;

      case 0x84:  // time
        *(ptime + 0) = *(pdbuf + 1);
        *(ptime + 1) = *(pdbuf + 2);
        *(ptime + 2) = *(pdbuf + 3);
        *(ptime + 3) = *(pdbuf + 4);
        *(ptime + 4) = *(pdbuf + 5);
        *(ptime + 5) = *(pdbuf + 6);
        disp_request_f = 1;
        break;

      case 0x85:  // storage usage
        hdd_usage_rate = *(pdbuf + 1);
        disp_request_f = 1;
        break;

      case 0x86:  // main_state
        if(*(pdbuf + 1) == 0x00)  // SYS_STATE
        {
          // valid_err_msg_f = 0;
          persist_err_msg_f = 0;
          err_code = 0;
          //key_idle_cnt = KEY_IDLE_MAX_CNT;
          key_idle_cnt = LCDMSG_TIMEOUT(KEY_IDLE_MAX_CNT,data->lcd[0].timeout[0]);

          if(*(pdbuf + 2) == 0x01)  // SYS_STATE_BOOT_MAIN
          {
            if(main_status == MAIN_STATUS_BOOT)
            {
              wc_msg_shift = wc_msg_disp_cnt = 0;
              main_status = MAIN_STATUS_MAIN;
              disp_request_f = 1;
            }
            else if(type_0_seq == 8)
            {
              if(boot_wait_cnt > 200)
                type_0_seq++;
            }
          }
          else if(*(pdbuf + 2) == 0x02)  // SYS_STATE_MAIN_SHUTDOWN
          {
            if(((main_status&0xf0) == MAIN_STATUS_MAIN)
              || ((main_status&0xf0) == MAIN_STATUS_BOOT_ODD))
            {
              main_status = MAIN_STATUS_SHUTDOWN;
              disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT;
              disp_request_f = 1;
            }
          }
          else if(*(pdbuf + 2) == 0x03)  // SYS_STATE_SHUTDOWN_WAIT
          {
            if(((main_status&0xf0) == MAIN_STATUS_SHUTDOWN)
              || ((main_status&0xf0) == MAIN_STATUS_MAIN)
              || ((main_status&0xf0) == MAIN_STATUS_BOOT_ODD))  // For debugging purpose
            {
              main_status = MAIN_STATUS_WAIT;
              ;	//power_down_seq();
              boot_wait_cnt = 0; boot_to_cnt = 0;
              key_state = 0;
              ;	//fan_on(0);
              disp_return_to_main_cnt = 0;
              disp_request_f = 1;
            }
            else if(type_0_seq == 2)
            {
              type_0_seq++;
              key_state = 0;
            }
          }
          else if(*(pdbuf + 2) == 0x04)   // SYS_STATE_BOOT_ODD
          {
            if(main_status == MAIN_STATUS_BOOT)
            {
              main_status = MAIN_STATUS_BOOT_ODD;
              disp_request_f = 1;
            }
          }
          else if(*(pdbuf + 2) == 0x05)   // SYS_STATE_BOOT_ODD
          {
            if(main_status == MAIN_STATUS_BOOT_ODD)
            {
              main_status = MAIN_STATUS_BOOT_ODD_SETUP;
              disp_request_f = 1;
            }
          }
          else if(*(pdbuf + 2) == 0x06)   // SYS_STATE_BOOT_AGING
          {
            if(main_status == MAIN_STATUS_BOOT)
            {
              main_status = MAIN_STATUS_BOOT_AGING;
              aging_status = 1;
              disp_request_f = 1;
            }
          }
          else if(*(pdbuf + 2) == 0x07)   // JUMP_TO_ANY_STATE
          {
            if(main_status == MAIN_STATUS_MAIN)
            {
              if(*(pdbuf + 3) == 0x00)
              {
                main_status = MAIN_STATUS_USB_BCK_ENTRY;
  	            disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT;
                disp_request_f = 1;
              }
            }
          }
          else if(*(pdbuf + 2) == 0x08)   // SYS_STATE_BOOT
          {
            if((main_status == MAIN_STATUS_BOOT_ODD_DRAID) || (main_status == MAIN_STATUS_BOOT_ODD_URAID))
            {
              main_status = MAIN_STATUS_BOOT;
              boot_type = 0;  // notmal boot
              setup_type = 0xff;
              disp_request_f = 1;
            }
          }
        }
        else
        {
          //key_idle_cnt = KEY_IDLE_MAX_CNT;
          key_idle_cnt = LCDMSG_TIMEOUT(KEY_IDLE_MAX_CNT,data->lcd[0].timeout[0]);

          if(*(pdbuf + 1) == 0x01)   // MNT_STATE
          {
            valid_err_msg_f = 0;
            persist_err_msg_f = 0;
            err_code = 0;

            backup_status = burn_status = 0;
            if(mnt_status != *(pdbuf + 2))
            {
              overwrite_warn_f = 0;
              mnt_status = *(pdbuf + 2);
              disp_request_f = 1;
            }
            mnt_dev = *(pdbuf + 3);
          }
          else if(*(pdbuf + 1) == 0x02)   // BACKUP_STATE
          {
            mnt_status = burn_status = 0;
            {
              backup_status = *(pdbuf + 2);
              if(backup_status == 0x02)
              {
                // Work around for Displaying progress status
                //disp_return_to_main_cnt = 20;
                wc_msg_shift = wc_msg_disp_cnt = 0;
                main_status = MAIN_STATUS_MAIN;
              }
              disp_request_f = 1;
            }
          }
          else if(*(pdbuf + 1) == 0x03)   // BURN_STATE
          {
            mnt_status = backup_status = 0;
            {
              burn_status = *(pdbuf + 2);
              if(burn_status == 0x02)
              {
                //disp_return_to_main_cnt = 20;
                wc_msg_shift = wc_msg_disp_cnt = 0;
                main_status = MAIN_STATUS_MAIN;
              }
              disp_request_f = 1;
            }
          }
          else if(*(pdbuf + 1) == 0x04)   // BURN_STATE
          {
            if(main_status == MAIN_STATUS_PWD_WAIT_INIT)
            {
              wc_msg_shift = wc_msg_disp_cnt = 0;
              main_status = MAIN_STATUS_MAIN;
            }
          }
          else if(*(pdbuf + 1) == 0x05)
          {
            wc_msg_shift = wc_msg_disp_cnt = 0;
            main_status = MAIN_STATUS_MAIN;
            disp_request_f = 1;
          }
          else if(*(pdbuf + 1) == 0x06)
          {
          	make_value_f = 1;
          	disp_shift_occurred_f = 1;
            disp_request_f = 1;
          }

        }
        break;

      case 0x87:  // error code
        svc_code[0] = *(pdbuf + 1);
        svc_code[1] = *(pdbuf + 2);
        disp_request_f = 1;
        break;

      case 0x88:  // raid state
        raidstate.vol=*(pdbuf + 1);
        raidstate.state=*(pdbuf + 2);
        raidstate.percent=*(pdbuf + 3);
        disp_request_f = 1;
        break;

      case 0x89:  // led
        bTmp2 = *(pdbuf + 1);
        if(!bTmp2)
        {
          serial[0] = *(pdbuf + 2);
          serial[1] = *(pdbuf + 3);
          serial[2] = *(pdbuf + 4);
          serial[3] = *(pdbuf + 5);
        }
        else
        {
          serial[4] = *(pdbuf + 2);
          serial[5] = *(pdbuf + 3);
          serial[6] = *(pdbuf + 4);
          serial[7] = *(pdbuf + 5);
        }
        break;

      case 0x8a:
        for(bTmp2=0; bTmp2 < MAX_MSG_SIZE; bTmp2++)
        {
          bg_msg[bTmp2] = 0x00;
        }
        if(*(pdbuf + 1))
        {
          wc_msg_size = sizeof(str_welcome_dvd) - 1;
          memcpy(bg_msg, (unsigned char *) str_welcome_dvd, wc_msg_size);
        }
        else
        {
          wc_msg_size = strlen(str_welcome) - 1;
          memcpy(bg_msg, (unsigned char *) str_welcome, wc_msg_size);
        }
        wc_msg_shift = wc_msg_disp_cnt = 0;
        bg_err_msg_f = 0;
        disp_request_f = 1;
        break;

      case 0x8b:  // lcd bright
        if(*(pdbuf + 1) == 1){
          lcd_bl_full = *(pdbuf + 2);
  	      if(key_idle_cnt || persist_err_msg_f)
		        lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_FULL]);
        }else if(*(pdbuf + 1) == 2){
          lcd_bl_full = LCD_BL_FULL;
  	      if(key_idle_cnt || persist_err_msg_f)
		        lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_FULL]);
        }
        if(*(pdbuf + 3) == 1)
          lcd_bl_half = *(pdbuf + 4);
        else if(*(pdbuf + 3) == 2)
          lcd_bl_half = LCD_BL_HALF;
        break;

      case 0x8c:  // main fw version (rear)
        *(mn_fw_ver + 6) = (*(pdbuf + 1) >= 0x20) ? *(pdbuf + 1) : 0x20;
        *(mn_fw_ver + 7) = (*(pdbuf + 2) >= 0x20) ? *(pdbuf + 2) : 0x20;
        *(mn_fw_ver + 8) = (*(pdbuf + 3) >= 0x20) ? *(pdbuf + 3) : 0x20;
        *(mn_fw_ver + 9) = (*(pdbuf + 4) >= 0x20) ? *(pdbuf + 4) : 0x20;
        *(mn_fw_ver + 10) = (*(pdbuf + 5) >= 0x20) ? *(pdbuf + 5) : 0x20;
        *(mn_fw_ver + 11) = (*(pdbuf + 6) >= 0x20) ? *(pdbuf + 6) : 0x20;
        break;

      case 0x8d:  // main fw version (front)
        *(mn_fw_ver + 0) = (*(pdbuf + 1) >= 0x20) ? *(pdbuf + 1) : 0x20;
        *(mn_fw_ver + 1) = (*(pdbuf + 2) >= 0x20) ? *(pdbuf + 2) : 0x20;
        *(mn_fw_ver + 2) = (*(pdbuf + 3) >= 0x20) ? *(pdbuf + 3) : 0x20;
        *(mn_fw_ver + 3) = (*(pdbuf + 4) >= 0x20) ? *(pdbuf + 4) : 0x20;
        *(mn_fw_ver + 4) = (*(pdbuf + 5) >= 0x20) ? *(pdbuf + 5) : 0x20;
        *(mn_fw_ver + 5) = (*(pdbuf + 6) >= 0x20) ? *(pdbuf + 6) : 0x20;
        disp_request_f = 1;
        break;

      case 0x8e:  // led
        bTmp2 = *(pdbuf + 1);

        if(bTmp2)
        {
          ;	//led_set(1, (bTmp2>>0)&0x01);
          ;	//led_set(2, (bTmp2>>1)&0x01);
          ;	//led_set(3, (bTmp2>>2)&0x01);
          ;	//led_set(4, (bTmp2>>3)&0x01);
        }
        else
        {
          ;	//led_set(1, 0); led_set(2, 0); led_set(3, 0); led_set(4, 0);
        }
        break;

      case 0x8f:  // fan rpm
        fan_rpm = *(pdbuf + 1);
        ;	//CR50 = fan_rpm;
        break;

      case 0x90:  // title_name
        *(ptitle_name + 0) = (*(pdbuf + 1) >= 0x20) ? *(pdbuf + 1) : 0x20;
        *(ptitle_name + 1) = (*(pdbuf + 2) >= 0x20) ? *(pdbuf + 2) : 0x20;
        *(ptitle_name + 2) = (*(pdbuf + 3) >= 0x20) ? *(pdbuf + 3) : 0x20;
        *(ptitle_name + 3) = (*(pdbuf + 4) >= 0x20) ? *(pdbuf + 4) : 0x20;
        *(ptitle_name + 4) = (*(pdbuf + 5) >= 0x20) ? *(pdbuf + 5) : 0x20;
        *(ptitle_name + 5) = (*(pdbuf + 6) >= 0x20) ? *(pdbuf + 6) : 0x20;
        break;

      case 0x91:  // title_name
        *(ptitle_name + 6) = (*(pdbuf + 1) >= 0x20) ? *(pdbuf + 1) : 0x20;
        *(ptitle_name + 7) = (*(pdbuf + 2) >= 0x20) ? *(pdbuf + 2) : 0x20;
        break;

      case 0x92:  // netmask
        if((main_status == MAIN_STATUS_IP_GATEWAY_SETUP)
          || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
          || ((main_status == MAIN_STATUS_IP_IP_SETUP) && value_pos))
        {
          *(pnetmask + 0) = *(pdbuf + 1);
          *(pnetmask + 1) = *(pdbuf + 2);
          *(pnetmask + 2) = *(pdbuf + 3);
          *(pnetmask + 3) = *(pdbuf + 4);
        }
        else
        {
          *(pnetmask_s + 0) = *(pnetmask + 0) = *(pdbuf + 1);
          *(pnetmask_s + 1) = *(pnetmask + 1) = *(pdbuf + 2);
          *(pnetmask_s + 2) = *(pnetmask + 2) = *(pdbuf + 3);
          *(pnetmask_s + 3) = *(pnetmask + 3) = *(pdbuf + 4);
          disp_request_f = 1; make_value_f = 1;
        }
        break;

      case 0x93:  // gateway
        if((main_status == MAIN_STATUS_IP_GATEWAY_SETUP)
          || (main_status == MAIN_STATUS_IP_NETMASK_SETUP)
          || ((main_status == MAIN_STATUS_IP_IP_SETUP) && value_pos))
        {
          *(pgateway + 0) = *(pdbuf + 1);
          *(pgateway + 1) = *(pdbuf + 2);
          *(pgateway + 2) = *(pdbuf + 3);
          *(pgateway + 3) = *(pdbuf + 4);
        }
        else
        {
          *(pgateway_s + 0) = *(pgateway + 0) = *(pdbuf + 1);
          *(pgateway_s + 1) = *(pgateway + 1) = *(pdbuf + 2);
          *(pgateway_s + 2) = *(pgateway + 2) = *(pdbuf + 3);
          *(pgateway_s + 3) = *(pgateway + 3) = *(pdbuf + 4);
          disp_request_f = 1; make_value_f = 1;
        }
        break;

      case 0x94:  // progress
        //key_idle_cnt = KEY_IDLE_MAX_CNT;
        key_idle_cnt = LCDMSG_TIMEOUT(KEY_IDLE_MAX_CNT,data->lcd[0].timeout[0]);
        // pch_031015 : turn on backlight again
		    lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_FULL]);
        if((main_status == MAIN_STATUS_USB_BCK_PROG) && (*(pdbuf + 1) != 0x02)) break;
        prog_type = *(pdbuf + 1);
        prog_rate = *(pdbuf + 2);
        prog_type_ext = *(pdbuf + 3);
        disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT_FROM_INPUT2D;
        if(prog_rate == 0x64)
          disp_return_to_main_cnt = NAS_INIT_LCD_TO_DEFAULT_CNT;
        disp_request_f = 1;
        break;

      case 0x95:
        overwrite_warn_f = *(pdbuf + 1);
        break;

      case 0xa0:  // Buzzer on/off
        //beep_set(0x01, 1);
        msleep(100);
        //beep_set(0x01, 0);
        msleep(100);
        //beep_set(0x02, 1);
        msleep(100);
        //beep_set(0x02, 0);
        msleep(100);
        //beep_set(0x03, 1);
        msleep(100);
        //beep_set(0x03, 0);
        msleep(100);
        break;

      case 0xa1:  // Buzzer control
        if(!beep_task_flag_f)
        {
          beep_prog = 0;
          beep_cont_cnt = beep_int_cnt = 0;
          if(*(pdbuf + 5) == 0x00) *(pdbuf + 5) = 0x01;
          if(*(pdbuf + 6) == 0x00) *(pdbuf + 6) = 0x01;
          beep_buf[0] = *(pdbuf + 1);
          beep_buf[1] = *(pdbuf + 2);
          beep_buf[2] = *(pdbuf + 3);
          beep_buf[3] = *(pdbuf + 4);
          beep_buf[4] = *(pdbuf + 5);
          beep_buf[5] = *(pdbuf + 6);
          beep_task_flag_f = 1;
        }
        break;

      case 0xd0:  // Filename
        file_name_size = *(pdbuf + 1) - 2;
        file_num = *(pdbuf + 2);
        total_file_num = *(pdbuf + 3);

        for(bTmp2 = 0; bTmp2 < file_name_size; bTmp2++)
        {
          if((wc_msg[bTmp2] < 0x20) || (wc_msg[bTmp2] >= 0x7f))
          {
            wc_msg[bTmp2] = '_';
          }
          file_name[bTmp2] = wc_msg[bTmp2];
        }

        valid_file_name_f = 1;
        break;

      case 0xd1:  // Error Message
      case 0xd2:
      case 0xd3:
        err_msg_size = *(pdbuf + 1) - 2;
        err_code = *(pdbuf + 3);
        for(bTmp2 = 0; bTmp2 < err_msg_size; bTmp2++)
        {
          //if(!err_msg[bTmp2])
          if((wc_msg[bTmp2] < 0x20) || (wc_msg[bTmp2] >= 0x7f))
          {
            err_msg_size = bTmp2+1;
            break;
          }
          err_msg[bTmp2] = wc_msg[bTmp2];
        }
        if(*pdbuf == 0xd1)
        {
          valid_err_msg_f = 1;
          persist_err_msg_f = 0;
        }
        else if(*pdbuf == 0xd2)
        {
          valid_err_msg_f = 0;
          persist_err_msg_f = 1;
        }
        else if(*pdbuf == 0xd3)
        {
          wc_msg_size = err_msg_size;
          memcpy(bg_msg, wc_msg, wc_msg_size);
          wc_msg_shift = wc_msg_disp_cnt = 0;
          valid_err_msg_f = 0;
          persist_err_msg_f = 0;
          bg_err_msg_f = 1;
        }
        err_msg_shift = err_msg_disp_cnt = 0;
        //key_idle_cnt = KEY_IDLE_MAX_CNT;
        key_idle_cnt = LCDMSG_TIMEOUT(KEY_IDLE_MAX_CNT,data->lcd[0].timeout[0]);
		    lcd_backlight(data->lcd[0].brightness[LCD_BRIGHT_FULL]);
        if(mnt_status >= MNT_STATUS_LOADED) mnt_status = 0x00;
        break;

      case 0xf0:  // uCom F/W Update
        if((*(pdbuf + 1) == 0xff) && (*(pdbuf + 2) == 0xff))
        {
          bfw_update = 1;
        }
        break;

      case 0xff:
        break;

      default:
        break;
    }
    ;	//COMM_END;
  }
  else  // Checksum NG
  {
    ;	//COMM_END;
  }

  com_flag = 0;
}
