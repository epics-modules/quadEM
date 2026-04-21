#include <linux/ioctl.h>

#define TRIGGER_DMA_TRANSFER _IO('p', 1)
#define DMA_STATUS           _IOR('p', 2, pldrv_io_t *)
#define SET_DMA_CONTROL      _IOW('p', 3, pldrv_io_t *)
#define DEBUG                _IOW('p', 4, pldrv_io_t *)
#define SET_BURST_LENGTH     _IOW('p', 5, pldrv_io_t *)
#define SET_BUFF_LENGTH      _IOW('p', 6, pldrv_io_t *)
#define SET_RATE             _IOW('p', 7, pldrv_io_t *)
#define READ_REG             _IOWR('p', 8, pldrv_io_t *)
#define WRITE_REG            _IOW('p', 9, pldrv_io_t *)

typedef struct {
    uint32_t address;
    uint32_t data;
} pldrv_io_t;
