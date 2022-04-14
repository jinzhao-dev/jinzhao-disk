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
	this->blocks_per_seg = le32_to_cpu(disk_super->blocks_per_seg);
	this->nr_segment = le64_to_cpu(disk_super->nr_segment);
	this->common_ratio = le32_to_cpu(disk_super->common_ratio);
	this->nr_disk_level = le32_to_cpu(disk_super->nr_disk_level);
	this->max_disk_level_capacity = le64_to_cpu(disk_super->max_disk_level_capacity);
	this->index_region_start = le64_to_cpu(disk_super->index_region_start);
	this->journal_size = le32_to_cpu(disk_super->journal_size);
	this->nr_journal = le64_to_cpu(disk_super->nr_journal);
	this->cur_journal = le64_to_cpu(disk_super->cur_journal);
	this->journal_region_start = le64_to_cpu(disk_super->journal_region_start);
	this->seg_validity_table_start = le64_to_cpu(disk_super->seg_validity_table_start);
	this->data_seg_table_start = le64_to_cpu(disk_super->data_seg_table_start);
	this->reverse_index_table_start = le64_to_cpu(disk_super->reverse_index_table_start);
	this->block_index_table_catalogue_start = le64_to_cpu(disk_super->block_index_table_catalogue_start);

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
	disk_super->blocks_per_seg = cpu_to_le32(this->blocks_per_seg);
	disk_super->nr_segment = cpu_to_le64(this->nr_segment);
	disk_super->common_ratio = cpu_to_le32(this->common_ratio);
	disk_super->nr_disk_level = cpu_to_le32(this->nr_disk_level);
	disk_super->max_disk_level_capacity = cpu_to_le64(this->max_disk_level_capacity);
	disk_super->index_region_start = cpu_to_le64(this->index_region_start);
	disk_super->journal_size = cpu_to_le32(this->journal_size);
	disk_super->nr_journal = cpu_to_le64(this->nr_journal);
	disk_super->cur_journal = cpu_to_le64(this->cur_journal);
	disk_super->journal_region_start = cpu_to_le64(this->journal_region_start);
	disk_super->seg_validity_table_start = cpu_to_le64(this->seg_validity_table_start);
	disk_super->data_seg_table_start = cpu_to_le64(this->data_seg_table_start);
	disk_super->reverse_index_table_start = cpu_to_le64(this->reverse_index_table_start);
	disk_super->block_index_table_catalogue_start = cpu_to_le64(this->block_index_table_catalogue_start);	

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
	DMINFO("\tblocks_per_seg: %d", this->blocks_per_seg);
	DMINFO("\tnr_segment: %lld", this->nr_segment);
	DMINFO("\tcommon ratio: %d", this->common_ratio);
	DMINFO("\tnr_disk_level: %d", this->nr_disk_level);
	DMINFO("\tmax_disk_level_capacity: %lld", this->max_disk_level_capacity);
	DMINFO("\tindex_region_start: %lld", this->index_region_start);
	DMINFO("\tjournal_size: %d", this->journal_size);
	DMINFO("\tnr_journal: %lld", this->nr_journal);
	DMINFO("\tcur_journal: %lld", this->cur_journal);
	DMINFO("\tjournal_region_start: %lld", this->journal_region_start);
	DMINFO("\tseg_validity_table_start: %lld", this->seg_validity_table_start);
	DMINFO("\tdata_seg_table_start: %lld", this->data_seg_table_start);
	DMINFO("\treverse_index_table_start: %lld", this->reverse_index_table_start);
	DMINFO("\tblock_index_table_catalogue_start: %lld", this->block_index_table_catalogue_start);
}

#include "../include/segment_allocator.h"
#define LSM_TREE_DISK_LEVEL_COMMON_RATIO 8

size_t __disk_array_blocks(size_t nr_elem, size_t elem_size, size_t block_size) {
	size_t elems_per_block = block_size / elem_size;

	return nr_elem / elems_per_block + 1;
}

size_t __index_region_blocks(size_t nr_disk_level, size_t common_ratio, size_t max_disk_level_capacity) {
	size_t i, capacity, total_blocks = 0;

	capacity = max_disk_level_capacity;
	for (i = 0; i < nr_disk_level; ++i) {
		total_blocks += __disk_array_blocks(capacity, sizeof(struct bit_node), SWORNDISK_METADATA_BLOCK_SIZE);
		capacity /= common_ratio;
	}

	return total_blocks;
}

size_t __bytes_to_block(size_t bytes, size_t block_size) {
	if (bytes == 0)
		return 0;
	
	return (bytes - 1) / block_size + 1;
}

size_t __journal_region_blocks(size_t nr_journal, size_t journal_size) {
	return __disk_array_blocks(nr_journal, journal_size, SWORNDISK_METADATA_BLOCK_SIZE);
}

size_t __seg_validity_table_blocks(size_t nr_segment) {
	if (!nr_segment)
		return 0;
	return (((nr_segment - 1) / BITS_PER_LONG + 1) * sizeof(unsigned long) - 1) / SWORNDISK_METADATA_BLOCK_SIZE + 1;
}

size_t __data_seg_table_blocks(size_t nr_segment) {
	return __disk_array_blocks(nr_segment, sizeof(struct data_segment_entry), SWORNDISK_METADATA_BLOCK_SIZE);
}

size_t __reverse_index_table_blocks(size_t nr_segment, size_t blocks_per_seg) {
	return __disk_array_blocks(nr_segment * blocks_per_seg, sizeof(struct data_segment_entry), SWORNDISK_METADATA_BLOCK_SIZE);
}

int superblock_init(struct superblock* this, struct dm_block_manager* bm, bool* should_format) {
	int r;

	*should_format = false;
	this->bm = bm;

	this->read = superblock_read;
	this->write = superblock_write;
	this->validate = superblock_validate;
	this->print = superblock_print;

	r = this->read(this);
	if (r)
		return r;
	
	if (this->validate(this))
		return 0;

	*should_format = true;
	this->magic = SUPERBLOCK_MAGIC;
	this->blocks_per_seg = BLOCKS_PER_SEGMENT;
	this->nr_segment = NR_SEGMENT;
	this->common_ratio = LSM_TREE_DISK_LEVEL_COMMON_RATIO;
	this->nr_disk_level = DEFAULT_LSM_TREE_NR_LEVEL - 1;
	this->max_disk_level_capacity = NR_SEGMENT * BLOCKS_PER_SEGMENT;
	this->index_region_start = SUPERBLOCK_LOCATION +  STRUCTURE_BLOCKS(struct superblock);
	this->journal_size = 0;
	this->nr_journal = 0;
	this->cur_journal = 0;
	this->journal_region_start = this->index_region_start + __index_region_blocks(
	  this->nr_disk_level, this->common_ratio, this->max_disk_level_capacity);
	this->seg_validity_table_start = this->journal_region_start + __journal_region_blocks(this->nr_journal, this->journal_size);
	this->data_seg_table_start = this->seg_validity_table_start + __seg_validity_table_blocks(this->nr_segment);
	this->reverse_index_table_start = this->data_seg_table_start + __data_seg_table_blocks(this->nr_segment);
	this->block_index_table_catalogue_start = this->reverse_index_table_start + __reverse_index_table_blocks(this->nr_segment, this->blocks_per_seg);

	this->write(this);
	return 0;
}

struct superblock* superblock_create(struct dm_block_manager* bm, bool* should_format) {
	int r;
	struct superblock* this;

	this = kmalloc(sizeof(struct superblock), GFP_KERNEL);
	if (!this)
		return NULL;
	
	r = superblock_init(this, bm, should_format);
	if (r)
		return NULL;
	
	return this;
}

void superblock_destroy(struct superblock* this) {
	if (!IS_ERR_OR_NULL(this)) {
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
	size_t try_times = 0;

next_try:
	while(this->cur_segment < this->nr_segment) {
		r = this->seg_validity_table->get(this->seg_validity_table, this->cur_segment, &valid);
		if (!r && !valid) {
			*next_seg = this->cur_segment;
			return 0;
		}
		this->cur_segment += 1;
	}

	if (try_times < 1) {
		try_times += 1;
		this->cur_segment = 0;
		goto next_try;
	}

	return -ENODATA;
}

int seg_validator_test_and_return(struct seg_validator* this, size_t seg, bool* old) {
	int r;
	bool valid;

	r = this->seg_validity_table->get(this->seg_validity_table, seg, &valid);
	if (r)
		return r;

	r = this->seg_validity_table->clear(this->seg_validity_table, seg);
	if (r)
		return r;

	*old = valid;
	return 0;
}

int seg_validator_format(struct seg_validator* this) {
	int r;

	r = this->seg_validity_table->format(this->seg_validity_table, false);
	if (r)
		return r;

	this->cur_segment = 0;
	return 0;
}

int seg_validator_valid_segment_count(struct seg_validator* this, size_t* count) {
	int r;
	bool valid;
	size_t segment_id;

	*count = 0;
	for (segment_id = 0; segment_id < this->nr_segment; ++segment_id) {
		r = this->seg_validity_table->get(this->seg_validity_table, segment_id, &valid);
		if (r)
			return r;
		*count += valid;
	}

	return 0;
}

int seg_validator_init(struct seg_validator* this, struct dm_block_manager* bm, dm_block_t start, size_t nr_segment) {
	this->nr_segment = nr_segment;
	this->cur_segment = 0;
	this->seg_validity_table = disk_bitset_create(bm, start, nr_segment);
	if (IS_ERR_OR_NULL(this->seg_validity_table))
		return -ENOMEM;

	this->take = seg_validator_take;
	this->next = seg_validator_next;
	this->format = seg_validator_format;
	this->valid_segment_count = seg_validator_valid_segment_count;
	this->test_and_return = seg_validator_test_and_return;

	return 0;
} 

struct seg_validator* seg_validator_create(struct dm_block_manager* bm, dm_block_t start, size_t nr_segment) {
	int r;
	struct seg_validator* this;

	this = kmalloc(sizeof(struct seg_validator), GFP_KERNEL);
	if (!this)
		return NULL;
	
	r = seg_validator_init(this, bm, start, nr_segment);
	if (r)
		return NULL;

	return this;
}

// reverse index table implementation
int reverse_index_table_format(struct reverse_index_table* this) {
	return this->array->format(this->array, false);
}

int __reverse_index_table_set_entry(struct reverse_index_table* this, dm_block_t pba, struct reverse_index_entry* entry) {
	return this->array->set(this->array, pba, entry);
}

int reverse_index_table_set(struct reverse_index_table* this, dm_block_t pba, dm_block_t lba) {
	int r;
	struct reverse_index_entry entry = {
		.valid = true,
		.lba = lba
	};

	r = __reverse_index_table_set_entry(this, pba, &entry);
	if (r)
		return r;
	
	return 0;
}

struct reverse_index_entry* __reverse_index_table_get_entry(struct reverse_index_table* this, dm_block_t pba) {
	return this->array->get(this->array, pba);
}

int reverse_index_table_get(struct reverse_index_table* this, dm_block_t pba, dm_block_t* lba) {
	struct reverse_index_entry* entry;

	entry = __reverse_index_table_get_entry(this, pba);
	if (IS_ERR_OR_NULL(entry))
		return -ENODATA;
	
	if (!entry->valid)
		return -ENODATA;
	
	*lba = entry->lba;
	return 0;
}

int reverse_index_table_init(struct reverse_index_table* this, struct dm_block_manager* bm, dm_block_t start, size_t nr_block) {
	this->nr_block = nr_block;
	this->array = disk_array_create(bm, start, this->nr_block, sizeof(struct reverse_index_entry));
	if (IS_ERR_OR_NULL(this->array))
		return -ENOMEM;

	this->format = reverse_index_table_format;
	this->set = reverse_index_table_set;
	this->get = reverse_index_table_get;

	return 0;
}

struct reverse_index_table* reverse_index_table_create(struct dm_block_manager* bm, dm_block_t start, size_t nr_block) {
	int r;
	struct reverse_index_table* this;

	this = kmalloc(sizeof(struct reverse_index_table), GFP_KERNEL);
	if (!this)
		return NULL;
	
	r = reverse_index_table_init(this, bm, start, nr_block);
	if (r)
		return NULL;
	
	return this;
}

void reverse_index_table_destroy(struct reverse_index_table* this) {
	if (!IS_ERR_OR_NULL(this)) {
		if (!IS_ERR_OR_NULL(this->array))
			disk_array_destroy(this->array);
		kfree(this);
	}
}

void seg_validator_destroy(struct seg_validator* this) {
	if (!IS_ERR_OR_NULL(this)) {
		if (!IS_ERR_OR_NULL(this->seg_validity_table)) 
			disk_bitset_destroy(this->seg_validity_table);
		kfree(this);
	}
}

// data segment table implementaion
struct victim* victim_create(size_t segment_id, size_t nr_valid_block, unsigned long* block_validity_table) {
	struct victim* victim;

	victim = kmalloc(sizeof(struct victim), GFP_KERNEL);
	if (!victim)
		return NULL;
	
	victim->segment_id = segment_id;
	victim->nr_valid_block = nr_valid_block;
	bitmap_copy(victim->block_validity_table, block_validity_table, BLOCKS_PER_SEGMENT);
	return victim;
}

void victim_destroy(struct victim* victim) {
	if (!IS_ERR_OR_NULL(victim)) {
		kfree(victim);
	}
}

bool victim_less(struct rb_node* node1, const struct rb_node* node2) {
	struct victim *victim1, *victim2;

	victim1 = container_of(node1, struct victim, node);
	victim2 = container_of(node2, struct victim, node);
	return victim1->nr_valid_block < victim2->nr_valid_block;
} 

int data_segment_table_load(struct data_segment_table* this) {
	size_t segment_id;
	struct victim* victim;
	struct data_segment_entry* segment;
	
	for (segment_id = 0; segment_id < this->nr_segment; ++segment_id) {
		segment = this->array->get(this->array, segment_id);
		if (IS_ERR_OR_NULL(segment))
			return -ENODATA;
		
		if (segment->nr_valid_block < BLOCKS_PER_SEGMENT) {
			victim = victim_create(segment_id, segment->nr_valid_block, segment->block_validity_table);
			if (IS_ERR_OR_NULL(victim))
				return -ENOMEM;
			this->node_list[segment_id] = &victim->node;
			rb_add(&victim->node, &this->victims, victim_less);
		}

		kfree(segment);
	}

	return 0;
}

inline size_t __block_to_segment(dm_block_t block_id) {
	return block_id / BLOCKS_PER_SEGMENT;
}

inline size_t __block_offset_whthin_segment(dm_block_t block_id) {
	return block_id % BLOCKS_PER_SEGMENT;
}

int data_segment_table_take_segment(struct data_segment_table* this, size_t segment_id) {
	int err = 0;
	struct victim* victim = NULL;
	struct data_segment_entry* entry = NULL;

	entry = this->array->get(this->array, segment_id);
	if (IS_ERR_OR_NULL(entry))
		return -EINVAL;
	
	if (entry->nr_valid_block) {
		err = -ENODATA;
		goto exit;
	}

	entry->nr_valid_block = BLOCKS_PER_SEGMENT;
	bitmap_fill(entry->block_validity_table, BLOCKS_PER_SEGMENT);

	err = this->array->set(this->array, segment_id, entry);
	if (err) 
		goto exit;


	victim = this->remove_victim(this, segment_id);
	victim_destroy(victim);

exit:
	if (!IS_ERR_OR_NULL(entry))
		kfree(entry);
	return err;
}

int data_segment_table_return_block(struct data_segment_table* this, dm_block_t block_id) {
	int err = 0;
	struct victim* victim = NULL;
	struct data_segment_entry* entry = NULL;
	size_t segment_id, offset;

	segment_id = __block_to_segment(block_id);
	offset = __block_offset_whthin_segment(block_id);

	entry = this->array->get(this->array, segment_id);
	if (IS_ERR_OR_NULL(entry))
		return -EINVAL;

	if (!entry->nr_valid_block) {
		err = -ENODATA;
		goto exit;
	}

	entry->nr_valid_block -= 1;
	clear_bit(offset, entry->block_validity_table);

	err = this->array->set(this->array, segment_id, entry);
	if (err) 
		goto exit;

	victim = this->remove_victim(this, segment_id);
	victim_destroy(victim);
	
	victim = victim_create(segment_id, entry->nr_valid_block, entry->block_validity_table);
	if (IS_ERR_OR_NULL(victim)) {
		err = -ENOMEM;
		goto exit;
	}

	rb_add(&victim->node, &this->victims, victim_less);
	this->node_list[segment_id] = &victim->node;

exit:
	if (!IS_ERR_OR_NULL(entry))
		kfree(entry);
	return err;
}

bool data_segment_table_victim_empty(struct data_segment_table* this) {
	return RB_EMPTY_ROOT(&this->victims);
}

struct victim* data_segment_table_peek_victim(struct data_segment_table* this) {
	struct rb_node* node;

	node = rb_first(&this->victims);
	if (IS_ERR_OR_NULL(node))
		return NULL;
	
	return rb_entry(node, struct victim, node);
}

struct victim* data_segment_table_pop_victim(struct data_segment_table* this) {
	struct victim* victim;

	victim = this->peek_victim(this);
	if (IS_ERR_OR_NULL(victim))
		return NULL;
	
	rb_erase(&victim->node, &this->victims);
	this->node_list[victim->segment_id] = NULL;

	return victim;
}

struct victim* data_segment_table_remove_victim(struct data_segment_table* this, size_t segment_id) {
	struct victim* victim = NULL;

	if (this->node_list[segment_id]) {
		victim = rb_entry(this->node_list[segment_id], struct victim, node);
		rb_erase(&victim->node, &this->victims);
		this->node_list[segment_id] = NULL;
	}
	
	return victim;
}

int data_segment_table_init(struct data_segment_table* this, struct dm_block_manager* bm, dm_block_t start, size_t nr_segment);
int data_segment_table_format(struct data_segment_table* this) {
	int r;

	r = this->array->format(this->array, false);
	if (r)
		return r;
	
	while(!RB_EMPTY_ROOT(&this->victims)) {
		victim_destroy(this->pop_victim(this));
	}

	kfree(this->node_list);

	r = data_segment_table_init(this, this->bm, this->start, this->nr_segment);
	if (r)
		return r;
	
	return 0;
}

int data_segment_table_init(struct data_segment_table* this, struct dm_block_manager* bm, dm_block_t start, size_t nr_segment) {
	int r;

	this->load = data_segment_table_load;
	this->take_segment = data_segment_table_take_segment;
	this->return_block = data_segment_table_return_block;
	this->victim_empty = data_segment_table_victim_empty;
	this->peek_victim = data_segment_table_peek_victim;
	this->pop_victim = data_segment_table_pop_victim;
	this->remove_victim = data_segment_table_remove_victim;
	this->format = data_segment_table_format;

	this->bm = bm;
	this->start = start;
	this->nr_segment = nr_segment;
	this->array = disk_array_create(this->bm, this->start, this->nr_segment, sizeof(struct data_segment_entry));
	if (IS_ERR_OR_NULL(this->array))
		return -ENOMEM;

	this->node_list = kzalloc(this->nr_segment * sizeof(struct rb_node*), GFP_KERNEL);
	if (!this->node_list)
		return -ENOMEM;

	this->victims = RB_ROOT;
	r = this->load(this);
	if (r)
		return r;
	
	return 0;
}

struct data_segment_table* data_segment_table_create(struct dm_block_manager* bm, dm_block_t start, size_t nr_segment) {
	int r;
	struct data_segment_table* this;

	this = kmalloc(sizeof(struct data_segment_table), GFP_KERNEL);
	if (!this)	
		return NULL;
	
	r = data_segment_table_init(this, bm, start, nr_segment);
	if (r)
		return NULL;

	return this;
}

void data_segment_table_destroy(struct data_segment_table* this) {	
	if (!IS_ERR_OR_NULL(this)) {
		while(!RB_EMPTY_ROOT(&this->victims)) {
			victim_destroy(this->pop_victim(this));
		}
		if (!IS_ERR_OR_NULL(this->node_list)) 
			kfree(this->node_list);
		kfree(this);
	}
}

// metadata implementation
int metadata_format(struct metadata* this) {
	int r;

	r = this->seg_validator->format(this->seg_validator);
	if (r)
		return r;
	
	r = this->reverse_index_table->format(this->reverse_index_table);
	if (r)
		return r;

	r = this->data_segment_table->format(this->data_segment_table);
	if (r)
		return r;

	return 0;
}

int metadata_init(struct metadata* this, struct block_device* bdev) {
	int r;
	bool should_format;

	this->bdev = bdev;
	this->bm = dm_block_manager_create(this->bdev, SWORNDISK_METADATA_BLOCK_SIZE, SWORNDISK_MAX_CONCURRENT_LOCKS);
	if (IS_ERR_OR_NULL(this->bm))
		goto bad;

	this->superblock = superblock_create(this->bm, &should_format);
	if (IS_ERR_OR_NULL(this->superblock))
		goto bad;
	
	this->seg_validator = seg_validator_create(this->bm, this->superblock->seg_validity_table_start, this->superblock->nr_segment);
	if (IS_ERR_OR_NULL(this->seg_validator))
		goto bad;

	this->reverse_index_table = reverse_index_table_create(this->bm, 
	  this->superblock->reverse_index_table_start, this->superblock->nr_segment * this->superblock->blocks_per_seg);
	if (IS_ERR_OR_NULL(this->reverse_index_table))
		goto bad;

	this->data_segment_table = data_segment_table_create(this->bm, this->superblock->data_seg_table_start, this->superblock->nr_segment);
	if (IS_ERR_OR_NULL(this->data_segment_table))
		goto bad;

	this->format = metadata_format;
	if (should_format) {
		r = this->format(this);
		if (r)
			goto bad;
	}
	
	return 0;
bad:
	if (!IS_ERR_OR_NULL(this->bm))
		dm_block_manager_destroy(this->bm);
	superblock_destroy(this->superblock);
	seg_validator_destroy(this->seg_validator);
	reverse_index_table_destroy(this->reverse_index_table);
	data_segment_table_destroy(this->data_segment_table);
	return -EAGAIN;
}

struct metadata* metadata_create(struct block_device* bdev) {
	int r;
	struct metadata* this;

	this = kzalloc(sizeof(struct metadata), GFP_KERNEL);
	if (!this)
		return NULL;
	
	r = metadata_init(this, bdev);
	if (r)
		return NULL;
	
	return this;
}

void metadata_destroy(struct metadata* this) {
	if (!IS_ERR_OR_NULL(this)) {
		if (!IS_ERR_OR_NULL(this->bm)) {
			dm_bm_flush(this->bm);
			dm_block_manager_destroy(this->bm);
		}
		superblock_destroy(this->superblock);
		seg_validator_destroy(this->seg_validator);
		reverse_index_table_destroy(this->reverse_index_table);
		data_segment_table_destroy(this->data_segment_table);
		kfree(this);
	}
}
