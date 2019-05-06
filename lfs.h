#define BLOCK_SIZE 256
#define BLOCKS 40640
#define DISK_START_BYTE 81920
#define DISK_SIZE DISK_START_BYTE + BLOCKS * BLOCK_SIZE
#define DISK_LOCATION "/mnt/home/erik/Documents/DM510OperatingSystems/assignment4/source/lfs-disk"
#define MAX_PATH_LENGTH 32

#define BLOCK_OFFSET(i) (DISK_START_BYTE + (i)*BLOCK_SIZE) 

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif


typedef unsigned short table_idx;

typedef struct inode {
  char name[MAX_PATH_LENGTH];
  unsigned int size;
  time_t mod_time;
  time_t acc_time;
  mode_t mode;
} Inode;

// Functions called by FUSE
int lfs_getattr( const char *, struct stat * );
int lfs_readdir( const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info * );
int lfs_open( const char *, struct fuse_file_info * );
int lfs_read( const char *, char *, size_t, off_t, struct fuse_file_info * );
int lfs_write( const char *, const char *, size_t, off_t, struct fuse_file_info * );
int lfs_create(const char *, mode_t, struct fuse_file_info *);
int lfs_unlink(const char *);
int lfs_mkdir(const char *, mode_t);
int lfs_rmdir(const char *);

// Internal functions
void load_table();
void update_table(table_idx, table_idx);
table_idx get_index(const char *);
table_idx get_free_block(void);

table_idx get_parent_dir(const char *);
void remove_dir_entry(table_idx, table_idx);
int add_dir_entry(table_idx, table_idx);
int write_dir_content(table_idx, void *);
int read_dir_content(table_idx, void *);


void read_block(table_idx, void *, size_t, off_t);
void write_block(table_idx, void *, size_t, off_t);
void load_inode(table_idx, Inode *);
void save_inode(table_idx, Inode *);
void truncate_filename(char *, char *);

  
table_idx FREE_BLOCK = 0x0;
table_idx END_BLOCK = 0xffff;
table_idx DIR_BLOCK = 0xfffe;
