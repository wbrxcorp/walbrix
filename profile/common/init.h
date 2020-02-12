#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <blkid/blkid.h>
#include <libmount/libmount.h>
#include <linux/limits.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/statvfs.h>

#ifdef INIFILE
#include <iniparser.h>
#endif

struct partition_struct {
  char device[PATH_MAX];
  char type[PATH_MAX];
};

#define S_755 (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)

#ifndef MS_MOVE
#define MS_MOVE 8192
#endif

#define CP "/bin/cp"
#define CAT "/bin/cat"
#define TAR "/bin/tar"
#define SWITCH_ROOT "/sbin/switch_root"
#define SWAPON "/sbin/swapon"
#define MKSWAP "/sbin/mkswap"
#define UMOUNT "/bin/umount"
#define MKFS_XFS "/sbin/mkfs.xfs"
#define PASSWD "/usr/bin/passwd"
#define CHPASSWD "/usr/sbin/chpasswd"

void halt()
{
  reboot(RB_HALT_SYSTEM);
}

void mkdir_p(const char *dir)
{
  const char *psrc = dir;
  char *buf = (char *)malloc(strlen(dir) + 1);
  char *pdst = buf;
  *buf = '\0';
  while (*psrc) {
    if (*psrc == '/' && strlen(buf) > 0) {
      mkdir(buf, S_755);
    }
    *pdst++ = *psrc++;
    *pdst = '\0';
  }
  mkdir(buf, S_755);
  free(buf);
}

int mount_procdevsys()
{
  mkdir("/proc", S_755);
  if (mount("proc", "/proc", "proc", MS_NOEXEC|MS_NOSUID|MS_NODEV, "") < 0) return -1;
  mkdir("/dev", S_755);
  if (mount("udev", "/dev", "devtmpfs", MS_NOSUID, "mode=0755,siz=10M") < 0) return -1;
  mkdir("/sys", S_755);
  return mount("sysfs", "/sys", "sysfs", MS_NOEXEC|MS_NOSUID|MS_NODEV, "");
}


void mount_procdevsys_or_die()
{
  if (mount_procdevsys() < 0) {
    perror("mount_procdevsys");
    halt();
  }
}

int search_partition(const char *type, const char *value, struct partition_struct *partition)
{
  blkid_dev dev = NULL;
  blkid_dev_iterate iter;
  blkid_tag_iterate tag_iter;
  blkid_cache cache;
  const char *_type, *_value;
  blkid_get_cache(&cache, "/dev/null");
  blkid_probe_all(cache);
  iter = blkid_dev_iterate_begin(cache);
  blkid_dev_set_search(iter, type, value);
  while (blkid_dev_next(iter, &dev) == 0) {
    dev = blkid_verify(cache, dev);
    if (dev) break;
  }
  blkid_dev_iterate_end(iter);

  if (!dev) {
    errno = ENOENT;
    return -1;
  }
  strcpy(partition->device, blkid_dev_devname(dev));

  partition->type[0] = '\0';
  tag_iter = blkid_tag_iterate_begin(dev);
  while (blkid_tag_next(tag_iter, &_type, &_value) == 0) {
    if (strcmp(_type,"TYPE") == 0) {
      strcpy(partition->type, _value);
      break;
    }
  }
  blkid_tag_iterate_end(tag_iter);
  blkid_put_cache(cache);

  return 0;
}

int search_partition_by_uuid(const char *uuid, struct partition_struct *partition)
{
  return search_partition("UUID", uuid, partition);
}

int search_partition_by_fstype(const char *fstype, struct partition_struct *partition)
{
  return search_partition("TYPE", fstype, partition);
}

int search_boot_partition(struct partition_struct *partition, int max_retry)
{
  int retry_count;
  const char *boot_partition_uuid;
  boot_partition_uuid = getenv("boot_partition_uuid");
  if (!boot_partition_uuid) {
    errno = ENOENT;
    return -1;
  }
  // else

  for (retry_count = 0 ; retry_count < max_retry + 1; retry_count++) {
    if (search_partition_by_uuid(boot_partition_uuid, partition) == 0) break;
    // else
    sleep(retry_count);
  }

  if (retry_count == max_retry + 1) {
    errno = ENOENT;
    return -1;
  }
  // else
  return 0;
}

void search_partition_by_fstype_or_die(const char *fstype, struct partition_struct *partition, int max_retry)
{
  int retry_count;
  for (retry_count = 0 ; retry_count < max_retry + 1; retry_count++) {
    if (search_partition_by_fstype(fstype, partition) == 0) break;
    // else
    sleep(retry_count);
  }

  if (retry_count == max_retry + 1) {
    perror("search_partition_by_fstype");
    halt();
  }
}

void mount_or_die(const char *source, const char *target,
                 const char *filesystemtype, unsigned long mountflags,
                 const void *data)
{
  mkdir_p(target);
  if (mount(source, target, filesystemtype, mountflags, data) < 0) {
    char *buf = (char *)malloc(strlen(target) + 7);
    if (buf) {
      strcpy(buf, "mount ");
      strcat(buf, target);
      perror(buf);
      free(buf);
    }
    halt();
  }
  // else
  printf("%s mounted.\n", target);
}

int mount_overlay(const char *lowerdir, const char *upperdir, const char *workdir, const char *mountpoint)
{
  char buf[PATH_MAX * 5];
  sprintf(buf, "lowerdir=%s,upperdir=%s,workdir=%s", lowerdir, upperdir, workdir);
  return mount("overlay", mountpoint, "overlay", MS_RELATIME, buf);
}

void mount_overlay_or_die(const char *lowerdir, const char *upperdir, const char *workdir, const char *mountpoint)
{
  mkdir_p(upperdir);
  mkdir_p(workdir);
  mkdir_p(mountpoint);
  if (mount_overlay(lowerdir, upperdir, workdir, mountpoint) < 0) {
    perror("mount_overlay");
    halt();
  }
  // else
  printf("Overlayfs(lowerdir=%s,upperdir=%s,workdir=%s) mounted on %s.\n", lowerdir, upperdir, workdir, mountpoint);
}

int move_mount(const char *old, const char *new)
{
  return mount(old, new, NULL, MS_MOVE, NULL);
}

void move_mount_or_die(const char *old, const char *new)
{
  mkdir_p(new);
  if (move_mount(old, new) < 0) {
    perror("move_mount");
    halt();
  }
  // else
  printf("Mountpoint moved from %s to %s.\n", old, new);
}

int mount_loop(const char *imgfile, const char *mountpoint, int mflags, int offset)
{
  struct libmnt_context *ctx;
  int rst = -1;
  char options[32];
  ctx = mnt_new_context();
  if (!ctx) {
    errno = ENOMEM;
    return -1;
  }
  // else
  mnt_context_set_fstype_pattern(ctx, "auto");
  mnt_context_set_source(ctx, imgfile);
  mnt_context_set_target(ctx, mountpoint);
  mnt_context_set_mflags(ctx, mflags);
  sprintf(options, "loop,offset=%d", offset);
  mnt_context_set_options(ctx, options);
  rst = mnt_context_mount(ctx);
  mnt_free_context(ctx);
  return rst;
}

int mount_ro_loop(const char *imgfile, const char *mountpoint, int offset)
{
  mkdir_p(mountpoint);
  return mount_loop(imgfile, mountpoint, MS_RDONLY, offset);
}

void mount_ro_loop_or_die(const char *imgfile, const char *mountpoint, int offset)
{
  if (mount_ro_loop(imgfile, mountpoint, offset) != 0) {
    perror("mount_ro_loop");
    halt();
  }
  // else
  printf("Filesystem image %s mounted on %s.\n", imgfile, mountpoint);
}

int mount_rw_loop(const char *imgfile, const char *mountpoint)
{
  mkdir_p(mountpoint);
  return mount_loop(imgfile, mountpoint, MS_RELATIME, 0);
}

int switch_root(const char *newroot)
{
  return execl(SWITCH_ROOT, SWITCH_ROOT, newroot, "/sbin/init", NULL);
}

void switch_root_or_die(const char *newroot)
{
  if (switch_root(newroot) < 0) {
    perror("switch_root");
    halt();
  }
}

int exists(const char *path)
{
  struct stat st;
  return (stat(path, &st) == 0);
}

int is_file(const char *path)
{
  struct stat st;
  if (stat(path, &st) < 0) return 0;
  return S_ISREG(st.st_mode);
}

int is_dir(const char *path)
{
  struct stat st;
  if (stat(path, &st) < 0) return 0;
  return S_ISDIR(st.st_mode);
}

int fork_exec_wait(const char *cmd, ...)
{
  va_list list;
  int i = 0, pid, rst;
  char *argv[32];

  argv[i++] = (char*)cmd; // first arg to be cmd. removing const qualifier is unavoidable...

  va_start(list, cmd);
  while ((argv[i] = va_arg(list, char *)) != NULL && i < 31) {
    i++;
  }
  argv[i] = NULL;
  va_end(list);

  pid = fork();
  switch (pid) {
    case 0:
      if (execv(cmd, argv) < 0) _exit(-1);
      break; // never reach here
    case -1:
      return -1;
    default:
      waitpid(pid, &rst, 0);
      break;
  }
  return WIFEXITED(rst)? WEXITSTATUS(rst) : -1;
}

int fork_chroot_exec_wait(const char *rootdir, const char *cmd, ...)
{
  va_list list;
  int i = 0, pid, rst;
  char *argv[32];

  argv[i++] = (char*)cmd; // first arg to be cmd. removing const qualifier is unavoidable...

  va_start(list, cmd);
  while ((argv[i] = va_arg(list, char *)) != NULL && i < 31) {
    i++;
  }
  argv[i] = NULL;
  va_end(list);

  pid = fork();
  switch (pid) {
    case 0:
      if (chroot(rootdir) < 0) _exit(-1);
      if (execv(cmd, argv) < 0) _exit(-1);
      break; // never reach here
    case -1:
      return -1;
    default:
      waitpid(pid, &rst, 0);
      break;
  }
  return WIFEXITED(rst)? WEXITSTATUS(rst) : -1;
}

int fork_exec_write_wait(const char *data, const char *cmd, ...)
{
  va_list list;
  int i = 0, pid, rst;
  char *argv[32];
  int fd[2];

  argv[i++] = (char*)cmd; // first arg to be cmd. removing const qualifier is unavoidable...

  va_start(list, cmd);
  while ((argv[i] = va_arg(list, char *)) != NULL && i < 31) {
    i++;
  }
  argv[i] = NULL;
  va_end(list);

  pipe(fd);
  pid = fork();
  switch (pid) {
    case 0:
      close(fd[1]);
      if (dup2(fd[0], STDIN_FILENO) < 0) _exit(-1);
      close(fd[0]);
      if (execv(cmd, argv) < 0) _exit(-1);
      break; // never reach here
    case -1:
      return -1;
    default:
      close(fd[0]);
      write(fd[1], data, strlen(data));
      close(fd[1]);
      waitpid(pid, &rst, 0);
      break;
  }
  return WIFEXITED(rst)? WEXITSTATUS(rst) : -1;
}

int fork_chroot_exec_write_wait(const char *rootdir, const char *data, const char *cmd, ...)
{
  va_list list;
  int i = 0, pid, rst;
  char *argv[32];
  int fd[2];

  argv[i++] = (char*)cmd; // first arg to be cmd. removing const qualifier is unavoidable...

  va_start(list, cmd);
  while ((argv[i] = va_arg(list, char *)) != NULL && i < 31) {
    i++;
  }
  argv[i] = NULL;
  va_end(list);

  pipe(fd);
  pid = fork();
  switch (pid) {
    case 0:
      close(fd[1]);
      if (dup2(fd[0], STDIN_FILENO) < 0) _exit(-1);
      close(fd[0]);
      if (chroot(rootdir) < 0) _exit(-1);
      if (execv(cmd, argv) < 0) _exit(-1);
      break; // never reach here
    case -1:
      return -1;
    default:
      close(fd[0]);
      write(fd[1], data, strlen(data));
      close(fd[1]);
      waitpid(pid, &rst, 0);
      break;
  }
  return WIFEXITED(rst)? WEXITSTATUS(rst) : -1;
}


int cp_a(const char *src, const char *dst)
{
  return fork_exec_wait(CP, "-a", src, dst, NULL);
}

void setup_initramfs_shutdown(const char *newroot)
{
  char buf[PATH_MAX];
  sprintf(buf, "%s/run/initramfs/bin", newroot);
  mkdir_p(buf);
  cp_a("/bin/.", buf);

  if (is_dir("/lib")) {
    sprintf(buf, "%s/run/initramfs/lib", newroot);
    mkdir_p(buf);
    cp_a("/lib/.", buf);
  }

  if (is_dir("/usr/lib")) {
    sprintf(buf, "%s/run/initramfs/usr/lib", newroot);
    mkdir_p(buf);
    cp_a("/usr/lib/.", buf);
  }

  if (is_dir("/lib64")) {
    sprintf(buf, "%s/run/initramfs/lib64", newroot);
    mkdir_p(buf);
    cp_a("/lib64/.", buf);
  }

  if (is_dir("/usr/lib64")) {
    sprintf(buf, "%s/run/initramfs/usr/lib64", newroot);
    mkdir_p(buf);
    cp_a("/usr/lib64/.", buf);
  }

  sprintf(buf, "%s/run/initramfs/shutdown", newroot);
  cp_a("/init", buf);
}

#ifdef INIFILE
typedef dictionary *inifile_t;

int process_inifile(const char *inifile, void (callback)(inifile_t))
{
  dictionary *ini = NULL;
  if (is_file(inifile)) {
    ini = iniparser_load(inifile);
  }
  if (!ini) {
    ini = dictionary_new(0);
  }

  callback(ini);

  iniparser_freedict(ini);
  return 0;
}

const char *ini_string(inifile_t d, const char *key, char *def)
{
  return iniparser_getstring((dictionary *)d, key, def);
}

int ini_int(inifile_t d, const char *key, int notfound)
{
  return iniparser_getint((dictionary *)d, key, notfound);
}

int ini_bool(inifile_t d, const char *key, int notfound)
{
  return iniparser_getboolean((dictionary *)d,key, notfound);
}

int ini_exists(inifile_t d, char *entry)
{
  return iniparser_find_entry((dictionary *)d, entry);
}
#endif

int extract_archive(const char *rootdir, const char *archive, const char *path, int strip_components)
{
  char buf[PATH_MAX];
  sprintf(buf, "%s%s", rootdir, path);
  mkdir_p(buf);
  sprintf(buf, "--strip-components=%d", strip_components);
  return fork_chroot_exec_wait(rootdir, TAR, "xf", archive, buf, "-C", path, NULL);
}

int cat(const char *file)
{
  return fork_exec_wait(CAT, file, NULL);
}

int umount_recursive(const char *path)
{
  return fork_exec_wait(UMOUNT, "-R", "-n", path, NULL);
}

long get_free_disk_space(const char *mountpoint)
{
  struct statvfs s;
  int rst;
  rst = statvfs(mountpoint, &s);
  if (rst < 0) return rst;
  //else
  return s.f_bsize * s.f_bfree;
}

int create_xfs_imagefile(const char *imagefile, off_t length)
{
  int fd, rst;
  fd = creat(imagefile, S_755);
  if (fd < 0) return fd;
  //else
  rst = ftruncate(fd, length);
  close(fd);
  if (rst < 0) return rst;
  // else
  return fork_exec_wait(MKFS_XFS, "-f", "-q", imagefile, NULL);
}

int create_swapfile(const char* swapfile, off_t length)
{
  int fd, rst;
  fd = creat(swapfile, S_IRUSR | S_IWUSR);
  if (fd < 0) return fd;
  //else
  rst = ftruncate(fd, length);
  close(fd);
  if (rst < 0) return rst;
  // else
  return fork_exec_wait(MKSWAP, swapfile, NULL);
}

int activate_swap(const char *swapfile)
{
  if (fork_exec_wait(SWAPON, swapfile, NULL) != 0) {
    printf("Broken swapfile? performing mkswap...\n");
    if (fork_exec_wait(MKSWAP, swapfile, NULL) == 0) {
      fork_exec_wait(SWAPON, swapfile, NULL);
    }
  }
}

int generate_default_hostname(char* hostname)
{
  FILE *f;
  uint16_t randomnumber;
  f = fopen("/dev/urandom", "r");
  if (!f) return -1;
  //else
  fread(&randomnumber, sizeof(randomnumber), 1, f);
  fclose(f);
  sprintf(hostname, "host-%04x", randomnumber);
  return 0;
}

int set_hostname(const char *rootdir, const char *hostname)
{
  FILE *f;
  char buf[PATH_MAX];
  sprintf(buf, "%s/etc/hostname", rootdir);
  f = fopen(buf, "w");
  if (!f) return -1;
  //else
  fprintf(f, "%s", hostname);
  return fclose(f);
}

int set_root_password(const char *rootdir, const char *password/* NULL to remove password*/)
{
  char buf[128 + 5];

  if (!password || password[0] == '\0') {
    return fork_chroot_exec_wait(rootdir, PASSWD, "-d", "root", NULL);
  }
  // else
  if (strlen(password) > 127) return -1;
  strcpy(buf, "root:");
  strcat(buf, password);
  return fork_chroot_exec_write_wait(rootdir, buf, CHPASSWD, NULL);
}

int set_timezone(const char *rootdir, const char *timezone)
{
  FILE *f;
  char buf1[PATH_MAX], buf2[PATH_MAX];
  sprintf(buf1, "../usr/share/zoneinfo/%s", timezone);
  sprintf(buf2, "%s/etc/localtime", rootdir);
  unlink(buf2);
  return symlink(buf1, buf2);
}

int set_keymap(const char *rootdir, const char *keymap)
{
  FILE *f;
  char buf[PATH_MAX];
  sprintf(buf, "%s/etc/vconsole.conf", rootdir);
  f = fopen(buf, "w");
  if (!f) return -1;
  //else
  fprintf(f, "KEYMAP=%s\n", keymap);
  return fclose(f);
}

int enable_autologin(const char *rootdir)
{
  FILE *f;
  char buf[PATH_MAX];
  sprintf(buf, "%s/etc/systemd/system/getty@tty1.service.d", rootdir);
  mkdir_p(buf);
  sprintf(buf, "%s/etc/systemd/system/getty@tty1.service.d/autologin.conf", rootdir);
  f = fopen(buf, "w");
  if (!f) return -1;
  //else
  fputs("[Service]\nExecStart=\nExecStart=-/sbin/agetty -o '-p -- \\\\u' --autologin root --noclear %I $TERM", f);
  return fclose(f);
}

int setup_wifi(const char *rootdir, const char *ssid, const char *key)
{
  FILE *f;
  char buf[PATH_MAX];
  sprintf(buf, "%s/etc/wpa_supplicant/wpa_supplicant-wlan0.conf", rootdir);
  f = fopen(buf, "w");
  if (!f) return -1;
  // else
  fprintf(f, "network={\n");
  fprintf(f, "\tssid=\"%s\"\n", ssid);
  fprintf(f, "\tpsk=\"%s\"\n", key);
  fprintf(f, "\tproto=WPA\n");
  fprintf(f, "}\n");
  if (fclose(f) != 0) return -1;
  //else
  sprintf(buf, "%s/etc/systemd/network/51-wlan0-dhcp.network", rootdir);
  f = fopen(buf, "w");
  if (!f) return -1;
  //else
  fprintf(f, "[Match]\nName=wlan0\n[Network]\nDHCP=yes\nMulticastDNS=yes\nLLMNR=yes\n");
  if (fclose(f) != 0) return -1;
  // else
  sprintf(buf, "%s/etc/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service", rootdir);
  return symlink("/lib/systemd/system/wpa_supplicant@.service", buf);
}

void init();
void shutdown();

int main(int argc, char *argv[])
{
  if (strcmp(argv[0], "/init") == 0) {
    init();
    return 0; // should not reach here
  }
  //else
  if (strcmp(argv[0], "/shutdown") != 0) {
    printf("Not a valid program name.\n");
    halt();
  }
  //else
  shutdown();

  if (strcmp(argv[1], "poweroff") == 0) {
    reboot(RB_POWER_OFF);
  } else if (strcmp(argv[1], "reboot") == 0) {
    reboot(RB_AUTOBOOT);
  } else {
    halt();
  }
  return 0;
}
