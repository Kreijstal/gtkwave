#include "gw-shared-memory.h"
#include <glib.h>
#include <errno.h>

#ifdef __linux__
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

struct _GwSharedMemory {
    gchar *shm_id;
    gsize size;
    gpointer data;
    gboolean is_owner;
#ifdef __linux__
    int fd;
#endif
};

GwSharedMemory *gw_shared_memory_create(const gchar *id, gsize size)
{
    GwSharedMemory *shm = g_new0(GwSharedMemory, 1);
    shm->shm_id = g_strdup_printf("/%s", id);
    shm->size = size;
    shm->is_owner = TRUE;
    
#ifdef __linux__
    shm->fd = shm_open(shm->shm_id, O_CREAT | O_RDWR, 0666);
    if (shm->fd == -1) {
        g_warning("Failed to create shared memory segment '%s': %s",
                 shm->shm_id, g_strerror(errno));
        g_free(shm->shm_id);
        g_free(shm);
        return NULL;
    }
    
    if (ftruncate(shm->fd, size) == -1) {
        g_warning("Failed to set shared memory size: %s", g_strerror(errno));
        close(shm->fd);
        shm_unlink(shm->shm_id);
        g_free(shm->shm_id);
        g_free(shm);
        return NULL;
    }
    
    shm->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
    if (shm->data == MAP_FAILED) {
        g_warning("Failed to map shared memory: %s", g_strerror(errno));
        close(shm->fd);
        shm_unlink(shm->shm_id);
        g_free(shm->shm_id);
        g_free(shm);
        return NULL;
    }
#else
    /* Fallback to regular memory for unsupported platforms */
    shm->data = g_malloc0(size);
#endif
    
    return shm;
}

GwSharedMemory *gw_shared_memory_open(const gchar *id)
{
    GwSharedMemory *shm = g_new0(GwSharedMemory, 1);
    shm->shm_id = g_strdup_printf("/%s", id);
    shm->is_owner = FALSE;
    
#ifdef __linux__
    shm->fd = shm_open(shm->shm_id, O_RDWR, 0666);
    if (shm->fd == -1) {
        g_warning("Failed to open shared memory segment '%s': %s",
                 shm->shm_id, g_strerror(errno));
        g_free(shm->shm_id);
        g_free(shm);
        return NULL;
    }
    
    struct stat st;
    if (fstat(shm->fd, &st) == -1) {
        g_warning("Failed to get shared memory size: %s", g_strerror(errno));
        close(shm->fd);
        g_free(shm->shm_id);
        g_free(shm);
        return NULL;
    }
    
    shm->size = st.st_size;
    shm->data = mmap(NULL, shm->size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
    if (shm->data == MAP_FAILED) {
        g_warning("Failed to map shared memory: %s", g_strerror(errno));
        close(shm->fd);
        g_free(shm->shm_id);
        g_free(shm);
        return NULL;
    }
#else
    /* Fallback - not supported on this platform */
    g_warning("Shared memory not supported on this platform");
    g_free(shm->shm_id);
    g_free(shm);
    return NULL;
#endif
    
    return shm;
}

void gw_shared_memory_free(GwSharedMemory *shm)
{
    if (shm == NULL) {
        return;
    }
    
#ifdef __linux__
    if (shm->data != NULL && shm->data != MAP_FAILED) {
        munmap(shm->data, shm->size);
    }
    
    if (shm->fd != -1) {
        close(shm->fd);
    }
    
    if (shm->is_owner) {
        shm_unlink(shm->shm_id);
    }
#else
    if (shm->data != NULL) {
        g_free(shm->data);
    }
#endif
    
    g_free(shm->shm_id);
    g_free(shm);
}

gpointer gw_shared_memory_get_data(GwSharedMemory *shm)
{
    g_return_val_if_fail(shm != NULL, NULL);
    return shm->data;
}

gsize gw_shared_memory_get_size(GwSharedMemory *shm)
{
    g_return_val_if_fail(shm != NULL, 0);
    return shm->size;
}

gboolean gw_shared_memory_is_owner(GwSharedMemory *shm)
{
    g_return_val_if_fail(shm != NULL, FALSE);
    return shm->is_owner;
}