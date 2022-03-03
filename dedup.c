/* dedup - file deduplication tool by grunfink - public domain */

#define VERSION "1.03"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>

struct entry {
    char *fname;        /* file name */
    off_t size;         /* file size */
};

/* file entries */
struct entry *entries = NULL;

/* allocated entries */
int entries_sz = 0;

/* number of used entries */
int entries_n = 0;

/* minimum size of file to be tested */
off_t min_size = 16;

/* verbose flag */
int verbose = 1;

/* debugging mode flag */
int debug = 0;

/* dry run flag */
int dry_run = 0;

/* block size */
int blk_size = 1024 * 1024;


void fill_entries(char *globspec)
/* fills the entries struct recursively */
{
    glob_t globs;

    globs.gl_offs = 1;

    if (glob(globspec, GLOB_MARK, NULL, &globs) == 0) {
        int n;

        for (n = 0; globs.gl_pathv[n] != NULL; n++) {
            char *p = globs.gl_pathv[n];
            int z = strlen(p);

            if (p[z - 1] == '/') {
                /* subdirectory: glob there */
                char *sd = malloc(z + 2);
                strcpy(sd, p);
                strcat(sd, "*");

                fill_entries(sd);

                free(sd);
            }
            else {
                /* file: get size */
                struct stat st;

                if (stat(p, &st) != -1) {
                    if (st.st_size >= min_size) {
                        /* store this entry */

                        /* out of space? allocate more */
                        if (entries_n == entries_sz) {
                            entries_sz += 32;
                            entries = realloc(entries, entries_sz * sizeof(struct entry));
                        }

                        entries[entries_n].size  = st.st_size;
                        entries[entries_n].fname = strdup(p);

                        entries_n++;
                    }
                }
                else {
                    printf("ERROR: stat() error for %s\n", p);
                }
            }
        }
    }

    globfree(&globs);
}


int entry_compare(const void *va, const void *vb)
/* entry sort callback */
{
    const struct entry *ea = va;
    const struct entry *eb = vb;

    return ea->size > eb->size ? 1 : ea->size < eb->size ? -1 : 0;
}


void sort_entries(void)
/* sorts the entries by size */
{
    qsort(entries, entries_n, sizeof(struct entry), entry_compare);
}


void dispose_entry(struct entry *e)
/* frees and invalidates an entry */
{
    free(e->fname);
    e->fname = NULL;
}


void dedup_entries(void)
/* iterates the entries, deduplicating if applicable */
{
    int n;
    struct entry *fa = entries;
    char *ba;
    char *bb;

    if (debug)
        printf("DEBUG: starting deduplication of %d files\n", entries_n);

    /* allocates blocks for files a and b */
    ba = (char *)malloc(blk_size);
    bb = (char *)malloc(blk_size);

    for (n = 0; n < entries_n - 1; n++) {
        if (fa->fname) {
            struct entry *fb = fa + 1;
            struct stat sfa;

            /* get stat for file a (not expected to fail) */
            stat(fa->fname, &sfa);

            /* iterate all entries with the same size */
            while (fa->fname && fa->size == fb->size) {
                if (fb->fname) {
                    struct stat sfb;

                    /* get stat for file b */
                    stat(fb->fname, &sfb);

                    /* same device and *not* same inode? */
                    if (sfa.st_dev == sfb.st_dev && sfa.st_ino != sfb.st_ino) {
                        FILE *fda;
        
                        if (debug)
                            printf("DEBUG: comparing %s and %s\n", fa->fname, fb->fname);

                        /* open files */
                        if ((fda = fopen(fa->fname, "rb"))) {
                            FILE *fdb;

                            if ((fdb = fopen(fb->fname, "rb"))) {
                                /* compare contents */
                                int z;

                                /* read from both files */
                                while ((z = fread(ba, 1, blk_size, fda))) {
                                    fread(bb, 1, blk_size, fdb);

                                    /* different content in blocks? done */
                                    if (memcmp(ba, bb, z))
                                        break;
                                }

                                fclose(fdb);

                                /* if no more data to read, files are identical */
                                if (z == 0) {
                                    if (verbose)
                                        printf("%s -> %s\n", fa->fname, fb->fname);

                                    if (!dry_run) {
                                        if (unlink(fb->fname) == -1)
                                            printf("ERROR: unlink() error on %s\n", fb->fname);
                                        else
                                        if (link(fa->fname, fb->fname) == -1)
                                            printf("ERROR: link() error on %s\n", fb->fname);
                                    }

                                    dispose_entry(fb);
                                }
                                else
                                if (debug)
                                    printf("DEBUG: %s not identical to %s\n", fa->fname, fb->fname);
                            }
                            else {
                                printf("ERROR: cannot open %s\n", fb->fname);

                                dispose_entry(fb);
                            }

                            fclose(fda);
                        }
                        else {
                            printf("ERROR: cannot open %s\n", fa->fname);

                            dispose_entry(fa);
                        }
                    }
                }

                /* point to next file b */
                fb++;
            }
        }

        /* point to next file a */
        dispose_entry(fa);
        fa++;
    }

    free(ba);
    free(bb);
}


int usage(char *argv0)
{
    printf("dedup %s - file deduplication tool by grunfink - public domain\n\n", VERSION);

    printf("Iterates a set of files and hard-links those that are identical.\n\n");

    printf("Usage: %s [-q] [-n] [-m {min_size}] [-b {blk_size}] {files...}\n\n", argv0);

    printf("Options:\n");
    printf(" -m {min_size}          Minimum file size in bytes (default: %ld).\n", min_size);
    printf(" -b {blk_size}          Block size in bytes (default: %d).\n", blk_size);
    printf(" -q                     Be quiet (only print errors).\n");
    printf(" -n                     Dry run (print what would be done, do nothing).\n");

    return 1;
}


int main(int argc, char *argv[])
{
    int ret = 0;

    if (getenv("DEBUG"))
        debug = 1;

    if (argc == 1)
        ret = usage(argv[0]);
    else {
        int n;

        for (n = 1; n < argc; n++) {
            if (strcmp(argv[n], "-m") == 0)
                min_size = atoi(argv[++n]);
            else
            if (strcmp(argv[n], "-b") == 0)
                blk_size = atoi(argv[++n]);
            else
            if (strcmp(argv[n], "-q") == 0)
                verbose = 0;
            else
            if (strcmp(argv[n], "-n") == 0)
                dry_run = verbose = 1;
            else
                fill_entries(argv[n]);
        }

        if (entries_n == 0) {
            printf("WARN : no files\n");
            ret = 10;
        }
        else {
            sort_entries();

            dedup_entries();
        }
    }

    return ret;
}
