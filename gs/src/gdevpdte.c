/* Copyright (C) 2002 Aladdin Enterprises.  All rights reserved.
  
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
/* Encoding-based (Type 1/2/42) text processing for pdfwrite. */
#include "math_.h"
#include "memory_.h"
#include "gx.h"
#include "gserrors.h"
#include "gxfcmap.h"
#include "gxfcopy.h"
#include "gxfont.h"
#include "gxfont0.h"
#include "gxfont0c.h"
#include "gxpath.h"		/* for getting current point */
#include "gdevpsf.h"
#include "gdevpdfx.h"
#include "gdevpdfg.h"
#include "gdevpdtx.h"
#include "gdevpdtd.h"
#include "gdevpdtf.h"
#include "gdevpdts.h"
#include "gdevpdtt.h"

private int pdf_char_widths(gx_device_pdf *const pdev,
			    pdf_font_resource_t *pdfont, int ch,
			    gs_font_base *font,
			    pdf_glyph_widths_t *pwidths /* may be NULL */);
private int pdf_encode_string(gx_device_pdf *pdev, const pdf_text_enum_t *penum, 
			      const gs_string *pstr,
			      pdf_font_resource_t **ppdfont);
private int pdf_process_string(pdf_text_enum_t *penum, gs_string *pstr,
			       pdf_font_resource_t *pdfont,
			       const gs_matrix *pfmat,
			       pdf_text_process_state_t *ppts);

/*
 * Encode and process a string with a simple gs_font.
 */
int
pdf_encode_process_string(pdf_text_enum_t *penum, gs_string *pstr,
			  const gs_matrix *pfmat,
			  pdf_text_process_state_t *ppts)
{
    gx_device_pdf *const pdev = (gx_device_pdf *)penum->dev;
    pdf_font_resource_t *pdfont;
    gs_font_base *font;
    int code = 0;

    switch (penum->current_font->FontType) {
    case ft_TrueType:
    case ft_encrypted:
    case ft_encrypted2:
	break;
    default:
	return_error(gs_error_rangecheck);
    }
    font = (gs_font_base *)penum->current_font;

    code = pdf_encode_string(pdev, penum, pstr, &pdfont); /* Must not change penum. */
    if (code < 0)
	return code;
    return pdf_process_string(penum, pstr, pdfont, pfmat, ppts);
}

/*
 * Given a text string and a simple gs_font, return a font resource suitable
 * for the text string, possibly re-encoding the string.  This
 * may involve creating a font resource and/or adding glyphs and/or Encoding
 * entries to it.
 *
 * Sets *ppdfont.
 */
private int
pdf_encode_string(gx_device_pdf *pdev, const pdf_text_enum_t *penum,
		  const gs_string *pstr,
		  pdf_font_resource_t **ppdfont)
{
    gs_font *font = (gs_font *)penum->current_font;
    pdf_font_resource_t *pdfont = 0;
    gs_font_base *cfont;
    int code, i;

    /*
     * This crude version of the code simply uses pdf_obtain_font_resource,
     * and never re-encodes characters.
     */
    code = pdf_obtain_font_resource((const gs_text_enum_t *)penum, pstr, &pdfont);
    if (code < 0)
	return code;
    cfont = pdf_font_resource_font(pdfont);
    for (i = 0; i < pstr->size; ++i) {
	int ch = pstr->data[i];
	pdf_encoding_element_t *pet = &pdfont->u.simple.Encoding[ch];
	gs_glyph glyph =
	    font->procs.encode_char(font, ch, GLYPH_SPACE_NAME);
	gs_glyph copied_glyph;
	gs_const_string gnstr;

	if (glyph == GS_NO_GLYPH || glyph == pet->glyph)
	    continue;
	if (pet->glyph != GS_NO_GLYPH) { /* encoding conflict */
	    return_error(gs_error_rangecheck); 
	    /* Must not happen because pdf_obtain_font_resource
	     * checks for encoding compatibility. 
	     */
	}
	code = font->procs.glyph_name(font, glyph, &gnstr);
	if (code < 0)
	    return code;	/* can't get name of glyph */
	/* The standard 14 fonts don't have a FontDescriptor. */
	code = (pdfont->base_font != 0 ?
		pdf_base_font_copy_glyph(pdfont->base_font, glyph, (gs_font_base *)font) :
		pdf_font_used_glyph(pdfont->FontDescriptor, glyph, (gs_font_base *)font));
	if (code == gs_error_undefined)
	    continue;	/* notdef */
	if (code < 0)
	    return code;
	pet->glyph = glyph;
	pet->str = gnstr;
	/*
	 * We arbitrarily allow the first encoded character in a given
	 * position to determine the encoding associated with the copied
	 * font.
	 */
	copied_glyph = cfont->procs.encode_char((gs_font *)cfont, ch,
						GLYPH_SPACE_NAME);
	if (glyph != copied_glyph &&
	    gs_copied_font_add_encoding((gs_font *)cfont, ch, glyph) < 0
	    )
	    pet->is_difference = true;
	pdfont->used[ch >> 3] |= 0x80 >> (ch & 7);
    }
    *ppdfont = pdfont;
    return 0;
}

/*
 * Estimate text bbox.
 */
private int
process_text_estimate_bbox(pdf_text_enum_t *pte, gs_font_base *font,
			  const gs_const_string *pstr,
			  const gs_matrix *pfmat,
			  gs_rect *text_bbox, gs_point *pdpt)
{
    int i;
    int space_char =
	(pte->text.operation & TEXT_ADD_TO_SPACE_WIDTH ?
	 pte->text.space.s_char : -1);
    int WMode = font->WMode;
    int code = 0;
    gs_point total = {0, 0};
    gs_fixed_point origin;
    gs_matrix m;
    int xy_index = pte->xy_index;

    if (font->FontBBox.p.x == font->FontBBox.q.x ||
	font->FontBBox.p.y == font->FontBBox.q.y)
	return_error(gs_error_undefined);
    code = gx_path_current_point(pte->path, &origin);
    if (code < 0)
	return code;
    m = ctm_only(pte->pis);
    m.tx = fixed2float(origin.x);
    m.ty = fixed2float(origin.y);
    gs_matrix_multiply(pfmat, &m, &m);
    for (i = 0; i < pstr->size; ++i) {
	byte c = pstr->data[i];
	gs_rect bbox;
	gs_point wanted, tpt, p0, p1, p2, p3;
	gs_glyph glyph = font->procs.encode_char((gs_font *)font, c, 
					GLYPH_SPACE_NAME);
	gs_glyph_info_t info;
	int code = font->procs.glyph_info((gs_font *)font, glyph, NULL,
					    GLYPH_INFO_WIDTH0 << WMode,
					    &info);

	if (code < 0)
	    return code;
	gs_point_transform(font->FontBBox.p.x, font->FontBBox.p.y, &m, &p0);
	gs_point_transform(font->FontBBox.p.x, font->FontBBox.q.y, &m, &p1);
	gs_point_transform(font->FontBBox.q.x, font->FontBBox.p.y, &m, &p2);
	gs_point_transform(font->FontBBox.q.x, font->FontBBox.q.y, &m, &p3);
	bbox.p.x = min(min(p0.x, p1.x), min(p1.x, p2.x)) + total.x;
	bbox.p.y = min(min(p0.y, p1.y), min(p1.y, p2.y)) + total.y;
	bbox.q.x = max(max(p0.x, p1.x), max(p1.x, p2.x)) + total.x;
	bbox.q.y = max(max(p0.y, p1.y), max(p1.y, p2.y)) + total.y;
	if (i == 0)
	    *text_bbox = bbox;
	else
	    rect_merge(*text_bbox, bbox);
	if (pte->text.operation & TEXT_REPLACE_WIDTHS) {
	    gs_text_replaced_width(&pte->text, xy_index++, &tpt);
	    gs_distance_transform(tpt.x, tpt.y, &ctm_only(pte->pis), &wanted);
	} else {
	    gs_distance_transform(info.width[WMode].x,
				  info.width[WMode].y,
				  &m, &wanted);
	    if (pte->text.operation & TEXT_ADD_TO_ALL_WIDTHS) {
		gs_distance_transform(pte->text.delta_all.x,
				      pte->text.delta_all.y,
				      &ctm_only(pte->pis), &tpt);
		wanted.x += tpt.x;
		wanted.y += tpt.y;
	    }
	    if (pstr->data[i] == space_char) {
		gs_distance_transform(pte->text.delta_space.x,
				      pte->text.delta_space.y,
				      &ctm_only(pte->pis), &tpt);
		wanted.x += tpt.x;
		wanted.y += tpt.y;
	    }
	}
	total.x += wanted.x;
	total.y += wanted.y;
    }
    *pdpt = total;
    return 0;
}

/*
 * Internal procedure to process a string in a non-composite font.
 * Doesn't use or set pte->{data,size,index}; may use/set pte->xy_index;
 * may set penum->returned.total_width.  Sets ppts->values.
 *
 * Note that the caller is responsible for re-encoding the string, if
 * necessary; for adding Encoding entries in pdfont; and for copying any
 * necessary glyphs.  penum->current_font provides the gs_font for getting
 * glyph metrics, but this font's Encoding is not used.
 */
private int process_text_return_width(const pdf_text_enum_t *pte,
				      gs_font_base *font,
				      pdf_text_process_state_t *ppts,
				      const gs_const_string *pstr,
				      gs_point *pdpt);
private int
pdf_process_string(pdf_text_enum_t *penum, gs_string *pstr,
		   pdf_font_resource_t *pdfont, const gs_matrix *pfmat,
		   pdf_text_process_state_t *ppts)
{
    gx_device_pdf *const pdev = (gx_device_pdf *)penum->dev;
    gs_font_base *font = (gs_font_base *)penum->current_font;
    const gs_text_params_t *text = &penum->text;
    int i;
    int code = 0, mask;
    gs_point width_pt;
    gs_rect text_bbox;

    if (pfmat == 0)
	pfmat = &font->FontMatrix;
    if (text->operation & TEXT_RETURN_WIDTH) {
	code = gx_path_current_point(penum->path, &penum->origin);
	if (code < 0)
	    return code;
    }
    
    /*
     * Acrobat Reader can't handle text with huge coordinates,
     * so skip the text if it is outside the clip bbox.
     */
    code = process_text_estimate_bbox(penum, font, (gs_const_string *)pstr, pfmat, 
				      &text_bbox, &width_pt);
    if (code == 0) {
	gs_fixed_rect clip_bbox;
	gs_rect rect;

	gx_cpath_outer_box(penum->pcpath, &clip_bbox);
	rect.p.x = fixed2float(clip_bbox.p.x);
	rect.p.y = fixed2float(clip_bbox.p.y);
	rect.q.x = fixed2float(clip_bbox.q.x);
	rect.q.y = fixed2float(clip_bbox.q.y);
	rect_intersect(rect, text_bbox);
	if (rect.p.x > rect.q.x || rect.p.y > rect.q.y) {
	    penum->index += pstr->size;
	    goto finish;
	}
    }

    /*
     * Note that pdf_update_text_state sets all the members of ppts->values
     * to their current values.
     */
    code = pdf_update_text_state(ppts, penum, pdfont, pfmat);
    if (code > 0) {
	/* Try not to emulate ADD_TO_WIDTH if we don't have to. */
	if (code & TEXT_ADD_TO_SPACE_WIDTH) {
	    if (!memchr(pstr->data, penum->text.space.s_char, pstr->size))
		code &= ~TEXT_ADD_TO_SPACE_WIDTH;
	}
    }
    if (code < 0)
	return code;
    mask = code;

    /*
     * For incrementally loaded fonts, expand FirstChar..LastChar
     * if needed.
     */

    for (i = 0; i < pstr->size; ++i) {
	int chr = pstr->data[i];

	if (chr < pdfont->u.simple.FirstChar)
	    pdfont->u.simple.FirstChar = chr;
	if (chr > pdfont->u.simple.LastChar)
	    pdfont->u.simple.LastChar = chr;
    }

    if (text->operation & TEXT_REPLACE_WIDTHS)
	mask |= TEXT_REPLACE_WIDTHS;

    /*
     * The only operations left to handle are TEXT_DO_DRAW and
     * TEXT_RETURN_WIDTH.
     */
    if (mask == 0) {
	/*
	 * If any character has real_width != Width, we have to process
	 * the string character-by-character.  process_text_return_width
	 * will tell us what we need to know.
	 */
	if (!(text->operation & (TEXT_DO_DRAW | TEXT_RETURN_WIDTH)))
	    return 0;
	code = process_text_return_width(penum, font, ppts,
					 (gs_const_string *)pstr,
					 &width_pt);
	if (code < 0)
	    return code;
	if (code == 0) {
	    /* No characters with redefined widths -- the fast case. */
	    if (text->operation & TEXT_DO_DRAW) {
		code = pdf_append_chars(pdev, pstr->data, pstr->size,
					width_pt.x, width_pt.y, false);
		if (code < 0)
		    return code;
		penum->index += pstr->size;
	    }
	} else {
	    /* Use the slow case.  Set mask to any non-zero value. */
	    mask = TEXT_RETURN_WIDTH;
	}
    }
    if (mask) {
	code = process_text_modify_width(penum, (gs_font *)font, ppts,
					 (gs_const_string *)pstr,
					 &width_pt);
	if (code < 0)
	    return code;
    }


finish:
    /* Finally, return the total width if requested. */
    if (!(text->operation & TEXT_RETURN_WIDTH))
	return 0;
    penum->returned.total_width = width_pt;
    return gx_path_add_point(penum->path,
			     penum->origin.x + float2fixed(width_pt.x),
			     penum->origin.y + float2fixed(width_pt.y));
}

/*
 * Get the widths (unmodified and possibly modified) of a given character
 * in a simple font.  May add the widths to the widths cache (pdfont->Widths
 * and pdf_font_cache_elem::real_widths).  Return 1 if the widths were not cached.
 */
private int
pdf_char_widths(gx_device_pdf *const pdev,
		pdf_font_resource_t *pdfont, int ch, gs_font_base *font,
		pdf_glyph_widths_t *pwidths /* may be NULL */)
{
    pdf_glyph_widths_t widths;
    int code;
    byte *glyph_usage;
    double *real_widths;
    int char_cache_size, width_cache_size;
    pdf_font_resource_t *pdfont1;

    pdf_attached_font_resource(pdev, (gs_font *)font, &pdfont1, 
				&glyph_usage, &real_widths, &char_cache_size, &width_cache_size);
    if (pdfont1 != pdfont)
	return_error(gs_error_unregistered); /* Must not happen. */
    if (ch < 0 || ch > 255)
	return_error(gs_error_rangecheck);
    if (ch >= width_cache_size)
	return_error(gs_error_unregistered); /* Must not happen. */
    if (pwidths == 0)
	pwidths = &widths;
    if (real_widths[ch] == 0) {
	/* Might be an unused char, or just not cached. */
	gs_glyph glyph = pdfont->u.simple.Encoding[ch].glyph;

	code = pdf_glyph_widths(pdfont, font->WMode, glyph, (gs_font *)font, pwidths);
	if (code < 0)
	    return code;
	if (font->WMode != 0 && code > 0 &&
	    pwidths->real_width.v.x == 0 && pwidths->real_width.v.y == 0) {
	    /*
	     * The font has no Metrics2, so it must write
	     * horizontally due to PS spec.
	     * Therefore we need to fill the Widths array,
	     * which is required by PDF spec.
	     * Take it from WMode==0.
	     */
	    int save_WMode = font->WMode;
	    font->WMode = 0; /* Temporary patch font because font->procs.glyph_info
	                        has no WMode argument. */
	    code = pdf_glyph_widths(pdfont, font->WMode, glyph, (gs_font *)font, pwidths);
	    font->WMode = save_WMode;
	}
	pdfont->u.simple.v[ch].x = pwidths->real_width.v.x - pwidths->Width.v.x;
	pdfont->u.simple.v[ch].y = pwidths->real_width.v.y - pwidths->Width.v.y;
	if (code == 0) {
	    pdfont->Widths[ch] = pwidths->Width.w;
	    real_widths[ch] = pwidths->real_width.w;
	}
    } else {
	pwidths->Width.w = pdfont->Widths[ch];
	pwidths->real_width.w = real_widths[ch];
	pwidths->Width.v = pdfont->u.simple.v[ch];
	if (font->WMode) {
	    pwidths->Width.xy.x = 0;
	    pwidths->Width.xy.y = pwidths->Width.w;
	    pwidths->real_width.xy.x = 0;
	    pwidths->real_width.xy.y = pwidths->real_width.w;
	} else {
	    pwidths->Width.xy.x = pwidths->Width.w;
	    pwidths->Width.xy.y = 0;
	    pwidths->real_width.xy.x = pwidths->real_width.w;
	    pwidths->real_width.xy.y = 0;
	}
	code = 0;
    }
    return code;
}

/*
 * Compute the total text width (in user space).  Return 1 if any
 * character had real_width != Width, otherwise 0.
 */
private int
process_text_return_width(const pdf_text_enum_t *pte, gs_font_base *font,
			  pdf_text_process_state_t *ppts,
			  const gs_const_string *pstr,
			  gs_point *pdpt)
{
    int i;
    gs_point w;
    double scale = 0.001 * ppts->values.size;
    gs_point dpt;
    int num_spaces = 0;
    int space_char =
	(pte->text.operation & TEXT_ADD_TO_SPACE_WIDTH ?
	 pte->text.space.s_char : -1);
    int widths_differ = 0;

    for (i = 0, w.x = w.y = 0; i < pstr->size; ++i) {
	pdf_glyph_widths_t cw;
	int code = pdf_char_widths((gx_device_pdf *)pte->dev,
	                           ppts->values.pdfont, pstr->data[i], font,
				   &cw);

	if (code < 0)
	    return code;
	w.x += cw.real_width.xy.x;
	w.y += cw.real_width.xy.y;
	if (cw.real_width.xy.x != cw.Width.xy.x ||
	    cw.real_width.xy.y != cw.Width.xy.y
	    )
	    widths_differ = 1;
	if (pstr->data[i] == space_char)
	    ++num_spaces;
    }
    gs_distance_transform(w.x * scale, w.y * scale,
			  &ppts->values.matrix, &dpt);
    if (pte->text.operation & TEXT_ADD_TO_ALL_WIDTHS) {
	int num_chars = pstr->size;
	gs_point tpt;

	gs_distance_transform(pte->text.delta_all.x, pte->text.delta_all.y,
			      &ctm_only(pte->pis), &tpt);
	dpt.x += tpt.x * num_chars;
	dpt.y += tpt.y * num_chars;
    }
    if (pte->text.operation & TEXT_ADD_TO_SPACE_WIDTH) {
	gs_point tpt;

	gs_distance_transform(pte->text.delta_space.x, pte->text.delta_space.y,
			      &ctm_only(pte->pis), &tpt);
	dpt.x += tpt.x * num_spaces;
	dpt.y += tpt.y * num_spaces;
    }
    *pdpt = dpt;

    return widths_differ;
}

/*
 * Retrieve glyph origing shift for WMode = 1 in design units.
 */
private void 
pdf_glyph_origin(pdf_font_resource_t *pdfont, int ch, int WMode, gs_point *p)
{
    /* For CID fonts PDF viewers provide glyph origin shift automatically.
     * Therefore we only need to do for non-CID fonts.
     */
    switch (pdfont->FontType) {
	case ft_encrypted:
	case ft_encrypted2:
	case ft_TrueType:
	    *p = pdfont->u.simple.v[ch];
	    break;
	default:
	    p->x = p->y = 0;
	    break;
    }
}

/*
 * Emulate TEXT_ADD_TO_ALL_WIDTHS and/or TEXT_ADD_TO_SPACE_WIDTH,
 * and implement TEXT_REPLACE_WIDTHS if requested.
 * Uses and updates ppts->values.matrix; uses ppts->values.pdfont.
 */
int
process_text_modify_width(pdf_text_enum_t *pte, gs_font *font,
			  pdf_text_process_state_t *ppts,
			  const gs_const_string *pstr,
			  gs_point *pdpt)
{
    gx_device_pdf *const pdev = (gx_device_pdf *)pte->dev;
    double scale = 0.001 * ppts->values.size;
    int space_char =
	(pte->text.operation & TEXT_ADD_TO_SPACE_WIDTH ?
	 pte->text.space.s_char : -1);
    int code = 0;
    gs_point start, total;
    bool composite = (ppts->values.pdfont->FontType == ft_composite);

    pte->text.data.bytes = pstr->data;
    pte->text.size = pstr->size;
    pte->index = 0;
    start.x = ppts->values.matrix.tx;
    start.y = ppts->values.matrix.ty;
    total.x = total.y = 0;	/* user space */
    /*
     * Note that character widths are in design space, but text.delta_*
     * values and the width value returned in *pdpt are in user space,
     * and the width values for pdf_append_chars are in device space.
     */
    for (;;) {
	pdf_glyph_widths_t cw;	/* design space */
	gs_point did, wanted, tpt;	/* user space */
	gs_point v; /* design space */
	gs_char chr;
	gs_glyph glyph;
	int code, index = pte->index;

	code = pte->orig_font->procs.next_char_glyph((gs_text_enum_t *)pte, &chr, &glyph);
	if (code == 2) /* end of string */
	    break;
	if (code < 0)
	    return code;
	if (composite) /* from process_cmap_text */
	    code = pdf_glyph_widths(ppts->values.pdfont->u.type0.DescendantFont, font->WMode, glyph, font, &cw);
	else /* must be a base font */
	    code = pdf_char_widths((gx_device_pdf *)pte->dev,
	                           ppts->values.pdfont, chr, (gs_font_base *)font,
				   &cw);
	if (code < 0)
	    return code;
	pdf_glyph_origin(ppts->values.pdfont, chr, font->WMode, &v);
	if (v.x != 0 || v.y != 0) {
	    gs_point glyph_origin_shift;

	    gs_distance_transform(-v.x * scale, -v.y * scale,
				  &ctm_only(pte->pis), &glyph_origin_shift);
	    if (glyph_origin_shift.x != 0 || glyph_origin_shift.y != 0) {
		ppts->values.matrix.tx = start.x + total.x + glyph_origin_shift.x;
		ppts->values.matrix.ty = start.y + total.y + glyph_origin_shift.y;
		code = pdf_set_text_state_values(pdev, &ppts->values);
		if (code < 0)
		    break;
	    }
	}
	if (pte->text.operation & TEXT_DO_DRAW) {
	    gs_distance_transform(cw.Width.xy.x * scale,
				  cw.Width.xy.y * scale,
				  &ppts->values.matrix, &did);
	    gs_distance_transform((font->WMode ? 0 : ppts->values.character_spacing),
				  (font->WMode ? ppts->values.character_spacing : 0),
				  &ppts->values.matrix, &tpt);
	    did.x += tpt.x;
	    did.y += tpt.y;
	    if (chr == space_char) {
		gs_distance_transform((font->WMode ? 0 : ppts->values.word_spacing),
				      (font->WMode ? ppts->values.word_spacing : 0),
				      &ppts->values.matrix, &tpt);
		did.x += tpt.x;
		did.y += tpt.y;
	    }
	    code = pdf_append_chars(pdev, pstr->data + index, pte->index - index, did.x, did.y, composite);
	    if (code < 0)
		break;
	} else
	    did.x = did.y = 0;
	if (pte->text.operation & TEXT_REPLACE_WIDTHS) {
	    gs_point dpt;

	    gs_text_replaced_width(&pte->text, pte->xy_index++, &dpt);
	    gs_distance_transform(dpt.x, dpt.y, &ctm_only(pte->pis), &wanted);
	} else {
	    gs_distance_transform(cw.real_width.xy.x * scale,
				  cw.real_width.xy.y * scale,
				  &ppts->values.matrix, &wanted);
	    if (pte->text.operation & TEXT_ADD_TO_ALL_WIDTHS) {
		gs_distance_transform(pte->text.delta_all.x,
				      pte->text.delta_all.y,
				      &ctm_only(pte->pis), &tpt);
		wanted.x += tpt.x;
		wanted.y += tpt.y;
	    }
	    if (chr == space_char) {
		gs_distance_transform(pte->text.delta_space.x,
				      pte->text.delta_space.y,
				      &ctm_only(pte->pis), &tpt);
		wanted.x += tpt.x;
		wanted.y += tpt.y;
	    }
	}
	total.x += wanted.x;
	total.y += wanted.y;
	if (wanted.x != did.x || wanted.y != did.y) {
	    ppts->values.matrix.tx = start.x + total.x;
	    ppts->values.matrix.ty = start.y + total.y;
	    code = pdf_set_text_state_values(pdev, &ppts->values);
	    if (code < 0)
		break;
	}
    }
    *pdpt = total;
    return code;
}

private int 
pdf_encode_glyph(gs_font_base *bfont, gs_glyph glyph0,
	    byte *buf, int buf_size, int *char_code_length)
{
    gs_char c;

    *char_code_length = 1;
    if (*char_code_length > buf_size) 
	return_error(gs_error_rangecheck); /* Must not happen. */
    for (c = 0; c < 255; c++) {
	gs_glyph glyph1 = bfont->procs.encode_char((gs_font *)bfont, c, 
		    GLYPH_SPACE_NAME);
	if (glyph1 == glyph0) {
	    buf[0] = (byte)c;
	    return 0;
	}
    }
    return_error(gs_error_rangecheck); /* Can't encode. */
}

/* ---------------- Type 1 or TrueType font ---------------- */

/*
 * Process a text string in a simple font.
 */
int
process_plain_text(gs_text_enum_t *pte, const void *vdata, void *vbuf,
		   uint size)
{
    byte *const buf = vbuf;
    uint count;
    uint operation = pte->text.operation;
    pdf_text_enum_t *const penum = (pdf_text_enum_t *)pte;
    int code;
    gs_string str;
    bool encoded;
    pdf_text_process_state_t text_state;

    if (operation & (TEXT_FROM_STRING | TEXT_FROM_BYTES)) {
	count = size;
	memcpy(buf, (const byte *)vdata + pte->index, size);
	encoded = false;
    } else if (operation & (TEXT_FROM_CHARS | TEXT_FROM_SINGLE_CHAR)) {
	/* Check that all chars fit in a single byte. */
	const gs_char *const cdata = vdata;
	int i;

	count = size / sizeof(gs_char);
	for (i = 0; i < count; ++i) {
	    gs_char chr = cdata[pte->index + i];

	    if (chr & ~0xff)
		return_error(gs_error_rangecheck);
	    buf[i] = (byte)chr;
	}
	encoded = false;
    } else if (operation & (TEXT_FROM_GLYPHS | TEXT_FROM_SINGLE_GLYPH)) {
	/*
	 * Since PDF has no analogue of 'glyphshow',
	 * we try to encode glyphs with the current
	 * font's encoding. If the current font has no encoding,
	 * or the encoding doesn't contain necessary glyphs,
	 * the text will be represented with a Type 3 font with bitmaps.
	 *
	 * When we fail with encoding (136-01.ps is an example),
	 * we could locate a PDF font resource or create a new one
	 * with same outlines and an appropriate encoding.
	 * Also we could change .notdef entries in the
	 * copied font (assuming that document designer didn't use
	 * .notdef for a meanful printing).
	 * fixme: Not implemented yet.
	 */
	const gs_glyph *const gdata = vdata;
	gs_font *font = pte->current_font;
	int i;

	if (!pdf_is_simple_font(font))
	    return_error(gs_error_unregistered); /* Must not ahppen. */
	count = 0;
	for (i = 0; i < size / sizeof(gs_glyph); ++i) {
	    gs_glyph glyph = gdata[pte->index + i];
	    int char_code_length;

	    code = pdf_encode_glyph((gs_font_base *)font, glyph, 
			 buf + count, size - count, &char_code_length);
	    if (code < 0) {
		/* 
		 * pdf_text_process will fall back 
		 * to default implementation.
		 */
		return code;
	    }
	    count += char_code_length;
	}
	encoded = true;
    } else
	return_error(gs_error_rangecheck);
    str.data = buf;
    if (count > 1 && (pte->text.operation & TEXT_INTERVENE)) {
	/* Just do one character. */
	str.size = 1;
	code = pdf_encode_process_string(penum, &str, NULL, &text_state);
	if (code >= 0) {
	    pte->returned.current_char = buf[0];
	    code = TEXT_PROCESS_INTERVENE;
	}
    } else {
	str.size = count;
	code = pdf_encode_process_string(penum, &str, NULL, &text_state);
    }
    return code;
}
