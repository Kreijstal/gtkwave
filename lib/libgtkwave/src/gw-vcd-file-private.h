#pragma once

struct _GwVcdFile
{
    GwDumpFile parent_instance;

    gboolean preserve_glitches;
    gboolean preserve_glitches_real;

    GwVlist *time_vlist;
    gboolean is_prepacked;

    GwTime start_time;
    GwTime end_time;

    GwHistEntFactory *hist_ent_factory;
};

void gw_vcd_file_import_trace_scalar(GwVcdFile *self,
                                     GwNode *np,
                                     GwNodeHistory *history,
                                     GwVlistReader *reader);
void gw_vcd_file_import_trace_vector(GwVcdFile *self,
                                     GwNode *np,
                                     GwNodeHistory *history,
                                     GwVlistReader *reader,
                                     guint32 len);
void gw_vcd_file_import_trace_real(GwVcdFile *self, GwNodeHistory *history, GwVlistReader *reader);
void gw_vcd_file_import_trace_string(GwVcdFile *self, GwNodeHistory *history, GwVlistReader *reader);


// The unit separator control character is used to represent the hierarchy
// delimiter internally.
#define VCD_HIERARCHY_DELIMITER '\x1F'
#define VCD_HIERARCHY_DELIMITER_STR "\x1F"