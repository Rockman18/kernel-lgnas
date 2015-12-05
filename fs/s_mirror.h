#ifndef __S_MIRROR__
#define __S_MIRROR__

#include <linux/freezer.h>
#if (!defined(CONFIG_MACH_NC5)||!defined(CONFIG_MACH_NT3))
//#include <asm/memory.h>
#endif
#include <linux/list.h>
#include <linux/limits.h>

#define P_PTR_COUNT		1024
#define PATH_SIZE		PATH_MAX
#define DIR_ENT_SIZE	8192
#define SM_BUF_SIZE		4096

struct sm_path_list {
	char * source;
	char * mirror;
	char * buf;
	int    count;

	//int src_len;
	//int mir_len;
};


enum {
	WORK_CMD_DIR_CP		= 0,
	WORK_CMD_DIR_MV,
	WORK_CMD_DIR_RM,
	WORK_CMD_FILE_CP,
	WORK_CMD_FILE_MV,
	WORK_CMD_FILE_RM,
	WORK_CMD_LN,
	WORK_CMD_CHOWN,
	WORK_CMD_CHMOD,

	WORK_CMD_MAX
};

enum {
	WORK_TYPE_NORMAL = 0,
	WORK_TYPE_ORDERED, 
	WORK_TYPE_ONESHOT,
};


struct sm_work {
	struct list_head	list;

	uint32_t			cmd;
	uint32_t			type;

	uint32_t			len;
	uint32_t			result;

	uint32_t 			retry;
	uint32_t			t_delta;		// for work scheduling

	uint64_t			jiffies;

	char				*src;
	char				*dest;

	//struct dentry		*s_mnt;
	//struct dentry		*d_mnt;
	//struct dentry		*d_dentry;

	//struct file			*s_file;
	//struct file			*d_file;

	void 				(*callback)(struct sm_work * work);
	char				path[0];
};


struct sm_dirent64 {
	u64		d_ino;
	s64		d_off;
	u32		d_reclen;
	u32		d_type;
	u32		namelen;
	u32		pad;
	char	name[0];
};


struct sm_callback64 {
	struct sm_dirent64 * current_dir;
	struct sm_dirent64 * previous;
	int count;
	int error;
};



// in namei.c
extern struct dentry *lookup_hash(struct nameidata *nd);
extern int do_rename(int olddfd, const char *oldname, int newdfd, const char *newname);
extern int do_path_lookup(int dfd, const char *name, unsigned int flags, struct nameidata *nd);

// in s_mirror.c
extern struct task_struct *sm_get_task(void);
extern void file_copy_attr(struct inode *si, struct inode *di);
extern char * do_file_compare( struct dentry *mount, struct dentry *dentry );
extern long sm_sys_mkdirat(char * path, struct dentry *src, int mode);
extern int sm_sys_renameat(int olddfd, char *from, int newdfd, char *to);
extern long sm_sys_unlink(struct dentry *mount, struct dentry *fdentry);
extern long sm_sys_mkdir_mir(struct dentry *mount, struct dentry *fdentry, int mode);
extern long sm_do_rmdir(struct dentry *mount, struct dentry *fdentry);
extern long sm_sys_chmod(struct path *path, const char __user *filename, mode_t mode);
extern void sm_sys_open(unsigned int dfd, struct file *src, char *filename, int flags, int mode);
extern long sm_sys_close(struct file * filp, struct files_struct *files);
extern ssize_t sm_vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos);
extern ssize_t sm_sys_writev(struct file *mfilp, loff_t pos, const struct iovec __user *vec, unsigned long vlen, unsigned long len);

extern uint32_t sm_enable;

#endif
