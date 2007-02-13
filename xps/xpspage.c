#include "ghostxps.h"

int xps_parse_glyphs(xps_context_t *ctx, xps_item_t *node)
{
    dputs("Ooo some text!\n");
    return 0;
}

int xps_parse_canvas(xps_context_t *ctx, xps_item_t *root)
{
    xps_item_t *node;

    dputs("Begin Canvas!\n");

    if (xps_att(root, "RenderTransform"))
	dputs("  canvas att render transform\n");
    if (xps_att(root, "Clip"))
	dputs("  canvas att clip\n");
    if (xps_att(root, "Opacity"))
	dputs("  canvas att opacity\n");
    if (xps_att(root, "OpacityMask"))
	dputs("  canvas att opacity mask\n");

    for (node = xps_down(root); node; node = xps_next(node))
    {
	if (!strcmp(xps_tag(node), "Canvas.Resources"))
	    dputs("  canvas resources\n");

	if (!strcmp(xps_tag(node), "Canvas.RenderTransform"))
	    dputs("  canvas render transform\n");

	if (!strcmp(xps_tag(node), "Canvas.Clip"))
	    dputs("  canvas clip\n");

	if (!strcmp(xps_tag(node), "Canvas.OpacityMask"))
	    dputs("  canvas opacity mask\n");

	if (!strcmp(xps_tag(node), "Path"))
	    xps_parse_path(ctx, node);
	if (!strcmp(xps_tag(node), "Glyphs"))
	    xps_parse_glyphs(ctx, node);
	if (!strcmp(xps_tag(node), "Canvas"))
	    xps_parse_canvas(ctx, node);
    }

    dputs("Finish Canvas!\n");

    return 0;
}

int
xps_parse_fixed_page(xps_context_t *ctx, xps_part_t *part)
{
    xps_item_t *root, *node;
    char *width, *height;
    int code;

    dprintf1("milestone: processing page %s\n", part->name);

    root = xps_parse_xml(ctx, part->data, part->size);
    if (!root)
	return gs_rethrow(-1, "cannot parse xml");

    if (strcmp(xps_tag(root), "FixedPage"))
	return gs_throw1(-1, "expected FixedPage element (found %s)", xps_tag(root));

    width = xps_att(root, "Width");
    height = xps_att(root, "Height");

    if (!width)
	return gs_throw(-1, "FixedPage missing required attribute: Width");
    if (!height)
	return gs_throw(-1, "FixedPage missing required attribute: Height");

    dprintf2("FixedPage width=%d height=%d\n", atoi(width), atoi(height));

    /* Setup new page */
    {
	gs_memory_t *mem = ctx->memory;
	gs_state *pgs = ctx->pgs;
	gx_device *dev = gs_currentdevice(pgs);
	gs_param_float_array fa;
	float fv[2];
	gs_c_param_list list;

	gs_c_param_list_write(&list, mem);
	fv[0] = atoi(width) / 96.0 * 72.0;
	fv[1] = atoi(height) / 96.0 * 72.0;
	fa.size = 2;

	code = param_write_float_array((gs_param_list *)&list, ".MediaSize", &fa);
	gs_c_param_list_write(&list, mem);
	if ( code >= 0 )
	{ 
	    gs_c_param_list_read(&list);
	    code = gs_putdeviceparams(dev, (gs_param_list *)&list);
	}
	gs_c_param_list_release(&list);

	/* nb this is for the demo it is wrong and should be removed */
	gs_initgraphics(pgs);

	/* 96 dpi default - and put the origin at the top of the page */

	gs_initmatrix(pgs);

	code = gs_scale(pgs, 72.0/96.0, -72.0/96.0);
	if (code < 0)
	    return gs_rethrow(code, "cannot set page transform");

	code = gs_translate(pgs, 0.0, -atoi(height));
	if (code < 0)
	    return gs_rethrow(code, "cannot set page transform");

	code = gs_erasepage(pgs);
	if (code < 0)
	    return gs_rethrow(code, "cannot clear page");
    }

    /* Draw contents */

    for (node = xps_down(root); node; node = xps_next(node))
    {
	if (!strcmp(xps_tag(node), "FixedPage.Resources"))
	    dputs("FixedPage has resources\n");

	if (!strcmp(xps_tag(node), "Path"))
	    xps_parse_path(ctx, node);

	if (!strcmp(xps_tag(node), "Glyphs"))
	    xps_parse_glyphs(ctx, node);

	if (!strcmp(xps_tag(node), "Canvas"))
	    xps_parse_canvas(ctx, node);
    }

    /* Flush page */
    {
	code = xps_show_page(ctx, 1, true); /* copies, flush */
	if (code < 0)
	    return gs_rethrow(code, "cannot flush page");
    }

    xps_free_item(ctx, root);

    return 0;
}

