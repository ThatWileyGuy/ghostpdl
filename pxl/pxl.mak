#    Copyright (C) 1996, 1997, 1998 Aladdin Enterprises.  All rights reserved.
#    Unauthorized use, copying, and/or distribution prohibited.

# makefile for PCL XL interpreters.
# Users of this makefile must define the following:
#	GLSRCDIR - the GS library source directory
#	GLGENDIR - the GS library generated file directory
#	PLSRCDIR - the PCL* support library source directory
#	PLOBJDIR - the PCL* support library object / executable directory
#	PXLSRCDIR - the source directory
#	PXLGENDIR - the directory for source files generated during building
#	PXLOBJDIR - the object / executable directory

PLOBJ=$(PLOBJDIR)$(D)

PXLSRC=$(PXLSRCDIR)$(D)
PXLGEN=$(PXLGENDIR)$(D)
PXLOBJ=$(PXLOBJDIR)$(D)
PXLO_=$(O_)$(PXLOBJ)

PXLCCC=$(CC_) -I$(PXLSRCDIR) -I$(PXLGENDIR) -I$(PLSRCDIR) -I$(GLSRCDIR) -I$(GLGENDIR) $(C_)

# Define the name of this makefile.
PXL_MAK=$(PXLSRC)pxl.mak

pxl.clean: pxl.config-clean pxl.clean-not-config-clean

pxl.clean-not-config-clean:
	$(RM_) $(PXLOBJ)*.$(OBJ)
	$(RMN_) $(PXLGEN)pxbfont.c $(PXLGEN)pxsymbol.c $(PXLGEN)pxsymbol.h
	$(RM_) $(PXLOBJ)devs.tr6
	make -f $(GLSRCDIR)$(D)ugcclib.mak \
	GLSRCDIR='$(GLSRCDIR)' GLGENDIR='$(GLGENDIR)' \
	GLOBJDIR='$(GLOBJDIR)' clean

# devices are still created in the current directory.  Until that 
# is fixed we will have to remove them from both directories.
pxl.config-clean:
	$(RM_) $(PXLOBJ)*.dev
	$(RM_) *.dev

################ PCL XL ################

#### Functionality left to implement:
#	Symbol set mapping
#### Other stuff:
#	Free subsidiary objects when freeing patterns

pxattr_h=$(PXLSRC)pxattr.h $(gdevpxat_h)
pxbfont_h=$(PXLSRC)pxbfont.h
pxenum_h=$(PXLSRC)pxenum.h $(gdevpxen_h)
pxerrors_h=$(PXLSRC)pxerrors.h
pxfont_h=$(PXLSRC)pxfont.h $(plfont_h)
pxptable_h=$(PXLSRC)pxptable.h
pxtag_h=$(PXLSRC)pxtag.h $(gdevpxop_h)
pxsymbol_h=$(PXLGEN)pxsymbol.h
pxvalue_h=$(PXLSRC)pxvalue.h $(gstypes_h) $(pxattr_h)
# Nested headers
pxdict_h=$(PXLSRC)pxdict.h $(pldict_h) $(pxvalue_h)
pxgstate_h=$(PXLSRC)pxgstate.h $(gsccolor_h) $(gsiparam_h) $(gsmatrix_h) $(gsrefct_h) $(gxbitmap_h) $(plsymbol_h) $(pxdict_h) $(pxenum_h)
pxoper_h=$(PXLSRC)pxoper.h $(gserror_h) $(pxattr_h) $(pxerrors_h) $(pxvalue_h)
pxparse_h=$(PXLSRC)pxparse.h $(pxoper_h)
pxstate_h=$(PXLSRC)pxstate.h $(gsmemory_h) $(pxgstate_h)

# To avoid having to build the Ghostscript interpreter to generate pxbfont.c,
# we normally ship a pre-constructed pxbfont.psc with the source code.
# If the following rule doesn't work, try:
#    /usr/bin/gs -I/usr/lib/ghostscript -q -dNODISPLAY pxbfont.ps >pxbfont.psc
# .psc is an intermediate .c file created by executing PostScript code.
# In order to avoid creating a partial or empty file, we create this file
# under a different name and then rename it (by copying and deleting,
# since non-Unix operating systems typically don't support rename on files
# with directory names included).  Normally 'make' would delete the file
# if the rule fails, but in this case for some reason it doesn't.
$(PXLSRC)pxbfont.psc: $(PXLSRC)pxbfont.ps
	$(GS_XE) -q -dNODISPLAY $(PXLSRC)pxbfont.ps >$(PXLGEN)pxbfont_.psc
	$(CP_) $(PXLGEN)pxbfont_.psc $(PXLSRC)pxbfont.psc
	$(RM_) $(PXLGEN)pxbfont_.psc
	
$(PXLGEN)pxbfont.c: $(PXLSRC)pxbfont.psc
	$(CP_) $(PXLSRC)pxbfont.psc $(PXLGEN)pxbfont.c

$(PXLOBJ)pxbfont.$(OBJ): $(PXLGEN)pxbfont.c $(AK) $(stdpre_h)\
 $(pxbfont_h)
	$(PXLCCC) $(PXLGEN)pxbfont.c $(PXLO_)pxbfont.$(OBJ)

$(PXLOBJ)pxerrors.$(OBJ): $(PXLSRC)pxerrors.c $(AK)\
 $(memory__h) $(stdio__h) $(string__h)\
 $(gsccode_h) $(gscoord_h) $(gsmatrix_h) $(gsmemory_h)\
 $(gspaint_h) $(gspath_h) $(gsstate_h) $(gstypes_h) $(gsutil_h)\
 $(gxchar_h) $(gxfixed_h) $(gxfont_h) $(scommon_h)\
 $(pxbfont_h) $(pxerrors_h) $(pxfont_h) $(pxparse_h) $(pxptable_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxerrors.c $(PXLO_)pxerrors.$(OBJ)

$(PXLOBJ)pxparse.$(OBJ): $(PXLSRC)pxparse.c $(AK) $(memory__h) $(stdio__h)\
 $(gdebug_h) $(gserror_h) $(gstypes_h)\
 $(plparse_h)\
 $(pxattr_h) $(pxenum_h) $(pxerrors_h) $(pxoper_h) $(pxparse_h) $(pxptable_h)\
 $(pxstate_h) $(pxtag_h) $(pxvalue_h)
	$(PXLCCC) $(PXLSRC)pxparse.c $(PXLO_)pxparse.$(OBJ)

$(PXLOBJ)pxstate.$(OBJ): $(PXLSRC)pxstate.c $(AK) $(stdio__h)\
 $(gsmemory_h) $(gsstruct_h) $(gstypes_h)\
 $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxstate.c $(PXLO_)pxstate.$(OBJ)

# See the comment above under pxbfont.c.
#    /usr/bin/gs -I/usr/lib/ghostscript -q -dNODISPLAY -dHEADER pxsymbol.ps >pxsymbol.psh
$(PXLSRC)pxsymbol.psh: $(PXLSRC)pxsymbol.ps
	$(GS_XE) -q -dNODISPLAY -dHEADER $(PXLSRC)pxsymbol.ps >$(PXLGEN)pxsym_.psh
	$(CP_) $(PXLGEN)pxsym_.psh $(PXLSRC)pxsymbol.psh
	$(RM_) $(PXLGEN)pxsym_.psh

$(PXLGEN)pxsymbol.h: $(PXLSRC)pxsymbol.psh
	$(CP_) $(PXLSRC)pxsymbol.psh $(PXLGEN)pxsymbol.h

# See the comment above under pxbfont.c.
#    /usr/bin/gs -I/usr/lib/ghostscript -q -dNODISPLAY pxsymbol.ps >pxsymbol.psc
$(PXLSRC)pxsymbol.psc: $(PXLSRC)pxsymbol.ps
	$(GS_XE) -q -dNODISPLAY $(PXLSRC)pxsymbol.ps >$(PXLGEN)pxsym_.psc
	$(CP_) $(PXLGEN)pxsym_.psc $(PXLSRC)pxsymbol.psc
	$(RM_) $(PXLGEN)pxsym_.psc

$(PXLGEN)pxsymbol.c: $(PXLSRC)pxsymbol.psc
	$(CP_) $(PXLSRC)pxsymbol.psc $(PXLGEN)pxsymbol.c

$(PXLOBJ)pxsymbol.$(OBJ): $(PXLGEN)pxsymbol.c $(AK) $(pxsymbol_h)
	$(PXLCCC) $(PXLGEN)pxsymbol.c $(PXLO_)pxsymbol.$(OBJ)

$(PXLOBJ)pxptable.$(OBJ): $(PXLSRC)pxptable.c $(AK) $(std_h)\
 $(pxenum_h) $(pxoper_h) $(pxptable_h) $(pxvalue_h)
	$(PXLCCC) $(PXLSRC)pxptable.c $(PXLO_)pxptable.$(OBJ)

$(PXLOBJ)pxvalue.$(OBJ): $(PXLSRC)pxvalue.c $(AK) $(std_h) $(gsmemory_h) $(pxvalue_h)
	$(PXLCCC) $(PXLSRC)pxvalue.c $(PXLO_)pxvalue.$(OBJ)

# We have to break up pxl_other because of the MS-DOS command line
# limit of 120 characters.
pxl_other_obj1=$(PXLOBJ)pxbfont.$(OBJ) $(PXLOBJ)pxerrors.$(OBJ) $(PXLOBJ)pxparse.$(OBJ)
pxl_other_obj2=$(PXLOBJ)pxstate.$(OBJ) $(PXLOBJ)pxptable.$(OBJ) $(PXLOBJ)pxvalue.$(OBJ)
pxl_other_obj=$(pxl_other_obj1) $(pxl_other_obj2)

# Operators

# This implements finding fonts by name, but doesn't implement any operators
# per se.
$(PXLOBJ)pxffont.$(OBJ): $(PXLSRC)pxffont.c $(AK) $(string__h)\
 $(gschar_h) $(gsmatrix_h) $(gx_h) $(gxfont_h) $(gxfont42_h)\
 $(pxfont_h) $(pxoper_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxffont.c $(PXLO_)pxffont.$(OBJ)

$(PXLOBJ)pxfont.$(OBJ): $(PXLSRC)pxfont.c $(AK) $(math__h) $(stdio__h) $(string__h)\
 $(gdebug_h) $(gschar_h) $(gscoord_h) $(gserrors_h) $(gsimage_h)\
 $(gspaint_h) $(gspath_h) $(gsstate_h) $(gsstruct_h) $(gsutil_h)\
 $(gxchar_h) $(gxfixed_h) $(gxfont_h) $(gxfont42_h) $(gxpath_h) $(gzstate_h)\
 $(plvalue_h)\
 $(pxfont_h) $(pxoper_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxfont.c $(PXLO_)pxfont.$(OBJ)

$(PXLOBJ)pxgstate.$(OBJ): $(PXLSRC)pxgstate.c $(AK) $(math__h) $(memory__h) $(stdio__h)\
 $(gscoord_h) $(gsimage_h) $(gsmemory_h) $(gspath_h) $(gspath2_h) $(gsrop_h)\
 $(gsstate_h) $(gsstruct_h) $(gstypes_h)\
 $(gxcspace_h) $(gxstate_h)\
 $(pxoper_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxgstate.c $(PXLO_)pxgstate.$(OBJ)

$(PXLOBJ)pximage.$(OBJ): $(PXLSRC)pximage.c $(AK) $(std_h)\
 $(gscoord_h) $(gsimage_h) $(gspaint_h) $(gspath_h) $(gspath2_h)\
 $(gsrefct_h) $(gsrop_h) $(gsstate_h) $(gsstruct_h) $(gsuid_h) $(gsutil_h)\
 $(gxbitmap_h) $(gxcspace_h) $(gxdevice_h) $(gxpcolor_h)\
 $(scommon_h) $(srlx_h) $(strimpl_h)\
 $(pldraw_h)\
 $(pxerrors_h) $(pxoper_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pximage.c $(PXLO_)pximage.$(OBJ)

$(PXLOBJ)pxink.$(OBJ): $(PXLSRC)pxink.c $(math__h) $(stdio__h)\
 $(gscolor2_h) $(gscoord_h) $(gsimage_h) $(gsmemory_h) $(gspath_h)\
 $(gstypes_h)\
 $(gxarith_h) $(gxcspace_h) $(gxdevice_h) $(gxht_h) $(gxstate_h)\
 $(pxoper_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxink.c $(PXLO_)pxink.$(OBJ)

$(PXLOBJ)pxpaint.$(OBJ): $(PXLSRC)pxpaint.c $(AK) $(math__h) $(stdio__h)\
 $(gscoord_h) $(gspaint_h) $(gspath_h) $(gspath2_h) $(gsrop_h) $(gsstate_h)\
 $(gxfarith_h) $(gxfixed_h) $(gxistate_h) $(gxmatrix_h) $(gxpath_h)\
 $(pxfont_h) $(pxoper_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxpaint.c $(PXLO_)pxpaint.$(OBJ)

$(PXLOBJ)pxsessio.$(OBJ): $(PXLSRC)pxsessio.c $(AK) $(math__h) $(stdio__h)\
 $(gschar_h) $(gscoord_h) $(gserrors_h) $(gspaint_h) $(gsparam_h) $(gsstate_h)\
 $(gxfcache_h) $(gxfixed_h)\
 $(pxfont_h) $(pxoper_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxsessio.c $(PXLO_)pxsessio.$(OBJ)

$(PXLOBJ)pxstream.$(OBJ): $(PXLSRC)pxstream.c $(AK) $(memory__h)\
 $(gsmemory_h) $(scommon_h)\
 $(pxoper_h) $(pxparse_h) $(pxstate_h)
	$(PXLCCC) $(PXLSRC)pxstream.c $(PXLO_)pxstream.$(OBJ)

# We have to break up pxl_ops because of the MS-DOS command line
# limit of 120 characters.
pxl_ops_obj1=$(PXLOBJ)pxffont.$(OBJ) $(PXLOBJ)pxfont.$(OBJ) $(PXLOBJ)pxgstate.$(OBJ) $(PXLOBJ)pximage.$(OBJ)
pxl_ops_obj2=$(PXLOBJ)pxink.$(OBJ) $(PXLOBJ)pxpaint.$(OBJ) $(PXLOBJ)pxsessio.$(OBJ) $(PXLOBJ)pxstream.$(OBJ)
pxl_ops_obj=$(pxl_ops_obj1) $(pxl_ops_obj2)

# Note that we must initialize pxfont before pxerrors.
$(PXLOBJ)pxl.dev: $(PXL_MAK) $(ECHOGS_XE) $(pxl_other_obj) $(pxl_ops_obj)\
 $(PLOBJ)pl.dev $(PLOBJ)pjl.dev
	$(SETMOD) $(PXLOBJ)pxl $(pxl_other_obj1)
	$(ADDMOD) $(PXLOBJ)pxl $(pxl_other_obj2)
	$(ADDMOD) $(PXLOBJ)pxl $(pxl_ops_obj1)
	$(ADDMOD) $(PXLOBJ)pxl $(pxl_ops_obj2)
	$(ADDMOD) $(PXLOBJ)pxl -include $(PLOBJ)pl $(PLOBJ)pjl
	$(ADDMOD) $(PXLOBJ)pxl -init pxfont pxerrors

