#include <fuse.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "lfs.h"

static FILE *DISK;
static table_idx TABLE[BLOCKS];

static struct fuse_operations lfs_oper = {
  .getattr = lfs_getattr,
  .readdir = lfs_readdir,
  .mknod = NULL,
  .mkdir = lfs_mkdir,
  .unlink = lfs_unlink,
  .rmdir = lfs_rmdir,
  .truncate = lfs_truncate,
  .open	= lfs_open,
  .read	= lfs_read,
  .release = NULL,
  .write = lfs_write,
  .rename = NULL,
  .utime = NULL,
  .create = lfs_create
};  

int lfs_getattr( const char *path, struct stat *stbuf ) {
  int ret = 0;
  Inode ffile;
  memset(stbuf, 0, sizeof(struct stat));
  table_idx file_idx = get_index(path);
  if (file_idx == END_BLOCK) ret = -ENOENT;
  else {
    load_inode(file_idx, &ffile);
    stbuf->st_mode = ffile.mode;
    stbuf->st_mtim.tv_sec = ffile.mod_time;
    stbuf->st_atim.tv_sec = ffile.acc_time;
    stbuf->st_size = ffile.size;
    stbuf->st_blksize = BLOCK_SIZE;
  }
  return ret;
}

int lfs_readdir( const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi ) {
  int ret = 0;
  table_idx dir_idx;
  Inode dir, entry;
  dir_idx = get_index(path);
  if (dir_idx == END_BLOCK) ret = -ENOENT;
  else {
    load_inode(dir_idx, &dir);
    table_idx content[dir.size/sizeof(table_idx)];
    read_dir_content(dir_idx, &content);
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    for (int i = 0; i < dir.size/sizeof(table_idx); i++) {
        load_inode(content[i], &entry);
	char name[MAX_PATH_LENGTH];
	truncate_filename(entry.name, name);
	filler(buf, name, NULL, 0);
      }
  }
  return ret;
}

int lfs_mkdir(const char* path, mode_t mode) {
  table_idx parent_idx = get_parent_dir(path);
  table_idx dir_idx = get_free_block();
  if (dir_idx == END_BLOCK) return -ENOSPC;
  Inode dir;
  strcpy (dir.name, path);
  dir.size = 0;
  dir.acc_time = time(NULL);
  dir.mod_time = time(NULL);
  dir.mode = 0755 | S_IFDIR;
  DISK=fopen(DISK_LOCATION, "r+b");
  fseek(DISK, BLOCK_OFFSET(dir_idx), SEEK_SET);
  fwrite(&dir, sizeof(Inode), 1, DISK);
  fclose(DISK);
  update_table(dir_idx, END_BLOCK);
  if (add_dir_entry(dir_idx, parent_idx))
    {
      update_table(dir_idx, FREE_BLOCK);
      return -ENOSPC;
    }
  return 0;
}

int lfs_unlink(const char *path) {
  table_idx temp;
  table_idx idx = get_index(path);
  table_idx parent_idx = get_parent_dir(path);
  if (idx == END_BLOCK) return -ENOENT;
  remove_dir_entry(idx, parent_idx);
  while(idx != END_BLOCK) {
    temp = idx;
    idx = TABLE[idx];
    update_table(temp, FREE_BLOCK);
  }
  return 0;
}

int lfs_rmdir(const char *path) {
  Inode dir;
  table_idx dir_idx = get_index(path);
  if (dir_idx == END_BLOCK) return -ENOTDIR;
  table_idx parent_idx = get_parent_dir(path);
  load_inode(dir_idx, &dir);
  if (dir.size != 0) return -ENOTEMPTY;
  remove_dir_entry(dir_idx, parent_idx);
  update_table(dir_idx, FREE_BLOCK);
  return 0;
}

int lfs_truncate(const char* path, off_t size) {
  Inode ffile;
  table_idx file_idx = get_index(path);
  if (file_idx == END_BLOCK) return -ENOENT;
  else {
    load_inode(file_idx, &ffile);
    ffile.size = size;
    ffile.mod_time = time(NULL);
    save_inode(file_idx, &ffile);
  }
  return 0;
}

int lfs_open( const char *path, struct fuse_file_info *fi ) {
  table_idx idx = get_index(path);
  if (idx == END_BLOCK) return -ENOENT;
  fi->fh = idx;
  return 0;
}

int lfs_read( const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi ) {
  table_idx idx = (table_idx) fi->fh;
  Inode ffile;
  load_inode(idx, &ffile);
  size = min(size, ffile.size);
  int block_offset = (offset+sizeof(Inode))/BLOCK_SIZE;
  for (int i=0; i< block_offset; i++) {
    idx = TABLE[idx];
  }
  offset = (offset+sizeof(Inode)) % BLOCK_SIZE;
  read_block(idx, (void *) buf, size, offset);
  ffile.acc_time = time(NULL);
  save_inode(fi->fh, &ffile);
  return size;
}

int lfs_write( const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi ) {
  Inode ffile;
  table_idx block = fi->fh;
  load_inode((table_idx) fi->fh, &ffile);
  int block_offset = (offset+sizeof(Inode))/BLOCK_SIZE;
  while (block_offset > 0) {
    if (TABLE[block] == END_BLOCK) {
      table_idx temp = get_free_block();
      if (temp == END_BLOCK) return -ENOSPC;
      update_table(block, temp);
      update_table(temp, END_BLOCK);
      block = temp;
      break;
    }
    else {
      block = TABLE[block];
      block_offset--;
    }
  }
  offset = (ffile.size + sizeof(Inode)) % BLOCK_SIZE;
  size = min(size, BLOCK_SIZE - offset);
  write_block(block, (void*) buf, size, offset);
  ffile.size += size;
  ffile.mod_time = time(NULL);
  save_inode(fi->fh, &ffile);
  return size;
}


int lfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  table_idx block = get_free_block();
  if (block == END_BLOCK) return -ENOSPC;
  Inode ffile;
  fi->fh = (unsigned long) block;
  strcpy(ffile.name, path);
  ffile.size = 0;
  ffile.acc_time = time(NULL);
  ffile.mod_time = time(NULL);
  ffile.mode = 0666 | S_IFREG;
  save_inode(block, &ffile);
  table_idx dir = get_parent_dir(path);
  if (add_dir_entry(block, dir)) return -ENOSPC;
  update_table(block, END_BLOCK);
  return 0;
}

/*///////////////// Functions that are not called directly by FUSE /////////////////*/

int add_dir_entry(table_idx entry, table_idx dir) {
  Inode fdir;
  off_t offset;
  load_inode(dir, &fdir);
  table_idx block = dir;
  while(TABLE[block] != END_BLOCK) block = TABLE[block];
  offset = (sizeof(Inode) + fdir.size) % BLOCK_SIZE;
  if (offset == 0) {
    table_idx new_block = get_free_block();
    if (new_block == END_BLOCK) return -ENOSPC;
    update_table(block, new_block);
    update_table(new_block, END_BLOCK);
    block = new_block;
  }
  write_block(block, &entry, sizeof(table_idx), offset);
  fdir.size += sizeof(table_idx);
  fdir.mod_time = time(NULL);
  save_inode(dir, &fdir);
  return 0;
}

void remove_dir_entry(table_idx entry_idx, table_idx dir_idx) {
  Inode dir;
  load_inode(dir_idx, &dir);
  table_idx content[dir.size/sizeof(table_idx)];
  read_dir_content(dir_idx, &content);
  for (int i = 0; i < dir.size/sizeof(table_idx); i++) {
    if (content[i] == entry_idx) {
      content[i] = content[dir.size/sizeof(table_idx) -1];
      break;
    }
  }
  write_dir_content(dir_idx, &content);
  dir.size -= sizeof(table_idx);
  dir.mod_time = time(NULL);
  save_inode(dir_idx, &dir);
  if ((dir.size + sizeof(Inode)) % BLOCK_SIZE == 0 ) {
    while (1) {
      if (TABLE[TABLE[dir_idx]] == END_BLOCK) {
	update_table(TABLE[TABLE[dir_idx]], FREE_BLOCK);
	update_table(TABLE[dir_idx], END_BLOCK);
	break;
      }
    }
  }
  return;
}

void load_inode(table_idx i, Inode *ffile) {
  DISK = fopen(DISK_LOCATION, "rb");
  fseek(DISK, BLOCK_OFFSET(i), SEEK_SET);
  fread(ffile, sizeof(Inode), 1, DISK);
  fclose(DISK);
  return;
}

void save_inode(table_idx i, Inode *ffile) {
  DISK = fopen(DISK_LOCATION, "r+b");
  fseek(DISK, BLOCK_OFFSET(i), SEEK_SET);
  fwrite(ffile, sizeof(Inode), 1, DISK);
  fclose(DISK);
  return;
}

int write_dir_content(table_idx idx, void *buf) {
  Inode ffile;
  load_inode(idx, &ffile);
  off_t offset = sizeof(Inode);
  size_t size = min( BLOCK_SIZE - sizeof(Inode), ffile.size);
  int bytes_written = 0;
  while (TABLE[idx] != END_BLOCK) {
    write_block(idx, buf + bytes_written, size, offset);
    bytes_written += size;
    offset = 0;
    size = (ffile.size - bytes_written) % BLOCK_SIZE;
    idx = TABLE[idx];
  }
  write_block(idx, buf + bytes_written, size, offset);
  bytes_written += size;
  return bytes_written;
}


int read_dir_content(table_idx idx, void* buf) {
  Inode ffile;
  load_inode(idx, &ffile);
  off_t offset = sizeof(Inode);
  size_t size = min( BLOCK_SIZE - sizeof(Inode), ffile.size);
  int bytes_read = 0;
  while (TABLE[idx] != END_BLOCK) {
    read_block(idx, buf + bytes_read, size, offset);
    bytes_read += size;
    offset = 0;
    size = (ffile.size - bytes_read) % BLOCK_SIZE;
    idx = TABLE[idx];
  }
  read_block(idx, buf + bytes_read, size, offset);
  bytes_read += size;
  return bytes_read;
}

void read_block(table_idx block_idx, void *content, size_t size, off_t offset) {
  DISK = fopen(DISK_LOCATION, "rb");
  fseek(DISK, BLOCK_OFFSET(block_idx) + offset, SEEK_SET);
  fread(content, size, 1, DISK);
  fclose(DISK);
}

void write_block(table_idx block_idx, void *content, size_t size, off_t offset) {
  DISK = fopen(DISK_LOCATION, "r+b");
  fseek(DISK, BLOCK_OFFSET(block_idx) + offset, SEEK_SET);
  fwrite(content, size, 1, DISK);
  fclose(DISK);
}

void update_table(table_idx index, table_idx ptr) {
  TABLE[index]=ptr;
  DISK = fopen(DISK_LOCATION, "r+b");
  fseek(DISK, index*sizeof(table_idx), SEEK_SET);
  fwrite(&ptr, sizeof(table_idx), 1, DISK);
  fclose(DISK);
  return;
}

table_idx get_index(const char *path) {
  table_idx i;
  Inode ffile;
  for (i =0; i<BLOCKS; i++) {
    if (TABLE[i] == FREE_BLOCK) continue;
    load_inode(i, &ffile);
    if (strcmp(path, ffile.name) == 0) return i;
  }
  return END_BLOCK;
}

void truncate_filename(char *path, char *name) {
  int i=0, j=0;
  while(path[i] != '\0') {
    if (path[i] == '/') j=i;
    i++;
  }
  memcpy(name, path+j+1, (i-j));
  name[i]='\0';
}

table_idx get_parent_dir(const char *path) {
  char m[MAX_PATH_LENGTH];
  int i=0, j=0;
  while(path[i] != '\0') {
    if (path[i] == '/') j=i;
    i++;
  }
  if (j==0) return 0; // parent is root dir
  memcpy(m, path, j);
  m[j]='\0';
  return get_index(m);
}

table_idx get_free_block() {
  for (table_idx i=1; i<BLOCKS; i++) {
    if (TABLE[i] == FREE_BLOCK) return i;
  }
  return END_BLOCK;
}

void load_table() {
  DISK = fopen(DISK_LOCATION, "rb");
  fread(TABLE, sizeof(table_idx), 40640, DISK);
  fclose(DISK);
  return;
}

int main( int argc, char *argv[] ) {
  load_table();
  fuse_main( argc, argv, &lfs_oper );
  return 0;
}
