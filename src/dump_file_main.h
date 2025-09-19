#pragma once

#include <gtkwave.h>

void set_common_settings(GwLoader *loader);

GwDumpFile *vcd_recoder_main(char *fname);
GwDumpFile *ghw_main(char *fname);
GwDumpFile *fst_main(char *fname, char *skip_start, char *skip_end);