#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/buffer_head.h>
#include "async_copy_directory.h"
#include <linux/syscalls.h>
#include <linux/splice.h>
#include <linux/fs.h>
#include <linux/path.h>

static bool async_copy_actor(struct dir_context *ctx, const char *name, int namlen, loff_t offset, u64 ino, unsigned d_>

struct async_copy_work {
    struct work_struct work;
    int src_fd;
    int dst_fd;
    char* src_dir;
    char* dst_dir;
};

struct async_copy_context {
    struct dir_context ctx;
    struct path *dst_path;
    struct path *src_path;
    struct path **src_dir;};

static bool async_copy_actor(struct dir_context *ctx, const char *name, int namlen, loff_t offset, u64 ino, unsigned in>{
    struct async_copy_context *acc = container_of(ctx, struct async_copy_context, ctx);
    struct path *src_path = acc->src_path;
    struct path src_entry_path, dst_entry_path;
    struct dentry *src_entry;
    struct file *src_file;
    struct file *dst_file;
    struct qstr qstr = QSTR_INIT(name, namlen);
    int error;
    acc->src_dir = &src_path;

    // Ignore the "." and ".." entries
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }

    // Build the source and destination entry paths
    src_entry = d_lookup((*acc->src_dir)->dentry, &qstr);
    if (!src_entry) {
        return -ENOENT;
    }

    src_entry_path.mnt = src_path->mnt;
    src_entry_path.dentry = src_entry;

    error = kern_path(name, LOOKUP_FOLLOW, &dst_entry_path);
    if (error) {    return error;
    }

    if (d_type == DT_DIR) {
        // Create a directory in the destination path
        error = vfs_mkdir(src_entry->d_inode->i_sb->s_user_ns, dst_entry_path.dentry->d_inode, dst_entry_path.dentry, S>    } else if (d_type == DT_REG) {
        // Copy the file from the source path to the destination path
        error = do_splice_direct(src_file, &src_file->f_pos, dst_file, &dst_file->f_pos, src_entry->d_inode->i_size, 0);    } else {
        // Unsupported entry type, return an error
        error = -EINVAL;
    }

    // Release the path references
    path_put(&src_entry_path);
    path_put(&dst_entry_path);

    return error;
}

static void async_copy_directory_worker(struct work_struct *work) {
    struct async_copy_work *acw;
    struct file *src_file;
    struct file *dst_file;
    struct inode *src_inode;
    struct path dst_path;
    struct file *dst_dir_file;
    struct file *src_dir_file;
    struct async_copy_context acc = {
        .ctx.actor = async_copy_actor,    .dst_path = &dst_path,
    };
    int error = 0;
    acw = container_of(work, struct async_copy_work, work);
    src_file = fget(acw->src_fd);
    dst_file = fget(acw->dst_fd);
    src_inode = src_file->f_path.dentry->d_inode;
    dst_path = dst_file->f_path;
    if (src_file == NULL || dst_file == NULL) {
        error = -EBADF;
        goto out;
    }

    if (!S_ISDIR(src_inode->i_mode)) {
        error = -ENOTDIR;
        goto out;
    }

    error = vfs_mkdir(src_inode->i_sb->s_user_ns, dst_path.dentry->d_inode, dst_path.dentry, S_IRWXU | S_IRWXG | S_IRWX>    if (error) {
        goto out;
    }
    dst_dir_file = dentry_open(&dst_path, O_DIRECTORY, current_cred());
    if (IS_ERR(dst_dir_file)) {
        error = PTR_ERR(dst_dir_file);
        goto out;
    }

    src_dir_file = dentry_open(&src_file->f_path, O_DIRECTORY, current_cred());
    if (IS_ERR(src_dir_file)) {    error = PTR_ERR(src_dir_file);
        goto out_close_dst_dir_file;
    }

    error = iterate_dir(src_dir_file, &acc.ctx);
    if (error) {
        goto out_close_src_dir_file;
    }

out_close_src_dir_file:
    fput(src_dir_file);
out_close_dst_dir_file:
    fput(dst_dir_file);
out:
    fput(src_file);
    fput(dst_file);
    if (error) {
        pr_err("async_copy_directory_worker: error %d\n", error);
    }
}

SYSCALL_DEFINE2(async_copy_directory, const char __user *, src_dir, const char __user *, dst_dir) {
    struct async_copy_work *work;
    size_t len;
    char *k_src_dir, *k_dst_dir;

    // Copy src_dir and dst_dir from user space to kernel space
    len = strnlen_user(src_dir, PATH_MAX);
    k_src_dir = kmalloc(len, GFP_KERNEL);
    if (!k_src_dir) {
        return -ENOMEM; }
    if (copy_from_user(k_src_dir, src_dir, len)) {
        kfree(k_src_dir);
        return -EFAULT;
    }

    len = strnlen_user(dst_dir, PATH_MAX);
    k_dst_dir = kmalloc(len, GFP_KERNEL);
    if (!k_dst_dir) {
        kfree(k_src_dir);
        return -ENOMEM;
    }
    if (copy_from_user(k_dst_dir, dst_dir, len)) {
        kfree(k_src_dir);
        kfree(k_dst_dir);
        return -EFAULT;
    }

    // Allocate work struct
    work = kzalloc(sizeof(*work), GFP_KERNEL);
    if (!work) {
        kfree(k_src_dir);
        kfree(k_dst_dir);
        return -ENOMEM;
    }

    INIT_WORK(&work->work, async_copy_directory_worker);
    work->src_dir = k_src_dir;
    work->dst_dir = k_dst_dir;

    // Queue the work
    schedule_work(&work->work);

    return 0;
}