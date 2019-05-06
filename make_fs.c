#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fuse.h>
#include <string.h>
#include "lfs.h"

char disk[BLOCKS*BLOCK_SIZE];

int main() {
  printf("Creating a new disk file on %s\nIf a disk already resides here it will be overwritten.\n", DISK_LOCATION);
  memset(disk, 0, BLOCKS*BLOCK_SIZE);
  FILE *DISK = fopen(DISK_LOCATION, "w");
  fwrite(disk, BLOCK_SIZE, BLOCKS, DISK);
  fclose(DISK);
  DISK = fopen(DISK_LOCATION, "r+");
  if (DISK<0) return -1;
  // Create entry for the root directory
  fwrite(&END_BLOCK, sizeof(table_idx), 1, DISK);
  fclose(DISK);
  // Create the root directory structure. 
  Inode root;
  root.size = 0;
  strcpy(root.name, "/");
  root.mod_time = time(NULL);
  root.acc_time = time(NULL);
  root.mode = 0755 | S_IFDIR;
  DISK = fopen(DISK_LOCATION, "r+");
  fseek(DISK, DISK_START_BYTE, SEEK_SET);
  fwrite(&root, sizeof(Inode), 1, DISK);
  return 0;
}
