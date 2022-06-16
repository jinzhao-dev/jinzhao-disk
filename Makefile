# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel software RAID and LVM drivers.
#

dm-mod-y	+= dm.o dm-table.o dm-target.o dm-linear.o dm-stripe.o \
		   dm-ioctl.o dm-io.o dm-kcopyd.o dm-sysfs.o dm-stats.o \
		   dm-rq.o
dm-multipath-y	+= dm-path-selector.o dm-mpath.o
dm-historical-service-time-y += dm-ps-historical-service-time.o
dm-io-affinity-y += dm-ps-io-affinity.o
dm-queue-length-y += dm-ps-queue-length.o
dm-round-robin-y += dm-ps-round-robin.o
dm-service-time-y += dm-ps-service-time.o
dm-snapshot-y	+= dm-snap.o dm-exception-store.o dm-snap-transient.o \
		    dm-snap-persistent.o
dm-mirror-y	+= dm-raid1.o
dm-log-userspace-y += dm-log-userspace-base.o dm-log-userspace-transfer.o
dm-bio-prison-y += dm-bio-prison-v1.o dm-bio-prison-v2.o
dm-thin-pool-y	+= dm-thin.o dm-thin-metadata.o
dm-cache-y	+= dm-cache-target.o dm-cache-metadata.o dm-cache-policy.o \
		    dm-cache-background-tracker.o
dm-cache-smq-y	+= dm-cache-policy-smq.o
dm-ebs-y	+= dm-ebs-target.o
dm-era-y	+= dm-era-target.o
dm-clone-y	+= dm-clone-target.o dm-clone-metadata.o
dm-verity-y	+= dm-verity-target.o
dm-zoned-y	+= dm-zoned-target.o dm-zoned-metadata.o dm-zoned-reclaim.o

md-mod-y	+= md.o md-bitmap.o
raid456-y	+= raid5.o raid5-cache.o raid5-ppl.o
linear-y	+= md-linear.o
multipath-y	+= md-multipath.o
faulty-y	+= md-faulty.o
sworndisk-y       += sworndisk/source/dm-sworndisk.o sworndisk/source/metadata.o sworndisk/source/hashmap.o sworndisk/source/memtable.o sworndisk/source/bio_operate.o sworndisk/source/crypto.o sworndisk/source/segment_allocator.o sworndisk/source/segment_buffer.o sworndisk/source/cache.o sworndisk/source/disk_structs.o sworndisk/source/lsm_tree.o sworndisk/source/bloom_filter.o sworndisk/source/async.o

# Note: link order is important.  All raid personalities
# and must come before md.o, as they each initialise 
# themselves, and md.o may use the personalities when it 
# auto-initialised.

obj-$(CONFIG_MD_LINEAR)		+= linear.o
obj-$(CONFIG_MD_RAID0)		+= raid0.o
obj-$(CONFIG_MD_RAID1)		+= raid1.o
obj-$(CONFIG_MD_RAID10)		+= raid10.o
obj-$(CONFIG_MD_RAID456)	+= raid456.o
obj-$(CONFIG_MD_MULTIPATH)	+= multipath.o
obj-$(CONFIG_MD_FAULTY)		+= faulty.o
obj-$(CONFIG_MD_CLUSTER)	+= md-cluster.o
obj-$(CONFIG_BCACHE)		+= bcache/
obj-$(CONFIG_BLK_DEV_MD)	+= md-mod.o
ifeq ($(CONFIG_BLK_DEV_MD),y)
obj-y				+= md-autodetect.o
endif
obj-$(CONFIG_BLK_DEV_DM)	+= dm-mod.o
obj-$(CONFIG_BLK_DEV_DM_BUILTIN) += dm-builtin.o
obj-$(CONFIG_DM_UNSTRIPED)	+= dm-unstripe.o
obj-$(CONFIG_DM_BUFIO)		+= dm-bufio.o
obj-$(CONFIG_DM_BIO_PRISON)	+= dm-bio-prison.o
obj-$(CONFIG_DM_CRYPT)		+= dm-crypt.o
obj-$(CONFIG_DM_DELAY)		+= dm-delay.o
obj-$(CONFIG_DM_DUST)		+= dm-dust.o
obj-$(CONFIG_DM_FLAKEY)		+= dm-flakey.o
obj-$(CONFIG_DM_MULTIPATH)	+= dm-multipath.o dm-round-robin.o
obj-$(CONFIG_DM_MULTIPATH_QL)	+= dm-queue-length.o
obj-$(CONFIG_DM_MULTIPATH_ST)	+= dm-service-time.o
obj-$(CONFIG_DM_MULTIPATH_HST)	+= dm-historical-service-time.o
obj-$(CONFIG_DM_MULTIPATH_IOA)	+= dm-io-affinity.o
obj-$(CONFIG_DM_SWITCH)		+= dm-switch.o
obj-$(CONFIG_DM_SNAPSHOT)	+= dm-snapshot.o
obj-$(CONFIG_DM_PERSISTENT_DATA) += persistent-data/
obj-$(CONFIG_DM_MIRROR)		+= dm-mirror.o dm-log.o dm-region-hash.o
obj-$(CONFIG_DM_LOG_USERSPACE)	+= dm-log-userspace.o
obj-$(CONFIG_DM_ZERO)		+= dm-zero.o
obj-$(CONFIG_DM_RAID)		+= dm-raid.o
obj-$(CONFIG_DM_THIN_PROVISIONING) += dm-thin-pool.o
obj-$(CONFIG_DM_VERITY)		+= dm-verity.o
obj-$(CONFIG_DM_CACHE)		+= dm-cache.o
obj-$(CONFIG_DM_CACHE_SMQ)	+= dm-cache-smq.o
obj-$(CONFIG_DM_EBS)		+= dm-ebs.o
obj-$(CONFIG_DM_ERA)		+= dm-era.o
obj-$(CONFIG_DM_CLONE)		+= dm-clone.o
obj-$(CONFIG_DM_LOG_WRITES)	+= dm-log-writes.o
obj-$(CONFIG_DM_INTEGRITY)	+= dm-integrity.o
obj-$(CONFIG_DM_ZONED)		+= dm-zoned.o
obj-$(CONFIG_DM_WRITECACHE)	+= dm-writecache.o
obj-$(CONFIG_DM_SWORNDISK)           += sworndisk.o

ifeq ($(CONFIG_DM_INIT),y)
dm-mod-objs			+= dm-init.o
endif

ifeq ($(CONFIG_DM_UEVENT),y)
dm-mod-objs			+= dm-uevent.o
endif

ifeq ($(CONFIG_BLK_DEV_ZONED),y)
dm-mod-objs			+= dm-zone.o
endif

ifeq ($(CONFIG_IMA),y)
dm-mod-objs			+= dm-ima.o
endif

ifeq ($(CONFIG_DM_VERITY_FEC),y)
dm-verity-objs			+= dm-verity-fec.o
endif

ifeq ($(CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG),y)
dm-verity-objs			+= dm-verity-verify-sig.o
endif

ifeq ($(CONFIG_DM_AUDIT),y)
dm-mod-objs			+= dm-audit.o
endif

ifeq ($(CONFIG_DM_SWORNDISK),y)
dm-mod-objs			+= dm-sworndisk.o
endif
