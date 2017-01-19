#include "pl_ioctl.h"

#define DEVNAME "/dev/vipic"
#define PAGE_SIZE 4096

int pl_open(int *fd);
uint32_t pl_register_read(int fd, uint32_t addr);
int pl_register_write(int fd, uint32_t addr, uint32_t data);
int pl_trigger_dma(int fd);
uint32_t pl_get_dma_status(int fd);
int pl_set_rate(int fd, uint32_t data);
int pl_set_buff_len(int fd, uint32_t len);
int pl_set_brust_len(int fd, uint32_t len);
int pl_set_debug_level(int fd, uint32_t level);
