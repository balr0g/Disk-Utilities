/*
 * libdisk/container_adf.c
 * 
 * Read/write ADF images.
 * 
 * Written in 2011 by Keir Fraser
 */

#include <libdisk/util.h>
#include "../private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void adf_init_track(struct track_info *ti)
{
    unsigned int i;

    init_track_info(ti, TRKTYP_amigados);
    ti->dat = memalloc(ti->len);
    ti->data_bitoff = 1024;
    ti->total_bits = DEFAULT_BITS_PER_TRACK;

    for (i = 0; i < ti->len/4; i++)
        memcpy(ti->dat+i*4, "NDOS", 4);
}

static void adf_init(struct disk *d)
{
    struct disk_info *di;
    unsigned int i;

    d->di = di = memalloc(sizeof(*di));
    di->nr_tracks = 160;
    di->flags = 0;
    di->track = memalloc(di->nr_tracks * sizeof(struct track_info));

    for (i = 0; i < di->nr_tracks; i++)
        adf_init_track(&di->track[i]);
}

static const struct container *adf_open(struct disk *d)
{
    struct track_info *ti;
    struct disk_info *di;
    unsigned int i, j, k;
    char sig[8];
    off_t sz;

    read_exact(d->fd, sig, sizeof(sig));
    if (!strncmp(sig, "UAE-1ADF", sizeof(sig)))
        return containers[CONTTYP_eadf]->open(d);

    sz = lseek(d->fd, 0, SEEK_END);
    if (sz != 160*512*11) {
        warnx("ADF file bad size: %lu bytes", (unsigned long)sz);
        return NULL;
    }
    lseek(d->fd, 0, SEEK_SET);

    adf_init(d);
    di = d->di;

    for (i = 0; i < di->nr_tracks; i++) {
        ti = &di->track[i];
        read_exact(d->fd, ti->dat, ti->len);
        set_all_sectors_invalid(ti);
        for (j = 0; j < ti->nr_sectors; j++) {
            unsigned char *p = ti->dat + j*ti->bytes_per_sector;
            for (k = 0; k < ti->bytes_per_sector/4; k++)
                if (memcmp(p+k*4, "NDOS", 4))
                    break;
            if (k != ti->bytes_per_sector/4)
                set_sector_valid(ti, j);
        }
    }

    return containers[CONTTYP_eadf];
}

static void adf_close(struct disk *d)
{
    struct disk_info *di = d->di;
    unsigned int i;

    lseek(d->fd, 0, SEEK_SET);
    if (ftruncate(d->fd, 0) < 0)
        err(1, NULL);

    for (i = 0; i < di->nr_tracks; i++)
        write_exact(d->fd, di->track[i].dat, 11*512);
}

static int adf_write_raw(
    struct disk *d, unsigned int tracknr, enum track_type type,
    struct stream *s)
{
    struct disk_info *di = d->di;
    struct track_info *ti = &di->track[tracknr];

    if (type != TRKTYP_amigados)
        errx(1, "Only AmigaDOS tracks can be written to ADF files");

    dsk_write_raw(d, tracknr, type, s);

    if (ti->type != TRKTYP_amigados) {
        memfree(ti->dat);
        ti->dat = NULL;
    }

    if (ti->dat == NULL)
        adf_init_track(ti);

    return 0;
}

struct container container_adf = {
    .init = adf_init,
    .open = adf_open,
    .close = adf_close,
    .write_raw = adf_write_raw
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
