/* Copyright (C) 1996, 1997, 1998, 1999, 2000 Aladdin Enterprises.  All rights reserved.

   This file is part of Aladdin Ghostscript.

   Aladdin Ghostscript is distributed with NO WARRANTY OF ANY KIND.  No author
   or distributor accepts any responsibility for the consequences of using it,
   or for whether it serves any particular purpose or works at all, unless he
   or she says so in writing.  Refer to the Aladdin Ghostscript Free Public
   License (the "License") for full details.

   Every copy of Aladdin Ghostscript must include a copy of the License,
   normally in a plain ASCII text file named PUBLIC.  The License grants you
   the right to copy, modify and redistribute Aladdin Ghostscript, but only
   under certain conditions described in the License.  Among other things, the
   License requires that the copyright notice and this notice be preserved on
   all copies.
 */

/*$Id$ */
/* CID-keyed font operators */
#include "memory_.h"
#include "ghost.h"
#include "oper.h"
#include "gsmatrix.h"
#include "gsccode.h"
#include "gsstruct.h"
#include "gxfcid.h"
#include "gxfont1.h"
#include "bfont.h"
#include "icid.h"
#include "idict.h"
#include "idparam.h"
#include "ifont1.h"
#include "ifont42.h"
#include "store.h"

/* ---------------- Shared utilities ---------------- */

/* Get the CIDSystemInfo of a CIDFont. */
private int
cid_font_system_info_param(gs_cid_system_info_t *pcidsi, const ref *prfont)
{
    ref *prcidsi;

    if (dict_find_string(prfont, "CIDSystemInfo", &prcidsi) <= 0)
	return_error(e_rangecheck);
    return cid_system_info_param(pcidsi, prcidsi);
}

/* Get the additional information for a CIDFontType 0 or 2 CIDFont. */
private int
cid_font_data_param(os_ptr op, gs_font_cid_data *pdata, ref *pGlyphDirectory)
{
    int code;

    check_type(*op, t_dictionary);
    code = cid_font_system_info_param(&pdata->CIDSystemInfo, op);
    if (code < 0 ||
	(code = dict_int_param(op, "CIDCount", 0, max_int, -1,
			       &pdata->CIDCount)) < 0 ||
	(code = font_GlyphDirectory_param(op, pGlyphDirectory)) < 0
	)
	return code;
    if (r_has_type(pGlyphDirectory, t_null)) {
	/* Standard CIDFont, require GDBytes. */
	return dict_int_param(op, "GDBytes", 1, 4, 0, &pdata->GDBytes);
    } else {
	pdata->GDBytes = 0;
	return 0;
    }
}

/* ---------------- CIDFontType 0 (FontType 9) ---------------- */

/* Get one element of a FDArray. */
private int
fd_array_element(i_ctx_t *i_ctx_p, gs_font_type1 **ppfont, /*const*/ ref *prfd)
{
    charstring_font_refs_t refs;
    gs_type1_data data1;
    build_proc_refs build;
    gs_font_base *pbfont;
    gs_font_type1 *pfont;
    int code = charstring_font_get_refs(prfd, &refs);

    if (code < 0)
	return code;
    /****** HANDLE ALTERNATE SUBR REPRESENTATION ******/
    data1.interpret = gs_type1_interpret;
    data1.subroutineNumberBias = 0;
    data1.lenIV = DEFAULT_LENIV_1;
    code = charstring_font_params(prfd, &refs, &data1);
    if (code < 0)
	return code;
    /****** CHANGE THIS FOR 3011 TO HANDLE FontType 2 ******/
    code = build_proc_name_refs(&build,
				"%Type1BuildChar", "%Type1BuildGlyph");
    if (code < 0)
	return code;
    code = build_gs_FDArray_font(i_ctx_p, prfd, &pbfont, ft_encrypted,
				 &st_gs_font_type1, &build);
    if (code < 0)
	return code;
    pfont = (gs_font_type1 *)pbfont;
    charstring_font_init(pfont, &refs, &data1);
    *ppfont = pfont;
    return 0;
}

/* <string|name> <font_dict> .buildfont9 <string|name> <font> */
gs_private_st_ptr(st_gs_font_type1_ptr, gs_font_type1 *, "gs_font_type1 *",
  font1_ptr_enum_ptrs, font1_ptr_reloc_ptrs);
gs_private_st_element(st_gs_font_type1_ptr_element, gs_font_type1 *,
  "gs_font_type1 *[]", font1_ptr_element_enum_ptrs,
  font1_ptr_element_reloc_ptrs, st_gs_font_type1_ptr);
private int
zbuildfont9(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;
    build_proc_refs build;
    int code = build_proc_name_refs(&build, NULL, "%Type9BuildGlyph");
    gs_font_cid_data common;
    uint CIDMapOffset;
    ref *prfda;
    gs_font_type1 **FDArray;
    uint FDArray_size;
    int FDBytes;
    gs_font_base *pfont;
    gs_font_cid0 *pfcid;
    ref GlyphDirectory;
    uint i;

    if (code < 0 ||
	(code = cid_font_data_param(op, &common, &GlyphDirectory)) < 0 ||
	(code = dict_find_string(op, "FDArray", &prfda)) < 0 ||
	(code = dict_int_param(op, "FDBytes", 0, 4, -1, &FDBytes)) < 0
	)
	return code;
    if (r_has_type(&GlyphDirectory, t_null)) {
	/* Standard CIDFont, require CIDMapOffset. */
	if ((code = dict_uint_param(op, "CIDMapOffset", 0, max_uint - 1,
				    max_uint, &CIDMapOffset)) < 0)
	    return code;
    } else {
	CIDMapOffset = 0;
    }
    if (!r_is_array(prfda))
	return_error(e_invalidfont);
    FDArray_size = r_size(prfda);
    if (FDArray_size == 0)
	return_error(e_invalidfont);
    FDArray = ialloc_struct_array(FDArray_size, gs_font_type1 *,
				  &st_gs_font_type1_ptr_element,
				  "buildfont9(FDarray)");
    if (FDArray == 0)
	return_error(e_VMerror);
    memset(FDArray, 0, sizeof(gs_font_type1 *) * FDArray_size);
    for (i = 0; i < FDArray_size; ++i) {
	ref rfd;

	array_get(prfda, (long)i, &rfd);
	code = fd_array_element(i_ctx_p, &FDArray[i], &rfd);
	if (code < 0)
	    return code;
    }
    code = build_gs_simple_font(i_ctx_p, op, &pfont, ft_CID_encrypted,
				&st_gs_font_cid0, &build,
				bf_Encoding_optional |
				bf_FontBBox_required |
				bf_UniqueID_ignored);
    if (code < 0)
	return code;
    pfcid = (gs_font_cid0 *)pfont;
    pfcid->cidata.common = common;
    pfcid->cidata.CIDMapOffset = CIDMapOffset;
    pfcid->cidata.FDArray = FDArray;
    pfcid->cidata.FDArray_size = FDArray_size;
    pfcid->cidata.FDBytes = FDBytes;
    /****** SET glyph_data ******/
    pfcid->cidata.proc_data = 0;	/* for GC */
    ref_assign(&pfont_data(pfont)->u.cid0.GlyphDirectory, &GlyphDirectory);
    return define_gs_font((gs_font *)pfont);
}

/* ---------------- CIDFontType 1 (FontType 10) ---------------- */

/* <string|name> <font_dict> .buildfont10 <string|name> <font> */
private int
zbuildfont10(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;
    build_proc_refs build;
    int code = build_gs_font_procs(op, &build);
    gs_cid_system_info_t cidsi;
    gs_font_base *pfont;

    if (code < 0)
	return code;
    code = cid_font_system_info_param(&cidsi, op);
    if (code < 0)
	return code;
    make_null(&build.BuildChar);	/* only BuildGlyph */
    code = build_gs_simple_font(i_ctx_p, op, &pfont, ft_CID_user_defined,
				&st_gs_font_cid1, &build,
				bf_Encoding_optional |
				bf_FontBBox_required |
				bf_UniqueID_ignored);
    if (code < 0)
	return code;
    ((gs_font_cid1 *)pfont)->cidata.CIDSystemInfo = cidsi;
    return define_gs_font((gs_font *)pfont);
}

/* ---------------- CIDFontType 2 (FontType 11) ---------------- */

/* Map a glyph CID to a TrueType glyph number using the CIDMap. */
private int
z11_CIDMap_proc(gs_font_cid2 *pfont, gs_glyph glyph)
{
    const ref *pcidmap = &pfont_data(pfont)->u.type42.CIDMap;
    ulong cid = glyph - gs_min_cid_glyph;
    int gdbytes = pfont->cidata.common.GDBytes;
    int gnum = 0;
    const byte *data;
    int i, code;
    ref rcid;
    ref *prgnum;

    switch (r_type(pcidmap)) {
    case t_string:
	if (cid >= r_size(pcidmap) / gdbytes)
	    return_error(e_rangecheck);
	data = pcidmap->value.const_bytes + cid * gdbytes;
	break;
    case t_integer:
	return cid + pcidmap->value.intval;
    case t_dictionary:
	make_int(&rcid, cid);
	code = dict_find(pcidmap, &rcid, &prgnum);
	if (code <= 0)
	    return (code < 0 ? code : gs_note_error(e_undefined));
	if (!r_has_type(prgnum, t_integer))
	    return_error(e_typecheck);
	return prgnum->value.intval;
    default:			/* array type */
	code = string_array_access_proc(pcidmap, 1, cid * gdbytes,
					gdbytes, &data);

	if (code < 0)
	    return code;
    }
    for (i = 0; i < gdbytes; ++i)
	gnum = (gnum << 8) + data[i];
    return gnum;
}

/* Handle MetricsCount when accessing outline or metrics information. */
private int
z11_get_outline(gs_font_type42 * pfont, uint glyph_index,
		gs_const_string * pgstr)
{
    gs_font_cid2 *const pfcid = (gs_font_cid2 *)pfont;
    int skip = pfcid->cidata.MetricsCount << 1;
    int code = pfcid->cidata.orig_procs.get_outline(pfont, glyph_index, pgstr);

    if (code >= 0) {
	if (pgstr->size <= skip)
	    pgstr->data = 0, pgstr->size = 0;
	else
	    pgstr->data += skip, pgstr->size -= skip;
    }
    return code;
}
private int
z11_get_metrics(gs_font_type42 * pfont, uint glyph_index, int wmode,
		float sbw[4])
{
    gs_font_cid2 *const pfcid = (gs_font_cid2 *)pfont;
    int skip = pfcid->cidata.MetricsCount << 1;
    gs_const_string gstr;
    const byte *pmetrics;
    int lsb, width;

    if (wmode > skip >> 2 ||
	pfcid->cidata.orig_procs.get_outline(pfont, glyph_index, &gstr) < 0 ||
	gstr.size < skip
	)
	return pfcid->cidata.orig_procs.get_metrics(pfont, glyph_index, wmode,
						    sbw);
    pmetrics = gstr.data + skip - (wmode << 2);
    lsb = (pmetrics[2] << 8) + pmetrics[3];
    width = (pmetrics[0] << 8) + pmetrics[1];
    {
	double factor = 1.0 / pfont->data.unitsPerEm;

	if (wmode) {
	    sbw[0] = 0, sbw[1] = -lsb * factor;
	    sbw[2] = 0, sbw[3] = -width * factor;
	} else {
	    sbw[0] = lsb * factor, sbw[1] = 0;
	    sbw[2] = width * factor, sbw[3] = 0;
	}
    }
    return 0;
}

/* <string|name> <font_dict> .buildfont11 <string|name> <font> */
private int
zbuildfont11(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;
    gs_font_cid_data common;
    gs_font_type42 *pfont;
    gs_font_cid2 *pfcid;
    int MetricsCount;
    ref rcidmap, ignore_gdir;
    int code = cid_font_data_param(op, &common, &ignore_gdir);

    if (code < 0 ||
	(code = dict_int_param(op, "MetricsCount", 0, 4, 0, &MetricsCount)) < 0
	)
	return code;
    if (MetricsCount & 1)	/* only allowable values are 0, 2, 4 */
	return_error(e_rangecheck);
    code = font_string_array_param(op, "CIDMap", &rcidmap);
    switch (code) {
    case 0:
	break;
    default:
	return code;
    case e_typecheck:
	switch (r_type(&rcidmap)) {
	case t_string:		/* in PLRM3 */
	case t_dictionary:	/* added in 3011 */
	case t_integer:		/* added in 3011 */
	    break;
	default:
	    return code;
	}
	break;
    }
    code = build_gs_TrueType_font(i_ctx_p, op, &pfont, ft_CID_TrueType,
				  &st_gs_font_cid2,
				  (const char *)0, "%Type11BuildGlyph",
				  bf_Encoding_optional |
				  bf_FontBBox_required |
				  bf_UniqueID_ignored |
				  bf_CharStrings_optional);
    if (code < 0)
	return code;
    pfcid = (gs_font_cid2 *)pfont;
    pfcid->cidata.common = common;
    pfcid->cidata.MetricsCount = MetricsCount;
    ref_assign(&pfont_data(pfont)->u.type42.CIDMap, &rcidmap);
    pfcid->cidata.CIDMap_proc = z11_CIDMap_proc;
    if (MetricsCount) {
	/* "Wrap" the glyph accessor procedures. */
	pfcid->cidata.orig_procs.get_outline = pfont->data.get_outline;
	pfont->data.get_outline = z11_get_outline;
	pfcid->cidata.orig_procs.get_metrics = pfont->data.get_metrics;
	pfont->data.get_metrics = z11_get_metrics;
    }
    return define_gs_font((gs_font *)pfont);
}

/* <cid11font> <cid> .type11mapcid <glyph_index> */
private int
ztype11mapcid(i_ctx_t *i_ctx_p)
{
    os_ptr op = osp;
    gs_font *pfont;
    int code = font_param(op - 1, &pfont);

    if (code < 0)
	return code;
    if (pfont->FontType != ft_CID_TrueType)
	return_error(e_invalidfont);
    check_type(*op, t_integer);
    code = z11_CIDMap_proc((gs_font_cid2 *)pfont,
			   (gs_glyph)(gs_min_cid_glyph + op->value.intval));
    if (code < 0)
	return code;
    make_int(op - 1, code);
    pop(1);
    return 0;
}

/* ------ Initialization procedure ------ */

const op_def zfcid_op_defs[] =
{
    {"2.buildfont9", zbuildfont9},
    {"2.buildfont10", zbuildfont10},
    {"2.buildfont11", zbuildfont11},
    {"2.type11mapcid", ztype11mapcid},
    op_def_end(0)
};
