/*
 * disk/appleii_16.c
 * 
 * 16-sector Apple II format.
 * 
 * Notes on IBM-compatible MFM data format:
 * ----------------------------------------
 * Supported by uPD765A, Intel 8272, and many other FDC chips, as used in
 * pretty much every home computer (except Amiga and C64!).
 * 
 * Useful references:
 *  "Beneath Apple DOS" by Don Worth and Pieter Lechner,
 *  "Understanding the Apple II" by Jim Sather
 * 
 * Index Address Mark (IAM):
 *      0xc2c2c2fc
 * ID Address Mark (IDAM):
 *      0xa1a1a1fe, <cyl>, <hd> <sec>, <sz>, <crc16_ccitt>
 * Data Address Mark (DAM):
 *      0xa1a1a1fb, <N bytes data>, <crc16_ccitt> [N = 128 << sz]
 * Deleted Data Address Mark (DDAM):
 *      As DAM, but identifier 0xfb -> 0xf8
 * 
 * NB. In above, 0xc2 and 0xa1 are sync marks which have one of their clock
 *     bits forced to zero. Hence 0xc2 -> 0x5224; 0xa1 -> 0x4489.
 * 
 * Written in 2013 by balr0g
 */

#include <libdisk/util.h>
#include "../private.h"

struct apple_II_extra_data {
    uint32_t address_mark;
    uint32_t data_mark;
    uint32_t data_raw_length;
    uint32_t postamble;
    int (*decode_bytes)(
    uint8_t *in, uint8_t *out, int size, int sec_size);
};

struct apple_II_address_field {
    uint32_t address_mark;
    uint8_t volume,
            track,
            sector,
            checksum;
    uint32_t postamble;
};

const uint8_t gcr6bw_tb[0x100] =
{
    // 0     1     2     3     4     5     6     7     8     9     a     b     c     d     e     f
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x03, 0x00, 0x04, 0x05, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x08, 0x00, 0x00, 0xFE, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
    0x00, 0x00, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x00, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x1c, 0x1d, 0x1e,
    0x00, 0x00, 0x00, 0x1f, 0x00, 0xFF, 0x20, 0x21, 0x00, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x2a, 0x2b, 0x00, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
    0x00, 0x00, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x00, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};


uint8_t apple_II_gcr4_decode(uint8_t e0, uint8_t e1)
{
    return ((e0 << 1) & 0xaa) | (e1 & 0x55);
}

int apple_II_16sector_decode_bytes(uint8_t *in, uint8_t *out, int size, int sec_size)
{
    // bail out if this isn't a 16-sector A2 sector
    if(size != 342 && sec_size != 256) {
        errx(1, "Invalid sector being decoded -- wrong sizes!");
    }

    return 0;
}

int16_t apple_II_get_nybble(struct stream *s, unsigned int max_scan)
{
    while((stream_next_bit(s) != -1) && --max_scan) {
        if((s->word & 0x80) == 0x80) { // valid nybble
            int16_t val = (s->word) & 0xFF;
            stream_next_bits(s,7);
            return val;
        }

    }
    return -1; // didn't find any!
}

/**
 @return 0 if found, -1 if no mark found before end of stream
**/
int apple_II_scan_mark(struct stream *s, uint32_t mark, unsigned int max_scan, unsigned int max_bits_between_nybbles)
{
    int16_t tempnybble; // needs to be signed since get_nybble can return -1 if the stream ran out
    uint32_t lastfour = 0; // last four nybbles read
    while (1) {
        tempnybble = apple_II_get_nybble(s, max_scan); // grab one nybble
        if (tempnybble == -1) return -1; // if end of stream, return that
        // otherwise stash the nybble we just got
        lastfour <<= 8;
        lastfour |= (tempnybble&0xFF);
        if ((lastfour&0x00FFFFFF)==(mark&0x00FFFFFF)) { // found mark
            printf("%08X ", lastfour);
            // describe what was found, for the viewers at home
            switch(lastfour&0x00FFFFFF) {
                case 0x00D5AA96:
                    printf("Address mark header (16sector)\n");
                    break;
                case 0x00D5AAAB:
                    printf("Address mark header (13sector)\n");
                    break;
                case 0x00D5AAAD:
                    printf("Data mark header\n");
                    break;
                default:
                    printf("Unknown mark\n");
                    break;
            }
        return 0;
        }
    }
}

int apple_II_scan_address_field(struct stream *s, uint32_t addrmark, struct apple_II_address_field *address_field)
{
    int mark_status = apple_II_scan_mark(s, addrmark, ~0u, 12);
    int i;
    int16_t tempnybble; // needs to be signed since get_nybble can return -1 if the stream ran out
    uint32_t lastfour; // last four nybbles read
    if(mark_status == -1) return -2; // bail out if we ran out of bits before even starting, and return -2 instead of -1
    address_field->address_mark = addrmark; // we know this is correct, otherwise we'd have failed
    /* volume, track */
    lastfour = 0;
    for (i = 0; i < 4; i++) {
        lastfour <<=8;
        tempnybble = apple_II_get_nybble(s, 12); // should number of allowed slack bits be 12 here? probably more like 0...
        if (tempnybble == -1) return -1; // bail out of we ran out of bits
        lastfour |= tempnybble&0xFF;
    }
    address_field->volume = apple_II_gcr4_decode((lastfour>>24)&0xFF, (lastfour>>16)&0xFF);
    address_field->track = apple_II_gcr4_decode((lastfour>>8)&0xFF, lastfour&0xFF);

    /* sector, checksum */
    lastfour = 0;
    for (i = 0; i < 4; i++) {
        lastfour <<=8;
        tempnybble = apple_II_get_nybble(s, 12); // should number of allowed slack bits be 12 here? probably more like 0...
        if (tempnybble == -1) return -1; // bail out of we ran out of bits
        lastfour |= tempnybble&0xFF;
    }
    address_field->sector = apple_II_gcr4_decode((lastfour>>24)&0xFF, (lastfour>>16)&0xFF);
    address_field->checksum = apple_II_gcr4_decode((lastfour>>8)&0xFF, lastfour&0xFF);

    /* postamble */
    lastfour = 0;
    for (i = 0; i < 3; i++) {
        lastfour <<=8;
        tempnybble = apple_II_get_nybble(s, 12); // should number of allowed slack bits be 12 here? probably more like 0...
        if (tempnybble == -1) return -1; // bail out of we ran out of bits
        lastfour |= tempnybble&0xFF;
    }
    address_field->postamble = s->word & 0xFFFFFF;

    return mark_status;
}

static void *apple_II_16sector_write_raw(
struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct apple_II_extra_data *extra_data = handlers[ti->type]->extra_data;
    int stream_is_over = 0;
    //char *block = memalloc(ti->len + 1);
    //unsigned int nr_valid_blocks = 0;

    // loop until we find all sectors
    while (!stream_is_over) { 
    //(nr_valid_blocks != ti->nr_sectors)) {

        int idx_off;
        uint8_t dat[extra_data->data_raw_length]; // ? what does this do
        uint8_t buf[ti->bytes_per_sector]; // ? what does this do
        struct apple_II_address_field addrfld; // ?

        if((idx_off = apple_II_scan_address_field(s, extra_data->address_mark, &addrfld)) < 0) {
            if(idx_off == -1) trk_warn(ti, tracknr, "No AM found");
            if(idx_off == -2) { stream_is_over = 1; break; }
            continue;
        }

        if((addrfld.address_mark != extra_data->address_mark)) {
            // SANITY CHECK. THIS SHOULD NEVER HAPPEN SO BAIL OUT.
            errx(1, "Invalid address mark. This should be impossible!");
        }

        uint8_t cksum = (addrfld.sector ^ addrfld.track ^ addrfld.volume);
        int am_status = 2; // 2 = good, 1 = warn, 0 = bad
        static const char *const status_labels[] = { "BAD", "WARN", "GOOD" };
        if(addrfld.sector >= ti->nr_sectors) { trk_warn(ti, tracknr, "Sector out of range: expected %02x <= found %02x", ti->nr_sectors, addrfld.sector); am_status = 1; }
        if(addrfld.track != tracknr/2) { trk_warn(ti, tracknr, "Unexpected Track value: expected %02x, found %02x", tracknr/2, addrfld.track); am_status = 1; }
        if(addrfld.postamble != extra_data->postamble) { trk_warn(ti, tracknr, "Unexpected postamble: expected %06x, found %06x", extra_data->postamble, addrfld.postamble); am_status = 1; }
        if(cksum != addrfld.checksum) { trk_warn(ti, tracknr, "Incorrect checksum: expected %02x, found %02x", cksum, addrfld.checksum); am_status = 0; }
        trk_warn(ti, tracknr, "AM %s", status_labels[am_status]);


        // find data mark
        if(apple_II_scan_mark(s, extra_data->data_mark, 20*8, 12) < 0) {
            trk_warn(ti, tracknr, "No data mark for sec=%02x", addrfld.sector);
            continue;
        }
        if(stream_next_bytes(s, dat, extra_data->data_raw_length) == -1) {
            trk_warn(ti, tracknr, "Could not read data for sec=%02x", addrfld.sector);
            continue;
        }
        trk_warn(ti, tracknr, "DM OK");
        /*
        if(stream_next_bits(s,1) == -1)) { 
            trk_warn(ti, tracknr, 
            continue;
        } 
        */
        extra_data->decode_bytes(dat, buf, extra_data->data_raw_length, ti->bytes_per_sector);

    }
    return NULL;
}

static void apple_II_16sector_read_raw(
struct disk *d, unsigned int tracknr, struct tbuf *tbuf)
{
}

/*
 *   Apple II 16-sector format
 */
struct track_handler apple_II_16sector_handler = {
    .density = trkden_single,
    .bytes_per_sector = 256,
    .nr_sectors = 16,
    .write_raw = apple_II_16sector_write_raw,
    .read_raw = apple_II_16sector_read_raw,
//    .write_sectors = apple_II_16sector_write_sectors,
//    .read_sectors = apple_II_16sector_read_sectors,
    .extra_data = & (struct apple_II_extra_data) {
        .address_mark = 0xFFD5AA96,
        .data_mark = 0xFFD5AAAD,
        .data_raw_length = 342,
        .postamble = 0xDEAAEB,
        .decode_bytes = apple_II_16sector_decode_bytes
    }
};

/*
struct track_handler apple_II_13sector_handler = {
    .density = trkden_double,
    .bytes_per_sector = 256,
    .nr_sectors = 13,
    .write_raw = apple_II_16sector_write_raw,
    .read_raw = apple_II_16sector_read_raw,
    .write_sectors = apple_II_16sector_write_sectors,
    .read_sectors = apple_II_16sector_read_sectors,
    .extra_data = & (struct ibm_extra_data) {
        .address_mark = 0xFFD5AAAB;
        .data_mark = 0xFFD5AAAD;
    }
};
*/


/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
