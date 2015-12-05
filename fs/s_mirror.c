/*
 *  linux/fs/s_mirror.c
 *
 *  Copyright (C) 2009 Ahn, Yong Hyun @HLDS
 */

#include <linux/slab.h> 
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/smp_lock.h>
#include <linux/fsnotify.h>
#include <linux/security.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/fs_struct.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <linux/kthread.h>

#include "s_mirror.h"

#define DEFAULT_RETRY	0
//#define DEBUG

char *g_buf[NR_CPUS] = { 0, };
//char * g_path_buf[PATH_MAX] = {0};

struct task_struct * asyncd_task = NULL;
DEFINE_SPINLOCK(sm_event_lock);
DEFINE_RWLOCK(sm_conf_lock);
LIST_HEAD(sm_event_list); 
DECLARE_WAIT_QUEUE_HEAD(sm_asyncd_wait);

uint32_t sm_enable = 0;

struct sm_path_list sm_path_conf_new[256] = { {0, 0}, };
struct sm_path_list sm_path_conf_old[256] = { { 0, 0 }, };

struct sm_path_list *sm_path_conf = NULL;

struct sm_work *sm_alloc_work(uint32_t cmd, uint32_t type, char *src, char *dest);
struct sm_work *sm_fill_work(struct sm_work * work, uint32_t cmd, uint32_t type, char *src, char *dest);
struct sm_work *sm_kick_worker(struct sm_work * work);
int sm_dispose_work(struct sm_work * work);


void dump_inode( const unsigned char * name, struct inode * inode )
{
	printk("* Inode of [%s]\n", name);
	printk("	i_size     : %lld\n", (long long)inode->i_size);
	printk("	Access Time: %ld:%ld\n", inode->i_atime.tv_sec, inode->i_atime.tv_nsec);
	printk("	Modifi Time: %ld:%ld\n", inode->i_mtime.tv_sec, inode->i_mtime.tv_nsec);
	printk("	Create Time: %ld:%ld\n", inode->i_ctime.tv_sec, inode->i_ctime.tv_nsec);
	printk("	i_count    : %u\n", atomic_read(&inode->i_count));
	printk("	i_ino      : %lu\n", inode->i_ino);
	printk("	i_nlink    : %d\n", inode->i_nlink);
	//printk("	i_cindex   : %d\n", inode->i_cindex);  LGNAS 20110404
	printk("	i_blkbits  : %d\n", inode->i_blkbits);
	printk("	i_blocks   : %lu\n", (unsigned long)inode->i_blocks);
	printk("	i_bytes    : %d\n", inode->i_bytes);
	printk("	i_mode     : %d\n", inode->i_mode);
	printk("	i_flags    : %d\n", inode->i_flags);
}

void dump_path_ptr(long *path, int i, int size)
{
	if( i < size )  {
		printk("DUMP: depth:%d, size:%d, path:%p, *path:%x\n", i, size, path, (unsigned)*path);
		for(;i<size; i++)
			printk("[%d:%p]:%s ", i, (char*)path[i], (char*)path[i]);
		printk("\n"); 
	}
}

void dump_dentry(char * name, struct dentry * fdentry) 
{
	static char *path[P_PTR_COUNT];	
	int i = P_PTR_COUNT;

	while( fdentry ) {
		path[--i] = (char*)fdentry->d_name.name;
		if( IS_ROOT(fdentry) || i == 0) break;
		fdentry = fdentry->d_parent;
	}
	printk("<%s> [", name);

	if( i < P_PTR_COUNT ) {
		if( *(path[i]) == '/' ) i++;
		if( i == P_PTR_COUNT ) printk("/");
		while( i < P_PTR_COUNT ) { 
			printk("/%s", path[i]);
			i++;
		}
	}
	printk("]\n"); 
}

int dump_dentry_path( struct dentry *mount, struct dentry *dentry)
{
	printk("dump dentry: ");
	while( dentry ) {
		printk("[%s]", dentry->d_name.name);
		if( IS_ROOT(dentry) ) break;
		dentry = dentry->d_parent;
	}
	printk("  ");

	while( mount ) {
		printk("[%s]", mount->d_name.name);
		if( IS_ROOT(mount) ) break;
		mount = mount->d_parent;
	}
	printk("\n");
	return 0;
}


int is_same_file(struct inode *si, struct inode *di)
{
	#define is_not_equal(_si, _di, _entry) ((_si)->_entry != (_di)->_entry)

	if( is_not_equal(si, di, i_size) ) return 0;
	if( is_not_equal(si, di, i_mode) ) return 0;
	if (!timespec_equal(&di->i_mtime, &si->i_mtime)) return 0;
	if (!timespec_equal(&di->i_ctime, &si->i_ctime)) return 0;
	return 1;
}

void file_copy_attr(struct inode *si, struct inode *di)
{
	int sync_it = 0;

	if (IS_NOCMTIME(di))
	{
		printk("%s: nocmtime\n", __func__);
		return;
	}
//	if (IS_RDONLY(di)) return;

	if (!timespec_equal(&di->i_mtime, &si->i_mtime)) {
		di->i_mtime = si->i_mtime;
		sync_it = 1;
	}

	if (!timespec_equal(&di->i_ctime, &si->i_ctime)) {
		di->i_ctime = si->i_ctime;
		sync_it = 1;
	}

	if (di->i_mode != si->i_mode) {
		di->i_mode = si->i_mode;
		sync_it = 1;
	}
	
	if (di->i_size != si->i_size) {
		di->i_size = si->i_size;
		sync_it = 1;
	}
	if (di->i_gid != si->i_gid) {
		di->i_gid = si->i_gid;
		sync_it = 1;
	}
	if (di->i_uid != si->i_uid) {
		di->i_uid = si->i_uid;
		sync_it = 1;
	}
	if (sync_it)
		mark_inode_dirty_sync(di);
}
EXPORT_SYMBOL(file_copy_attr);

char * get_compare_buf(void) 
{
	char * buf;
	buf = (char*) kmalloc((PATH_SIZE) + (P_PTR_COUNT * sizeof(char*)), GFP_KERNEL);
	if( !buf ) {
		printk("%s: kmalloc fail \n", __func__);
	}
	return buf;
}

void put_compare_buf(char * buf)
{
	if( buf )
		kfree( buf );
}

int path_p_cmp(char * conf, long *p_path, int depth, int count)
{
	char * ptr;

	for(; depth < count; depth++) 
	{
		if( !(ptr = (char*)p_path[depth]) ){
			break;
		}
			
		//printk( " p_path[%d]: %s \n", depth, (char*)p_path[depth]);
		while( *conf == '/' ) conf++;
		while( *conf != '/' && *conf != '\0' ) 
			if( *conf++ != *ptr++ ) return -1;
		if( *conf == '\0' && *ptr == '\0' ) 
			return depth;
	}

	return -1;
}

inline int make_p_path( struct dentry *mount, struct dentry *dentry, long *p_path, int d)
{
	if( dentry && mount ) {
		while( (!IS_ROOT(dentry)) && (--d > 0)) {
			p_path[d] = (long)dentry->d_name.name;
			dentry = dentry->d_parent;
		}
		while( (!IS_ROOT(mount)) && (--d > 0)) {
			p_path[d] = (long)mount->d_name.name;
			mount = mount->d_parent;
		}
	}
	return d;
}

long copy_p_path( char * dest, size_t limit, char * src_dir, long *p_path, int depth ) 
{
	long li;

	li = strlcpy( dest, src_dir, limit);

	if( unlikely(li >= limit) ) return -1;

	while( ++depth < P_PTR_COUNT ) 
	{
		li += strlcat( dest + li, "/", limit - li);
		li += strlcat( dest + li, (char*)p_path[depth], limit - li );
		if( unlikely(li >= limit) ) return -1;
	}
	return li;
}

char * make_path_str( struct dentry *mount, struct dentry *dentry, char *path)
{
	char * buf, *ptr;
	if( dentry && mount ) {
		buf = (char*) kmalloc( PATH_SIZE, GFP_KERNEL);
		if( !buf ) goto out;
		ptr = buf + PATH_SIZE;

		*(--ptr) = 0;
		while( !IS_ROOT(dentry) ) {
			ptr -= dentry->d_name.len;
			memcpy( ptr, dentry->d_name.name, dentry->d_name.len);
			*(--ptr) = '/';
			dentry = dentry->d_parent;
		}
		while( !IS_ROOT(mount) ) {
			ptr -= mount->d_name.len;
			memcpy( ptr, mount->d_name.name, mount->d_name.len);
			*(--ptr) = '/';
			mount = mount->d_parent;
		}
		strcpy(path, ptr);
		kfree( buf );
		return path;
	}
out:
	return NULL;
}

char * do_file_compare_next( char *outbuf, struct dentry *mount, struct dentry *dentry, int *index, int *depth_save) 
{
	long *p_path;
	int i, d, depth;
	char * path_buf = NULL;

	if( !sm_path_conf || !outbuf ) goto out;
	if( !mount || !mount->d_name.name || IS_ROOT(mount) || 
			memcmp(mount->d_name.name, "volume", 6) ){
		
#ifdef DEBUG
		//printk("%s: mount->d_name.name:%s\n", __FUNCTION__,mount->d_name.name);
#endif
		goto out;
	}

	p_path = (long*)(outbuf + (PATH_SIZE));

	/* 1. merge mount point & path */
	if ( *index == 0 ) {
		depth = make_p_path( mount, dentry, p_path, P_PTR_COUNT );
		if( depth < 0 ){ 

#ifdef DEBUG
			printk("%s: depth:%d\n", __FUNCTION__,depth);
#endif
			goto out;
		}
	//	printk(" [0]:%s [1]:%s \n", p_path[0], p_path[1] );
		*depth_save = depth;
	}
	else 
		depth = *depth_save;

	/* 2. compare merged path with mirror source */
	read_lock(&sm_conf_lock);
	path_buf = (char*) kmalloc( 2*PATH_SIZE, GFP_KERNEL);
	for(i=*index; sm_path_conf[i].source; i++) 
	{	
		//char * tmp = NULL;
		struct nameidata nd;
		int error = 1;

		/* for ftp because ftp use chroot and then change root path */
		error = do_path_lookup(AT_FDCWD , sm_path_conf[i].source, LOOKUP_DIRECTORY, &nd);
		if(error){
			d = path_p_cmp(sm_path_conf[i].source, p_path, depth, P_PTR_COUNT);
			//printk("%s: do_path_lookup error sm_path_conf.source:%s  d:%d\n", __func__, sm_path_conf[i].source, d);
			if( d >= 0 ) {
				strcpy( outbuf, sm_path_conf[i].mirror+9); 
				while( ++d < P_PTR_COUNT ) {
					strcat( outbuf, "/" );
					strcat( outbuf, (char*)p_path[d] );
				}
#ifdef DEBUG
				printk("matched file: source 2 [%s] -> mirror [%s]\n", sm_path_conf[i].source, outbuf);
#endif
				*index = i;
				read_unlock(&sm_conf_lock);
				if( path_buf ){
					kfree(path_buf);
				}
				return outbuf;

			}
			continue;
		}
/*
		tmp = d_path(&nd.path, path_buf, PATH_MAX);
		if( !tmp ){
			printk(" d_path error \n" );
			continue;
		}

*/
		path_put(&nd.path);
		//printk(" d_path result from nameidata: %s \n", tmp);

		d = path_p_cmp(sm_path_conf[i].source, p_path, depth, P_PTR_COUNT);
		if( d >= 0 ) {
			if( memcmp(sm_path_conf[i].mirror, "/mnt/disk/", 10) ){
#ifdef debug
				printk(" %s: memcmp \n", __func__);
#endif
				read_unlock(&sm_conf_lock);
				goto out;
			}

			strcpy( outbuf, sm_path_conf[i].mirror);
			while( ++d < P_PTR_COUNT ) {
				strcat( outbuf, "/" );
				strcat( outbuf, (char*)p_path[d] );
			}
#ifdef debug
			printk("matched file: source [%s] -> mirror [%s]\n", sm_path_conf[i].source, outbuf);
#endif
			*index = i;
			read_unlock(&sm_conf_lock);
			if( path_buf ){
				kfree(path_buf);
			}
			return outbuf;
		}
	}
	read_unlock(&sm_conf_lock);
out:
	if( path_buf ){
		kfree(path_buf);
	}
	return NULL;
}


unsigned int sm_task_ptr[NR_CPUS] = {0,};
#define sm_set_task() do { sm_task_ptr[smp_processor_id()] = 1; } while(0)
#define sm_restore_task() do { sm_task_ptr[smp_processor_id()] = 0; } while(0)

struct task_struct *sm_get_task(void)
{
	if( sm_task_ptr[smp_processor_id()] ) return asyncd_task;
	return current;
}
EXPORT_SYMBOL(sm_get_task);


void sm_sys_open(unsigned int dfd, struct file *src, char *filename, int flags, int mode)
{
	char * mirror, *buf;
	struct file * next = NULL, *cur = NULL;
	struct inode * inode;
	int index = 0, depth = 0;

	src->f_mirror = NULL;

	if ( !sm_enable ) return;

	inode = src->f_dentry->d_inode;

	if (S_ISDIR(inode->i_mode)) {
		return;
	}
	if (!S_ISREG(inode->i_mode)) {
		return;
	}

	buf = get_compare_buf();
	if( !buf ) return;


	sm_set_task();
	cur = src;
	do {
		mirror = do_file_compare_next( buf, src->f_vfsmnt->mnt_mountpoint, src->f_dentry, &index, &depth );
		if( !mirror ) break;

		next = filp_open(mirror, flags, mode );

		if (IS_ERR(next)) {
			if( (flags & O_CREAT) != O_CREAT ) flags |= O_CREAT;
			if( (flags & O_EXCL) == O_EXCL )  {
				flags &= ~O_EXCL;
				flags |= O_TRUNC;
			}

			next = filp_open(mirror, flags, mode );
			printk("%s: 2nd mirror open [%s->%s][%x->%x], mode:flags[%x:%o]\n", __FUNCTION__, 
					filename, mirror, (unsigned)cur, (unsigned)cur->f_mirror, mode, flags);
		} 

#ifdef DEBUG
			/*if( next && file_count(next) > 1){
				printk(" %s: is called file_count is %d \n", file_count(src));
			} //20110514 omw*/
#endif

		if (!IS_ERR(next)) {
			cur->f_mirror = next;
			next->updated = 0;
			next->f_mirror = NULL;
			cur = next;
			index ++;
#ifdef DEBUG
			printk("%s: mirror open [%s->%s][%x->%x], mode:flags[%x:%o]\n", __FUNCTION__, 
					filename, mirror, cur, cur->f_mirror, mode, flags);
#endif
		} else  {
			cur = NULL;
			printk("%s: mirror open failed [%s->%s] error:%ld, mode:flags[%x:%o], I:D=%d:%d\n", __FUNCTION__, 
					filename, mirror, PTR_ERR(next), mode, flags, index, depth);
		}
	} while( cur );

	sm_restore_task();
	put_compare_buf( buf );

	return;
}

EXPORT_SYMBOL(sm_sys_open);
		
long sm_sys_close(struct file * filp, struct files_struct *files)
{
	struct file *next, *cur;

	if( !filp->f_mirror ) return 0;

	sm_set_task();
	files = sm_get_task()->files;

	cur = filp->f_mirror;
	filp->f_mirror = NULL;

	while (cur) {
#ifdef DEBUG
		char * tmp;
		char path_buf[4092] = {0};
		tmp = d_path(&filp->f_path, path_buf, 4092);
#endif
		if( cur->updated )
			file_copy_attr( filp->f_dentry->d_inode, cur->f_dentry->d_inode );

		next = cur->f_mirror;

#ifdef DEBUG

		printk("%s: file close[%x->%x], filename:%s , filecount:%d \n", __FUNCTION__, cur, next, tmp, file_count(cur));

#endif
		filp_close(cur, files);

		cur = next;
	} 
	sm_restore_task();

	return 0;
}

ssize_t __sm_vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;


	if (!file || *pos < 0 || count < 0)
		return -EINVAL;
	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!file->f_op || (!file->f_op->write && !file->f_op->aio_write))
		return -EINVAL;
	if (unlikely(!access_ok(VERIFY_READ, buf, count)))
		return -EFAULT;

	ret = rw_verify_area(WRITE, file, pos, count);
	if (ret >= 0) {
		count = ret;
		ret = security_file_permission (file, MAY_WRITE);
		if (!ret) {
			if(!file_count(file)){
				printk(" %s: is called. file_count is zero \n");
			}
			if (file->f_op->write)
				ret = file->f_op->write(file, buf, count, pos);
			else
				ret = do_sync_write(file, buf, count, pos);
			if (ret > 0) {
				//fsnotify_modify(file->f_path.dentry);
				add_wchar(current, ret);
				file->updated++;
				file->f_pos = *pos;	// set new position
			}
			else {
				if (ret == -ENOSPC ) {
					printk("%s: No space left.\n", __FUNCTION__);
				}
				else {
					printk("%s: Write error(%d) %s.\n", __FUNCTION__,  (int)ret, file->f_dentry->d_name.name);
				}
			}
			inc_syscw(current);
		}
	}

	return ret;
}

ssize_t sm_vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;
	loff_t pos_2;
	if(!file){
		return 0;
	}
	do { 
		pos_2 = *pos;
		ret = __sm_vfs_write( file, buf, count, &pos_2 );
		if( ret < 0 )
			return ret;
		
	} while( (file = file->f_mirror) );
	return ret;
}

ssize_t __sm_sys_writev(struct file *mfilp, loff_t pos, const struct iovec __user *vec, 
		unsigned long vlen, unsigned long len)
{
	ssize_t ret = -EBADF;

	if( !sm_enable ) 
		return ret;

	if (!mfilp || pos < 0 || (long)len < 0)
		return -EINVAL;

	if(!file_count(mfilp)){
		printk(" %s: is called. file_count is zero \n");
	}
	ret = vfs_writev(mfilp, vec, vlen, &pos);
	mfilp->f_pos = pos;
	if( ret != len ) {
		printk("%s: Can't write(%d) as input(%lu)!\n", __FUNCTION__, ret, len);
	}

	if (ret > 0)
		add_wchar(current, ret);
	inc_syscw(current);

	return ret;
}

ssize_t sm_sys_writev(struct file *mfilp, loff_t pos, const struct iovec __user *vec, 
		unsigned long vlen, unsigned long len)
{
	ssize_t ret;
	if(!mfilp){
		return 0;
	}
	do {
		ret = __sm_sys_writev(mfilp, pos, vec, vlen, len);
		if( ret < 0 )
			return ret;
	} while( (mfilp = mfilp->f_mirror) );
	return ret;

}

long sm_sys_chmod(struct path *path, const char __user *filename, mode_t mode)
{
	char *mirror, *buf;
	struct file * filp;
	struct inode *si, *di;
	int index = 0, depth = 0;

	if( sm_enable == 0 )
		return 0;

#ifdef DEBUG
	printk("%s: filename:%s...\n", __FUNCTION__, filename);
#endif

	buf = get_compare_buf();
	if( !buf ) return 0;

	while ( 1 ) {
		mirror = do_file_compare_next( buf, path->mnt->mnt_mountpoint, path->dentry, &index, &depth );
		if( !mirror ) {
			put_compare_buf( buf );
			return 0;
		}
#ifdef DEBUG
	printk("%s: filename:%s...\n", __FUNCTION__, filename);
#endif

		filp = filp_open(mirror, O_WRONLY, 0);
		if (IS_ERR(filp)) {
			struct sm_work * work;
			char *tmp = getname(filename);
			if (!IS_ERR(tmp)) {
				work = sm_alloc_work(WORK_CMD_FILE_CP, WORK_TYPE_NORMAL, tmp, mirror);
				putname(tmp);
				sm_kick_worker(work);
			}
		}
		else {
			si = path->dentry->d_inode;
			di = filp->f_dentry->d_inode;
			if( !is_same_file(si, di) ) {
				file_copy_attr(si, di);
			}
			filp_close( filp, current->files );
		}
		index ++;
	}
	put_compare_buf( buf );
	return 1;
}

long __sm_sys_unlink(char * mirror) 
{
	long error = 0;
	struct dentry *dentry;
	struct nameidata nd;
	struct inode *inode = NULL;
	int mutex_locked;

	if( mirror < (char*)PAGE_SIZE ) return -EINVAL;
#ifdef DEBUG
	printk("%s: Deleting %s ...\n", __FUNCTION__, mirror);
#endif

	sm_set_task();
	error = do_path_lookup(0, mirror, LOOKUP_PARENT, &nd);
	if (error) {
		sm_restore_task();
		return error;
	}
	if (nd.last_type != LAST_NORM) {
		sm_restore_task();
		path_put(&nd.path);
		return -EISDIR;
	}

	mutex_locked = mutex_trylock(&nd.path.dentry->d_inode->i_mutex);
	dentry = lookup_hash(&nd);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		if (!nd.last.name[nd.last.len]) {
			inode = dentry->d_inode;
			if (inode) {
				atomic_inc(&inode->i_count);

				if (S_ISDIR(inode->i_mode)) {
#ifdef DEBUG
					printk("%s: Directory: %s\n", __FUNCTION__,mirror);
#endif
					atomic_dec(&inode->i_count);
				}
				else {
					error = vfs_unlink(nd.path.dentry->d_inode, dentry);
					if( error ) {
						struct sm_work *work;
						work = sm_alloc_work(WORK_CMD_FILE_RM, WORK_TYPE_NORMAL, mirror, mirror);
						if( work ) work->t_delta = 1 * HZ;
						sm_kick_worker(work);
#ifdef DEBUG
						printk("unlinked: error = %d \n", error);
#endif
					}
				}
			}
		}
		dput(dentry);
	}
	if( mutex_locked )
		mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	if (inode) iput(inode);	
	path_put(&nd.path);
	sm_restore_task();
	return error;
}

long sm_sys_unlink(struct dentry *mount, struct dentry *fdentry) 
{
	int error = 0;
	char * mirror, *buf;
	int index = 0, depth= 0;

	if( sm_enable == 0 ) return 0;

	buf = get_compare_buf();
	if( !buf ) return 0;

	while( 1 ) {
#ifdef DEBUG
		printk("%s:[%d] Deleting %s 1...\n", __FUNCTION__, index, mirror);
#endif
		mirror = do_file_compare_next( buf, mount, fdentry, &index, &depth);
		if( !mirror ) break;
#ifdef DEBUG
		printk("%s:[%d] Deleting %s 2...\n", __FUNCTION__, index, mirror);
#endif
		error = __sm_sys_unlink(mirror);
		index ++;
	}
	put_compare_buf( buf );
	return error;
}

int sm_sys_renameat(int olddfd, char *from, int newdfd, char *to)
{
	int error = -1;
	struct file *filp_src = NULL, *filp_dest = NULL;
	char *path_buf = NULL, *mir_to;
	char *ptr, *mir, *buf = NULL;
	int index = 0, depth = 0, len;
	struct sm_work *work;
#define MIR_BUF_SIZE 	(2*PATH_SIZE)
#define MIR_BUF_END 	(path_buf + MIR_BUF_SIZE)

	if( sm_enable == 0 ) goto org_work;

#ifdef DEBUG
	printk("RENAME: %s --> %s\n", from, to);	
#endif
	// Get path name from old file
	filp_src = filp_open(from, O_RDONLY|O_LARGEFILE, 0);
	if ( IS_ERR(filp_src) ) goto org_work;

	buf = get_compare_buf();
	if( !buf ) goto org_work;

	mir = do_file_compare_next( buf, filp_src->f_vfsmnt->mnt_mountpoint, filp_src->f_dentry, &index, &depth );
	if( !mir ) goto org_work;

	path_buf = (char*) kmalloc( MIR_BUF_SIZE, GFP_KERNEL);
	if(!path_buf) goto org_work; 
	ptr = path_buf;
	if( mir ) {
		len = strlen( mir );
		memcpy(ptr, mir, len);
		ptr += len;
		*ptr++ = 0;
		index ++;

		while(mir){
			mir = do_file_compare_next( buf, filp_src->f_vfsmnt->mnt_mountpoint, filp_src->f_dentry, &index, &depth );
			if( mir ) {
				len = strlen( mir );
				if( ptr + len >= MIR_BUF_END ) break;

				memcpy(ptr, mir, len);
				ptr += len;
				*ptr++ = 0;
				index ++;
			}
		}
		*ptr++ = 0;
	}

	filp_close( filp_src, current->files );
	filp_src = NULL;

	// Rename old file in source directory
	error = do_rename(olddfd, from, newdfd, to);
	if( error < 0 ) goto out;

	// Get path name from new file
	filp_dest = filp_open(to, O_RDONLY|O_LARGEFILE, 0);
	if ( IS_ERR(filp_dest) ) goto org_work;

	if( ptr == path_buf ) {
		while(1) {
			mir_to = do_file_compare_next( buf, filp_dest->f_vfsmnt->mnt_mountpoint, filp_dest->f_dentry, &index, &depth  );
			if( !mir_to ) goto out;

			mir = make_path_str( filp_dest->f_vfsmnt->mnt_mountpoint, filp_dest->f_dentry, ptr);
			if( mir ) {
				work = sm_alloc_work(WORK_CMD_FILE_CP, WORK_TYPE_NORMAL, mir, mir_to);
				sm_kick_worker(work);
			}
			index ++;
		}
	}
	ptr = path_buf;
	index = depth = 0;
	while( *ptr && ptr < MIR_BUF_END) {
		int m_err;

		mir_to = do_file_compare_next( buf, filp_dest->f_vfsmnt->mnt_mountpoint, filp_dest->f_dentry, &index, &depth  );
		if( mir_to ) { 
			m_err = do_rename(olddfd, ptr, newdfd, mir_to);
			if( m_err < 0 ) {
				mir = make_path_str( filp_dest->f_vfsmnt->mnt_mountpoint, filp_dest->f_dentry, ptr);
#ifdef DEBUG
				printk("do_rename failed[%d], %s --> %s\n", m_err, ptr, mir_to);
#endif
				if( mir ) {
					work = sm_alloc_work(WORK_CMD_FILE_CP, WORK_TYPE_NORMAL, mir, mir_to);
					sm_kick_worker(work);
				}
				break;
			}
		}
		else {
			// XXX
			__sm_sys_unlink(ptr);
		}

		index ++;
		ptr += strlen(ptr) + 1;
	} 
out:
	if ( filp_dest && !IS_ERR(filp_dest) ) filp_close( filp_dest, current->files );
	if( path_buf ) kfree( path_buf );
	put_compare_buf( buf );
	return error;

org_work:
	if ( filp_src && !IS_ERR(filp_src) ) filp_close( filp_src, current->files );
	if( path_buf ) kfree( path_buf );
	put_compare_buf( buf );

	return do_rename(olddfd, from, newdfd, to);
}

long sm_do_rmdir(struct dentry *mount, struct dentry *fdentry)
{
	int error = 0;
	char * mirror, *buf;
	struct dentry *dentry;
	struct nameidata nd;
	int mutex_locked;
	int index = 0, depth = 0;

	if( sm_enable == 0 ) return 0;

	buf = get_compare_buf();
	if( !buf ) return 0;

	sm_set_task();

	while ( 1 ) {
		mirror = do_file_compare_next( buf, mount, fdentry, &index, &depth );
		if( !mirror ) break; 
#ifdef DEBUG
		printk("%s: Deleting directory %s ...\n", __FUNCTION__, mirror);
		dump_dentry( "Org file", fdentry );
		dump_dentry( "Org mount", mount );
#endif

		error = do_path_lookup(0, mirror, LOOKUP_PARENT, &nd);
		if (error) break;

		switch(nd.last_type) {
			case LAST_DOTDOT:	error = -ENOTEMPTY; break;
			case LAST_DOT:		error = -EINVAL; break;
			case LAST_ROOT:		error = -EBUSY; break;
			default:			error = 0; break;
		}
		if( !error ) {
			mutex_locked = mutex_trylock(&nd.path.dentry->d_inode->i_mutex);
			dentry = lookup_hash(&nd);
			error = PTR_ERR(dentry);
			if (!IS_ERR(dentry)) {
				error = vfs_rmdir(nd.path.dentry->d_inode, dentry);
				dput(dentry);
			}
			if( mutex_locked )
				mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
		}
		path_put(&nd.path);
		index ++;
	}

	sm_restore_task();
	put_compare_buf(buf);
	return error;
}

struct dentry *sm_lookup_create(struct nameidata *nd, int is_dir)
{
	struct dentry *dentry = ERR_PTR(-EEXIST);

	/*
	 * Yucky last component or no last component at all?
	 * (foo/., foo/.., /////)
	 */
	if (nd->last_type != LAST_NORM)
		goto fail;
	nd->flags &= ~LOOKUP_PARENT;
	nd->flags |= LOOKUP_CREATE;
	nd->intent.open.flags = O_EXCL;

	/*
	 * Do the final lookup.
	 */
	dentry = lookup_hash(nd);
	if (IS_ERR(dentry))
		goto fail;

	/*
	 * Special case - lookup gave negative, but... we had foo/bar/
	 * From the vfs_mknod() POV we just have a negative dentry -
	 * all is fine. Let's be bastards - you had / on the end, you've
	 * been asking for (non-existent) directory. -ENOENT for you.
	 */
	if (!is_dir && nd->last.name[nd->last.len] && !dentry->d_inode)
		goto enoent;
	return dentry;
enoent:
	dput(dentry);
	dentry = ERR_PTR(-ENOENT);
fail:
	return dentry;
}


long sm_sys_mkdirat(char * path, struct dentry *src, int mode)
{
	int error = 0;
	struct dentry *dentry;
	struct nameidata nd;
	int mutex_locked;

	sm_set_task();
	error = do_path_lookup(0, path, LOOKUP_PARENT, &nd);
	if (error) {
		printk("%s: mirror mkdir (%s) failed. path_lookup error:%d\n", __FUNCTION__, path, error);
		sm_restore_task();
		return error;
	}

	mutex_locked = mutex_trylock(&nd.path.dentry->d_inode->i_mutex);
	dentry = sm_lookup_create(&nd, 1);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry)) {
		printk("%s: mirror mkdir (%s) failed. lookup_create error:%d\n", __FUNCTION__, path, error);
		goto out_unlock;
	}

	if (!IS_POSIXACL(nd.path.dentry->d_inode))
		mode &= ~current->fs->umask;
	mode |= S_IALLUGO;
	error = vfs_mkdir(nd.path.dentry->d_inode, dentry, mode);
	if( error == 0 )
		file_copy_attr( src->d_inode, dentry->d_inode );
	else {
		printk("%s: mirror mkdir (%s) failed.  error:%d\n", __FUNCTION__, path, error);
	}

	dput(dentry);

out_unlock:
	if( mutex_locked ) 
		mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
	path_put(&nd.path);
	sm_restore_task();
	return error;
}

long sm_sys_mkdir_mir(struct dentry *mount, struct dentry *fdentry, int mode)
{
	char * mirror, *buf;
	int err = 0;
	int index = 0, depth = 0;

	if( sm_enable == 0 ) 
		return -1;

	buf = get_compare_buf();
	if( !buf ) return -1;

	while ( 1 ) {
		mirror = do_file_compare_next( buf, mount, fdentry, &index, &depth );
		if( !mirror ) {
#ifdef DEBUG
			if( index && index < 4) {
				printk("%s: Failed. index=%d creating...", __func__, index);
				dump_dentry( "Source file", fdentry );
			}
#endif
			break;
		}
#ifdef DEBUG
		dump_dentry( "Org file", fdentry );
		dump_dentry( "Org mount", mount );
		printk("mode[%x] Creating %s ...\n", mode, mirror);
#endif
		err = sm_sys_mkdirat(mirror, fdentry, mode);
		index ++;
	}

	put_compare_buf( buf );

	return err;
}

void sm_add_head_work( struct sm_work * work )
{
	spin_lock(&sm_event_lock);
	work->result = 0;
	work->retry = DEFAULT_RETRY;
	work->jiffies = jiffies + work->t_delta;
	list_add(&work->list, &sm_event_list); 
	spin_unlock(&sm_event_lock);
}

void sm_add_tail_work( struct sm_work * work )
{
	spin_lock(&sm_event_lock);
	work->result = 0;
	work->retry = DEFAULT_RETRY;
	work->jiffies = jiffies + work->t_delta;
	list_add_tail(&work->list, &sm_event_list); 
	spin_unlock(&sm_event_lock);
}

struct sm_work *sm_alloc_work(uint32_t cmd, uint32_t type, char *src, char *dest)
{
	struct sm_work * work;
	uint32_t len;

	if( !src || !dest ) 
		return NULL;

	len = sizeof(struct sm_work) + PATH_SIZE * 2;

	work = kmalloc(len, GFP_KERNEL);
	if( !work ) {
		printk("%s: Error! Memory Full.\n", __FUNCTION__);
		return NULL;
	}
	work->cmd = cmd;
	work->type = type;
	work->src = &work->path[0];
	work->dest = &work->path[PATH_SIZE];
	work->len = len;
	work->retry = DEFAULT_RETRY;
	work->result = 0;
	work->t_delta = 3 * HZ;
	work->callback = NULL;

	strlcpy(work->src, src, PATH_SIZE);
	strlcpy(work->dest, dest, PATH_SIZE);

	return work;
}

struct sm_work *sm_fill_work(struct sm_work * work, uint32_t cmd, uint32_t type, char *src, char *dest)
{
	work->cmd = cmd;
	work->type = type;
	work->src = src;
	work->dest = dest;
	work->retry = DEFAULT_RETRY;
	work->result = 0;
	work->t_delta = 3 * HZ;
	work->callback = NULL;

	return work;
}

struct sm_work *sm_kick_worker(struct sm_work * work)
{
	if( !work ) return NULL;
	sm_add_tail_work( work );
	wake_up(&sm_asyncd_wait);
	return work;
}

int sm_del_work( struct sm_work * work )
{
	if( work && virt_addr_valid(work) ) {
		kfree(work);
	}
	return 0;
}

struct sm_work *sm_get_work(void)
{
	struct sm_work * work = NULL, *entry;
	struct list_head * tmp, *next;

	spin_lock(&sm_event_lock);
	if (list_empty(&sm_event_list)) {
		goto out;
	}

	list_for_each_safe(tmp, next, &sm_event_list) 
	{
		entry = list_entry(tmp, struct sm_work, list);
		if (time_after(jiffies, entry->jiffies)) {
			work = entry;
			list_del_init(&work->list);
			goto out;
		}
	}
out:
	spin_unlock(&sm_event_lock);

	return work;
}

int sm_file_cp(struct sm_work *work) 
{
	struct file *src = NULL;
	struct file *dest = NULL;
	char *buf = g_buf[smp_processor_id()];
	int ret, flags = 0;

	if( !work ) return -1;

	if (!sm_enable) {
		goto out;	//20110801 by omw
	}
	work->result = -1;
//	if (force_o_largefile()) 
	{
		flags |= O_LARGEFILE;
	}

	src = filp_open(work->src, O_RDONLY | flags, 0);
	if (IS_ERR(src)) {
		printk("Src Open Error: (%ld) %s\n",  PTR_ERR(src), work->src);
		goto out;
	}
#ifdef DEBUG
	if(file_count(src) > 1){
		printk(" %s: is called file_count is %d \n", file_count(src));
	}	//20110514 by omw
#endif
	if (S_ISDIR(src->f_dentry->d_inode->i_mode)) {
		// call sm_dir_cp
#ifdef DEBUG
		printk("%s: is called : add sm_alloc_work \n", __FUNCTION__);
#endif
		struct sm_work * new_work = sm_alloc_work(WORK_CMD_DIR_CP, WORK_TYPE_NORMAL, work->src, work->dest);
		sm_add_tail_work( new_work );
		goto out;
	}
	if (!S_ISREG(src->f_dentry->d_inode->i_mode)) {
		goto out;
	}

	if( file_count( src ) > 1 ) {
		work->result = 0;
		work->t_delta = 600*HZ;
#ifdef DEBUG
		printk("Already Opended: (%s->%s):(%d:%d)\n", work->src, work->dest, file_count(src), file_count(dest));
#endif
	//	goto out;
	}
	
	dest = filp_open(work->dest, O_CREAT | O_WRONLY | flags, 0);
	if (IS_ERR(dest)) {
		printk("Dest Open Error: (%ld) %s\n",  PTR_ERR(dest), work->dest);
		goto out;
	}

#ifdef DEBUG
	if(file_count(src) > 1){
		printk(" %s: is called file_count is %d \n", file_count(src));
	}	//20110514 by omw
#endif

	if( is_same_file(src->f_dentry->d_inode, dest->f_dentry->d_inode) ) {
		work->result = 1;
#ifdef DEBUG
		printk("Same files: (%s->%s)\n", work->src, work->dest);
#endif
		goto out;
	}

#ifdef DEBUG
	printk("Copying files (%s->%s)\n", work->src, work->dest);
	//dump_inode( src->f_dentry->d_name.name, src->f_dentry->d_inode );
	//dump_inode( dest->f_dentry->d_name.name, dest->f_dentry->d_inode );
#endif

	src->f_pos = dest->f_pos = 0;
	do {
		if (src->f_op->read) ret = src->f_op->read(src, buf, SM_BUF_SIZE, &src->f_pos); 
		else ret = do_sync_read(src, buf, SM_BUF_SIZE, &src->f_pos);
		if( ret > 0 ) {
			if (dest->f_op->write) ret = dest->f_op->write(dest, buf, ret, &dest->f_pos); 
			else ret = do_sync_write(dest, buf, ret, &dest->f_pos);
		}
		if( !sm_enable ){
			goto out;	//20110513 by omw
		}
	} while (ret > 0);

	if( ret == 0 ) {
		file_copy_attr( src->f_dentry->d_inode, dest->f_dentry->d_inode );
		work->result = 1;
#ifdef DEBUG
		printk("Copy Done: pos(%llu: %llu)\n", src->f_pos, dest->f_pos);
#endif
	}
	else {
		printk("Copy Fail!!! result=%d, (%s->%s)\n", ret, work->src, work->dest);
	}
out:
	if( src && !IS_ERR(src) )
		filp_close( src, current->files );
	if( dest && !IS_ERR(dest) )
		filp_close( dest, current->files );

	return 0;
}

int sm_file_mv(struct sm_work *work) {
	return 0;
}

int sm_file_rm(struct sm_work *work) {
	int error = 0;
	char * mirror;
	struct dentry *dentry;
	struct nameidata nd;
	struct inode *inode = NULL;
	int mutex_locked;

	if( sm_enable == 0 ) {
		work->result = 1;
		return 0;
	}

	work->result = 1;
	mirror = work->src;
	if( !mirror ) {
		return 0;
	}

	error = do_path_lookup(0, mirror, LOOKUP_PARENT, &nd);
	if (error) return error;
	if (nd.last_type != LAST_NORM) {
		path_put(&nd.path);
		return -EISDIR;
	}

	mutex_locked = mutex_trylock(&nd.path.dentry->d_inode->i_mutex);
	dentry = lookup_hash(&nd);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		if (!nd.last.name[nd.last.len]) {
			inode = dentry->d_inode;
			if (inode)
				atomic_inc(&inode->i_count);

			error = vfs_unlink(nd.path.dentry->d_inode, dentry);
		}
		dput(dentry);
	}
	if( mutex_locked )
		mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	if (inode) iput(inode);	
	path_put(&nd.path);
	return error;
}

int fill_inode64(void * __buf, const char * name, int namelen, loff_t offset, u64 ino, unsigned int d_type)
{
	struct sm_dirent64 *dirent;
	struct sm_callback64 * buf = (struct sm_callback64 *) __buf;
	int reclen = ALIGN(sizeof(struct sm_dirent64) + namelen + 1, sizeof(u64));

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
	{
		printk("%s: reclen:%d, buf count: %d\n", __func__, reclen, buf->count);
		return -EINVAL;
	}
	dirent = buf->previous;
	if (dirent) 
		dirent->d_off = offset;
	
	dirent = buf->current_dir;
	dirent->d_ino = ino;
	dirent->d_off = 0;
	dirent->d_reclen = reclen;
	dirent->d_type = d_type;
	dirent->namelen = namelen;
	memcpy(dirent->name, name, namelen);
	dirent->name[namelen] = 0;

	buf->previous = dirent;
	dirent = (void *)dirent + reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

long sm_readdir_inode(struct file *file, struct sm_dirent64 * dirent, unsigned int count)
{
	struct sm_dirent64 * last;
	struct sm_callback64 buf;
	int error;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, fill_inode64, &buf);
	if (error < 0)
		goto out;

	error = buf.error;
	last = buf.previous;
	if (last) {
		last->d_off = file->f_pos;
		error = count - buf.count;
	}
out:
	return error;
}

int sm_dir_cp(struct sm_work *work) 
{
	struct file *src = NULL;
	struct file *dest = NULL;
	struct inode *inode;
	struct sm_dirent64 *dirent = NULL, *ent;
	int res, slen, dlen;
	char *ptr;

#ifdef DEBUG
	printk("%s: source dir=%s, dest dir=%s\n", __FUNCTION__, work->src, work->dest);
#endif

	if (!sm_enable) {
		goto out;		//20110801 by omw
	}
	work->result = -1;

	src = filp_open(work->src, O_RDONLY , 0);
	if (IS_ERR(src)) {
		printk("%s: Open source directory error!: %s\n", __FUNCTION__, work->src);
		goto out;
	}

	inode = src->f_dentry->d_inode;
	if (!S_ISDIR(inode->i_mode)) {
		printk("%s: Not Directory: %s\n", __FUNCTION__, work->src);
		goto out;
	}
	if (IS_DEADDIR(inode) ) {
		printk("%s: Dead directory: %s\n", __FUNCTION__, work->src);
		goto out;
	}

	dest = filp_open(work->dest, O_RDONLY, 0 );
	if (IS_ERR(dest)) {
		printk("%s: creating dest directory: %s\n", __FUNCTION__, work->dest);
		sm_sys_mkdirat(work->dest, src->f_dentry, inode->i_mode);
	}
	else {
		filp_close( dest, current->files );
		dest = NULL;
	}


	dirent = kmalloc( DIR_ENT_SIZE, GFP_KERNEL);
	if( !dirent ) goto out;
	slen = strlen(work->src);
	dlen = strlen(work->dest);

	do {
		res = sm_readdir_inode(src, dirent, DIR_ENT_SIZE);
		if( res > 0 && res <= DIR_ENT_SIZE) {
			ent = dirent;
			while( (unsigned long)((long)ent - (long)dirent) < (unsigned long)res) 
			{
				/*
			    struct inode *in;
				in = ilookup(inode->i_sb, ent->d_ino);
				if (unlikely(!in)) {
					printk("%s: ino=%llu : Can't find INODE(ent-dirent=%ld)\n", __FUNCTION__, 
							ent->d_ino, (long)ent - (long)dirent + ent->d_reclen);
					break;
				}
				*/
				if( ent->namelen == 1 && *ent->name == '.' ) goto next;
				if( ent->namelen == 2 && *ent->name == '.' && *(ent->name+1) == '.') goto next;

				ptr = work->src + slen;
				strcpy(ptr, "/");
				strcat(ptr, ent->name);

				ptr = work->dest + dlen;
				strcpy(ptr, "/");
				strcat(ptr, ent->name);
				if( !sm_enable ){
					goto out;		//20110513 by omw
				}

				sm_file_cp(work);
next:
				ent = (struct sm_dirent64*)((long)ent + ent->d_reclen);
			}
		}
	} while(res > 0);

	work->result = 1;

out:
	if( dirent ) kfree( dirent );

	if( src && !IS_ERR(src) )
		filp_close( src, current->files );
	if( dest && !IS_ERR(dest) )
		filp_close( dest, current->files );

	return 0;
}

int sm_dir_mv(struct sm_work *work) {
	return 0;
}

int sm_dir_rm(struct sm_work *work) {
	return 0;
}

int sm_dispose_work( struct sm_work * work )
{
	// work->result
	// > 0: success
	// = 0: not executed yet & just re-add
	// < 0: execution failed & try again
	work->retry --;

	switch( work->type ) 
	{
		case WORK_TYPE_NORMAL:
			if( work->result > 0 || work->retry <= 0 ) 
				sm_del_work(work);
			else if( work->result == 0 ) 
				sm_add_tail_work(work);
			break;
		case WORK_TYPE_ORDERED:
			if( work->result > 0 || work->retry <= 0 ) 
				sm_del_work(work);
			else 
				sm_add_tail_work(work);
			break;
		case WORK_TYPE_ONESHOT:
		default:
			sm_del_work(work);
			break;
	}
	return 0;
}

int (*work_handler[])(struct sm_work *work) = {
	sm_dir_cp,		// WORK_CMD_DIR_CP
	sm_dir_mv,		// WORK_CMD_DIR_MV
	sm_dir_rm,		// WORK_CMD_DIR_RM
	sm_file_cp,		// WORK_CMD_FILE_CP
	sm_file_mv,		// WORK_CMD_FILE_MV
	sm_file_rm,		// WORK_CMD_FILE_RM
	NULL,			// WORK_CMD_LN
	NULL,			// WORK_CMD_CHOWN
	NULL			// WORK_CMD_CHMOD
};

int sm_do_work( struct sm_work * work )
{
	int ret = -1;

	if( work->cmd < WORK_CMD_MAX && work_handler[work->cmd] ) {
		ret = (work_handler[work->cmd])( work );
		
		sm_dispose_work( work );
	}
#if 0
	ret = kthread_run(run_user_cmd, (void*)work, "run_user_cmd");
#endif

	return ret;
}

static struct proc_dir_entry *sm_proc_init(void);
static void sm_proc_exit(void);

int asyncd_main(void * data)
{
	struct sm_work *work;
	int todo = 0;

	daemonize("sm_asyncd");
	printk("Starting Selective Mirror Daemon...\n");

	set_user_nice( current, 5 );

	sm_proc_init();

	do {
		//printk("before wait_event_interruptible_.. HZ:%x\n", HZ);
		wait_event_interruptible_timeout(sm_asyncd_wait, todo, HZ);
		
		try_to_freeze();

		work = sm_get_work();
		if( work && virt_addr_valid( work ) ) {
			sm_do_work( work );
			todo = !(list_empty(&sm_event_list));
		}
		else todo = 0;
		//todo = kthread_should_stop();
		//printk("end of do while %d \n",todo);
		//todo = 0;

	} while (1);//(!kthread_should_stop());
/*#if (!defined(CONFIG_MACH_NC5)||!defined(CONFIG_MACH_NT3))
	} while (!kthread_should_stop());
#else		
	} while (1);//(!kthread_should_stop());
#endif
*/
	sm_proc_exit();

	return 0;
}

int sm_init_buffer(void)
{
	int i;

	INIT_LIST_HEAD(&sm_event_list); 

	for(i=0; i< NR_CPUS; i++) 
	{
		g_buf[i] = (char*) kmalloc(SM_BUF_SIZE, GFP_KERNEL);
		if( !g_buf[i] ) return -1;
	}

	return 0;
}

#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#define SM_CMD_LEN	4096
static struct proc_dir_entry *base;

static int sm_proc_read_conf(char *page, char **start, off_t off,
		             int count, int *eof, void *data)
{
	int len;
	int i;

	if( !sm_path_conf ) {
		printk("SM: No configuration!\n");
		return 0;
	}

	read_lock(&sm_conf_lock);

	for(i=0, len=0; sm_path_conf[i].source; i++) 
	{
		len += sprintf(page + len, "%s  %s\n", sm_path_conf[i].source, sm_path_conf[i].mirror);
	}
	len -= off;
	*start = page + off;

	if( len > count )
		len = count;
	else *eof = 1;
	if( len < 0 ) len = 0;

	read_unlock(&sm_conf_lock);

	return len;
}

int sm_apply_conf(char * conf, int count)
{
	int i, ci;
	char *ptr; 
	static char *old = NULL;
	struct sm_path_list *cur = NULL;
	int error;							//omw
	struct nameidata nd;				//omw


	if( conf == NULL)
		goto free_conf;

	if( sm_path_conf == sm_path_conf_old ){
		cur = sm_path_conf_new;				//omw 20110516
	}
	else{
		cur = sm_path_conf_old;
	}
	
	

	ptr = conf;
	for(i=0, ci=0; i<count; i++) 
	{
		for(; i<count; i++) if (conf[i] == '\t') break;
		if( i == count ) goto error_out;

printk("%s: conf name:%s count:%d count_char:%c \n",__FUNCTION__,conf,count,conf[i]);
		if( conf[i] == '#' ) {
			for(; i<count; i++) if (conf[i] == '\r' && conf[i] == '\n') break;
			for(; i<count; i++) if (conf[i] != '\r' && conf[i] != '\n') break;
			continue;
		}


		cur[ci].source = ptr;
		conf[i++] = 0;
		error = do_path_lookup(AT_FDCWD, cur[ci].source, LOOKUP_DIRECTORY, &nd);
		if(0 == error){	 			//omw
			char * tmp;
			char * path_buf = (char*) kmalloc( 2*PATH_SIZE, GFP_KERNEL);
			tmp = d_path(&nd.path, path_buf, PATH_MAX);
			strcpy(cur[ci].source, tmp);	//omw
			path_put(&nd.path);
			kfree(path_buf);
		}

		for(; i<count; i++) if (conf[i] != '\t') break;
		if( i == count ) goto error_out;
		ptr = &conf[i];

		for(; i<count; i++) if (conf[i] == '\r' || conf[i] == '\n') break;
		if( i == count ) goto error_out;
		cur[ci].mirror = ptr;
		conf[i++] = 0;
		error = do_path_lookup(AT_FDCWD, cur[ci].mirror, LOOKUP_DIRECTORY, &nd);
		if(0 == error){				//omw
			char * tmp;
			char * path_buf = (char*) kmalloc( 2*PATH_SIZE, GFP_KERNEL);
			tmp = d_path(&nd.path, path_buf, PATH_MAX);
			strcpy(cur[ci].mirror, tmp);	//omw
			path_put(&nd.path);
			kfree(path_buf);
		}


		for(; i<count; i++) if (conf[i] != '\r' && conf[i] != '\n') break;
		ptr = &conf[i];

		if( sm_enable ) {
			struct sm_work *work;
			work = sm_alloc_work(WORK_CMD_DIR_CP, WORK_TYPE_NORMAL, cur[ci].source, cur[ci].mirror);
			sm_kick_worker(work);
		}
		ci ++;
	}
	cur[ci].source = cur[ci].mirror = NULL;
	

free_conf:
	// Now change conf. list
	write_lock(&sm_conf_lock);
printk("%s: sm_path_conf set :%x\n",__FUNCTION__,cur);
	sm_path_conf = cur;
	if( old ) vfree( old );
	old = conf;
	write_unlock(&sm_conf_lock);

	return ci;
error_out:
printk("%s: error_out\n",__FUNCTION__);

	write_unlock(&sm_conf_lock);
	return -1;
}


char *sm_read_conf_file(char *name, uint32_t *len)
{
	struct file *filp;
	uint32_t size;
	int ret, toread;
	char *buf = NULL;
	//unsigned long ds; 	LGNAS 20110404
	mm_segment_t ds;
#define CUR		 filp->f_pos

	filp = filp_open(name, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		*len = -1;
		goto out;
	}

	size = filp->f_dentry->d_inode->i_size;
	if( size == 0 ) {
		*len = size;
		goto out;
	}
	buf = vmalloc( size );
	if( !buf ) {
		*len = -1;
		goto out;
	}

	ds = get_fs();
	set_fs(get_ds());

	filp->f_pos = 0;
	do {
		toread = (size-CUR)>1024 ? 1024:size-CUR;
		if (filp->f_op->read) 
			ret = filp->f_op->read(filp, buf+CUR, toread, &filp->f_pos); 
		else 
			ret = do_sync_read(filp, buf+CUR, toread, &filp->f_pos);
	} while (ret > 0);

	if( ret < 0 ) {
		vfree( buf );
		buf = NULL;
		*len = -1;
	}
	else 
		*len = size;

	set_fs(ds);
out:
	if( filp && !IS_ERR(filp) )
		filp_close( filp, current->files );

	return buf;
}

static int sm_proc_write_conf(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	char *str, *buf;
	uint32_t buflen = 0;

	str = (char*)kmalloc(count + 32, GFP_KERNEL);
	if( !str ) return -ENOMEM;
	*str = 0;

	if( count > 0 ) {
		if (copy_from_user(str, buffer, count))
			return -EFAULT;
		str[count - 1] = 0;

		printk("SM Config File: %s (%lu)\n", str, count);

		buf = sm_read_conf_file(str, &buflen);
		if( !buf && (int)buflen < 0 ) 
			return -1;

		sm_apply_conf(buf, buflen);
	}

	return count;
}

static int sm_proc_read_sm_enable(char *page, char **start, off_t off, int count,
           int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", sm_enable);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;
	return len;
}

static int sm_proc_write_sm_enable(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if(rc) 
		return rc;

	if( c == '1' ) {
		sm_enable = 1;
	}
	else if( c == '0' )
		sm_enable = 0;
	return count;
}

static struct proc_dir_entry *sm_proc_init(void)
{
	struct proc_dir_entry *p;

	base = proc_mkdir("fs/s_mirror", NULL);
	if(!base) 
		printk("Can't create /proc/s_mirror directory\n");

	p = create_proc_entry("config", 0, base);
	if( p ) {
		p->read_proc = sm_proc_read_conf;
		p->write_proc = sm_proc_write_conf;
	}

	p = create_proc_entry("sm_enable", 0, base);
	if( p ) {
		p->read_proc = sm_proc_read_sm_enable;
		p->write_proc = sm_proc_write_sm_enable;
	}

	printk("/proc/s_mirror created !!!!!!!!!!!!!!!!!!!\n");

	return p;
}

static void sm_proc_exit(void)
{
	remove_proc_entry("cmd", base);
	remove_proc_entry("fs/s_mirror", NULL);
	printk("/proc/s_mirror deleted !!!!!!!!!!!!!!!!!!!\n");
}

int __init init_s_mirror(void)
{
	sm_init_buffer();

	asyncd_task = kthread_run(asyncd_main, NULL, "sm_asyncd");
	if (IS_ERR(asyncd_task)) {
		asyncd_task = NULL;
	}

	return 0;
}

module_init(init_s_mirror)

//late_initcall(init_s_mirror);


#if 0
static char *envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
static char *cp_file = "/bin/cp";
static char *mv_file = "/bin/mv";
static char *rm_file = "/bin/rm";
static char *ln_file = "/bin/ln";

int run_user_cmd(char * data)
{
	static char * argv[8];
	struct sm_work *work = (struct sm_work*)data;

	daemonize("run_user_cmd");

	printk("%s: cmd(%d), type(%d)\n", __FUNCTION__, work->cmd, work->type);
	printk("source = %s\n", work->src);
	printk("dest.  = %s\n", work->dest);

	memset( argv, 0, sizeof(char*)* 8);

	switch( work->cmd ) 
	{
		case WORK_CMD_FILE_CP:
			argv[0] = cp_file;
			argv[1] = work->src; argv[2] = work->dest;
			break;

		case WORK_CMD_FILE_MV:
			argv[0] = mv_file;
			argv[1] = work->src; argv[2] = work->dest;
			break;

		case WORK_CMD_FILE_RM:
			argv[0] = rm_file;
			argv[1] = work->src;
			break;

		case WORK_CMD_DIR_CP:
			argv[0] = cp_file;
			argv[1] = "-r";
			argv[2] = work->src; argv[3] = work->dest;
			break;

		case WORK_CMD_DIR_MV:
			argv[0] = mv_file;
			argv[1] = "-r";
			argv[2] = work->src; argv[3] = work->dest;
			break;

		case WORK_CMD_DIR_RM:
			argv[0] = rm_file;
			argv[1] = work->src;
			break;

		case WORK_CMD_LN:
			argv[0] = ln_file;
			argv[1] = "-s";
			argv[2] = work->src; argv[3] = work->dest;
			break;

		default:
			printk("%s: Unknown command (%d)", __FUNCTION__, work->cmd);
			work->result = -1;
			break;
	}
			
	if( argv[0] ) {
		work->result = call_usermodehelper(argv[0], argv, envp, 1);
	}

	printk("%s: result = %d\n", __FUNCTION__, work->result);
			
	sm_del_work( work );

	return 0;
}
#endif


