#include <stdint.h>
#include <stdlib.h>

#define __ALIGN_KERNEL(x, a) __ALIGN_KERNEL_MASK(x, (__typeof__(x))(a)-1)
#define __ALIGN_KERNEL_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define ALIGN_DOWN(x, a) __ALIGN_KERNEL((x) - ((a)-1), (a))

#define reverse_byte(n)                                                             \
    __extension__({                                                                 \
        __typeof__(n + 0) _n = (n);                                                 \
        _n = ((_n & 0xffffffff00000000) >> 32) | ((_n & 0x00000000ffffffff) << 32); \
        _n = ((_n & 0xffff0000ffff0000) >> 16) | ((_n & 0x0000ffff0000ffff) << 16); \
        _n = ((_n & 0xff00ff00ff00ff00) >> 8) | ((_n & 0x00ff00ff00ff00ff) << 8);   \
    })

void bitcpy(void *_dest,      /* Address of the buffer to write to */
            size_t _write,    /* Bit offset to start writing to */
            const void *_src, /* Address of the buffer to read from */
            size_t _read,     /* Bit offset to start reading from */
            size_t count)
{
    size_t read_lhs = _read & 7;
    size_t read_rhs = 8 - read_lhs;
    const uint8_t *source = (const uint8_t *)_src + (_read / 8);
    size_t write_lhs = _write & 7;
    size_t write_rhs = 8 - write_lhs;
    uint8_t *dest = (uint8_t *)_dest + (_write / 8);

    static const uint8_t read_mask[] = {
        0x00, /*    == 0    00000000b   */
        0x80, /*    == 1    10000000b   */
        0xC0, /*    == 2    11000000b   */
        0xE0, /*    == 3    11100000b   */
        0xF0, /*    == 4    11110000b   */
        0xF8, /*    == 5    11111000b   */
        0xFC, /*    == 6    11111100b   */
        0xFE, /*    == 7    11111110b   */
        0xFF  /*    == 8    11111111b   */
    };

    static const uint8_t write_mask[] = {
        0xFF, /*    == 0    11111111b   */
        0x7F, /*    == 1    01111111b   */
        0x3F, /*    == 2    00111111b   */
        0x1F, /*    == 3    00011111b   */
        0x0F, /*    == 4    00001111b   */
        0x07, /*    == 5    00000111b   */
        0x03, /*    == 6    00000011b   */
        0x01, /*    == 7    00000001b   */
        0x00  /*    == 8    00000000b   */
    };

    while (count > 0)
    {
        uint8_t data = *source++;
        size_t bitsize = (count > 8) ? 8 : count;
        if (read_lhs > 0)
        {
            data <<= read_lhs;
            if (bitsize > read_rhs)
                data |= (*source >> read_rhs);
        }

        if (bitsize < 8)
            data &= read_mask[bitsize];

        uint8_t original = *dest;
        uint8_t mask = read_mask[write_lhs];
        if (bitsize > write_rhs)
        {
            /* Cross multiple bytes */
            *dest++ = (original & mask) | (data >> write_lhs);
            original = *dest & write_mask[bitsize - write_rhs];
            *dest = original | (data << write_rhs);
        }
        else
        {
            // Since write_lhs + bitsize is never >= 8, no out-of-bound access.
            mask |= write_mask[write_lhs + bitsize];
            *dest++ = (original & mask) | (data >> write_lhs);
        }

        count -= bitsize;
    }
}

void bitcpy64(void *_dest,      /* Address of the buffer to write to */
              size_t _write,    /* Bit offset to start writing to */
              const void *_src, /* Address of the buffer to read from */
              size_t _read,     /* Bit offset to start reading from */
              size_t count)
{
    size_t read_lhs = _read & 63;
    size_t read_rhs = 64 - read_lhs;
    const uint64_t *source = (const uint64_t *)_src + (_read / 64);
    size_t write_lhs = _write & 63;
    size_t write_rhs = 64 - write_lhs;
    uint64_t *dest = (uint64_t *)_dest + (_write / 64);

    while (count > 0)
    {
        /* Downgrade 64-bit version to 8-bit version */
        if (count < 64)
        {
            size_t _read_lhs = ALIGN_DOWN(read_lhs, 8);
            size_t _write_lhs = ALIGN_DOWN(write_lhs, 8);
            bitcpy(((uint8_t *)dest) + _write_lhs / 8,
                   write_lhs - _write_lhs,
                   ((uint8_t *)source) + _read_lhs / 8,
                   read_lhs - _read_lhs,
                   count);
            return;
        }

        uint64_t data = reverse_byte(*source++);
        size_t bitsize = (count > 64) ? 64 : count;
        if (read_lhs > 0)
        {
            data <<= read_lhs;
            if (bitsize > read_rhs)
                data |= (reverse_byte(*source) >> read_rhs);
        }

        if (bitsize < 64)
            data &= (int64_t)(1UL << 63) >> (bitsize - 1);

        uint64_t original = reverse_byte(*dest);
        uint64_t mask = write_lhs ? ((int64_t)(1UL << 63) >> (write_lhs - 1)) : 0;
        if (bitsize > write_rhs)
        {
            /* Cross multiple bytes */
            *dest++ = reverse_byte((original & mask) | (data >> write_lhs));
            original = reverse_byte(*dest) & ~((int64_t)(1UL << 63) >> (bitsize - write_rhs - 1));
            *dest = reverse_byte(original | (data << write_rhs));
        }
        else
        {
            // Since write_lhs + bitsize is never >= 64, no out-of-bound access.
            mask |= ~((int64_t)(1UL << 63) >> (write_lhs + bitsize - 1));
            *dest++ = reverse_byte((original & mask) | (data >> write_lhs));
        }

        count -= bitsize;
    }
}
