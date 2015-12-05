#ifndef __FSM_H__
#define __FSM_H__

#include <linux/i2c.h>

void fsm_init(void);
int fsm_comm_proc(struct i2c_client *client, int button_id, int priority);
int fsm_show_address(char *buf);
int fsm_store_address(const char *buf);
int fsm_set_mode(struct i2c_client *client, int mode);
int fsm_get_mode(void);

#endif
