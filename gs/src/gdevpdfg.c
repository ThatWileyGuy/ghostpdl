/* Copyright (C) 1999, 2000, 2001 Aladdin Enterprises.  All rights reserved.
  
  This software is provided AS-IS with no warranty, either express or
  implied.
  
  This software is distributed under license and may not be copied,
  modified or distributed except as expressly authorized under the terms
  of the license contained in the file LICENSE in this distribution.
  
  For more information about licensing, please refer to
  http://www.ghostscript.com/licensing/. For information on
  commercial licensing, go to http://www.artifex.com/licensing/ or
  contact Artifex Software, Inc., 101 Lucas Valley Road #110,
  San Rafael, CA  94903, U.S.A., +1(415)492-9861.
*/

/* $Id$ */
/* Graphics state management for pdfwrite driver */
#include "math_.h"
#include "string_.h"
#include "memory_.h"
#include "gx.h"
#include "gserrors.h"
#include "gsfunc0.h"
#include "gsstate.h"
#include "gxbitmap.h"		/* for gxhttile.h in gzht.h */
#include "gxdht.h"
#include "gxfarith.h"		/* for gs_sin/cos_degrees */
#include "gxfmap.h"
#include "gxht.h"
#include "gxistate.h"
#include "gzht.h"
#include "gdevpdfx.h"
#include "gdevpdfg.h"
#include "gdevpdfo.h"
#include "szlibx.h"

/* ---------------- Miscellaneous ---------------- */

/* Reset the graphics state parameters to initial values. */
void
pdf_reset_graphics(gx_device_pdf * pdev)
{
    gx_color_index color = 0; /* black on DeviceGray and DeviceRGB */
    gx_device_color temp_color;

    if(pdev->color_info.num_components == 4) {
        gx_color_value cv[4];
        cv[0] = cv[1] = cv[2] = frac2cv(frac_0);
        cv[3] = frac2cv(frac_1);
        color = dev_proc(pdev, map_cmyk_color)((gx_device *)pdev, cv);
    }
    color_set_pure(&temp_color, color);
    memset(&pdev->saved_fill_color, 0, sizeof(pdev->saved_fill_color));
    memset(&pdev->saved_stroke_color, 0, sizeof(pdev->saved_stroke_color));
    gx_saved_color_update(&pdev->saved_fill_color, &temp_color);
    gx_saved_color_update(&pdev->saved_stroke_color, &temp_color);
    pdev->state.flatness = -1;
    {
	static const gx_line_params lp_initial = {
	    gx_line_params_initial
	};

	pdev->state.line_params = lp_initial;
    }
    pdev->fill_overprint = false;
    pdev->stroke_overprint = false;
    pdf_reset_text(pdev);
}

/* Set the fill or stroke color. */
private int
pdf_reset_color(gx_device_pdf * pdev, const gx_drawing_color *pdc,
		gx_device_color_saved * psc,
		const psdf_set_color_commands_t *ppscc)
{
    int code;
    gx_device_color_saved temp = *psc;

    /* Since pdfwrite never applies halftones and patterns, but monitors
     * halftone/pattern IDs separately, we don't need to compare
     * halftone/pattern bodies here.
     */
    if (!gx_saved_color_update(&temp, pdc))
	return 0;
    /*
     * In principle, we can set colors in either stream or text
     * context.  However, since we currently enclose all text
     * strings inside a gsave/grestore, this causes us to lose
     * track of the color when we leave text context.  Therefore,
     * we require stream context for setting colors.
     */
    code = pdf_open_page(pdev, PDF_IN_STREAM);
    if (code < 0)
	return code;
    code = pdf_put_drawing_color(pdev, pdc, ppscc);
    if (code < 0)
	return code;
    *psc = temp;
    return 0;
}
int
pdf_set_drawing_color(gx_device_pdf * pdev, const gx_drawing_color *pdc,
		      gx_device_color_saved * psc,
		      const psdf_set_color_commands_t *ppscc)
{
    return pdf_reset_color(pdev, pdc, psc, ppscc);
}
int
pdf_set_pure_color(gx_device_pdf * pdev, gx_color_index color,
		   gx_device_color_saved * psc,
		   const psdf_set_color_commands_t *ppscc)
{
    gx_drawing_color dcolor;

    color_set_pure(&dcolor, color);
    return pdf_reset_color(pdev, &dcolor, psc, ppscc);
}

/*
 * Convert a string into cos name.
 */
int
pdf_string_to_cos_name(gx_device_pdf *pdev, const byte *str, uint len, 
		       cos_value_t *pvalue)
{
    byte *chars = gs_alloc_string(pdev->pdf_memory, len + 1, 
                                  "pdf_string_to_cos_name");

    if (chars == 0)
	return_error(gs_error_VMerror);
    chars[0] = '/';
    memcpy(chars + 1, str, len);
    cos_string_value(pvalue, chars, len + 1);
    return 0;
}

/* Print a Boolean using a format. */
private void
pprintb1(stream *s, const char *format, bool b)
{
    pprints1(s, format, (b ? "true" : "false"));
}

/* ---------------- Graphics state updating ---------------- */

/* ------ Functions ------ */

/* Define the maximum size of a Function reference. */
#define MAX_FN_NAME_CHARS 9	/* /Default, /Identity */
#define MAX_FN_CHARS max(MAX_REF_CHARS + 4, MAX_FN_NAME_CHARS)

/*
 * Create and write a Function for a gx_transfer_map.  We use this for
 * transfer, BG, and UCR functions.  If check_identity is true, check for
 * an identity map.  Return 1 if the map is the identity map, otherwise
 * return 0.
 */
private data_source_proc_access(transfer_map_access); /* check prototype */
private int
transfer_map_access(const gs_data_source_t *psrc, ulong start, uint length,
		    byte *buf, const byte **ptr)
{
    const gx_transfer_map *map = (const gx_transfer_map *)psrc->data.str.data;
    uint i;

    if (ptr)
	*ptr = buf;
    for (i = 0; i < length; ++i)
	buf[i] = frac2byte(map->values[(uint)start + i]);
    return 0;
}
private int
transfer_map_access_signed(const gs_data_source_t *psrc,
			   ulong start, uint length,
			   byte *buf, const byte **ptr)
{
    const gx_transfer_map *map = (const gx_transfer_map *)psrc->data.str.data;
    uint i;

    *ptr = buf;
    for (i = 0; i < length; ++i)
	buf[i] = (byte)
	    ((frac2float(map->values[(uint)start + i]) + 1) * 127.5 + 0.5);
    return 0;
}
private int
pdf_write_transfer_map(gx_device_pdf *pdev, const gx_transfer_map *map,
		       int range0, bool check_identity,
		       const char *key, char *ids)
{
    gs_memory_t *mem = pdev->pdf_memory;
    gs_function_Sd_params_t params;
    static const float domain01[2] = { 0, 1 };
    static const int size = transfer_map_size;
    float range01[2];
    gs_function_t *pfn;
    long id;
    int code;

    if (map == 0) {
	*ids = 0;		/* no map */
	return 1;
    }
    if (check_identity) {
	/* Check for an identity map. */
	int i;

	if (map->proc == gs_identity_transfer)
	    i = transfer_map_size;
	else
 	    for (i = 0; i < transfer_map_size; ++i) {
 		fixed d = map->values[i] - bits2frac(i, log2_transfer_map_size);
 		if (any_abs(d) > fixed_epsilon) /* ignore small noise */
  		    break;
 	    }
	if (i == transfer_map_size) {
	    strcpy(ids, key);
	    strcat(ids, "/Identity");
	    return 1;
	}
    }
    params.m = 1;
    params.Domain = domain01;
    params.n = 1;
    range01[0] = (float)range0, range01[1] = 1.0;
    params.Range = range01;
    params.Order = 1;
    params.DataSource.access =
	(range0 < 0 ? transfer_map_access_signed : transfer_map_access);
    params.DataSource.data.str.data = (const byte *)map; /* bogus */
    /* DataSource */
    params.BitsPerSample = 8;	/* could be 16 */
    params.Encode = 0;
    params.Decode = 0;
    params.Size = &size;
    code = gs_function_Sd_init(&pfn, &params, mem);
    if (code < 0)
	return code;
    code = pdf_write_function(pdev, pfn, &id);
    gs_function_free(pfn, false, mem);
    if (code < 0)
	return code;
    sprintf(ids, "%s %ld 0 R", key, id);
    return 0;
}
private int
pdf_write_transfer(gx_device_pdf *pdev, const gx_transfer_map *map,
		   const char *key, char *ids)
{
    return pdf_write_transfer_map(pdev, map, 0, true, key, ids);
}

/* ------ Halftones ------ */

/*
 * Recognize the predefined PDF halftone functions.  Note that because the
 * corresponding PostScript functions use single-precision floats, the
 * functions used for testing must do the same in order to get identical
 * results.  Currently we only do this for a few of the functions.
 */
#define HT_FUNC(name, expr)\
  private floatp name(floatp xd, floatp yd) {\
    float x = (float)xd, y = (float)yd;\
    return d2f(expr);\
  }

/*
 * In most versions of gcc (e.g., 2.7.2.3, 2.95.4), return (float)xxx
 * doesn't actually do the coercion.  Force this here.  Note that if we
 * use 'inline', it doesn't work.
 */
private float
d2f(floatp d)
{
    float f = (float)d;
    return f;
}
private floatp
ht_Round(floatp xf, floatp yf)
{
    float x = (float)xf, y = (float)yf;
    float xabs = fabs(x), yabs = fabs(y);

    if (d2f(xabs + yabs) <= 1)
	return d2f(1 - d2f(d2f(x * x) + d2f(y * y)));
    xabs -= 1, yabs -= 1;
    return d2f(d2f(d2f(xabs * xabs) + d2f(yabs * yabs)) - 1);
}
private floatp
ht_Diamond(floatp xf, floatp yf)
{
    float x = (float)xf, y = (float)yf;
    float xabs = fabs(x), yabs = fabs(y);

    if (d2f(xabs + yabs) <= 0.75)
	return d2f(1 - d2f(d2f(x * x) + d2f(y * y)));
    if (d2f(xabs + yabs) <= d2f(1.23))
	return d2f(1 - d2f(d2f(d2f(0.85) * xabs) + yabs));
    xabs -= 1, yabs -= 1;
    return d2f(d2f(d2f(xabs * xabs) + d2f(yabs * yabs)) - 1);
}
private floatp
ht_Ellipse(floatp xf, floatp yf)
{
    float x = (float)xf, y = (float)yf;
    float xabs = fabs(x), yabs = fabs(y);
    /*
     * The PDF Reference, 2nd edition, incorrectly specifies the
     * computation w = 4 * |x| + 3 * |y| - 3.  The PostScript code in the
     * same book correctly implements w = 3 * |x| + 4 * |y| - 3.
     */
    float w = (float)(d2f(d2f(3 * xabs) + d2f(4 * yabs)) - 3);

    if (w < 0) {
	yabs /= 0.75;
	return d2f(1 - d2f((d2f(x * x) + d2f(yabs * yabs)) / 4));
    }
    if (w > 1) {
	xabs = 1 - xabs, yabs = d2f(1 - yabs) / 0.75;
	return d2f(d2f((d2f(xabs * xabs) + d2f(yabs * yabs)) / 4) - 1);
    }
    return d2f(0.5 - w);
}
/*
 * Most of these are recognized properly even without d2f.  We've only
 * added d2f where it apparently makes a difference.
 */
private float
d2fsin_d(double x) {
    return d2f(gs_sin_degrees(d2f(x)));
}
private float
d2fcos_d(double x) {
    return d2f(gs_cos_degrees(d2f(x)));
}
HT_FUNC(ht_EllipseA, 1 - (x * x + 0.9 * y * y))
HT_FUNC(ht_InvertedEllipseA, x * x + 0.9 * y * y - 1)
HT_FUNC(ht_EllipseB, 1 - sqrt(x * x + 0.625 * y * y))
HT_FUNC(ht_EllipseC, 1 - (0.9 * x * x + y * y))
HT_FUNC(ht_InvertedEllipseC, 0.9 * x * x + y * y - 1)
HT_FUNC(ht_Line, -fabs(y))
HT_FUNC(ht_LineX, x)
HT_FUNC(ht_LineY, y)
HT_FUNC(ht_Square, -max(fabs(x), fabs(y)))
HT_FUNC(ht_Cross, -min(fabs(x), fabs(y)))
HT_FUNC(ht_Rhomboid, (0.9 * fabs(x) + fabs(y)) / 2)
HT_FUNC(ht_DoubleDot, (d2fsin_d(x * 360) + d2fsin_d(y * 360)) / 2)
HT_FUNC(ht_InvertedDoubleDot, -(d2fsin_d(x * 360) + d2fsin_d(y * 360)) / 2)
HT_FUNC(ht_SimpleDot, 1 - d2f(d2f(x * x) + d2f(y * y)))
HT_FUNC(ht_InvertedSimpleDot, d2f(d2f(x * x) + d2f(y * y)) - 1)
HT_FUNC(ht_CosineDot, (d2fcos_d(x * 180) + d2fcos_d(y * 180)) / 2)
HT_FUNC(ht_Double, (d2fsin_d(x * 180) + d2fsin_d(y * 360)) / 2)
HT_FUNC(ht_InvertedDouble, -(d2fsin_d(x * 180) + d2fsin_d(y * 360)) / 2)
typedef struct ht_function_s {
    const char *fname;
    floatp (*proc)(floatp, floatp);
} ht_function_t;
private const ht_function_t ht_functions[] = {
    {"Round", ht_Round},
    {"Diamond", ht_Diamond},
    {"Ellipse", ht_Ellipse},
    {"EllipseA", ht_EllipseA},
    {"InvertedEllipseA", ht_InvertedEllipseA},
    {"EllipseB", ht_EllipseB},
    {"EllipseC", ht_EllipseC},
    {"InvertedEllipseC", ht_InvertedEllipseC},
    {"Line", ht_Line},
    {"LineX", ht_LineX},
    {"LineY", ht_LineY},
    {"Square", ht_Square},
    {"Cross", ht_Cross},
    {"Rhomboid", ht_Rhomboid},
    {"DoubleDot", ht_DoubleDot},
    {"InvertedDoubleDot", ht_InvertedDoubleDot},
    {"SimpleDot", ht_SimpleDot},
    {"InvertedSimpleDot", ht_InvertedSimpleDot},
    {"CosineDot", ht_CosineDot},
    {"Double", ht_Double},
    {"InvertedDouble", ht_InvertedDouble}
};

/* Write each kind of halftone. */
private int
pdf_write_spot_function(gx_device_pdf *pdev, const gx_ht_order *porder,
			long *pid)
{
    /****** DOESN'T HANDLE STRIP HALFTONES ******/
    int w = porder->width, h = porder->height;
    uint num_bits = porder->num_bits;
    gs_function_Sd_params_t params;
    static const float domain_spot[4] = { -1, 1, -1, 1 };
    static const float range_spot[4] = { -1, 1 };
    int size[2];
    gs_memory_t *mem = pdev->pdf_memory;
    /*
     * Even though the values are logically ushort, we must always store
     * them in big-endian order, so we access them as bytes.
     */
    byte *values;
    gs_function_t *pfn;
    uint i;
    int code = 0;

    params.m = 2;
    params.Domain = domain_spot;
    params.n = 1;
    params.Range = range_spot;
    params.Order = 0;		/* default */
    /*
     * We could use 8, 16, or 32 bits per sample to save space, but for
     * simplicity, we always use 16.
     */
    if (num_bits > 0x10000)
	return_error(gs_error_rangecheck);
    params.BitsPerSample = 16;
    params.Encode = 0;
    /*
     * The default Decode array maps the actual data values [1 .. w*h] to a
     * sub-interval of the Range, but that's OK, since all that matters is
     * the relative values, not the absolute values.
     */
    params.Decode = 0;
    size[0] = w;
    size[1] = h;
    params.Size = size;
    /* Create the (temporary) threshold array. */
    values = gs_alloc_byte_array(mem, num_bits, 2, "pdf_write_spot_function");
    if (values == 0)
	return_error(gs_error_VMerror);
    for (i = 0; i < num_bits; ++i) {
	gs_int_point pt;
	int value;

	if ((code = porder->procs->bit_index(porder, i, &pt)) < 0)
	    break;
	value = pt.y * w + pt.x;
	/* Always store the values in big-endian order. */
	values[i * 2] = (byte)(value >> 8);
	values[i * 2 + 1] = (byte)value;
    }
    data_source_init_bytes(&params.DataSource, (const byte *)values,
			   sizeof(*values) * num_bits);
    if (code >= 0 &&
	(code = gs_function_Sd_init(&pfn, &params, mem)) >= 0
	) {
	code = pdf_write_function(pdev, pfn, pid);
	gs_function_free(pfn, false, mem);
    }
    gs_free_object(mem, values, "pdf_write_spot_function");
    return code;
}
private int
pdf_write_spot_halftone(gx_device_pdf *pdev, const gs_spot_halftone *psht,
			const gx_ht_order *porder, long *pid)
{
    char trs[17 + MAX_FN_CHARS + 1];
    int code = pdf_write_transfer(pdev, porder->transfer, "/TransferFunction",
				  trs);
    long id, spot_id;
    stream *s;
    int i = countof(ht_functions);
    gs_memory_t *mem = pdev->pdf_memory;

    if (code < 0)
	return code;
    /*
     * See if we can recognize the spot function, by comparing its sampled
     * values against those in the order.
     */
    {	gs_screen_enum senum;
	gx_ht_order order;
	int code;

	order = *porder;
	code = gs_screen_order_alloc(&order, mem);
	if (code < 0)
	    goto notrec;
	for (i = 0; i < countof(ht_functions); ++i) {
	    floatp (*spot_proc)(floatp, floatp) = ht_functions[i].proc;
	    gs_point pt;

	    gs_screen_enum_init_memory(&senum, &order, NULL, &psht->screen,
				       mem);
	    while ((code = gs_screen_currentpoint(&senum, &pt)) == 0 &&
		   gs_screen_next(&senum, spot_proc(pt.x, pt.y)) >= 0)
		DO_NOTHING;
	    if (code < 0)
		continue;
	    /* Compare the bits and levels arrays. */
	    if (memcmp(order.levels, porder->levels,
		       order.num_levels * sizeof(*order.levels)))
		continue;
	    if (memcmp(order.bit_data, porder->bit_data,
		       order.num_bits * porder->procs->bit_data_elt_size))
		continue;
	    /* We have a match. */
	    break;
	}
	gx_ht_order_release(&order, mem, false);
    }
 notrec:
    if (i == countof(ht_functions)) {
	/* Create and write a Function for the spot function. */
	pdf_write_spot_function(pdev, porder, &spot_id);
    }	
    *pid = id = pdf_begin_separate(pdev);
    s = pdev->strm;
    /* Use the original, requested frequency and angle. */
    pprintg2(s, "<</Type/Halftone/HalftoneType 1/Frequency %g/Angle %g",
	     psht->screen.frequency, psht->screen.angle);
    if (i < countof(ht_functions))
	pprints1(s, "/SpotFunction/%s", ht_functions[i].fname);
    else
	pprintld1(s, "/SpotFunction %ld 0 R", spot_id);
    stream_puts(s, trs);
    if (psht->accurate_screens)
	stream_puts(s, "/AccurateScreens true");
    stream_puts(s, ">>\n");
    return pdf_end_separate(pdev);
}
private int
pdf_write_screen_halftone(gx_device_pdf *pdev, const gs_screen_halftone *psht,
			  const gx_ht_order *porder, long *pid)
{
    gs_spot_halftone spot;

    spot.screen = *psht;
    spot.accurate_screens = false;
    spot.transfer = 0;
    spot.transfer_closure.proc = 0;
    return pdf_write_spot_halftone(pdev, &spot, porder, pid);
}
private int
pdf_write_colorscreen_halftone(gx_device_pdf *pdev,
			       const gs_colorscreen_halftone *pcsht,
			       const gx_device_halftone *pdht, long *pid)
{
    int i;
    stream *s;
    long ht_ids[4];

    for (i = 0; i < 4; ++i) {
	int code = pdf_write_screen_halftone(pdev, &pcsht->screens.indexed[i],
					     &pdht->components[i].corder,
					     &ht_ids[i]);
	if (code < 0)
	    return code;
    }
    *pid = pdf_begin_separate(pdev);
    s = pdev->strm;
    pprintld1(s, "<</Type/Halftone/HalftoneType 5/Default %ld 0 R\n",
	      ht_ids[3]);
    pprintld2(s, "/Red %ld 0 R/Cyan %ld 0 R", ht_ids[0], ht_ids[0]);
    pprintld2(s, "/Green %ld 0 R/Magenta %ld 0 R", ht_ids[1], ht_ids[1]);
    pprintld2(s, "/Blue %ld 0 R/Yellow %ld 0 R", ht_ids[2], ht_ids[2]);
    pprintld2(s, "/Gray %ld 0 R/Black %ld 0 R", ht_ids[3], ht_ids[3]);
    return pdf_end_separate(pdev);
}
private int
pdf_write_threshold_halftone(gx_device_pdf *pdev,
			     const gs_threshold_halftone *ptht,
			     const gx_ht_order *porder, long *pid)
{
    char trs[17 + MAX_FN_CHARS + 1];
    int code = pdf_write_transfer(pdev, porder->transfer, "/TransferFunction",
				  trs);
    long id = pdf_begin_separate(pdev);
    stream *s = pdev->strm;
    pdf_data_writer_t writer;

    if (code < 0)
	return code;
    *pid = id;
    pprintd2(s, "<</Type/Halftone/HalftoneType 6/Width %d/Height %d",
	     ptht->width, ptht->height);
    stream_puts(s, trs);
    code = pdf_begin_data(pdev, &writer);
    if (code < 0)
	return code;
    stream_write(writer.binary.strm, ptht->thresholds.data, ptht->thresholds.size);
    return pdf_end_data(&writer);
}
private int
pdf_write_threshold2_halftone(gx_device_pdf *pdev,
			      const gs_threshold2_halftone *ptht,
			      const gx_ht_order *porder, long *pid)
{
    char trs[17 + MAX_FN_CHARS + 1];
    int code = pdf_write_transfer(pdev, porder->transfer, "/TransferFunction",
				  trs);
    long id = pdf_begin_separate(pdev);
    stream *s = pdev->strm;
    pdf_data_writer_t writer;

    if (code < 0)
	return code;
    *pid = id;
    pprintd2(s, "<</Type/Halftone/HalftoneType 16/Width %d/Height %d",
	     ptht->width, ptht->height);
    if (ptht->width2 && ptht->height2)
	pprintd2(s, "/Width2 %d/Height2 %d", ptht->width2, ptht->height2);
    stream_puts(s, trs);
    code = pdf_begin_data(pdev, &writer);
    if (code < 0)
	return code;
    s = writer.binary.strm;
    if (ptht->bytes_per_sample == 2)
	stream_write(s, ptht->thresholds.data, ptht->thresholds.size);
    else {
	/* Expand 1-byte to 2-byte samples. */
	int i;

	for (i = 0; i < ptht->thresholds.size; ++i) {
	    byte b = ptht->thresholds.data[i];

	    stream_putc(s, b);
	    stream_putc(s, b);
	}
    }
    return pdf_end_data(&writer);
}
private int 
pdf_get_halftone_component_index(const gs_multiple_halftone *pmht,
				 const gx_device_halftone *pdht,
				 int dht_index)
{
    int j;

    for (j = 0; j < pmht->num_comp; j++)
	if (pmht->components[j].comp_number == dht_index)
	    break;
    if (j == pmht->num_comp) { 
	/* Look for Default. */
	for (j = 0; j < pmht->num_comp; j++)
	    if (pmht->components[j].comp_number == GX_DEVICE_COLOR_MAX_COMPONENTS)
		break;
	if (j == pmht->num_comp)
	    return_error(gs_error_undefined);
    }
    return j;
}
private int
pdf_write_multiple_halftone(gx_device_pdf *pdev,
			    const gs_multiple_halftone *pmht,
			    const gx_device_halftone *pdht, long *pid)
{
    stream *s;
    int i, code, last_comp = 0;
    gs_memory_t *mem = pdev->pdf_memory;
    long *ids;
    bool done_Default = false;

    ids = (long *)gs_alloc_byte_array(mem, pmht->num_comp, sizeof(long),
				      "pdf_write_multiple_halftone");
    if (ids == 0)
	return_error(gs_error_VMerror);
    for (i = 0; i < pdht->num_comp; ++i) {
	const gs_halftone_component *phtc;
	const gx_ht_order *porder;

	code = pdf_get_halftone_component_index(pmht, pdht, i);
	if (code < 0)
	    return code;
	if (pmht->components[code].comp_number == GX_DEVICE_COLOR_MAX_COMPONENTS) {
	    if (done_Default)
		continue;
	    done_Default = true;
	}
	phtc = &pmht->components[code];
	porder = (pdht->components == 0 ? &pdht->order :
	               &pdht->components[i].corder);
	switch (phtc->type) {
	case ht_type_spot:
	    code = pdf_write_spot_halftone(pdev, &phtc->params.spot,
					   porder, &ids[i]);
	    break;
	case ht_type_threshold:
	    code = pdf_write_threshold_halftone(pdev, &phtc->params.threshold,
						porder, &ids[i]);
	    break;
	case ht_type_threshold2:
	    code = pdf_write_threshold2_halftone(pdev,
						 &phtc->params.threshold2,
						 porder, &ids[i]);
	    break;
	default:
	    code = gs_note_error(gs_error_rangecheck);
	}
	if (code < 0) {
	    gs_free_object(mem, ids, "pdf_write_multiple_halftone");
	    return code;
	}
    }
    *pid = pdf_begin_separate(pdev);
    s = pdev->strm;
    stream_puts(s, "<</Type/Halftone/HalftoneType 5\n");
    done_Default = false;
    for (i = 0; i < pdht->num_comp; ++i) {
	const gs_halftone_component *phtc;
	byte *str;
	uint len;
	cos_value_t value;

	code = pdf_get_halftone_component_index(pmht, pdht, i);
	if (code < 0)
	    return code;
	if (pmht->components[code].comp_number == GX_DEVICE_COLOR_MAX_COMPONENTS) {
	    if (done_Default)
		continue;
	    done_Default = true;
	}
	phtc = &pmht->components[code];
	if ((code = pmht->get_colorname_string(phtc->cname, &str, &len)) < 0 ||
            (code = pdf_string_to_cos_name(pdev, str, len, &value)) < 0)
	    return code;
	cos_value_write(&value, pdev);
	gs_free_string(mem, value.contents.chars.data,
		       value.contents.chars.size,
		       "pdf_write_multiple_halftone");
	pprintld1(s, " %ld 0 R\n", ids[i]);
	last_comp = i;
    }
    if (!done_Default) {
	/*
	 * BOGUS: Type 5 halftones must contain Default component.
	 * Perhaps we have no way to obtain it,
	 * because pdht contains ProcessColorModel components only.
	 * We copy the last component as Default one.
	 */
	pprintld1(s, " /Default %ld 0 R\n", ids[last_comp]);
    }
    stream_puts(s, ">>\n");
    gs_free_object(mem, ids, "pdf_write_multiple_halftone");
    return pdf_end_separate(pdev);
}

/*
 * Update the halftone.  This is a separate procedure only for
 * readability.
 */
private int
pdf_update_halftone(gx_device_pdf *pdev, const gs_imager_state *pis,
		    char *hts)
{
    const gs_halftone *pht = pis->halftone;
    const gx_device_halftone *pdht = pis->dev_ht;
    int code;
    long id;

    switch (pht->type) {
    case ht_type_screen:
	code = pdf_write_screen_halftone(pdev, &pht->params.screen,
					 &pdht->components[0].corder, &id);
	break;
    case ht_type_colorscreen:
	code = pdf_write_colorscreen_halftone(pdev, &pht->params.colorscreen,
					      pdht, &id);
	break;
    case ht_type_spot:
	code = pdf_write_spot_halftone(pdev, &pht->params.spot,
				       &pdht->components[0].corder, &id);
	break;
    case ht_type_threshold:
	code = pdf_write_threshold_halftone(pdev, &pht->params.threshold,
					    &pdht->components[0].corder, &id);
	break;
    case ht_type_threshold2:
	code = pdf_write_threshold2_halftone(pdev, &pht->params.threshold2,
					     &pdht->components[0].corder, &id);
	break;
    case ht_type_multiple:
    case ht_type_multiple_colorscreen:
	code = pdf_write_multiple_halftone(pdev, &pht->params.multiple,
					   pdht, &id);
	break;
    default:
	return_error(gs_error_rangecheck);
    }
    if (code < 0)
	return code;
    sprintf(hts, "/HT %ld 0 R", id);
    pdev->halftone_id = pis->dev_ht->id;
    return code;
}

/* ------ Graphics state updating ------ */

/* Open an ExtGState. */
private int
pdf_open_gstate(gx_device_pdf *pdev, pdf_resource_t **ppres)
{
    if (*ppres)
	return 0;
    /*
     * We write gs command only in stream context.
     * If we are clipped, and the clip path is about to change,
     * the old clipping must be undone before writing gs.
     */
    if (pdev->context != PDF_IN_STREAM) {
	/* We apparently use gs_error_interrupt as a request to change context. */
	return gs_error_interrupt;
    }
    return pdf_begin_resource(pdev, resourceExtGState, gs_no_id, ppres);
}

/* Finish writing an ExtGState. */
private int
pdf_end_gstate(gx_device_pdf *pdev, pdf_resource_t *pres)
{
    if (pres) {
	int code;

	stream_puts(pdev->strm, ">>\n");
	code = pdf_end_resource(pdev);
	pres->object->written = true; /* don't write at end of page */
	if (code < 0)
	    return code;
	code = pdf_open_page(pdev, PDF_IN_STREAM);
	if (code < 0)
	    return code;
	pprintld1(pdev->strm, "/R%ld gs\n", pdf_resource_id(pres));
    }
    return 0;
}

/*
 * Update the transfer functions(s).  This is a separate procedure only
 * for readability.
 */
private int
pdf_update_transfer(gx_device_pdf *pdev, const gs_imager_state *pis,
		    char *trs)
{
    int i, pi = -1;
    bool multiple = false, update = false;
    gs_id transfer_ids[4];
    int code = 0;
    const gx_transfer_map *tm[4];

    tm[0] = pis->set_transfer.red;
    tm[1] = pis->set_transfer.green;
    tm[2] = pis->set_transfer.blue;
    tm[3] = pis->set_transfer.gray;
    for (i = 0; i < 4; ++i) 
	if (tm[i] != NULL) {
	    transfer_ids[i] = tm[i]->id;
	    if (pdev->transfer_ids[i] != tm[i]->id)
		update = true;
	    if (pi != -1 && transfer_ids[i] != transfer_ids[pi])
		multiple = true;
	    pi = i;
	} else 
	    transfer_ids[i] = -1;
    if (update) {
	int mask;

	if (!multiple) {
	    code = pdf_write_transfer(pdev, tm[pi],
				      "/TR", trs);
	    if (code < 0)
		return code;
	    mask = code == 0;
	} else {
	    strcpy(trs, "/TR[");
	    mask = 0;
	    for (i = 0; i < 4; ++i) 
		if (tm[i] != NULL) {
		    code = pdf_write_transfer_map(pdev,
						  tm[i],
						  0, false, "", trs + strlen(trs));
		    if (code < 0)
			return code;
		    mask |= (code == 0) << i;
		}
	    strcat(trs, "]");
	}
	memcpy(pdev->transfer_ids, transfer_ids, sizeof(pdev->transfer_ids));
	pdev->transfer_not_identity = mask;
    }
    return code;
}

/*
 * Update the current alpha if necessary.  Note that because Ghostscript
 * stores separate opacity and shape alpha, a rangecheck will occur if
 * both are different from the current setting.
 */
private int
pdf_update_alpha(gx_device_pdf *pdev, const gs_imager_state *pis,
		 const char *ca_format, pdf_resource_t **ppres)
{
    bool ais;
    floatp alpha;
    int code;

    if (pdev->state.opacity.alpha != pis->opacity.alpha) {
	if (pdev->state.shape.alpha != pis->shape.alpha)
	    return_error(gs_error_rangecheck);
	ais = false;
	alpha = pdev->state.opacity.alpha = pis->opacity.alpha;
    } else if (pdev->state.shape.alpha != pis->shape.alpha) {
	ais = true;
	alpha = pdev->state.shape.alpha = pis->shape.alpha;
    } else
	return 0;
    code = pdf_open_gstate(pdev, ppres);
    if (code < 0)
	return code;
    pprintb1(pdev->strm, "/AIS %s", ais);
    pprintg1(pdev->strm, ca_format, alpha);
    return 0;
}

/*
 * Update the graphics subset common to all high-level drawing operations.
 */
private int
pdf_prepare_drawing(gx_device_pdf *pdev, const gs_imager_state *pis,
		    const char *ca_format, pdf_resource_t **ppres)
{
    int code = 0;

    if (pdev->CompatibilityLevel >= 1.4) {
	if (pdev->state.blend_mode != pis->blend_mode) {
	    static const char *const bm_names[] = { GS_BLEND_MODE_NAMES };

	    code = pdf_open_gstate(pdev, ppres);
	    if (code < 0)
		return code;
	    pprints1(pdev->strm, "/BM/%s", bm_names[pis->blend_mode]);
	    pdev->state.blend_mode = pis->blend_mode;
	}
	code = pdf_update_alpha(pdev, pis, ca_format, ppres);
	if (code < 0)
	    return code;
    } else {
	/*
	 * If the graphics state calls for any transparency functions,
	 * we can't represent them, so return a rangecheck.
	 */
	if (pis->opacity.alpha != 1 || pis->opacity.mask != 0 ||
	    pis->shape.alpha != 1 || pis->shape.mask != 0 ||
	    pis->transparency_stack != 0
	    )
	    return_error(gs_error_rangecheck);
    }
    /*
     * We originally thought the remaining items were only needed for
     * fill and stroke, but in fact they are needed for images as well.
     */
    /*
     * Update halftone, transfer function, black generation, undercolor
     * removal, halftone phase, overprint mode, smoothness, blend mode, text
     * knockout.
     */
    {
	gs_int_point phase, dev_phase;
	char hts[5 + MAX_FN_CHARS + 1],
	    trs[5 + MAX_FN_CHARS * 4 + 6 + 1],
	    bgs[5 + MAX_FN_CHARS + 1],
	    ucrs[6 + MAX_FN_CHARS + 1];

	hts[0] = trs[0] = bgs[0] = ucrs[0] = 0;
	if (pdev->params.PreserveHalftoneInfo &&
	    pdev->halftone_id != pis->dev_ht->id
	    ) {
	    code = pdf_update_halftone(pdev, pis, hts);
	    if (code < 0)
		return code;
	}
	if (pdev->params.TransferFunctionInfo == tfi_Preserve) {
	    code = pdf_update_transfer(pdev, pis, trs);
	    if (code < 0)
		return code;
	}
	if (pdev->params.UCRandBGInfo == ucrbg_Preserve) {
	    if (pdev->black_generation_id != pis->black_generation->id) {
		code = pdf_write_transfer_map(pdev, pis->black_generation,
					      0, false, "/BG", bgs);
		if (code < 0)
		    return code;
		pdev->black_generation_id = pis->black_generation->id;
	    }
	    if (pdev->undercolor_removal_id != pis->undercolor_removal->id) {
		code = pdf_write_transfer_map(pdev, pis->undercolor_removal,
					      -1, false, "/UCR", ucrs);
		if (code < 0)
		    return code;
		pdev->undercolor_removal_id = pis->undercolor_removal->id;
	    }
	}
	if (hts[0] || trs[0] || bgs[0] || ucrs[0]) {
	    code = pdf_open_gstate(pdev, ppres);
	    if (code < 0)
		return code;
	    stream_puts(pdev->strm, hts);
	    stream_puts(pdev->strm, trs);
	    stream_puts(pdev->strm, bgs);
	    stream_puts(pdev->strm, ucrs);
	}
	gs_currenthalftonephase((const gs_state *)pis, &phase);
	gs_currenthalftonephase((const gs_state *)&pdev->state, &dev_phase);
	if (dev_phase.x != phase.x || dev_phase.y != phase.y) {
	    code = pdf_open_gstate(pdev, ppres);
	    if (code < 0)
		return code;
	    pprintd2(pdev->strm, "/HTP[%d %d]", phase.x, phase.y);
	    gx_imager_setscreenphase(&pdev->state, phase.x, phase.y,
				     gs_color_select_all);
	}
    }
    if (pdev->CompatibilityLevel >= 1.3) {
	if (pdev->overprint_mode != pdev->params.OPM) {
	    code = pdf_open_gstate(pdev, ppres);
	    if (code < 0)
		return code;
	    pprintd1(pdev->strm, "/OPM %d", pdev->params.OPM);
	    pdev->overprint_mode = pdev->params.OPM;
	}
	if (pdev->state.smoothness != pis->smoothness) {
	    code = pdf_open_gstate(pdev, ppres);
	    if (code < 0)
		return code;
	    pprintg1(pdev->strm, "/SM %g", pis->smoothness);
	    pdev->state.smoothness = pis->smoothness;
	}
	if (pdev->CompatibilityLevel >= 1.4) {
	    if (pdev->state.text_knockout != pis->text_knockout) {
		code = pdf_open_gstate(pdev, ppres);
		if (code < 0)
		    return code;
		pprintb1(pdev->strm, "/TK %s", pis->text_knockout);
		pdev->state.text_knockout = pis->text_knockout;
	    }
	}
    }
    return code;
}

/* Update the graphics state for filling. */
private int
pdf_try_prepare_fill(gx_device_pdf *pdev, const gs_imager_state *pis)
{
    pdf_resource_t *pres = 0;
    int code = pdf_prepare_drawing(pdev, pis, "/ca %g", &pres);

    if (code < 0)
	return code;
    /* Update overprint. */
    if (pdev->params.PreserveOverprintSettings &&
	pdev->fill_overprint != pis->overprint
	) {
	code = pdf_open_gstate(pdev, &pres);
	if (code < 0)
	    return code;
	/* PDF 1.2 only has a single overprint setting. */
	if (pdev->CompatibilityLevel < 1.3) {
	    pprintb1(pdev->strm, "/OP %s", pis->overprint);
	    pdev->stroke_overprint = pis->overprint;
	} else {
	    pprintb1(pdev->strm, "/op %s", pis->overprint);
	}
	pdev->fill_overprint = pis->overprint;
    }
    return pdf_end_gstate(pdev, pres);
}
int
pdf_prepare_fill(gx_device_pdf *pdev, const gs_imager_state *pis)
{
    int code;

    if (pdev->context != PDF_IN_STREAM) {
	code = pdf_try_prepare_fill(pdev, pis);
	if (code != gs_error_interrupt) /* See pdf_open_gstate */
	    return code;
	code = pdf_open_contents(pdev, PDF_IN_STREAM);
	if (code < 0)
	    return code;
    }
    return pdf_try_prepare_fill(pdev, pis);
}

/* Update the graphics state for stroking. */
private int
pdf_try_prepare_stroke(gx_device_pdf *pdev, const gs_imager_state *pis)
{
    pdf_resource_t *pres = 0;
    int code = pdf_prepare_drawing(pdev, pis, "/CA %g", &pres);

    if (code < 0)
	return code;
    /* Update overprint, stroke adjustment. */
    if (pdev->params.PreserveOverprintSettings &&
	pdev->stroke_overprint != pis->overprint
	) {
	code = pdf_open_gstate(pdev, &pres);
	if (code < 0)
	    return code;
	pprintb1(pdev->strm, "/OP %s", pis->overprint);
	pdev->stroke_overprint = pis->overprint;
	/* PDF 1.2 only has a single overprint setting. */
	if (pdev->CompatibilityLevel < 1.3)
	    pdev->fill_overprint = pis->overprint;
    }
    if (pdev->state.stroke_adjust != pis->stroke_adjust) {
	code = pdf_open_gstate(pdev, &pres);
	if (code < 0)
	    return code;
	pprintb1(pdev->strm, "/SA %s", pis->stroke_adjust);
	pdev->state.stroke_adjust = pis->stroke_adjust;
    }
    return pdf_end_gstate(pdev, pres);
}
int
pdf_prepare_stroke(gx_device_pdf *pdev, const gs_imager_state *pis)
{
    int code;

    if (pdev->context != PDF_IN_STREAM) {
	code = pdf_try_prepare_stroke(pdev, pis);
	if (code != gs_error_interrupt) /* See pdf_open_gstate */
	    return code;
	code = pdf_open_contents(pdev, PDF_IN_STREAM);
	if (code < 0)
	    return code;
    }
    return pdf_try_prepare_stroke(pdev, pis);
}

/* Update the graphics state for an image other than an ImageType 1 mask. */
int
pdf_prepare_image(gx_device_pdf *pdev, const gs_imager_state *pis)
{
    /*
     * As it turns out, this requires updating the same parameters as for
     * filling.
     */
    return pdf_prepare_fill(pdev, pis);
}

/* Update the graphics state for an ImageType 1 mask. */
int
pdf_prepare_imagemask(gx_device_pdf *pdev, const gs_imager_state *pis,
		      const gx_drawing_color *pdcolor)
{
    int code = pdf_prepare_image(pdev, pis);

    if (code < 0)
	return code;
    return pdf_set_drawing_color(pdev, pdcolor, &pdev->saved_fill_color,
				 &psdf_set_fill_color_commands);
}

/* Save the viewer's graphic state. */
int
pdf_save_viewer_state(gx_device_pdf *pdev, stream *s)
{
    const int i = pdev->vgstack_depth;

    if (pdev->vgstack_depth >= count_of(pdev->vgstack))
	return_error(gs_error_unregistered); /* Must not happen. */
    pdev->vgstack[i].transfer_ids[0] = pdev->transfer_ids[0];
    pdev->vgstack[i].transfer_ids[1] = pdev->transfer_ids[1];
    pdev->vgstack[i].transfer_ids[2] = pdev->transfer_ids[2];
    pdev->vgstack[i].transfer_ids[3] = pdev->transfer_ids[3];
    pdev->vgstack[i].transfer_not_identity = pdev->transfer_not_identity;
    pdev->vgstack[i].opacity_alpha = pdev->state.opacity.alpha;
    pdev->vgstack[i].shape_alpha = pdev->state.shape.alpha;
    pdev->vgstack[i].blend_mode = pdev->state.blend_mode;
    pdev->vgstack[i].halftone_id = pdev->halftone_id;
    pdev->vgstack[i].black_generation_id = pdev->black_generation_id;
    pdev->vgstack[i].undercolor_removal_id = pdev->undercolor_removal_id;
    pdev->vgstack[i].overprint_mode = pdev->overprint_mode;
    pdev->vgstack[i].smoothness = pdev->state.smoothness;
    pdev->vgstack[i].text_knockout = pdev->state.text_knockout;
    pdev->vgstack[i].fill_overprint = pdev->fill_overprint;
    pdev->vgstack[i].stroke_overprint = pdev->stroke_overprint;
    pdev->vgstack[i].stroke_adjust = pdev->state.stroke_adjust;
    pdev->vgstack[i].saved_fill_color = pdev->saved_fill_color;
    pdev->vgstack[i].saved_stroke_color = pdev->saved_stroke_color;
    pdev->vgstack[i].line_params = pdev->state.line_params;
    pdev->vgstack[i].line_params.dash.pattern = 0; /* Use pdev->dash_pattern instead. */
    memcpy(pdev->vgstack[i].dash_pattern, pdev->dash_pattern, 
		sizeof(pdev->vgstack[i].dash_pattern));
    pdev->vgstack_depth++;
    stream_puts(s, "q\n");
    return 0;
}

/* Restore the viewer's graphic state. */
int
pdf_restore_viewer_state(gx_device_pdf *pdev, stream *s)
{   const int i = --pdev->vgstack_depth;

    if (i < 0)
	return_error(gs_error_unregistered); /* Must not happen. */
    stream_puts(s, "Q\n");
    pdev->transfer_ids[0] = pdev->vgstack[i].transfer_ids[0];
    pdev->transfer_ids[1] = pdev->vgstack[i].transfer_ids[1];
    pdev->transfer_ids[2] = pdev->vgstack[i].transfer_ids[2];
    pdev->transfer_ids[3] = pdev->vgstack[i].transfer_ids[3];
    pdev->transfer_not_identity = pdev->vgstack[i].transfer_not_identity;
    pdev->state.opacity.alpha = pdev->vgstack[i].opacity_alpha;
    pdev->state.shape.alpha = pdev->vgstack[i].shape_alpha;
    pdev->state.blend_mode = pdev->vgstack[i].blend_mode;
    pdev->halftone_id = pdev->vgstack[i].halftone_id;
    pdev->black_generation_id = pdev->vgstack[i].black_generation_id;
    pdev->undercolor_removal_id = pdev->vgstack[i].undercolor_removal_id;
    pdev->overprint_mode = pdev->vgstack[i].overprint_mode;
    pdev->state.smoothness = pdev->vgstack[i].smoothness;
    pdev->state.text_knockout = pdev->vgstack[i].text_knockout;
    pdev->fill_overprint = pdev->vgstack[i].fill_overprint;
    pdev->stroke_overprint = pdev->vgstack[i].stroke_overprint;
    pdev->state.stroke_adjust = pdev->vgstack[i].stroke_adjust;
    pdev->saved_fill_color = pdev->vgstack[i].saved_fill_color;
    pdev->saved_stroke_color = pdev->vgstack[i].saved_stroke_color;
    pdev->state.line_params = pdev->vgstack[i].line_params;
    memcpy(pdev->dash_pattern, pdev->vgstack[i].dash_pattern,
		sizeof(pdev->vgstack[i].dash_pattern));
    return 0;
}
