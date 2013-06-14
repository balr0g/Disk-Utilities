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
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x08, 0x00, 0x00, 0x00, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
    0x00, 0x00, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x00, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x1c, 0x1d, 0x1e,
    0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x20, 0x21, 0x00, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
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

int16_t apple_II_get_nybble(struct stream *s, unsigned int max_scan) // find and return a byte with the high bit set
{
    assert(max_scan >= 1);
    if (stream_next_bits(s,8) == -1)
        return -1; // end of stream!
    while((s->word&0x80)==0) {
        if ((--max_scan<=0)||(stream_next_bit(s)==-1))
            return -1; // timeout or end of stream
    }
    return (int16_t)(s->word&0xFF);
}

int apple_II_scan_mark(struct stream *s, uint32_t mark, unsigned int max_scan, unsigned int max_bits_between_nybbles)
{
    int idx_off = -1;
    int prev_off = 0;
    int a;
    int b;
    do {
        prev_off = s->index_offset;
        /*if((b=apple_II_get_nybble(s, max_scan)) == ((mark>>24)&0xFF)) {
            if(b==-1) {
                return -1;
            }
         //   printf("%02x", ((mark>>24)&0xFF));
       */
       a=apple_II_get_nybble(s, max_scan);
       if(a==-1)
        return -1;
      //  printf("\nT: %02x\n", (mark>>24)&0xFF);
      
   //     if(a==0xFF) {
            if ((b=apple_II_get_nybble(s, max_bits_between_nybbles)) == ((mark>>16)&0xFF)) {
                if(b==-1) {
                    return -1;
                }
                printf(" %x%02x", a,  ((mark>>16)&0xFF));
                if ((b=apple_II_get_nybble(s, max_bits_between_nybbles)) == ((mark>>8)&0xFF)) {
                    if(b==-1) {
                        return -1;
                    }
                    printf("%02x", ((mark>>8)&0xFF));
                    if ((b=apple_II_get_nybble(s, max_bits_between_nybbles)) == (mark&0xFF)) {
                        if(b==-1) {
                            return -1;
                        }
                        printf("%02x AM\n", ((mark)&0xFF));
                        return s->index_offset;
                    } else {
                        printf("%02x NOTAM\n", b);
                    }
                }
      //     } 
        }
        else {
            if(b == -1)
                return -1;
//            printf("%02x ", b);
//            printf("%d ", s->index_offset);// - prev_off);
            max_scan -= (s->index_offset - prev_off);
        }
    } while(max_scan > 0);
    return idx_off;
}

int apple_II_scan_address_field(struct stream *s, uint32_t addrmark, struct apple_II_address_field *address_field)
{
    int idx_off = apple_II_scan_mark(s, addrmark,  ~0u, 12);
    if(idx_off < 0)
        goto fail;
    
    address_field->address_mark = addrmark; // we know this is correct, otherwise we'd have failed
    /* volume, track */
    
    if (stream_next_bits(s, 32) == -1)
        goto fail;
    address_field->volume = apple_II_gcr4_decode(s->word >> 24, s->word >> 16);
    address_field->track = apple_II_gcr4_decode(s->word >> 8, s->word);
    
    /* sector, checksum */
    if (stream_next_bits(s, 32) == -1)
        goto fail;
    address_field->sector = apple_II_gcr4_decode(s->word >> 24, s->word >> 16);
    address_field->checksum = apple_II_gcr4_decode(s->word >> 8, s->word);
    if (stream_next_bits(s, 24) == -1)
        goto fail;
    /* postamble */
    address_field->postamble = s->word & 0xFFFFFF;
    
    return idx_off;
    
fail:
    return -1;
}

static void *apple_II_16sector_write_raw(
struct disk *d, unsigned int tracknr, struct stream *s)
{
    struct track_info *ti = &d->di->track[tracknr];
    struct apple_II_extra_data *extra_data = handlers[ti->type]->extra_data;
    //char *block = memalloc(ti->len + 1);
    //unsigned int nr_valid_blocks = 0;
    
    
    // loop until we find all sectors
    while ((stream_next_bit(s) != -1) ){ //&&
    //(nr_valid_blocks != ti->nr_sectors)) {
    
        int idx_off;
        uint8_t dat[extra_data->data_raw_length];
        uint8_t buf[ti->bytes_per_sector];
        struct apple_II_address_field addrfld;
        
        if((idx_off = apple_II_scan_address_field(s, extra_data->address_mark, &addrfld)) < 0) {
      //      trk_warn(ti, tracknr, "No AM");
            continue;
        }
        
        if((addrfld.address_mark != extra_data->address_mark)) {
            // SANITY CHECK. THIS SHOULD NEVER HAPPEN SO BAIL OUT.
            errx(1, "Invalid address mark. This should be impossible!");
        }
        
        uint8_t cksum = (addrfld.sector ^ addrfld.track ^ addrfld.volume);
        if((addrfld.sector >= ti->nr_sectors) ||
           (addrfld.track != tracknr/2) ||
           (addrfld.postamble != extra_data->postamble)) {
            trk_warn(ti, tracknr, "Unexpected address mark sec=%02x track=%02x postamble=%06x", addrfld.sector, addrfld.track, addrfld.postamble);
        } else if(cksum != addrfld.checksum) {
            trk_warn(ti, tracknr, "Incorrect checksum: expected=%02x actual=%02x", cksum, addrfld.checksum);
        } else {
        trk_warn(ti, tracknr, "AM OK");
        }
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
