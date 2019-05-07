/*
 * Copyright (c) 1998-2017 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2017 Stony Brook University
 * Copyright (c) 2003-2017 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bkpfs.h"

typedef struct {
    int min_ver, max_ver;
    char filename[256];
} query_arg_t;

#define LIST_VERSIONS           _IOWR('q', 1, query_arg_t *)
#define DELETE_VERSION          _IOW('q', 2, int)
#define VIEW_VERSION            _IOWR('q', 3, int)
#define RESTORE_VERSION         _IOW('q', 4, int)

int new_ver_toggle = 0;

static ssize_t bkpfs_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	int err;
	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;
	
	UDBG;
	lower_file = bkpfs_lower_file(file);
	err = vfs_read(lower_file, buf, count, ppos);
	/* update our inode atime upon a successful lower read */
	if (err >= 0)
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));

	return err;
}


/*
 * bkpfs_unlink_backup - unlink a bkpfs filesystem object
 * @parent  : parent directory
 * @name    : victim
 * @version : version to be unlinked
 * 
 * This function looks for the specified version file in 
 * parent directory and unlinks it if found.
 *
 * Returns 0 on success, else returns the corresponding 
 * error codes.
 */
static
int bkpfs_unlink_backup(struct dentry *parent, char *name, int version) {
	char bkpf_name[256];
	int length;
        char* curr_ver_str;
	struct qstr this;
	struct dentry *bkpfile_dentry = NULL;
	int error = 0;

	length = snprintf(NULL, 0, "%d", version);
        curr_ver_str = kmalloc(length + 1, GFP_KERNEL);
        snprintf(curr_ver_str, length + 1, "%d", version);
        printk("INFO:current version string : %s\n",curr_ver_str);

	strcpy(bkpf_name, ".");
	strcat(bkpf_name, name);
        strcat(bkpf_name, ".");
        strcat(bkpf_name, curr_ver_str);
        printk("Name of the backup file : %s\n", bkpf_name);

        printk("Instatiate a new negative dentry\n");
        this.name = bkpf_name;
        this.len = strlen(bkpf_name);
        this.hash = full_name_hash(parent, this.name, this.len);
        bkpfile_dentry = d_lookup(parent, &this);
	if (!bkpfile_dentry) {
                printk("INFO: no such backup file exists!\n");
		error = -ENOENT;
                goto out;
        }
	printk("INFO:Backup file found. Yay!\n");

	parent = lock_parent(bkpfile_dentry);
	error = vfs_unlink(parent->d_inode, bkpfile_dentry, NULL);
	if(error) {
                printk("INFO:Failed in vfs_unlink\n");
                goto out;
        }
out:
        unlock_dir(parent);
	return error;
}

static ssize_t bkpfs_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	int err;
	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;
	
	UDBG;
	lower_file = bkpfs_lower_file(file);
	err = vfs_write(lower_file, buf, count, ppos);
	/* update our inode times+sizes upon a successful lower write */
	if (err >= 0) {
		fsstack_copy_inode_size(d_inode(dentry),
					file_inode(lower_file));
		fsstack_copy_attr_times(d_inode(dentry),
					file_inode(lower_file));
	}
	new_ver_toggle = 1;
	return err;
}

struct bkpfs_getdents_callback {
	struct dir_context ctx;
	struct dir_context *caller;
};


/* Inspired by generic filldir in fs/readdir.c
 * bkpfs_filldir - imposes filtering logic to hide backups
 * @ctx		  : The actor to feed the entries to
 * @lower_name 	  : string repesenting the dir name
 * @lower_namelen : length of lower_name
 *
 * This function implements logic to filter and display
 * the files that do not contain '.bkp' extension. This
 * is primarily used to implement the visibility
 * policy
 *
 * returns non-zero on success
 */
static
int bkpfs_filldir(struct dir_context *ctx, const char *lower_name,
 	int lower_namelen, loff_t offset, u64 ino, unsigned int d_type)
{
	struct bkpfs_getdents_callback *buf =
	container_of(ctx, struct bkpfs_getdents_callback, ctx);
	char *name;
	int rc;
	char *ext = ".bkp";

	name = (char *) &lower_name[lower_namelen - 4];
	if (!strcmp(ext, name))
		return 0;

	printk("Current filldir file : %s\n", lower_name);
	// Default filldir implementation
	rc = !dir_emit(buf->caller, lower_name, lower_namelen, ino, d_type);
	return rc;
}

/*
 * Modified the code to incorporate the filldir logic for
 * visibility policy.
 */
static int bkpfs_readdir(struct file *file, struct dir_context *ctx)
{
	int err;
	struct file *lower_file = NULL;
	struct dentry *dentry = file->f_path.dentry;
	struct bkpfs_getdents_callback buf = {
		.ctx.actor = bkpfs_filldir,
		.caller = ctx
	};

	UDBG;
	lower_file = bkpfs_lower_file(file);
	err = iterate_dir(lower_file, &buf.ctx);
	file->f_pos = lower_file->f_pos;
	if (err >= 0)		/* copy the atime */
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));
	return err;
}

/*
 * bkpfs_get_dentry - create dentry with @name in parent dir
 *
 * @parent : directory in which dentry need to be created
 * @name   : filename 
 *
 * This function looks for the dentry with name -  @name in the 
 * parent dir. It retrns the dentry if it already exists, else
 * it allocates a nre dentry and returns it
 *
 * returns dentry of the file with given name
 */
static
struct dentry  *bkpfs_get_dentry(struct dentry *parent, char *name) {
	struct qstr this;
	struct dentry *dentry = NULL;
	int error = 0;

	this.name = name;
        this.len = strlen(name);
        this.hash = full_name_hash(parent, this.name, this.len);
        dentry = d_lookup(parent, &this);
        if (!dentry) {
                printk("INFO: no such backup file exists!\n");
                printk("lower_dentry is null");
                dentry = d_alloc(parent, &this);
                if (!dentry)
                        error = -ENOMEM;
        }
        
        printk("INFO:Backup file found. Yay!\n");
	printk("INFO:error code %d\n", error);
	return dentry;
}

/*
 * bkpfs_get_attr - get the attribute for a given file.
 * @file : name of the file
 * @attr : name of the attr
 *
 * Return the value of the attr by lookin up the xattrs
 * of the given file.
 */

static
int bkpfs_get_attr(struct file *file, const char *attr) {
	struct file *lower_file;
	int attr_val = 0;
	
	lower_file = bkpfs_lower_file(file);

	vfs_getxattr(lower_file->f_path.dentry, attr,
                      (void *)&attr_val, sizeof(int));

	printk("INFO:attr_val for %s is  %d\n", attr, attr_val);
	return attr_val;
}

/*
 * bkpfs_set_attr - set the attribute for a given file.
 * @file    : name of the file
 * @attr    : name of the attr
 * attr_val : attr value
 *
 * sets the value of the attr by lookin up the xattrs
 * of the given file.
 */
static
void bkpfs_set_attr(struct file *file, const char *attr, int attr_val) {
	struct file *lower_file;
        lower_file = bkpfs_lower_file(file);
	
	vfs_setxattr(lower_file->f_path.dentry, attr,
                      (void *)&attr_val, sizeof(int), 0);
}

/*
 * bkpfs_delete_version - deleted the bkpfs filesystem object
 * @file    : file whose backup needs to be deleted
 * @version : version to be deleted
 *
 * Returns 0 on success, else returns the corresponding
 * error codes.
 */
static int
bkpfs_delete_version(struct file *file, int version) {
	int error = 0;
	int curr_version, old_version;
	
	struct dentry *dentry = file->f_path.dentry;
	struct dentry *lower_parent_dentry;
	struct file *lower_file;
	int i;
	struct dentry *bkpf_dentry = NULL;
	char name[256];

	old_version = bkpfs_get_attr(file, "user.old_version");
        curr_version = bkpfs_get_attr(file, "user.curr_version");
	
	if(old_version >= curr_version) {
		error = -EINVAL;
		printk("INFO: no such file exists!\n");
		goto out_err;
	}
	

	lower_file = bkpfs_lower_file(file);
	lower_parent_dentry = lower_file->f_path.dentry->d_parent;

	strcpy(name, ".");
	strcat(name, file->f_path.dentry->d_name.name);
        strcat(name, ".bkp");
        printk("INFO:Backup folder name:%s\n", name);

	bkpf_dentry = bkpfs_get_dentry(lower_parent_dentry, name);
	if (!bkpf_dentry) {
		printk("INFO: no such file exists!\n");
		goto out_err;
	}

	if (version == -2) {
		version = old_version;
		old_version++;
		bkpfs_unlink_backup(bkpf_dentry, (char *)dentry->d_name.name,
					version);
	} else if (version == -1) {
		curr_version--;
		version = curr_version;
		bkpfs_unlink_backup(bkpf_dentry, (char *)dentry->d_name.name,
					version);
	} else if (version == 0){
		for(i = old_version; i < curr_version ; i++) {
			bkpfs_unlink_backup(bkpf_dentry,
				(char *)dentry->d_name.name, i);
		}
		old_version = curr_version;
	}

	bkpfs_set_attr(file, "user.old_version", old_version);
	bkpfs_set_attr(file, "user.curr_version", curr_version);
	
out_err:
	return error;
}

/*
 * bkpfs_restore_version - restore the bkpfs filesystem object
 * @file    : file whose backup needs to be restored
 * @version : version to be restored
 *
 * This functions creates a new file with '.rec' extension
 * in the same directory as the file in READONLY mode. This file
 * can be inspected , deleted, or copied over the main file.
 *
 * It also creates a temp file with .vue extension to view the
 * contents of the file.
 *
 * Returns 0 on success, else returns the corresponding
 * error codes.
 */
static int
bkpfs_restore_version(struct file *file, int version, int isVue) {
	int error = 0;
	
	struct dentry *lower_parent_dentry;
	struct dentry *bkp_file_dentry;
	struct dentry *bkp_dir_dentry;
        struct file *lower_file = NULL;
	struct file *backup_file = NULL;

	char bkp_dir[256];
	char bkp_file[256];

	int length;
        char* s_version = NULL;
	size_t size;
	struct path backup_path;

	struct path rec_path;
	struct file *rec_file = NULL;
	struct dentry *rec_dentry;

	int min_ver, max_ver;
	min_ver = bkpfs_get_attr(file, "user.old_version");
	max_ver = bkpfs_get_attr(file, "user.curr_version");

	if (version == -2) {
                version = min_ver;
        } else if (version == -1) {
                version = max_ver - 1;
        } else if (version < min_ver || version >= max_ver) {
		printk("ERROR: Invalid input");
		goto out_err;
	}
	
	lower_file = bkpfs_lower_file(file);
        lower_parent_dentry = lower_file->f_path.dentry->d_parent;
	
	strcpy(bkp_dir, ".");
	strcat(bkp_dir, file->f_path.dentry->d_name.name);
        strcat(bkp_dir, ".bkp");
        printk("INFO:Backup folder name:%s\n", bkp_dir);

        bkp_dir_dentry = bkpfs_get_dentry(lower_parent_dentry, bkp_dir);
        if (!bkp_dir_dentry) {
                printk("INFO: no such file exists!\n");
                goto out_err;
        }

	length = snprintf(NULL, 0, "%d", version);
        s_version = kmalloc(length + 1, GFP_KERNEL);
        snprintf(s_version, length + 1, "%d", version);
        printk("INFO:version string : %s\n", s_version);

	strcpy(bkp_file, ".");
	strcat(bkp_file, file->f_path.dentry->d_name.name);
	strcat(bkp_file, ".");
	strcat(bkp_file, s_version);
	printk("INFO:restore from : %s\n", bkp_file);
	bkp_file_dentry = bkpfs_get_dentry(bkp_dir_dentry, bkp_file);
	if (!bkp_file_dentry) {
                printk("INFO: no such file exists!\n");
                goto out_err;
        }

	// Create path struct for backup file
	backup_path.dentry = bkp_file_dentry;
        backup_path.mnt = lower_file->f_path.mnt;
        printk("INFO:created backup path struct. Fetching fp.\n");
	if (backup_path.dentry == NULL)
		goto out_err;
        backup_file = dentry_open(&backup_path, O_RDONLY, current_cred());
        path_put(&backup_path);
        if (IS_ERR(backup_file)) {
                error = PTR_ERR(backup_file);
                printk("ERROR:failed in dentry_open\n");
                goto out_err;
        }

	// Create new file for recovery 
	if (isVue) {
		strcpy(bkp_file, file->f_path.dentry->d_name.name);
        	strcat(bkp_file, ".");
        	strcat(bkp_file, s_version);	
		strcat(bkp_file, ".vue");
	}
	else
		strcat(bkp_file, ".swp");
	printk("Instatiate a new negative dentry for rec file\n");
	rec_dentry = bkpfs_get_dentry(lower_parent_dentry , bkp_file);
	if (rec_dentry != NULL)
                printk("INFO:rec dentry name : %s",rec_dentry->d_name.name);
       	 
	// Create inode for the rec file in readonly mode
	lower_parent_dentry = lock_parent(rec_dentry);
        error = vfs_create(d_inode(lower_parent_dentry), rec_dentry,
                                0444, true);
        if (error)
                printk("ERROR:failed while vfs_create for rec\n");
	unlock_dir(lower_parent_dentry);
        d_add(rec_dentry, NULL);
        printk("INFO:created new rec file.\n");

	// Get file structure i.e fp
	rec_path.dentry = rec_dentry;
	rec_path.mnt = lower_file->f_path.mnt;
	printk("INFO:created rec path struct. Fetching fp.\n");
        rec_file = dentry_open(&rec_path, O_WRONLY, current_cred());
        path_put(&rec_path);
        if (IS_ERR(rec_file)) {
                error = PTR_ERR(rec_file);
                printk("ERROR:failed in dentry_open for rec\n");
                goto out_err;
        } 

	// Writing contents to backup file
	size = backup_file->f_inode->i_size;
        rec_file->f_pos = 0;
        backup_file->f_pos = 0;

        backup_file->f_flags &= ~(O_APPEND);
        error = vfs_copy_file_range(backup_file, backup_file->f_pos, rec_file,
                                  rec_file->f_pos, size, 0);	
	
out_err:
	if(rec_file)
                fput(rec_file);
	if(backup_file)
		fput(backup_file);
	printk("INFO:error code is %d\n",error);
	return error;
}

/*
 * bkpfs_list_version - populates the min and max version for a 
 * 			given file
 * @file : file whose backup needs to be restored
 * @arg  : address where the versions need to be placed
 *
 * fetches the xattrs and copies them to the query_arg_t structure.
 *
 * returns 0 on success, else returns corresponding err code.
 */
static int
bkpfs_list_version(struct file *file, unsigned long arg) {
	query_arg_t q;
	int curr_version = 0;
        int old_version = 0;
	char *filename;
	int err = 0;

	old_version = bkpfs_get_attr(file, "user.old_version");
        curr_version = bkpfs_get_attr(file, "user.curr_version");

	q.min_ver = old_version;
	q.max_ver = curr_version;
	
	filename = (char *)file->f_path.dentry->d_name.name;

	strcpy(q.filename, filename);
	if (copy_to_user((query_arg_t *)arg, &q, sizeof(query_arg_t))) {
		printk("ERROR: failed in copy_to_user");
        	err = -EACCES;
        }
	return err;
}

static long bkpfs_unlocked_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	long err = -ENOTTY;
	int error = 0;
	struct file *lower_file;
	UDBG;
	lower_file = bkpfs_lower_file(file);

	switch(cmd) {
		case LIST_VERSIONS:
			printk("INFO:listing version");
			bkpfs_list_version(file, arg);
		break;
		case DELETE_VERSION:
			printk("INFO:deleting version %d", (int) arg);
			error = bkpfs_delete_version(file, (int) arg);
                break;
		case VIEW_VERSION:
			printk("INFO:restoring version\n");
			bkpfs_restore_version(file, (int) arg, 1);
                break;
		case RESTORE_VERSION:
			printk("INFO:restoring version %d",(int) arg);
			error = bkpfs_restore_version(file, (int) arg, 0);
                break;
		default:
			/* XXX: use vfs_ioctl if/when VFS exports it */
			if (!lower_file || !lower_file->f_op)
				goto out;
			if (lower_file->f_op->unlocked_ioctl)
				err = lower_file->f_op->unlocked_ioctl(lower_file, cmd, arg);

			/* some ioctls can change inode attributes (EXT2_IOC_SETFLAGS) */
			if (!err)
				fsstack_copy_attr_all(file_inode(file),
				      			file_inode(lower_file));
			
			break;
	}
out:
	return err;
}

#ifdef CONFIG_COMPAT
static long bkpfs_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file;

	UDBG;
	lower_file = bkpfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->compat_ioctl)
		err = lower_file->f_op->compat_ioctl(lower_file, cmd, arg);

out:
	return err;
}
#endif

static int bkpfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err = 0;
	bool willwrite;
	struct file *lower_file;
	const struct vm_operations_struct *saved_vm_ops = NULL;

	UDBG;
	/* this might be deferred to mmap's writepage */
	willwrite = ((vma->vm_flags | VM_SHARED | VM_WRITE) == vma->vm_flags);

	/*
	 * File systems which do not implement ->writepage may use
	 * generic_file_readonly_mmap as their ->mmap op.  If you call
	 * generic_file_readonly_mmap with VM_WRITE, you'd get an -EINVAL.
	 * But we cannot call the lower ->mmap op, so we can't tell that
	 * writeable mappings won't work.  Therefore, our only choice is to
	 * check if the lower file system supports the ->writepage, and if
	 * not, return EINVAL (the same error that
	 * generic_file_readonly_mmap returns in that case).
	 */
	lower_file = bkpfs_lower_file(file);
	if (willwrite && !lower_file->f_mapping->a_ops->writepage) {
		err = -EINVAL;
		printk(KERN_ERR "bkpfs: lower file system does not "
		       "support writeable mmap\n");
		goto out;
	}

	/*
	 * find and save lower vm_ops.
	 *
	 * XXX: the VFS should have a cleaner way of finding the lower vm_ops
	 */
	if (!BKPFS_F(file)->lower_vm_ops) {
		err = lower_file->f_op->mmap(lower_file, vma);
		if (err) {
			printk(KERN_ERR "bkpfs: lower mmap failed %d\n", err);
			goto out;
		}
		saved_vm_ops = vma->vm_ops; /* save: came from lower ->mmap */
	}

	/*
	 * Next 3 lines are all I need from generic_file_mmap.  I definitely
	 * don't want its test for ->readpage which returns -ENOEXEC.
	 */
	file_accessed(file);
	vma->vm_ops = &bkpfs_vm_ops;

	file->f_mapping->a_ops = &bkpfs_aops; /* set our aops */
	if (!BKPFS_F(file)->lower_vm_ops) /* save for our ->fault */
		BKPFS_F(file)->lower_vm_ops = saved_vm_ops;

out:
	return err;
}

static int bkpfs_open(struct inode *inode, struct file *file)
{
	int err = 0;
	struct file *lower_file = NULL;
	struct path lower_path;

	UDBG;
	/* don't open unhashed/deleted files */
	if (d_unhashed(file->f_path.dentry)) {
		err = -ENOENT;
		goto out_err;
	}

	file->private_data =
		kzalloc(sizeof(struct bkpfs_file_info), GFP_KERNEL);
	if (!BKPFS_F(file)) {
		err = -ENOMEM;
		goto out_err;
	}

	/* open lower object and link bkpfs's file struct to lower's */
	bkpfs_get_lower_path(file->f_path.dentry, &lower_path);
	lower_file = dentry_open(&lower_path, file->f_flags, current_cred());
	path_put(&lower_path);
	if (IS_ERR(lower_file)) {
		err = PTR_ERR(lower_file);
		lower_file = bkpfs_lower_file(file);
		if (lower_file) {
			bkpfs_set_lower_file(file, NULL);
			fput(lower_file); /* fput calls dput for lower_dentry */
		}
	} else {
		bkpfs_set_lower_file(file, lower_file);
	}

	if (err)
		kfree(BKPFS_F(file));
	else
		fsstack_copy_attr_all(inode, bkpfs_lower_inode(inode));
out_err:
	return err;
}

static int bkpfs_flush(struct file *file, fl_owner_t id)
{
	int err = 0;
	struct file *lower_file = NULL;

	UDBG;
	lower_file = bkpfs_lower_file(file);
	if (lower_file && lower_file->f_op && lower_file->f_op->flush) {
		filemap_write_and_wait(file->f_mapping);
		err = lower_file->f_op->flush(lower_file, id);
	}

	return err;
}

/*
 * bkpfs_create_new_backup - creates a new backup file, when ever 
 * 			     the file is opened in write mode
 * @file : the file for which backups need to be created.
 *
 * creates a backup file by making use of the min and max versions.
 */
static void
bkpfs_create_new_backup(struct file *file) {
	int error;
	struct file *lower_file;
	struct dentry *lower_parent_dentry = NULL;
	struct qstr this;
	struct qstr bkp_qstr;
	char name[256];
	char bkp_name[256];
	struct dentry *bkpf_dentry = NULL;
	struct dentry *bkpfile_dentry;

	const char *v_new = "user.curr_version";
        int curr_version;
        int length;
        char* curr_ver_str = NULL;
        int version_diff;

        const char *v_old = "user.old_version";
        int old_version;

	struct file *backup_file = NULL;
	size_t size;
	struct path backup_path;

	UDBG;
        lower_file = bkpfs_lower_file(file);
	lower_parent_dentry = lower_file->f_path.dentry->d_parent;

	strcpy(name, ".");	
	strcat(name, file->f_path.dentry->d_name.name);
	strcat(name, ".bkp");
        printk("INFO:Backup folder name:%s\n", name);

	printk("Lookup for backup folder dentry\n");
        this.name = name;
        this.len = strlen(name);
        this.hash = full_name_hash(lower_parent_dentry, this.name, this.len);
        bkpf_dentry = d_lookup(lower_parent_dentry, &this);
	if (!bkpf_dentry) {
		printk("INFO: no such backup folder exists!\n");
		goto out_err;
	}
	printk("INFO:Backup folder found. Yay!\n");

	// Logic to retive the current version of backup for xattr
	error = vfs_getxattr(lower_file->f_path.dentry, v_new,
                             (void *)&curr_version, sizeof(int));
        printk("INFO:current version %d\n",curr_version);
	
	length = snprintf(NULL, 0, "%d", curr_version);
        curr_ver_str = kmalloc(length + 1, GFP_KERNEL);
        snprintf(curr_ver_str, length + 1, "%d", curr_version);
        printk("INFO:current version string : %s\n",curr_ver_str);

	strcpy(bkp_name, ".");
	strcat(bkp_name, file->f_path.dentry->d_name.name);
        strcat(bkp_name, ".");
        strcat(bkp_name, curr_ver_str);
        printk("Name of the backup file : %s\n", bkp_name);

	printk("Instatiate a new negative dentry\n");
        bkp_qstr.name = bkp_name;
        bkp_qstr.len = strlen(bkp_name);
        bkp_qstr.hash = full_name_hash(bkpf_dentry, bkp_qstr.name, bkp_qstr.len);
        bkpfile_dentry = d_lookup(bkpf_dentry, &bkp_qstr);
	if (bkpfile_dentry != NULL)
		printk("INFO:dentry name : %s",bkpfile_dentry->d_name.name);
        if (!bkpfile_dentry) {
                printk("lower_dentry is null");
                bkpfile_dentry = d_alloc(bkpf_dentry, &bkp_qstr);
                if (!bkpfile_dentry) {
                        error = -ENOMEM;
                        goto out_err;
                }
        }
	bkpf_dentry = lock_parent(bkpfile_dentry);
	// Making the files read only
        error = vfs_create(d_inode(bkpf_dentry), bkpfile_dentry,
                        	0444, true);
        if (error)
                printk("ERROR:failed while vfs_create\n");
   
        unlock_dir(bkpf_dentry);
        d_add(bkpfile_dentry, NULL);
	printk("INFO:created new backup file.\n");

	// Create path struct for backup file
	backup_path.dentry = bkpfile_dentry;
	backup_path.mnt = lower_file->f_path.mnt;
	printk("INFO:created backup path struct. Fetching fp.\n");
	backup_file = dentry_open(&backup_path, O_WRONLY, current_cred());
        path_put(&backup_path);
        if (IS_ERR(backup_file)) {
                error = PTR_ERR(backup_file);
                printk("ERROR:failed in dentry_open\n");
                goto out_err;
        }
	
	// Writing contents to backup file
	size = lower_file->f_inode->i_size;
	lower_file->f_pos = 0;
        backup_file->f_pos = 0;
	
	lower_file->f_mode |= FMODE_READ;
        backup_file->f_mode &= ~(O_APPEND);
        error = vfs_copy_file_range(lower_file, lower_file->f_pos, backup_file,
                                  backup_file->f_pos, size, 0);
        if (error >= 0) {
                fsstack_copy_inode_size(d_inode(bkpfile_dentry),
                                        file_inode(backup_file));
                fsstack_copy_attr_times(d_inode(bkpfile_dentry),
                                        file_inode(backup_file));
        }

	// Logic to maintain maxver number of backups
	error = vfs_getxattr(lower_file->f_path.dentry, v_old,
                                (void *)&old_version, sizeof(int));
        printk("INFO:oldest version : %d\n", old_version);
	version_diff = curr_version - old_version;
        if (version_diff >= BKPFS_SB(file->f_path.dentry->d_sb)->maxver) {
                bkpfs_unlink_backup(bkpf_dentry, (char *) file->f_path.dentry->d_name.name,
				old_version);
                old_version++;
                printk("INFO:oldest version as a result of max_ver exceed : %d\n", old_version);
                error = vfs_setxattr(lower_file->f_path.dentry, v_old,
                                        (void *)&old_version, sizeof(int), 0);
		printk("Unlinked the backup file version : %d\n", version_diff);
	}
	curr_version++;
	error = vfs_setxattr(lower_file->f_path.dentry, v_new,
				(void *)&curr_version, sizeof(int), 0);
	printk("INFO:Done setting version to : %d", curr_version);
	printk("End of my code\n");

out_err:
	printk("INFO: my error code %d\n", error);
	if (backup_file)
		fput(backup_file);
	if (curr_ver_str)
		kfree(curr_ver_str);
}

/* release all lower object references & free the file info structure */
static int bkpfs_file_release(struct inode *inode, struct file *file)
{
	struct file *lower_file;

	UDBG;
	lower_file = bkpfs_lower_file(file);
	/*
	 * Taggle to make sure that backup is created only when bkpfs_write
	 * is invoked
	 */
	if (new_ver_toggle == 1) {
		bkpfs_create_new_backup(file);
		new_ver_toggle = 0;
	}

	if (lower_file) {
		bkpfs_set_lower_file(file, NULL);
		fput(lower_file);
	}

	kfree(BKPFS_F(file));
	return 0;
}

static int bkpfs_fsync(struct file *file, loff_t start, loff_t end,
			int datasync)
{
	int err;
	struct file *lower_file;
	struct path lower_path;
	struct dentry *dentry = file->f_path.dentry;

	UDBG;
	err = __generic_file_fsync(file, start, end, datasync);
	if (err)
		goto out;
	lower_file = bkpfs_lower_file(file);
	bkpfs_get_lower_path(dentry, &lower_path);
	err = vfs_fsync_range(lower_file, start, end, datasync);
	bkpfs_put_lower_path(dentry, &lower_path);
out:
	return err;
}

static int bkpfs_fasync(int fd, struct file *file, int flag)
{
	int err = 0;
	struct file *lower_file = NULL;

	UDBG;
	lower_file = bkpfs_lower_file(file);
	if (lower_file->f_op && lower_file->f_op->fasync)
		err = lower_file->f_op->fasync(fd, lower_file, flag);

	return err;
}

/*
 * Bkpfs cannot use generic_file_llseek as ->llseek, because it would
 * only set the offset of the upper file.  So we have to implement our
 * own method to set both the upper and lower file offsets
 * consistently.
 */
static loff_t bkpfs_file_llseek(struct file *file, loff_t offset, int whence)
{
	int err;
	struct file *lower_file;

	UDBG;
	err = generic_file_llseek(file, offset, whence);
	if (err < 0)
		goto out;

	lower_file = bkpfs_lower_file(file);
	err = generic_file_llseek(lower_file, offset, whence);

out:
	return err;
}

/*
 * Bkpfs read_iter, redirect modified iocb to lower read_iter
 */
ssize_t
bkpfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err;
	struct file *file = iocb->ki_filp, *lower_file;

	UDBG;
	lower_file = bkpfs_lower_file(file);
	if (!lower_file->f_op->read_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->read_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode atime as needed */
	if (err >= 0 || err == -EIOCBQUEUED)
		fsstack_copy_attr_atime(d_inode(file->f_path.dentry),
					file_inode(lower_file));
out:
	return err;
}

/*
 * Bkpfs write_iter, redirect modified iocb to lower write_iter
 */
ssize_t
bkpfs_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err;
	struct file *file = iocb->ki_filp, *lower_file;

	UDBG;
	lower_file = bkpfs_lower_file(file);
	if (!lower_file->f_op->write_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->write_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode times/sizes as needed */
	if (err >= 0 || err == -EIOCBQUEUED) {
		fsstack_copy_inode_size(d_inode(file->f_path.dentry),
					file_inode(lower_file));
		fsstack_copy_attr_times(d_inode(file->f_path.dentry),
					file_inode(lower_file));
	}
out:
	return err;
}

const struct file_operations bkpfs_main_fops = {
	.llseek		= generic_file_llseek,
	.read		= bkpfs_read,
	.write		= bkpfs_write,
	.unlocked_ioctl	= bkpfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bkpfs_compat_ioctl,
#endif
	.mmap		= bkpfs_mmap,
	.open		= bkpfs_open,
	.flush		= bkpfs_flush,
	.release	= bkpfs_file_release,
	.fsync		= bkpfs_fsync,
	.fasync		= bkpfs_fasync,
	.read_iter	= bkpfs_read_iter,
	.write_iter	= bkpfs_write_iter,
};

/* trimmed directory options */
const struct file_operations bkpfs_dir_fops = {
	.llseek		= bkpfs_file_llseek,
	.read		= generic_read_dir,
	.iterate	= bkpfs_readdir,
	.unlocked_ioctl	= bkpfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= bkpfs_compat_ioctl,
#endif
	.open		= bkpfs_open,
	.release	= bkpfs_file_release,
	.flush		= bkpfs_flush,
	.fsync		= bkpfs_fsync,
	.fasync		= bkpfs_fasync,
};
