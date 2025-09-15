#include "gw-hierpack.h"
#include <glib.h>

struct _GwHierpackContext {
    GHashTable *compression_dict;  // Maps original names to compressed tokens
    GHashTable *decompression_dict; // Maps tokens to original names
    guint next_token_id;
    gboolean auto_enable;
    guint auto_enable_threshold;
    
    // Performance monitoring
    guint compression_count;
    guint decompression_count;
    gsize total_compressed_bytes;
    gsize total_original_bytes;
    GTimer *timer;
};

GwHierpackContext *gw_hierpack_context_new(void)
{
    GwHierpackContext *context = g_new0(GwHierpackContext, 1);
    
    context->compression_dict = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    context->decompression_dict = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    context->next_token_id = 1;
    context->auto_enable = TRUE;
    context->auto_enable_threshold = 100; // Enable compression after 100 unique names
    
    // Initialize performance monitoring
    context->compression_count = 0;
    context->decompression_count = 0;
    context->total_compressed_bytes = 0;
    context->total_original_bytes = 0;
    context->timer = g_timer_new();
    
    return context;
}

void gw_hierpack_context_free(GwHierpackContext *context)
{
    if (context != NULL) {
        if (context->compression_dict != NULL) {
            g_hash_table_destroy(context->compression_dict);
        }
        if (context->decompression_dict != NULL) {
            g_hash_table_destroy(context->decompression_dict);
        }
        if (context->timer != NULL) {
            g_timer_destroy(context->timer);
        }
        g_free(context);
    }
}

char *gw_hierpack_compress(GwHierpackContext *context, const char *name)
{
    if (!context || !name || !*name) return g_strdup(name);
    
    // Check if already compressed
    gpointer compressed = g_hash_table_lookup(context->compression_dict, name);
    if (compressed) return g_strdup(compressed);
    
    // Start timing
    g_timer_start(context->timer);
    gsize original_size = strlen(name);
    
    // Implement LZW-style hierarchical compression
    GString *output = g_string_new(NULL);
    const char *current = name;
    const char *delimiter = strchr(current, '.');
    
    while (delimiter) {
        size_t segment_len = delimiter - current;
        char *segment = g_strndup(current, segment_len);
        
        // Check if segment is already in dictionary
        gpointer token = g_hash_table_lookup(context->compression_dict, segment);
        if (token) {
            g_string_append(output, token);
        } else {
            // Add to dictionary and use token
            guint token_id = context->next_token_id++;
            char *new_token = g_strdup_printf("~%u", token_id);
            g_hash_table_insert(context->compression_dict, g_strdup(segment), new_token);
            g_hash_table_insert(context->decompression_dict, new_token, g_strdup(segment));
            g_string_append(output, new_token);
        }
        
        g_string_append_c(output, '.');
        g_free(segment);
        current = delimiter + 1;
        delimiter = strchr(current, '.');
    }
    
    // Handle the last segment
    if (*current) {
        gpointer token = g_hash_table_lookup(context->compression_dict, current);
        if (token) {
            g_string_append(output, token);
        } else {
            guint token_id = context->next_token_id++;
            char *new_token = g_strdup_printf("~%u", token_id);
            g_hash_table_insert(context->compression_dict, g_strdup(current), new_token);
            g_hash_table_insert(context->decompression_dict, new_token, g_strdup(current));
            g_string_append(output, new_token);
        }
    }
    
    char *result = g_string_free(output, FALSE);
    
    // Update performance metrics
    context->compression_count++;
    context->total_original_bytes += original_size;
    context->total_compressed_bytes += strlen(result);
    
    g_timer_stop(context->timer);
    
    return result;
}

char *gw_hierpack_decompress(GwHierpackContext *context,
                            const char *compressed_name,
                            gboolean *was_packed)
{
    if (!context || !compressed_name || !*compressed_name) {
        if (was_packed != NULL) {
            *was_packed = FALSE;
        }
        return g_strdup(compressed_name);
    }
    
    // Start timing
    g_timer_start(context->timer);
    
    // Check if this is a compressed token
    gpointer decompressed = g_hash_table_lookup(context->decompression_dict, compressed_name);
    if (decompressed) {
        if (was_packed != NULL) {
            *was_packed = TRUE;
        }
        
        // Update performance metrics
        context->decompression_count++;
        
        g_timer_stop(context->timer);
        return g_strdup(decompressed);
    }
    
    // Not a compressed token
    if (was_packed != NULL) {
        *was_packed = FALSE;
    }
    
    g_timer_stop(context->timer);
    return g_strdup(compressed_name);
}

void gw_hierpack_get_compression_stats(GwHierpackContext *context,
                                      guint *compression_count,
                                      guint *decompression_count,
                                      gsize *total_original_bytes,
                                      gsize *total_compressed_bytes,
                                      gdouble *elapsed_time)
{
    if (!context) return;
    
    if (compression_count) *compression_count = context->compression_count;
    if (decompression_count) *decompression_count = context->decompression_count;
    if (total_original_bytes) *total_original_bytes = context->total_original_bytes;
    if (total_compressed_bytes) *total_compressed_bytes = context->total_compressed_bytes;
    if (elapsed_time) *elapsed_time = g_timer_elapsed(context->timer, NULL);
}

gdouble gw_hierpack_get_compression_ratio(GwHierpackContext *context)
{
    if (!context || context->total_compressed_bytes == 0) {
        return 1.0;
    }
    
    return (gdouble)context->total_original_bytes / (gdouble)context->total_compressed_bytes;
}

void gw_hierpack_reset_stats(GwHierpackContext *context)
{
    if (!context) return;
    
    context->compression_count = 0;
    context->decompression_count = 0;
    context->total_original_bytes = 0;
    context->total_compressed_bytes = 0;
    g_timer_start(context->timer);
}