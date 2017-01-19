#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "pl_lib.h"


int pl_open(int *fd) {
    if ( (*fd = open(DEVNAME, O_RDWR)) <= 0 ) {
        perror(__func__);
        return -1;
    }

    return 0;
}

uint32_t pl_register_read(int fd, uint32_t addr) {
    pldrv_io_t p;
    p.address = addr / sizeof(uint32_t);
    p.data = 0xbeef;
    
    if (ioctl(fd, READ_REG, &p) == -1) {
        perror(__func__);
        return -1;
    }
    
    return p.data;
}

int pl_register_write(int fd, uint32_t addr, uint32_t data) {
    pldrv_io_t p;
    p.address = addr / sizeof(uint32_t);
    p.data = data;
    
    if (ioctl(fd, WRITE_REG, &p) == -1) {
        perror(__func__);
        return -1;
    }
    
    return 0;
}

int pl_trigger_dma(int fd) {
    if (ioctl(fd, TRIGGER_DMA_TRANSFER) == -1) {
        perror(__func__);
        return -1;
    }
    return 0;
}

uint32_t pl_get_dma_status(int fd) {
    pldrv_io_t p;
    
    if (ioctl(fd, DMA_STATUS, &p) == -1) {
        perror(__func__);
        return -1;
    }
    
    return p.data;
}
    
int pl_set_rate(int fd, uint32_t data) {
    pldrv_io_t p;
    p.address = 0x0;
    p.data = data;
    if (ioctl(fd, SET_RATE, &p) == -1) {
        perror(__func__);
        return -1;
    }
    return 0;
}

int pl_set_buff_len(int fd, uint32_t len) {
    pldrv_io_t p;
    p.address = 0x0;
    p.data = len;
    if (ioctl(fd, SET_BUFF_LENGTH, &p) == -1) {
        perror(__func__);
        return -1;
    }
    return 0;
}

int pl_set_brust_len(int fd, uint32_t len) {
    pldrv_io_t p;
    p.address = 0x0;
    p.data = len;
    if (ioctl(fd, SET_BURST_LENGTH, &p) == -1) {
        perror(__func__);
        return -1;
    }
    return 0;
}

int pl_set_debug_level(int fd, uint32_t level) {
    pldrv_io_t p;
    p.address = 0x0;
    p.data = level;
    if (ioctl(fd, DEBUG, &p) == -1) {
        perror(__func__);
        return -1;
    }
    
    return 0;
}

int pl_get_data(int fd, uint32_t *buff, size_t len) {
    buff = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    if (buff == MAP_FAILED) {
        perror(__func__);
        return -1;
    }
    
    return 0;
}
    