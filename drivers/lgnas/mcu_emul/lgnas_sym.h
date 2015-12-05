#ifndef _LGNAS_N4B2_SYM_H_
#define _LGNAS_N4B2_SYM_H_

#include <linux/input.h>
#include "../hal/model_info.h"
#include "../hal/hal.h"

/*****************************************************************************
 * external variables
 ****************************************************************************/
struct raid_state {
  u8 vol;
  u8 state;
  u8 percent;
  u8 rsv;
};

typedef struct
{
    unsigned char b0:1;
    unsigned char b1:1;
    unsigned char b2:1;
    unsigned char b3:1;
    unsigned char b4:1;
    unsigned char b5:1;
    unsigned char b6:1;
    unsigned char b7:1;

} bit_struct;

extern bit_struct	bit_str_0, bit_str_1, bit_str_2, bit_str_3;

extern u32 lgnas_spin_mdelay;
extern u32 lgnas_key;
extern u32 lgnas_backlight;
extern u32 lgnas_sysfan;
extern u32 lgnas_led;
extern u32 lgnas_led_lock;
extern u32 lgnas_gpio_offset;
extern u8 lgnas_fwver[14];

extern  u8	key_cur, key_tmp, key_tmp_old, tmr_1000ms;
extern  u32	tick_cnt, key_chk_valid_cnt;
extern  u8	key_flag, key_proc, key_state, key_event, int_rsn, key_checked;
extern  u8	main_status, disp_status, mnt_status, backup_status, burn_status, btn_cnt;

extern  u8   com_state, com_flag;
extern  u8   xbuf[8], rbuf[8], ebuf[8], lbuf[8], dbuf[8], ip[5], time[6], str_debug[8], title_name[9];
extern  u8   netmask[4], gateway[4], ip_s[5], netmask_s[4], gateway_s[4];
extern  u8   *pdbuf, *plbuf, *pebuf, *pxbuf, *prbuf, *pstr_debug, *pip, *ptime, *ptitle_name, *pserver_name;
extern  u8   *pnetmask, *pgateway, *pip_s, *pnetmask_s, *pgateway_s;
extern  u8   value_setup[16], value_pos;
extern  u8   beep_cnt, beep_type, beep_buf[6], beep_prog, beep_cont_cnt, beep_int_cnt;
extern  u16  beep_cnt_w;
extern  u8   xidx, ridx, didx, dcnt, lcd_pos, lcd_len;
extern  u8   hdd_usage_rate, mnt_dev, prog_rate, prog_type, prog_type_ext;
extern  struct raid_state raidstate;

extern  u16  disp_return_to_main_cnt, boot_wait_cnt, key_idle_cnt;
extern  u8   fan_rpm, bfw_update, lcd_bl_full, lcd_bl_half;

extern  u8   file_num, total_file_num, file_name_size, file_name[MINFO_MAX_LCD_STR], file_name_shift, file_name_disp_cnt;
extern  u8   err_msg_size, err_code, err_msg_shift, err_msg_disp_cnt, svc_code[2];
extern  u8   wc_msg_size, wc_msg_shift, wc_msg_disp_cnt;

extern  u8   bg_msg[], wc_msg[], err_msg[];
extern  u8   server_name[12];

extern  u8    serial[8], mn_fw_ver[12];

extern  u8    test_type, type_0_seq, btest_run_stop;
extern  u16   fan_timer_cnt, fan_timer_cnt_old, fan_rpm_fb, i2c_bsy_cnt, boot_to_cnt;

extern  u8    boot_type, setup_type, bpower_key_pressed, aging_status, menu_seq[5], menu_idx, menu_top;
extern  u8    power_fail_cnt, admin_pwd_init_cnt, power_fail_ccured_cnt, odd_boot_chk_cnt;


//timer export
extern const u8* str_welcome;
extern const u8* str_welcome_dvd;


/*****************************************************************************
 * external functions
 ****************************************************************************/
//SIO export
extern void inline SIOSetBit(u8 data, int x);
extern void inline SIOClearBit(u8 data, int x);
extern void inline SHWMOutB( unsigned char data, unsigned char addr );
extern void inline SIOOutB(unsigned char data, int x);
extern u8 inline SIOInB(int x);
extern u8 inline SHWMInB(int addr);
extern u16 inline SHWMInW(int addr);
//CMOS
extern int cmos_control(u8 addr,u8 set,u8 onoff);
extern int cmos_control_rtc(unsigned char set, unsigned char date, 
  unsigned char hour, unsigned char minute, unsigned char second );
extern unsigned char inline cmos_read_byte(int x);

//LED export
extern void led_init(void);

//LCD export
extern void lcd_put_s(unsigned char pos, char * s, unsigned char num);
extern void lcd_disp(char pos, unsigned short value);
extern void lcd_disp_b(char pos, unsigned char value);
extern void lcd_disp_b2(char pos, unsigned char value);
extern void lcd_disp_h(char pos, unsigned char value);
extern void lcd_cursor_on(unsigned char on, unsigned char pos);
extern void lcd_fill_blank(char pos, unsigned char size);
extern void lcd_init(void);
extern void lcd_initialize(void);
extern void inline lcd_string(unsigned char command, const char *string);
extern void inline lcd_backlight_onoff(int on);
extern void inline lcd_backlight(u32 pwm);
extern void inline lcd_write(unsigned char data);
extern void inline lcd_command(unsigned char command);
extern void inline lcd_data(unsigned char data);
extern void lcd_init(void);
extern void lcd_initialize(void);

//FAN export
extern void cpufan_pwm_max_write(u32 pwm);
extern int cpufan_pwm_max_read(void);
extern void cpufan_pwm_middle_write(u32 pwm);
extern int cpufan_pwm_middle_read(void);
extern void cpufan_pwm_min_write(u32 pwm);
extern int cpufan_pwm_min_read(void);
extern void cpu_temp_high_write(u32 temp);
extern int cpu_temp_high_read(void);
extern void cpu_temp_low_write(u32 temp);
extern int cpu_temp_low_read(void);
extern int cpufan_rpm(void);
extern int cpu_temp(void);
extern void sysfan_speed(u32 pwm);


//interrupt export 
extern struct nashal_data *ns2data;
extern void com_processing(struct nashal_data *data);
void key_chk_valid(void);
void diag_key_check(void);

//EVENT export
extern void lgnas_event_trg(u8 rsn, u8 keysts);

//200 TIMER export
extern void com_make_rd_data(unsigned char raddr);

//FRONT export 
extern int set_message(u8 mode, char *msg, int len);
extern void set_message1(struct nashal_data *data, u8 mode, const char *msg, int len);
extern void set_hostname(struct nashal_data *data, const char *msg, int len);
extern void set_fwversion(struct nashal_data *data, const char *msg, int len);
extern int front_read(u8* pData, int iLen);
extern int front_write(u8* pData, int iLen);

//Interrupt GPIO 
extern int init_gpio_interrupt(void);

#endif
