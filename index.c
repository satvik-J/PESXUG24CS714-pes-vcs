// index.c — Staging area implementation

#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

// ─── IMPLEMENTATION ─────────────────────────────────────────────────────────

// 🔹 LOAD INDEX
int index_load(Index *index) {
    FILE *f = fopen(".pes/index", "r");

    // ✅ IMPORTANT FIX: if file doesn't exist → empty index
    if (!f) {
        index->count = 0;
        return 0;
    }

    index->count = 0;

    while (!feof(f)) {
        IndexEntry entry;
        char hash_hex[65];

        int res = fscanf(f, "%o %64s %ld %ld %s\n",
                         &entry.mode,
                         hash_hex,
                         &entry.mtime_sec,
                         &entry.size,
                         entry.path);

        if (res != 5) break;

        hex_to_hash(hash_hex, &entry.id);

        index->entries[index->count++] = entry;
    }

    fclose(f);
    return 0;
}

// 🔹 SAVE INDEX (ATOMIC)
int compare_entries(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path, ((IndexEntry*)b)->path);
}

int index_save(const Index *index) {
    // Sort entries
    Index temp = *index;
    qsort(temp.entries, temp.count, sizeof(IndexEntry), compare_entries);

    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) return -1;

    for (int i = 0; i < temp.count; i++) {
        char hash_hex[65];
        hash_to_hex(&temp.entries[i].id, hash_hex);

        fprintf(f, "%o %s %ld %ld %s\n",
                temp.entries[i].mode,
                hash_hex,
                temp.entries[i].mtime_sec,
                temp.entries[i].size,
                temp.entries[i].path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    rename(".pes/index.tmp", ".pes/index");

    return 0;
}

// 🔹 ADD FILE
int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        perror("stat");
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    char *buffer = malloc(st.st_size);
    if (!buffer) {
        fclose(f);
        return -1;
    }

    fread(buffer, 1, st.st_size, f);
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, buffer, st.st_size, &id) != 0) {
        free(buffer);
        return -1;
    }

    free(buffer);

    IndexEntry *existing = index_find(index, path);

    if (existing) {
        existing->id = id;
        existing->mtime_sec = st.st_mtime;
        existing->size = st.st_size;
        existing->mode = st.st_mode;
    } else {
        IndexEntry entry;
        entry.id = id;
        entry.mtime_sec = st.st_mtime;
        entry.size = st.st_size;
        entry.mode = st.st_mode;
        strcpy(entry.path, path);

        index->entries[index->count++] = entry;
    }

    return index_save(index);
}
