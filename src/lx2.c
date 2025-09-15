/*
 * Copyright (c) Tony Bybell 2003-2012.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "globals.h"
#include <config.h>
#include <stdio.h>
#include "lx2.h"

#include <unistd.h>

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include "symbol.h"
#include "vcd.h"
#include "debug.h"
#include "busy.h"
#include "hierpack.h"
#include "gw-vcd-file.h"
#include "gw-fst-file.h"

/*
 * actually import an lx2 trace but don't do it if it's already been imported
 */
void import_lx2_trace(GwNode *np)
{
    GwNode *nodes[1] = {np};
    switch (GLOBALS->is_lx2) {
        case LXT2_IS_VLIST:
        case LXT2_IS_FST:
            gw_dump_file_import_traces(GLOBALS->dump_file, nodes, NULL);
            return;
        default:
            g_return_if_reached();
            break;
    }
}

/*
 * pre-import many traces at once so function above doesn't have to iterate...
 */
void lx2_set_fac_process_mask(GwNode *np)
{
    /* TODO: This functionality may not have a direct equivalent in modern API */
    switch (GLOBALS->is_lx2) {
        case LXT2_IS_VLIST:
        case LXT2_IS_FST:
            /* No direct equivalent in modern API - functionality may need reimplementation */
            return;
        default:
            g_return_if_reached();
            break;
    }
}

void lx2_import_masked(void)
{
    switch (GLOBALS->is_lx2) {
        case LXT2_IS_VLIST:
        case LXT2_IS_FST:
            gw_dump_file_import_all(GLOBALS->dump_file, NULL);
            return;
        default:
            g_return_if_reached();
            break;
    }
}
