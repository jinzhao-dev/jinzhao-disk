#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <libdevmapper.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "internal.h"
#include "device.h"

static size_t device_block_size_fd(int fd)
{
	struct stat st;
	size_t bsize;
	int arg;

	if (fstat(fd, &st) < 0)
		return 0;

	// WL: assuming unistd.h has declaration of ‘getpagesize’, 4096 by default
	if (S_ISREG(st.st_mode))
		bsize = getpagesize();
	else {
		if (ioctl(fd, BLKSSZGET, &arg) < 0)
			bsize = getpagesize();
		else
			bsize = (size_t)arg;
	}

	return bsize;
}

// WL: Get path to device/file, file first
char *device_path(struct device *device)
{
	if (!device)
		return NULL;

	if (device->file_path)
		return device->file_path;

	return device->path;
}

size_t device_block_size(struct device *device)
{
	int fd;

	if (!device)
		return 0;

	if (device->block_size)
		return device->block_size;

	fd = open(device->file_path ?: device->path, O_RDONLY);
	if (fd >= 0) {
		device->block_size = device_block_size_fd(fd);
		close(fd);
	}

	if (!device->block_size)
		printf("Cannot get block size for device %s.\n",
		       device_path(device));

	return device->block_size;
}

// WL: The direct-io is always preferred.
static int device_ready(struct device *device)
{
	int devfd = -1, r = 0;
	struct stat st;

	if (!device)
		return -EINVAL;

	if (devfd < 0) {
		printf("Trying to open device %s without direct-io.\n",
		       device_path(device));
		devfd = open(device_path(device), O_RDONLY);
	}

	if (devfd < 0) {
		printf("Device %s does not exist or access denied.\n",
		       device_path(device));
		return -EINVAL;
	}

	if (fstat(devfd, &st) < 0)
		r = -EINVAL;
	else if (!S_ISBLK(st.st_mode))
		r = S_ISREG(st.st_mode) ? -ENOTBLK : -EINVAL;
	if (r == -EINVAL) {
		printf("Device %s is not compatible.\n", device_path(device));
		close(devfd);
		return r;
	}

	close(devfd);
	return r;
}

// WL: init device according to path, expects direct-io to work.
struct device *device_alloc(const char *path)
{
	struct device *dev;
	int r;

	if (!path)
		return NULL;

#if !HAVE_DECL_O_CLOEXEC
	printf("Running without O_CLOEXEC.\n");
#endif

	dev = malloc(sizeof(struct device));
	if (!dev)
		return NULL;
	memset(dev, 0, sizeof(struct device));

	// WL: set path for the device structure
	dev->path = strdup(path);
	if (!dev->path) {
		free(dev);
		return NULL;
	}

	dev->dev_fd = -1;

	r = device_ready(dev);
	if (r < 0) {
		free(dev->path);
		free(dev);
		return NULL;
	}

	return dev;
}

static int device_info(struct device *device, int *readonly, uint64_t *size)
{
	struct stat st;
	int fd = -1, r, flags = 0, real_readonly;
	uint64_t real_size;

	if (!device)
		return -ENOTBLK;

	real_readonly = 0;
	real_size = 0;

	if (stat(device->path, &st) < 0) {
		r = -EINVAL;
		goto out;
	}

	/* never wipe header on mounted device */
	// WL: assuming all mounted devices are exclusive
	if (S_ISBLK(st.st_mode))
		flags |= O_EXCL;

	/* coverity[toctou] */
	fd = open(device->path, O_RDWR | flags);
	if (fd == -1 && errno == EROFS) {
		real_readonly = 1;
		fd = open(device->path, O_RDONLY | flags);
	}

	if (fd == -1 && errno == EBUSY) {
		r = -EBUSY;
		goto out;
	}

	if (fd == -1) {
		r = errno ? -errno : -EINVAL;
		goto out;
	}

	r = 0;
	if (S_ISREG(st.st_mode)) {
		// FIXME: add readonly check
		real_size = (uint64_t)st.st_size;
		real_size >>= SECTOR_SHIFT;
	} else {
		// WL: check whether BKROGET says that it is read-only, e.g. read-only loop devices may be opened read-write but are read-only according to BLKROGET
		if (real_readonly == 0 &&
		    (r = ioctl(fd, BLKROGET, &real_readonly)) < 0)
			goto out;

		r = ioctl(fd, BLKGETSIZE64, &real_size);
		if (r >= 0) {
			real_size >>= SECTOR_SHIFT;
			goto out;
		}
	}
out:
	if (fd != -1)
		close(fd);

	switch (r) {
	case 0:
		if (readonly)
			*readonly = real_readonly;
		if (size)
			*size = real_size;
		break;
	case -EBUSY:
		printf("Cannot use device %s which is in use (already mapped or mounted).\n",
		       device_path(device));
		break;
	case -EACCES:
		printf("Cannot use device %s, permission denied.\n",
		       device_path(device));
		break;
	default:
		printf("Cannot get info about device %s.\n",
		       device_path(device));
		r = -EINVAL;
	}

	return r;
}

uint64_t get_payload_size(uint64_t disk_size)
{
	FILE *fp = NULL;
	int max_size = 256;
	char *disk_size_str = malloc(max_size);
	char *data_size_str = malloc(max_size);

	uint64_t data_size = 0;
	int r;
	r = snprintf(disk_size_str, max_size, "%lu", disk_size);

	fp = fopen("/sys/module/jindisk/calc_avail_sectors", "w");
	if (fp == NULL) {
		printf("Cannot find the sysfs object!\n");
		goto err;
	}
	fputs(disk_size_str, fp);
	fclose(fp);
	printf("Disk size: %lu.\n", disk_size);

	fp = fopen("/sys/module/jindisk/calc_avail_sectors", "r");
	if (fp == NULL) {
		printf("Cannot find the sysfs object!\n");
		goto err;
	}
	fscanf(fp, "%s", data_size_str);
	sscanf(data_size_str, "%lu", &data_size);
	fclose(fp);

	return data_size;

err:
	// FIXME: hack here
	data_size = disk_size * 4 / 5;
	return data_size;
}

int device_block_adjust(struct device *device, uint64_t device_offset,
			uint64_t *size)
{
	int r, real_readonly;
	uint64_t real_size;

	uint64_t disk_size, data_size = 0;

	if (!device)
		return -ENOTBLK;

	// WL: get the real size
	r = device_info(device, &real_readonly, &real_size);
	if (r)
		return r;

	if (MISALIGNED(real_size, device_block_size(device) >> SECTOR_SHIFT)) {
		printf("Device size is not aligned to device logical block size.\n");
		return -EINVAL;
	}

	if (device_offset >= real_size) {
		printf("Requested offset is beyond real size of device %s.\n",
		       device_path(device));
		return -EINVAL;
	}

	if (size && !*size) {
		*size = real_size;
		if (!*size) {
			printf("Device %s has zero size.\n",
			       device_path(device));
			return -ENOTBLK;
		}
		*size -= device_offset;
	}

	/* in case of size is set by parameter */
	if (size && ((real_size - device_offset) < *size)) {
		printf("Device %s: offset = %" PRIu64
		       " requested size = %" PRIu64
		       ", backing device size = %" PRIu64 ".\n",
		       device->path, device_offset, *size, real_size);
		printf("Device %s is too small.\n", device_path(device));
		return -EINVAL;
	}

	*size = get_payload_size(*size);

	if (size)
		printf("Calculated device size is %" PRIu64
		       " sectors (%s), offset %" PRIu64 ".\n",
		       *size, real_readonly ? "RO" : "RW", device_offset);
	return 0;
}

void device_close(struct device *device)
{
	if (!device)
		return;

	if (device->dev_fd != -1) {
		printf("Closing read write fd for %s.\n", device_path(device));
		if (close(device->dev_fd))
			printf("Failed to close read write fd for %s.\n",
			       device_path(device));
		device->dev_fd = -1;
	}
}

void device_free(struct device *device)
{
	if (!device)
		return;

	device_close(device);

	free(device->file_path);
	free(device->path);
	free(device);
}
