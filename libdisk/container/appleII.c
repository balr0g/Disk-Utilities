/*
 * libdisk/container_img.c
 * 
 * Write IMG images (dump of logical sector contents).
 * 
 * Written in 2012 by Keir Fraser
 */

#include <libdisk/util.h>
#include "../private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

struct appleII_extra_data {
    int *sector_translate_table;
};


int sector_translate_logical_order[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};
int sector_translate_dos_order[16] = {
    0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04,
    0x0B, 0x03, 0x0A, 0x02, 0x09, 0x01, 0x08, 0x0F
};
int sector_translate_prodos_order[16] = {
    0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B,
    0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F
};


static const struct container *appleII_open(struct disk *d)
{
    /* not supported */
    return NULL;
}

void sector_translate(uint8_t *secdata, uint32_t secbytes, int *sector_translate_table) {
    if(secbytes / 256 != 16) // only do this ever for 16-sector disks!
        return;
    uint8_t buf[secbytes];
    for(int i=0; i<16; i++) {
         memcpy(&buf[sector_translate_table[i]*256], secdata, 256);
    }
}

static void appleII_close(struct disk *d)
{
    unsigned int i;
    struct disk_info *di = d->di;
    struct track_sectors *sectors;
    struct appleII_extra_data *extra_data = d->container->extra_data;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    sectors = track_alloc_sector_buffer(d);
    for (i = 0; i < di->nr_tracks; i++) {
        if (track_read_sectors(sectors, i) != 0)
            continue;
        sector_translate(sectors->data, sectors->nr_bytes, extra_data->sector_translate_table);
        write_exact(d->fd, sectors->data, sectors->nr_bytes);
    }
    track_free_sector_buffer(sectors);
}

struct container container_appleII_logical = {
    .init = dsk_init,
    .open = appleII_open,
    .close = appleII_close,
    .write_raw = dsk_write_raw,
    .extra_data = & (struct appleII_extra_data) {
        .sector_translate_table = sector_translate_logical_order
    }
};

struct container container_appleII_do = {
    .init = dsk_init,
    .open = appleII_open,
    .close = appleII_close,
    .write_raw = dsk_write_raw,
    .extra_data = & (struct appleII_extra_data) {
        .sector_translate_table = sector_translate_dos_order
    }
};

struct container container_appleII_po = {
    .init = dsk_init,
    .open = appleII_open,
    .close = appleII_close,
    .write_raw = dsk_write_raw,
    .extra_data = & (struct appleII_extra_data) {
        .sector_translate_table = sector_translate_prodos_order
    }
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
