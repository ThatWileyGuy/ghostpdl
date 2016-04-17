/* Copyright (C) 2001-2012 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134, San Rafael,
   CA  94903, U.S.A., +1(415)492-9861, for further information.
*/

#include <assert.h>

#include "gdevprn.h"

/* Driver for Epson LQ510 */
static dev_proc_print_page(lq510_print_page);
const gx_device_printer gs_lq510_device =
prn_device(prn_bg_procs, "lq510",	/* The print_page proc is compatible with allowing bg printing */
    DEFAULT_WIDTH_10THS, DEFAULT_HEIGHT_10THS,
    360, 360,
    //              0, 0, 0.5, 0,	/* margins */
    0.25, 0.02, 0.25, 0.4,	/* margins */
    1, lq510_print_page);

/* ------ Internal routines ------ */

/* Forward references */
static void dot24_output_run(byte *, int, int, FILE *);
static void dot24_filter_bitmap(byte *data, byte *out, int count, int pass);
static void dot24_print_line(byte *out_temp, byte *out, byte *out_end, int x_high, int y_high, int xres, int bytes_per_pos, FILE *prn_stream);
static void dot24_print_line_backwards(byte*out_temp, byte *out, byte *out_end, int x_high, int y_high, int xres, int bytes_per_pos, FILE *prn_stream);
static void dot24_print_block(byte *out_temp, int pos, byte *blk_start, byte *blk_end, int x_high, int bytes_per_pos, FILE *prn_stream);

/* Send the page to the printer. */
static int
dot24_print_page(gx_device_printer *pdev, FILE *prn_stream, char *init_string, int init_len)
{
    int xres = (int)pdev->x_pixels_per_inch;
    int yres = (int)pdev->y_pixels_per_inch;
    int x_high = (xres == 360);
    int y_high = (yres == 360);
    int bits_per_column = (y_high ? 48 : 24);
    uint line_size = gdev_prn_raster(pdev);
    uint in_size = line_size * bits_per_column;
    byte *in = (byte *)gs_malloc(pdev->memory, in_size, 1, "dot24_print_page (in)");
    uint out_size = in_size;
    byte *out = (byte *)gs_malloc(pdev->memory, out_size, 1, "dot24_print_page (out)");
    byte *out_temp = (byte *)gs_malloc(pdev->memory, out_size, 1, "dot24_print_page (out_temp)");
    int dots_per_pos = xres / 60;
    int bytes_per_pos = dots_per_pos * 3;
    int skip = 0, lnum = 0;
    bool forward = true;

    /* Check allocations */
    if (in == 0 || out == 0)
    {
        if (out_temp)
            gs_free(pdev->memory, (char *)out_temp, out_size, 1, "dot24_print_page (out_temp)");
        if (out)
            gs_free(pdev->memory, (char *)out, out_size, 1, "dot24_print_page (out)");
        if (in)
            gs_free(pdev->memory, (char *)in, in_size, 1, "dot24_print_page (in)");
        return_error(gs_error_VMerror);
    }

    /* Initialize the printer and reset the margins. */
    fwrite(init_string, init_len - 1, sizeof(char), prn_stream);
    fputc((int)(pdev->width / pdev->x_pixels_per_inch * 10) + 2,
        prn_stream);

    /* Print lines of graphics */
    while (lnum < pdev->height)
    {
        byte *in_end;
        byte *out_end;
        byte *inp;
        int lcnt;

        memset(in, 0, in_size);

        /* Copy 1 scan line and test for all zero. */
        gdev_prn_copy_scan_lines(pdev, lnum, in, line_size);
        if (in[0] == 0
            && !memcmp((char *)in, (char *)in + 1, line_size - 1))
        {
            lnum += y_high ? 2 : 1;
            skip += y_high ? 2 : 1;
            continue;
        }

        /* Vertical tab to the appropriate position. */
        while ((skip >> 1) > 255)
        {
            fputs("\x1bJ\xff", prn_stream);
            skip -= 255 * 2;
        }

        if (skip)
        {
            if (skip >> 1)
                fprintf(prn_stream, "\033J%c", skip >> 1);
            if (skip & 1)
                fputc('\n', prn_stream);
        }

        /* Copy the rest of the scan lines. */
        if (y_high)
        {
            inp = in + line_size;
            for (lcnt = 1; lcnt < 24; lcnt++, inp += line_size)
                if (!gdev_prn_copy_scan_lines(pdev, lnum + lcnt * 2, inp,
                    line_size))
                {
                    memset(inp, 0, (24 - lcnt) * line_size);
                    break;
                }
            inp = in + line_size * 24;
            for (lcnt = 0; lcnt < 24; lcnt++, inp += line_size)
                if (!gdev_prn_copy_scan_lines(pdev, lnum + lcnt * 2 + 1, inp,
                    line_size))
                {
                    memset(inp, 0, (24 - lcnt) * line_size);
                    break;
                }

            assert(inp <= in + in_size);
        }
        else
        {
            lcnt = 1 + gdev_prn_copy_scan_lines(pdev, lnum + 1, in + line_size,
                in_size - line_size);
            if (lcnt < 24)
                /* Pad with lines of zeros. */
                memset(in + lcnt * line_size, 0, in_size - lcnt * line_size);
        }

        out_end = out;
        inp = in;
        in_end = in + line_size;

        for (; inp < in_end; inp++, out_end += 24)
        {
            memflip8x8(inp, line_size, out_end, 3);
            memflip8x8(inp + line_size * 8, line_size, out_end + 1, 3);
            memflip8x8(inp + line_size * 16, line_size, out_end + 2, 3);
        }

        assert(out_end <= out + out_size);

        if (forward)
        {
            dot24_print_line(out_temp, out, out_end, x_high, y_high, xres, bytes_per_pos, prn_stream);
        }
        else
        {
            dot24_print_line_backwards(out_temp, out, out_end, x_high, y_high, xres, bytes_per_pos, prn_stream);
        }

        forward = !forward;

        skip = 48;
        lnum += bits_per_column;
    }

    /* Eject the page and reinitialize the printer */
    fputs("\f\033@", prn_stream);
    fflush(prn_stream);

    gs_free(pdev->memory, (char *)out_temp, out_size, 1, "dot24_print_page (out_temp)");
    gs_free(pdev->memory, (char *)out, out_size, 1, "dot24_print_page (out)");
    gs_free(pdev->memory, (char *)in, in_size, 1, "dot24_print_page (in)");

    return 0;
}

void dot24_print_line(byte *out_temp, byte *in_start, byte *in_end, int x_high, int y_high, int xres, int bytes_per_pos, FILE *prn_stream)
{
    const byte *orig_in = in_start;
    byte *blk_start;
    byte *blk_end;

    while (in_end - 3 >= in_start && in_end[-1] == 0
        && in_end[-2] == 0 && in_end[-3] == 0)
        in_end -= 3;

    for (; in_start < in_end;)
    {
        /* Skip to the first position that isn't zero */
        for (blk_start = in_start; blk_start < in_end; blk_start += bytes_per_pos)
        {
            bool zero = true;
            for (int i = 0; i < bytes_per_pos && blk_start + i < in_end; i++)
            {
                if (blk_start[i] != 0)
                {
                    zero = false;
                    break;
                }
            }
            if (!zero)
            {
                break;
            }
        }

        if (blk_start >= in_end)
        {
            // done!
            break;
        }

        /* Seek until we find a zero gap that's at least (xres * 3) / 2 (0.5 inch) */
        for (blk_end = blk_start + bytes_per_pos; blk_end < in_end; blk_end += bytes_per_pos)
        {
            bool zero = true;
            for (int i = 0; i < (xres * 3) / 2 && blk_end + i < in_end; i++)
            {
                if (blk_end[i] != 0)
                {
                    zero = false;
                    break;
                }
            }
            if (zero)
            {
                break;
            }
        }

        if (blk_end > in_end)
        {
            blk_end = in_end;
        }

        dot24_print_block(out_temp, (blk_start - orig_in) / bytes_per_pos, blk_start, blk_end, x_high, bytes_per_pos, prn_stream);

        in_start = blk_end;
    }
}

void dot24_print_line_backwards(byte * out_temp, byte *in_start, byte *in_end, int x_high, int y_high, int xres, int bytes_per_pos, FILE *prn_stream)
{
    const byte *orig_in = in_start;
    byte *blk_start;
    byte *blk_end;

    while (in_end - 3 >= in_start && in_end[-1] == 0
        && in_end[-2] == 0 && in_end[-3] == 0)
        in_end -= 3;

    for (; in_start < in_end;)
    {
        /* Skip to the last position that isn't zero */
        for (blk_end = in_end; blk_end > in_start; blk_end -= bytes_per_pos)
        {
            bool zero = true;
            for (int i = -1; i >= -bytes_per_pos && blk_end + i >= in_start; i--)
            {
                if (blk_end[i] != 0)
                {
                    zero = false;
                    break;
                }
            }
            if (!zero)
            {
                break;
            }

            // some trickery because in_end isn't guaranteed to fall in a multiple of bytes_per_pos
            if ((blk_end - orig_in) % bytes_per_pos != 0)
            {
                blk_end += bytes_per_pos - (blk_end - orig_in) % bytes_per_pos;
            }

            assert((blk_end - orig_in) % bytes_per_pos == 0);
        }

        if (blk_end <= in_start)
        {
            // done!
            break;
        }

        blk_start = blk_end - (blk_end - orig_in) % bytes_per_pos;

        if (blk_start == blk_end)
        {
            blk_start -= bytes_per_pos;
        }

        /* Seek until we find a zero gap that's at least (xres * 3) / 2 (0.5 inch) */
        for (; blk_start >= in_start; blk_start -= bytes_per_pos)
        {
            bool zero = true;
            for (int i = -1; i >= -((xres * 3) / 2) && blk_start + i >= in_start; i--)
            {
                if (blk_start[i] != 0)
                {
                    zero = false;
                    break;
                }
            }
            if (zero)
            {
                break;
            }
        }

        if (blk_start < in_start)
        {
            blk_start = in_start;
        }

        dot24_print_block(out_temp, (blk_start - orig_in) / bytes_per_pos, blk_start, blk_end, x_high, bytes_per_pos, prn_stream);

        in_end = blk_start;
    }
}

void dot24_print_block(byte *out_temp, int pos, byte *blk_start, byte *blk_end, int x_high, int bytes_per_pos, FILE *prn_stream)
{
    int passes = x_high ? 2 : 1;
    const byte *orig_blk_start = blk_start;
    byte *seg_start;
    byte *seg_end;
    int seg_pos;

    for (int pass = 0; pass < passes; pass++)
    {
        blk_start = orig_blk_start;

        for (; blk_start < blk_end;)
        {
            for (seg_start = blk_start; seg_start < blk_end; seg_start += bytes_per_pos)
            {
                bool zero = true;
                for (int i = 0; i < bytes_per_pos && seg_start + i < blk_end; i++)
                {
                    if (seg_start[i] != 0)
                    {
                        zero = false;
                        break;
                    }
                }
                if (!zero)
                {
                    break;
                }
            }

            if (seg_start >= blk_end)
            {
                // done!
                break;
            }

            for (seg_end = seg_start; seg_end < blk_end; seg_end += bytes_per_pos)
            {
                bool zero = true;
                for (int i = 0; i < bytes_per_pos && seg_end + i < blk_end; i++)
                {
                    if (seg_end[i] != 0)
                    {
                        zero = false;
                        break;
                    }
                }
                if (zero)
                {
                    break;
                }
            }

            if (seg_end >= blk_end)
            {
                seg_end = blk_end;
            }

            assert(seg_start != seg_end);

            if (x_high)
            {
                dot24_filter_bitmap(seg_start, out_temp, seg_end - seg_start, pass);
            }
            else
            {
                memcpy(out_temp, seg_start, seg_end - seg_start);
            }

            // go to the start of the segment
            seg_pos = pos + (seg_start - orig_blk_start) / bytes_per_pos;
            fprintf(prn_stream, "\033$%c%c", seg_pos % 256, seg_pos / 256);

            // print
            dot24_output_run(out_temp, seg_end - seg_start, x_high, prn_stream);

            blk_start = seg_end;
        }

        fputc('\r', prn_stream);
    }
}

/* Output a single graphics command. */
static void
dot24_output_run(byte *data, int count, int x_high, FILE *prn_stream)
{
    int xcount;

    while (count > 3 && data[count - 1] == 0 && data[count - 2] == 0 && data[count - 3] == 0)
    {
        count -= 3;
    }

    xcount = count / 3;

    if (count == 0)
    {
        return;
    }

    fputc(033, prn_stream);
    fputc('*', prn_stream);
    fputc((x_high ? 40 : 39), prn_stream);
    fputc(xcount & 0xff, prn_stream);
    fputc(xcount >> 8, prn_stream);
    fwrite(data, 1, count, prn_stream);
}

static void
dot24_filter_bitmap(byte *data, byte *out, int count, int pass)
{
    int i;
    byte mask = 0xAA;

    if (pass)
        mask = ~mask;

    for (i = 0; i < count; i += 3)
    {
        out[i + 0] = data[i + 0] & mask;
        out[i + 1] = data[i + 1] & mask;
        out[i + 2] = data[i + 2] & mask;

        mask = ~mask;
    }
}

static int
lq510_print_page(gx_device_printer *pdev, FILE *prn_stream)
{
    char lq510_init_string[] = "\033@\033P\033l\000\r\033\053\001\033U\000\033Q";

    return dot24_print_page(pdev, prn_stream, lq510_init_string, sizeof(lq510_init_string));
}
