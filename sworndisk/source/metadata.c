#include "../include/metadata.h"

#define SUPERBLOCK_LOCATION 0
#define SUPERBLOCK_MAGIC 0x22946
#define SUPERBLOCK_CSUM_XOR 0x3828

// superblock implementation
int superblock_read(struct superblock* this) {
	int r;
	struct superblock* disk_super;
	struct dm_block* block;

	r = dm_bm_read_lock(this->bm, SUPERBLOCK_LOCATION, NULL, &block);
	if (r) 
		return -ENODATA;
	
	disk_super = dm_block_data(block);
	
	this->csum = le32_to_cpu(disk_super->csum);
	this->magic = le64_to_cpu(disk_super->magic);
	this->sectors_per_seg = le32_to_cpu(disk_super->sectors_per_seg);
	this->nr_segment = le64_to_cpu(disk_super->nr_segment);
	this->common_ratio = le32_to_cpu(disk_super->common_ratio);
	this->nr_disk_level = le32_to_cpu(disk_super->nr_disk_level);
	this->max_disk_level_size = le64_to_cpu(disk_super->max_disk_level_size);
	this->index_region_start = le64_to_cpu(disk_super->index_region_start);
	this->journal_size = le32_to_cpu(disk_super->journal_size);
	this->nr_journal = le64_to_cpu(disk_super->nr_journal);
	this->cur_journal = le64_to_cpu(disk_super->cur_journal);
	this->journal_region_start = le64_to_cpu(disk_super->journal_region_start);
	this->seg_validity_table_start = le64_to_cpu(disk_super->seg_validity_table_start);
	this->data_seg_table_start = le64_to_cpu(disk_super->data_seg_table_start);
	this->reverse_index_table_start = le64_to_cpu(disk_super->reverse_index_table_start);

	dm_bm_unlock(block);
	return 0;
}

int superblock_write(struct superblock* this) {
	int r;
	struct superblock* disk_super;
	struct dm_block* block;

	r = dm_bm_write_lock(this->bm, SUPERBLOCK_LOCATION, NULL, &block);
	if (r) {
		DMERR("dm bm write lock error\n");
		return -EAGAIN;
	}
		

	disk_super = dm_block_data(block);

	disk_super->csum = cpu_to_le32(dm_bm_checksum(&this->magic, SUPERBLOCK_ON_DISK_SIZE, SUPERBLOCK_CSUM_XOR));
	disk_super->magic = cpu_to_le64(this->magic);
	disk_super->sectors_per_seg = cpu_to_le32(this->sectors_per_seg);
	disk_super->nr_segment = cpu_to_le64(this->nr_segment);
	disk_super->common_ratio = cpu_to_le32(this->common_ratio);
	disk_super->nr_disk_level = cpu_to_le32(this->nr_disk_level);
	disk_super->max_disk_level_size = cpu_to_le64(this->max_disk_level_size);
	disk_super->index_region_start = cpu_to_le64(this->index_region_start);
	disk_super->journal_size = cpu_to_le32(this->journal_size);
	disk_super->nr_journal = cpu_to_le64(this->nr_journal);
	disk_super->cur_journal = cpu_to_le64(this->cur_journal);
	disk_super->journal_region_start = cpu_to_le64(this->journal_region_start);
	disk_super->seg_validity_table_start = cpu_to_le64(this->seg_validity_table_start);
	disk_super->data_seg_table_start = cpu_to_le64(this->data_seg_table_start);
	disk_super->reverse_index_table_start = cpu_to_le64(this->reverse_index_table_start);
	
	dm_bm_unlock(block);
	return dm_bm_flush(this->bm);
}

bool superblock_validate(struct superblock* this) {
	uint32_t csum;

	if (this->magic != SUPERBLOCK_MAGIC)
		return false;

	csum = dm_bm_checksum(&this->magic, SUPERBLOCK_ON_DISK_SIZE, SUPERBLOCK_CSUM_XOR);
	if (csum != this->csum)
		return false;

	return true;
}

void superblock_print(struct superblock* this) {
	DMINFO("superblock: ");
	DMINFO("\tmagic: %lld", this->magic);
	DMINFO("\tsectors_per_seg: %d", this->sectors_per_seg);
	DMINFO("\tnr_segment: %lld", this->nr_segment);
	DMINFO("\tcommon ratio: %d", this->common_ratio);
	DMINFO("\tnr_disk_level: %d", this->nr_disk_level);
	DMINFO("\tmax_disk_level_size: %lld", this->max_disk_level_size);
	DMINFO("\tindex_region_start: %lld", this->index_region_start);
	DMINFO("\tjournal_size: %d", this->journal_size);
	DMINFO("\tnr_journal: %lld", this->nr_journal);
	DMINFO("\tcur_journal: %lld", this->cur_journal);
	DMINFO("\tjournal_region_start: %lld", this->journal_region_start);
	DMINFO("\tseg_validity_table_start: %lld", this->seg_validity_table_start);
	DMINFO("\tdata_seg_table_start: %lld", this->data_seg_table_start);
	DMINFO("\treverse_index_table_start: %lld", this->reverse_index_table_start);
}

#include "../include/segment_allocator.h"
#define LSM_TREE_DISK_LEVEL_COMMON_RATIO 8

size_t __index_region_sectors(size_t nr_disk_level, size_t common_ratio, size_t max_disk_level_size) {
	size_t i, sectors, cur_size;

	cur_size = max_disk_level_size;
	for (i = 0; i < nr_disk_level; ++i) {
		sectors += cur_size;
		cur_size /= common_ratio;
	}

	return sectors;
}

size_t __journal_region_sectors(size_t nr_journal, size_t journal_size) {
	size_t bytes;

	bytes = nr_journal * journal_size;
	if (!bytes)
		return 0;
	
	return (bytes - 1) / SECTOR_SIZE + 1;
}

size_t __seg_validity_table_sectors(size_t nr_segment) {
	if (!nr_segment)
		return 0;
	return (((nr_segment - 1) / BITS_PER_LONG + 1) * sizeof(unsigned long) - 1) / SECTOR_SIZE + 1;
}

size_t __data_seg_table_sectors(size_t nr_segment, size_t sectors_per_seg) {
	return 0;
}

int superblock_init(struct superblock* this, struct block_device* bdev) {
	int r;

	this->bm = dm_block_manager_create(bdev, SECTOR_SIZE, SWORNDISK_MAX_CONCURRENT_LOCKS);
	if (IS_ERR_OR_NULL(this->bm))
		return -ENOMEM;

	this->read = superblock_read;
	this->write = superblock_write;
	this->validate = superblock_validate;
	this->print = superblock_print;

	r = this->read(this);
	if (r)
		return r;
	
	if (this->validate(this))
		return 0;

	this->magic = SUPERBLOCK_MAGIC;
	this->sectors_per_seg = SECTOES_PER_SEG;
	this->nr_segment = NR_SEGMENT;
	this->common_ratio = LSM_TREE_DISK_LEVEL_COMMON_RATIO;
	this->nr_disk_level = 0;
	this->max_disk_level_size = 0;
	this->index_region_start = SUPERBLOCK_LOCATION + sizeof(struct superblock) / SECTOR_SIZE + 1;
	this->journal_size = 0;
	this->nr_journal = 0;
	this->cur_journal = 0;
	this->journal_region_start = this->index_region_start + __index_region_sectors(
	  this->nr_disk_level, this->common_ratio, this->max_disk_level_size);
	this->seg_validity_table_start = this->journal_region_start + __journal_region_sectors(this->nr_journal, this->journal_size);
	this->data_seg_table_start = this->seg_validity_table_start + __seg_validity_table_sectors(this->nr_segment);
	this->reverse_index_table_start = this->data_seg_table_start + __data_seg_table_sectors(this->nr_segment, this->sectors_per_seg);

	this->write(this);
	return 0;
}

struct superblock* superblock_create(struct block_device* bdev) {
	int r;
	struct superblock* this;

	this = kmalloc(sizeof(struct superblock), GFP_KERNEL);
	if (!this)
		return NULL;
	
	r = superblock_init(this, bdev);
	if (r)
		return NULL;
	
	return this;
}

void superblock_destroy(struct superblock* this) {
	if (!IS_ERR_OR_NULL(this)) {
		if (!IS_ERR_OR_NULL(this->bm))
			dm_block_manager_destroy(this->bm);
		kfree(this);
	}
}

// disk array implememtation
sector_t disk_array_entry_sector(struct disk_array* this, size_t index) {
	return this->start + index * this->entry_size / SECTOR_SIZE;
}

size_t disk_array_entry_offset(struct disk_array* this, size_t index) {
	return (index * this->entry_size) % SECTOR_SIZE;
}

int disk_array_set(struct disk_array* this, size_t index, void* entry) {
	int r;
	struct dm_block* block;
	if (index < 0 || index >= this->nr_entry)
		return -EINVAL;

	r = dm_bm_write_lock(this->bm, disk_array_entry_sector(this, index), NULL, &block);
	if (r)
		return r;

	memcpy(dm_block_data(block) + disk_array_entry_offset(this, index), entry, this->entry_size);
	
	dm_bm_unlock(block);
	return dm_bm_flush(this->bm);
}

void* disk_array_get(struct disk_array* this, size_t index) {
	int r;
	void* entry;
	struct dm_block* block;

	entry = kmalloc(this->entry_size, GFP_KERNEL);
	if (!entry) 
		return NULL;
	
	r = dm_bm_read_lock(this->bm, disk_array_entry_sector(this, index), NULL, &block);
	if (r)
		return NULL;

	memcpy(entry, dm_block_data(block) + disk_array_entry_offset(this, index), this->entry_size);
	dm_bm_unlock(block);

	return entry;
}

int disk_array_format(struct disk_array* this, bool value) {
	int r;
	size_t shift, total, cycle;
	struct dm_block* block;

	shift = 0;
	total = this->nr_entry * this->entry_size;
	while (total > 0) {
		r = dm_bm_write_lock(this->bm, this->start + shift, NULL, &block);
		if (r)
			return r;
		cycle = SECTOR_SIZE;
		if (total < cycle)
			cycle = total;
		memset(dm_block_data(block), value ? -1 : 0, cycle);
		dm_bm_unlock(block);

		total -= cycle;
		shift += 1;
	}

	return dm_bm_flush(this->bm);
}

int disk_array_init(struct disk_array* this, struct block_device* bdev, sector_t start, size_t nr_entry, size_t entry_size) {
	this->bdev = bdev;
	this->start = start;
	this->nr_entry = nr_entry;
	this->entry_size = entry_size;

	this->bm = dm_block_manager_create(bdev, SECTOR_SIZE, SWORNDISK_MAX_CONCURRENT_LOCKS);
	if (IS_ERR_OR_NULL(this->bm))
		return -ENOMEM;

	this->set = disk_array_set;
	this->get = disk_array_get;
	this->format = disk_array_format;
	
	return 0;
}

struct disk_array* disk_array_create(struct block_device* bdev, sector_t start, size_t nr_entry, size_t entry_size) {
	int r;
	struct disk_array* this;

	this = kmalloc(sizeof(struct disk_array), GFP_KERNEL);
	if (!this)
		return NULL;
	
	r = disk_array_init(this, bdev, start, nr_entry, entry_size);
	if (r)
		return NULL;
	
	return this;
}

void disk_array_destroy(struct disk_array* this) {
	if (!IS_ERR_OR_NULL(this)) {
		if (!IS_ERR_OR_NULL(this->bm))
			dm_block_manager_destroy(this->bm);
		kfree(this);
	}
}

// disk bitset implementation
size_t __disk_bitset_group(size_t index) {
	return index / BITS_PER_LONG;
}

size_t __disk_bitset_offset(size_t index) {
	return index % BITS_PER_LONG;
}

size_t __disk_bitset_nr_group(size_t nr_bit) {
	return nr_bit ? (nr_bit - 1) / BITS_PER_LONG + 1 : 0;
}

int __disk_bitset_operate(struct disk_bitset* this, size_t index, bool set) {
	int r;
	unsigned long* group;

	if (index < 0 || index >= this->nr_bit)
		return -EINVAL;
	
	r = 0;
	group = this->array->get(this->array, __disk_bitset_group(index));
	if (IS_ERR_OR_NULL(group))
		return -ENODATA;
	
	(set ? set_bit : clear_bit)(__disk_bitset_offset(index), group);
	r = this->array->set(this->array, __disk_bitset_group(index), group);
	kfree(group);
	return r;
}

int disk_bitset_set(struct disk_bitset* this, size_t index) {
	return __disk_bitset_operate(this, index, true);
}

int disk_bitset_clear(struct disk_bitset* this, size_t index) {
	return __disk_bitset_operate(this, index, false);
}

int disk_bitset_get(struct disk_bitset* this, size_t index, bool* result) {
	unsigned long* group;

	group = this->array->get(this->array, __disk_bitset_group(index));
	if (IS_ERR_OR_NULL(group))
		return -EINVAL;
	
	*result = test_bit(__disk_bitset_offset(index), group);
	return 0;
}

int disk_bitset_format(struct disk_bitset* this, bool value) {
	return this->array->format(this->array, value);
}

int disk_bitset_init(struct disk_bitset* this, struct block_device* bdev, sector_t start, size_t nr_bit) {
	this->nr_bit = nr_bit;
	this->array = disk_array_create(bdev, start, __disk_bitset_nr_group(nr_bit), sizeof(unsigned long));
	if (IS_ERR_OR_NULL(this->array))
		return -ENOMEM;

	this->set = disk_bitset_set;
	this->clear = disk_bitset_clear;
	this->get = disk_bitset_get;
	this->format = disk_bitset_format;

	return 0;
}

struct disk_bitset* disk_bitset_create(struct block_device* bdev, sector_t start, size_t nr_bit) {
	struct disk_bitset* this;

	this = kmalloc(sizeof(struct disk_bitset), GFP_KERNEL);
	if (!this)
		return NULL;
	
	disk_bitset_init(this, bdev, start, nr_bit);

	return this;
}

void disk_bitset_destroy(struct disk_bitset* this) {
	if (!IS_ERR_OR_NULL(this)) {
		if (!IS_ERR_OR_NULL(this->array))
			disk_array_destroy(this->array);
		kfree(this);
	}
}


int seg_validator_take(struct seg_validator* this, size_t seg) {
	int r;

	r = this->seg_validity_table->set(this->seg_validity_table, seg);
	if (r)
		return r;
	
	this->cur_segment += 1;
	return 0;
}

int seg_validator_next(struct seg_validator* this, size_t* next_seg) {
	int r;
	bool valid;

	while(this->cur_segment < this->nr_segment) {
		r = this->seg_validity_table->get(this->seg_validity_table, this->cur_segment, &valid);
		if (!r && !valid) {
			*next_seg = this->cur_segment;
			return 0;
		}
		this->cur_segment += 1;
	}

	return -ENODATA;
}

int seg_validator_init(struct seg_validator* this, struct block_device* bdev, sector_t start, size_t nr_segment) {
	// int r;

	this->nr_segment = nr_segment;
	this->cur_segment = 0;
	this->seg_validity_table = disk_bitset_create(bdev, start, nr_segment);
	if (IS_ERR_OR_NULL(this->seg_validity_table))
		return -ENOMEM;

	// r = this->seg_validity_table->format(this->seg_validity_table, false);
	// if (r)
	// 	return r;

	this->take = seg_validator_take;
	this->next = seg_validator_next;

	return 0;
} 

struct seg_validator* seg_validator_create(struct block_device* bdev, sector_t start, size_t nr_segment) {
	int r;
	struct seg_validator* this;

	this = kmalloc(sizeof(struct seg_validator), GFP_KERNEL);
	if (!this)
		return NULL;
	
	r = seg_validator_init(this, bdev, start, nr_segment);
	if (r)
		return NULL;

	return this;
}

void seg_validator_destroy(struct seg_validator* this) {
	if (!IS_ERR_OR_NULL(this)) {
		if (!IS_ERR_OR_NULL(this->seg_validity_table)) 
			disk_bitset_destroy(this->seg_validity_table);
		kfree(this);
	}
}

// metadata implementation
int metadata_init(struct metadata* this, struct block_device* bdev) {
	this->bdev = bdev;

	this->superblock = superblock_create(bdev);
	if (IS_ERR_OR_NULL(this->superblock))
		goto bad;
	
	this->seg_validator = seg_validator_create(bdev, this->superblock->seg_validity_table_start, this->superblock->nr_segment);
	if (IS_ERR_OR_NULL(this->seg_validator))
		goto bad;
	
	return 0;
bad:
	superblock_destroy(this->superblock);
	seg_validator_destroy(this->seg_validator);
	return -EAGAIN;
}

struct metadata* metadata_create(struct block_device* bdev) {
	int r;
	struct metadata* this;

	this = kmalloc(sizeof(struct metadata), GFP_KERNEL);
	if (!this)
		return NULL;
	
	r = metadata_init(this, bdev);
	if (r)
		return NULL;
	
	return this;
}

void metadata_destroy(struct metadata* this) {
	if (!IS_ERR_OR_NULL(this)) {
		if (!IS_ERR_OR_NULL(this->superblock))
			superblock_destroy(this->superblock);
		if (!IS_ERR_OR_NULL(this->seg_validator))
			seg_validator_destroy(this->seg_validator);
		kfree(this);
	}
}
