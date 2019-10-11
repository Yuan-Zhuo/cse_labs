#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk() { bzero(blocks, sizeof(blocks)); }

void disk::read_block(blockid_t id, char *buf) {
  if (id < 0 || id >= BLOCK_NUM || buf == NULL) return;

  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void disk::write_block(blockid_t id, const char *buf) {
  if (id < 0 || id >= BLOCK_NUM || buf == NULL) return;

  memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t block_manager::alloc_block() {
  /*
   * your code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  blockid_t iblock = -1;
  uint32_t i = block_cursor;
  for (; i < BLOCK_NUM; ++i) {
    if (using_blocks[i] == 0) {
      iblock = i;
      break;
    }
  }

  if (iblock == -1) {
    uint32_t init_cursor = IBLOCK(INODE_NUM, sb.nblocks) + 1;
    assert(block_cursor > init_cursor);
    block_cursor = init_cursor;
    return alloc_block();
  }

  block_cursor = i;
  using_blocks[iblock] = 1;

  return iblock;
}

void block_manager::free_block(uint32_t id) {
  /*
   * your code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when
   * free.
   */
  // assert(using_blocks[id] == 1);
  using_blocks[id] = 0;
  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager() {
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

  block_cursor = IBLOCK(INODE_NUM, sb.nblocks) + 1;
}

void block_manager::read_block(uint32_t id, char *buf) {
  d->read_block(id, buf);
}

void block_manager::write_block(uint32_t id, const char *buf) {
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager() {
  bm = new block_manager();
  inode_cursor = 1;
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t inode_manager::alloc_inode(uint32_t type) {
  /*
   * your code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
  assert(type != 0);
  inode_t *ino = NULL;
  uint32_t id = 0;

  uint32_t i = inode_cursor;
  for (; i < INODE_NUM; ++i) {
    ino = get_inode(i);
    if (!ino) {
      id = i;
      break;
    }
    free(ino);
  }

  if (id == 0) {
    assert(inode_cursor > 1);
    inode_cursor = 1;
    return alloc_inode(type);
  }

  inode_cursor = i;
  ino = (inode_t *)malloc(sizeof(inode_t));
  memset(ino, 0, sizeof(inode_t));
  ino->type = type;
  put_inode(id, ino);
  free(ino);
  return id;
}

void inode_manager::free_inode(uint32_t inum) {
  /*
   * your code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */
  assert(inum >= 0 && inum < INODE_NUM);
  inode_t *ino = get_inode(inum);
  if (!ino) return;

  memset(ino, 0, sizeof(inode_t));
  put_inode(inum, ino);
  free(ino);

  return;
}

/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode *inode_manager::get_inode(uint32_t inum) {
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode *)buf + inum % IPB;
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode *)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void inode_manager::put_inode(uint32_t inum, struct inode *ino) {
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL) return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode *)buf + inum % IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define NDATA_BLOCK_OCCUPY(size) \
  ((size / BLOCK_SIZE) + ((size % BLOCK_SIZE == 0) ? 0 : 1))

/* Get all the data of a file by inum.
 * Return alloced data, should be freed by caller. */
void inode_manager::read_file(uint32_t inum, char **buf_out, int *size) {
  /*
   * your code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  inode_t *ino = get_inode(inum);
  assert(ino && (ino->size < MAXFILE * BLOCK_SIZE));

  long left = ino->size;
  int iblock = 0, nblock = NDATA_BLOCK_OCCUPY(ino->size);
  uint *indirect_bnum_buf = new uint[NINDIRECT];
  if (nblock > NDIRECT)
    bm->read_block(ino->blocks[NDIRECT], (char *)indirect_bnum_buf);

  *buf_out = new char[nblock * BLOCK_SIZE];
  char *buf = *buf_out;
  while (left > 0) {
    if (iblock < NDIRECT)
      bm->read_block(ino->blocks[iblock], buf);
    else
      bm->read_block(indirect_bnum_buf[iblock - NDIRECT], buf);
    buf += BLOCK_SIZE;
    left -= BLOCK_SIZE;
    iblock++;
  }
  *size = ino->size;

  assert(iblock == nblock);
  free(ino);
  free(indirect_bnum_buf);

  return;
}

/* alloc/free blocks if needed */
void inode_manager::write_file(uint32_t inum, const char *buf, int size) {
  /*
   * your code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf
   * is larger or smaller than the size of original inode
   */
  inode_t *ino = get_inode(inum);
  assert(ino && (ino->size < MAXFILE * BLOCK_SIZE));

  const int require_nblock = NDATA_BLOCK_OCCUPY(size);
  int current_nblock = NDATA_BLOCK_OCCUPY(ino->size);
  uint *indirect_bnum_buf = new uint[NINDIRECT];
  memset(indirect_bnum_buf, 0, BLOCK_SIZE);

  ino->mtime = time(NULL);
  ino->ctime = time(NULL);
  ino->atime = time(NULL);

  if (require_nblock > current_nblock) {  // alloc blocks
    if (current_nblock < NDIRECT) {       // alloc direct blocks
      for (int i = current_nblock; i < MIN(require_nblock, NDIRECT); i++) {
        ino->blocks[i] = bm->alloc_block();
        current_nblock++;
      }
    }
    if (require_nblock != current_nblock) {  // alloc indirect blocks
      if (current_nblock == NDIRECT)         // alloc block[NDIRECT]
        ino->blocks[NDIRECT] = bm->alloc_block();
      else
        bm->read_block(ino->blocks[NDIRECT], (char *)indirect_bnum_buf);
      for (int i = current_nblock; i < MIN(require_nblock, MAXFILE); i++) {
        indirect_bnum_buf[i - NDIRECT] = bm->alloc_block();
        current_nblock++;
      }
      bm->write_block(ino->blocks[NDIRECT], (const char *)indirect_bnum_buf);
    }
    assert(current_nblock == require_nblock);
  } else if (require_nblock < current_nblock) {  // free blocks
    if (require_nblock > NDIRECT) {              // free indirect blocks
      bm->read_block(ino->blocks[NDIRECT], (char *)indirect_bnum_buf);
      for (int i = require_nblock; i < current_nblock; i++)
        bm->free_block(indirect_bnum_buf[i - NDIRECT]);
    } else {
      for (int i = require_nblock; i < NDIRECT; i++)
        bm->free_block(ino->blocks[i]);
      bm->read_block(ino->blocks[NDIRECT], (char *)indirect_bnum_buf);
      for (int i = NDIRECT; i < current_nblock; i++)
        bm->free_block(indirect_bnum_buf[i - NDIRECT]);
      bm->free_block(ino->blocks[NDIRECT]);
    }
  }

  // write buf into blocks
  if (require_nblock > NDIRECT)
    bm->read_block(ino->blocks[NDIRECT], (char *)indirect_bnum_buf);
  long left = size;
  int iblock = 0;
  while (left > 0) {
    if (iblock < NDIRECT)
      bm->write_block(ino->blocks[iblock], buf);
    else if (iblock < MAXFILE)
      bm->write_block(indirect_bnum_buf[iblock - NDIRECT], buf);
    else
      break;
    buf += BLOCK_SIZE;
    left -= BLOCK_SIZE;
    iblock++;
  }
  ino->size = (left > 0) ? MAXFILE * BLOCK_SIZE : size;
  put_inode(inum, ino);

  assert(iblock == require_nblock || left > 0);
  free(ino);

  return;
}

void inode_manager::getattr(uint32_t inum, extent_protocol::attr &a) {
  /*
   * your code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  inode_t *ino = get_inode(inum);
  if (!ino) {
    ino = (inode_t *)malloc(sizeof(inode_t));
    memset(ino, 0, sizeof(inode_t));
  }
  a.type = (uint32_t)ino->type;
  a.size = ino->size;
  a.mtime = ino->mtime;
  a.ctime = ino->ctime;
  a.atime = ino->atime;
  free(ino);

  return;
}

void inode_manager::remove_file(uint32_t inum) {
  /*
   * your code goes here
   * note: you need to consider about both the data block and inode of the file
   */
  assert(inum >= 0 && inum < INODE_NUM);
  inode_t *ino = get_inode(inum);
  assert(ino);

  int nblock = NDATA_BLOCK_OCCUPY(ino->size);
  int iblock = 0;
  for (; iblock < MIN(nblock, NDIRECT); iblock++) {
    bm->free_block(ino->blocks[iblock]);
  }

  if (nblock > NDIRECT) {
    uint *indirect_bnum_buf = new uint[NINDIRECT];
    bm->read_block(ino->blocks[NDIRECT], (char *)indirect_bnum_buf);
    for (; iblock < nblock; iblock++) {
      bm->free_block(indirect_bnum_buf[iblock - NDIRECT]);
    }
    bm->free_block(ino->blocks[NDIRECT]);
  }

  free(ino);
  free_inode(inum);

  return;
}
