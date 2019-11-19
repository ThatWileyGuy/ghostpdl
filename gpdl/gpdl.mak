# Copyright (C) 2001-2019 Artifex Software, Inc.
# All Rights Reserved.
#
# This software is provided AS-IS with no warranty, either express or
# implied.
#
# This software is distributed under license and may not be copied,
# modified or distributed except as expressly authorized under the terms
# of the license contained in the file LICENSE in this distribution.
#
# Refer to licensing information at http://www.artifex.com or contact
# Artifex Software, Inc.,  1305 Grant Avenue - Suite 200, Novato,
# CA 94945, U.S.A., +1(415)492-9861, for further information.

# Define the name of this makefile.
GPDL_MAK=$(GPDLSRCDIR)$(D)gpdl.mak

GPDLSRC=$(GPDLSRCDIR)$(D)
GPDLPSISRC=$(GPDLSRCDIR)$(D)psi$(D)
GPDLURFSRC=$(URFSRCDIR)$(D)

GPDLOBJ=$(GPDLOBJDIR)$(D)
GPDLGEN=$(GPDLGENDIR)$(D)
GPDLO_=$(O_)$(GPDLOBJ)
GLGEN=$(GLGENDIR)$(D)

GPDL_PSI_TOP_OBJ_FILE=psitop.$(OBJ)
GPDL_PSI_TOP_OBJ=$(GPDLOBJ)/$(GPDL_PSI_TOP_OBJ_FILE)

GPDL_URF_TOP_OBJ_FILE=urftop.$(OBJ)

GPDL_JPG_TOP_OBJ_FILE=jpgtop.$(OBJ)
GPDL_JPG_TOP_OBJ=$(GPDLOBJ)/$(GPDL_JPG_TOP_OBJ_FILE)

GPDL_PWG_TOP_OBJ_FILE=pwgtop.$(OBJ)
GPDL_PWG_TOP_OBJ=$(GPDLOBJ)/$(GPDL_PWG_TOP_OBJ_FILE)

GPDL_PSI_TOP_OBJS=\
	$(GPDL_PWG_TOP_OBJ)\
	$(GPDL_JPG_TOP_OBJ)\
	$(GPDL_URF_TOP_OBJ)\
	$(GPDL_PSI_TOP_OBJ)\
	$(GPDLOBJ)gpdlimpl.$(OBJ)

LANG_CFLAGS=\
	$(D_)PCL_INCLUDED$(_D)\
	$(D_)PSI_INCLUDED$(_D)\
	$(D_)XPS_INCLUDED$(_D)\
	$(ENABLE_URF)\
	$(D_)JPG_INCLUDED$(_D)\
	$(D_)PWG_INCLUDED$(_D)

GPDLCC=$(CC_) $(LANG_CFLAGS) $(I_)$(PSSRCDIR)$(_I) $(I_)$(PLSRCDIR)$(_I) $(I_)$(GLSRCDIR)$(_I) $(I_)$(DEVSRCDIR)$(_I) $(I_)$(GLGENDIR)$(_I) $(C_)

$(GPDLGEN)gpdlimpl.c: $(PLSRC)plimpl.c $(MAKEDIRS)
	$(CP_) $(PLSRC)plimpl.c $(GPDLGEN)gpdlimpl.c

$(GPDLOBJ)gpdlimpl.$(OBJ): $(GPDLGEN)gpdlimpl.c          \
                        $(AK)                       \
                        $(memory__h)                \
                        $(scommon_h)                \
                        $(gxdevice_h)               \
                        $(pltop_h)
	$(GPDLCC) $(GPDLGEN)gpdlimpl.c $(GPDLO_)gpdlimpl.$(OBJ)


$(GPDL_PSI_TOP_OBJ): $(GPDLSRC)psitop.c $(AK) $(stdio__h)\
 $(string__h) $(gdebug_h) $(gp_h) $(gsdevice_h) $(gserrors_h) $(gsmemory_h)\
 $(gsstate_h) $(gsstruct_h) $(gspaint_h) $(gstypes_h) $(gxalloc_h) $(gxstate_h)\
 $(gsnogc_h) $(pltop_h) $(psitop_h) $(plparse_h) $(gsicc_manage_h)\
 $(plfont_h) $(uconfig_h)
	$(GPDLCC) $(GPDLSRC)psitop.c $(GPDLO_)$(GPDL_PSI_TOP_OBJ_FILE)

# Note that we don't use $(GPDL_URF_TOP_OBJ) as the target of the
# next make rule, as this expands to "" in builds that don't use
# URF.
$(GPDLOBJ)/$(GPDL_URF_TOP_OBJ_FILE): $(GPDLURFSRC)urftop.c $(AK)\
 $(gxdevice_h) $(gserrors_h) $(gsstate_h) $(surfx_h) $(strimpl_h)\
 $(gscoord_h) $(pltop_h)
	$(GPDLCC) $(GPDLURFSRC)urftop.c $(GPDLO_)$(GPDL_URF_TOP_OBJ_FILE)

$(GPDL_JPG_TOP_OBJ): $(GPDLSRC)jpgtop.c $(AK)\
 $(gxdevice_h) $(gserrors_h) $(gsstate_h) $(strimpl_h) $(gscoord_h)\
 $(jpeglib_h) $(setjmp__h) $(sjpeg_h) $(pltop_h)
	$(GPDLCC) $(II)$(JI_)$(_I) $(GPDLSRC)jpgtop.c $(GPDLO_)$(GPDL_JPG_TOP_OBJ_FILE)

$(GPDL_PWG_TOP_OBJ): $(GPDLSRC)pwgtop.c $(AK)\
 $(gxdevice_h) $(gserrors_h) $(gsstate_h) $(strimpl_h) $(gscoord_h)\
 $(spwgx_h) $(pltop_h)
	$(GPDLCC) $(GPDLSRC)pwgtop.c $(GPDLO_)$(GPDL_PWG_TOP_OBJ_FILE)
