/*
 * Copyright (c) 2024 GTKWave Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files ( the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "gw-vcd-loader.h" // We inherit from this
#include "gw-shared-memory.h"
#include "gw-dump-file.h"

G_BEGIN_DECLS

#define GW_TYPE_VCD_PARTIAL_LOADER (gw_vcd_partial_loader_get_type())
G_DECLARE_FINAL_TYPE(GwVcdPartialLoader, gw_vcd_partial_loader, GW, VCD_PARTIAL_LOADER, GwVcdLoader)

GwVcdPartialLoader *gw_vcd_partial_loader_new(void);

// This is now the main entry point, replacing the GwLoader vfunc
GwDumpFile *gw_vcd_partial_loader_load(GwVcdPartialLoader *self, const gchar *shm_id, GError **error);

void gw_vcd_partial_loader_kick(GwVcdPartialLoader *self);
void gw_vcd_partial_loader_cleanup(GwVcdPartialLoader *self);
gboolean gw_vcd_partial_loader_is_header_parsed(GwVcdPartialLoader *self);
void gw_vcd_partial_loader_update_time_range(GwVcdPartialLoader *self, GwDumpFile *dump_file);


G_END_DECLS