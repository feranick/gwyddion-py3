/*
 *  $Id: gwyzip.h 24727 2022-03-23 15:45:48Z yeti-dn $
 *  Copyright (C) 2015-2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#ifndef __GWY_FILE_MINIZIP_H__
#define __GWY_FILE_MINIZIP_H__

#include "config.h"
#include <glib.h>

/* You can include this header even if no ZIP library is available and then check if HAVE_GWYZIP is defined.  When it
 * is defined then you actually have GwyZip. */
#undef HAVE_GWYZIP

struct _GwyZipFile;

typedef struct _GwyZipFile *GwyZipFile;

/* This is the interface we implement. */
static GwyZipFile gwyzip_open                (const gchar *path,
                                              GError **error);
static void       gwyzip_close               (GwyZipFile zipfile);
static gboolean   gwyzip_first_file          (GwyZipFile zipfile,
                                              GError **error);
static gboolean   gwyzip_next_file           (GwyZipFile zipfile,
                                              GError **error);
static gboolean   gwyzip_get_current_filename(GwyZipFile zipfile,
                                              gchar **filename,
                                              GError **error);
static gboolean   gwyzip_locate_file         (GwyZipFile zipfile,
                                              const gchar *filename,
                                              gint casesens,
                                              GError **error);
/* @contentsize may be NULL */
static guchar*    gwyzip_get_file_content    (GwyZipFile zipfile,
                                              gsize *contentsize,
                                              GError **error);

/****************************************************************************
 *
 * Minizip 1.x wrapper
 *
 ****************************************************************************/
#ifdef HAVE_MINIZIP1
#define HAVE_GWYZIP 1
#include <errno.h>
#include <unzip.h>

struct _GwyZipFile {
    unzFile *unzfile;
    guint index;
};

#ifdef G_OS_WIN32
G_GNUC_UNUSED
static voidpf
gwyminizip_open_file_func(G_GNUC_UNUSED voidpf opaque,
                          const char* filename,
                          G_GNUC_UNUSED int mode)
{
    /* Don't implement other modes.  We never write ZIP files with minizip. */
    return (voidpf)gwy_fopen(filename, "rb");
}

G_GNUC_UNUSED
static uLong
gwyminizip_read_file_func(G_GNUC_UNUSED voidpf opaque,
                          voidpf stream,
                          void* buf,
                          uLong size)
{
    return fread(buf, 1, size, (FILE*)stream);
}

G_GNUC_UNUSED
static uLong
gwyminizip_write_file_func(G_GNUC_UNUSED voidpf opaque,
                           G_GNUC_UNUSED voidpf stream,
                           G_GNUC_UNUSED const void* buf,
                           G_GNUC_UNUSED uLong size)
{
    /* Don't implement writing.  We never write ZIP files with minizip. */
    errno = ENOSYS;
    return 0;
}

G_GNUC_UNUSED
static int
gwyminizip_close_file_func(G_GNUC_UNUSED voidpf opaque,
                           voidpf stream)
{
    return fclose((FILE*)stream);
}

G_GNUC_UNUSED
static int
gwyminizip_testerror_file_func(G_GNUC_UNUSED voidpf opaque,
                               voidpf stream)
{
    return ferror((FILE*)stream);
}

G_GNUC_UNUSED
static long
gwyminizip_tell_file_func(G_GNUC_UNUSED voidpf opaque,
                          voidpf stream)
{
    return ftell((FILE*)stream);
}

G_GNUC_UNUSED
static long
gwyminizip_seek_file_func(G_GNUC_UNUSED voidpf opaque,
                          voidpf stream,
                          uLong offset,
                          int origin)
{
    return fseek((FILE*)stream, offset, origin);
}

G_GNUC_UNUSED
static GwyZipFile
gwyzip_open(const gchar *path, GError **error)
{
    static zlib_filefunc_def ffdef = {
        gwyminizip_open_file_func,
        gwyminizip_read_file_func,
        gwyminizip_write_file_func,
        gwyminizip_tell_file_func,
        gwyminizip_seek_file_func,
        gwyminizip_close_file_func,
        gwyminizip_testerror_file_func,
        NULL,
    };
    struct _GwyZipFile *zipfile;
    unzFile *unzfile;

    if (!(unzfile = unzOpen2(path, &ffdef))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("%s cannot open the file as a ZIP file."), "Minizip");
        return NULL;
    }

    zipfile = g_new0(struct _GwyZipFile, 1);
    zipfile->unzfile = unzfile;
    return zipfile;
}
#else
G_GNUC_UNUSED
static GwyZipFile
gwyzip_open(const gchar *path, GError **error)
{
    struct _GwyZipFile *zipfile;
    unzFile *unzfile;

    if (!(unzfile = unzOpen(path))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("%s cannot open the file as a ZIP file."), "Minizip");
        return NULL;
    }

    zipfile = g_new0(struct _GwyZipFile, 1);
    zipfile->unzfile = unzfile;
    return zipfile;
}
#endif

G_GNUC_UNUSED
static gboolean
err_MINIZIP(gint status, GError **error)
{
    const gchar *errstr = _("Unknown error");

    if (status == UNZ_ERRNO)
        errstr = g_strerror(errno);
    else if (status == UNZ_EOF)
        errstr = _("End of file");
    else if (status == UNZ_END_OF_LIST_OF_FILE)
        errstr = _("End of list of files");
    else if (status == UNZ_PARAMERROR)
        errstr = _("Parameter error");
    else if (status == UNZ_BADZIPFILE)
        errstr = _("Bad zip file");
    else if (status == UNZ_INTERNALERROR)
        errstr = _("Internal error");
    else if (status == UNZ_CRCERROR)
        errstr = _("CRC error");

    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                _("%s error while reading the zip file: %s (%d)."), "Minizip", errstr, status);
    return FALSE;
}

G_GNUC_UNUSED
static void
gwyzip_close(GwyZipFile zipfile)
{
    unzClose(zipfile->unzfile);
    g_free(zipfile);
}

G_GNUC_UNUSED
static gboolean
gwyzip_next_file(GwyZipFile zipfile, GError **error)
{
    gint status;
    if ((status = unzGoToNextFile(zipfile->unzfile)) == UNZ_OK) {
        zipfile->index++;
        return TRUE;
    }
    err_MINIZIP(status, error);
    return FALSE;
}

G_GNUC_UNUSED
static gboolean
gwyzip_first_file(GwyZipFile zipfile, GError **error)
{
    gint status;
    if ((status = unzGoToFirstFile(zipfile->unzfile)) == UNZ_OK) {
        zipfile->index = 0;
        return TRUE;
    }
    err_MINIZIP(status, error);
    return FALSE;
}

G_GNUC_UNUSED
static gboolean
gwyzip_get_current_filename(GwyZipFile zipfile, gchar **filename, GError **error)
{
    unz_file_info fileinfo;
    gint status;
    gchar *filename_buf;
    guint size = 256;

    /* unzGetCurrentFileInfo() nul-terminates the string if we pass longer buffer than actual file name length - which
     * is only uint16. */
    filename_buf = g_new(gchar, size+1);
    status = unzGetCurrentFileInfo(zipfile->unzfile, &fileinfo, filename_buf, size, NULL, 0, NULL, 0);
    if (status == UNZ_OK && fileinfo.size_filename > size) {
        g_free(filename_buf);
        size = fileinfo.size_filename;
        filename_buf = g_new(gchar, size+1);
        status = unzGetCurrentFileInfo(zipfile->unzfile, &fileinfo, filename_buf, size, NULL, 0, NULL, 0);
    }
    if (status != UNZ_OK) {
        g_free(filename_buf);
        *filename = NULL;
        err_MINIZIP(status, error);
        return FALSE;
    }
    filename_buf[size] = '\0';
    *filename = filename_buf;
    return TRUE;
}

G_GNUC_UNUSED
static gboolean
gwyzip_locate_file(GwyZipFile zipfile, const gchar *filename, gint casesens, GError **error)
{
    gwy_debug("calling unzLocateFile() to find %s", filename);
    if (unzLocateFile(zipfile->unzfile, filename, casesens) != UNZ_OK) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("File %s is missing in the zip file."), filename);
        return FALSE;
    }
    return TRUE;
}

G_GNUC_UNUSED
static guchar*
gwyzip_get_file_content(GwyZipFile zipfile, gsize *contentsize, GError **error)
{
    unz_file_info fileinfo;
    guchar *buffer;
    gulong size;
    glong readbytes;
    gint status;

    gwy_debug("calling unzGetCurrentFileInfo() to figure out buffer size");
    status = unzGetCurrentFileInfo(zipfile->unzfile, &fileinfo, NULL, 0, NULL, 0, NULL, 0);
    if (status != UNZ_OK) {
        err_MINIZIP(status, error);
        return NULL;
    }

    gwy_debug("calling unzGetCurrentFileInfo()");
    status = unzOpenCurrentFile(zipfile->unzfile);
    if (status != UNZ_OK) {
        err_MINIZIP(status, error);
        return NULL;
    }

    size = fileinfo.uncompressed_size;
    gwy_debug("uncompressed_size: %lu", size);
    buffer = g_new(guchar, size + 1);
    gwy_debug("calling unzReadCurrentFile()");
    readbytes = unzReadCurrentFile(zipfile->unzfile, buffer, size);
    if (readbytes != size) {
        err_MINIZIP(status, error);
        unzCloseCurrentFile(zipfile->unzfile);
        g_free(buffer);
        return NULL;
    }
    gwy_debug("calling unzCloseCurrentFile()");
    unzCloseCurrentFile(zipfile->unzfile);

    buffer[size] = '\0';
    if (contentsize)
        *contentsize = size;
    return buffer;
}

#endif

/****************************************************************************
 *
 * Minizip 2.x (3.x, etc.) wrapper
 *
 ****************************************************************************/
#ifdef HAVE_MINIZIP2
#define HAVE_GWYZIP 1
/* This either includes stdint.h or replicates its typedefs. */
#if defined(HAVE_MINIZIP_MZ_H)
#include <minizip/mz.h>
#include <minizip/mz_zip.h>
#include <minizip/mz_strm.h>
#include <minizip/mz_strm_mem.h>
#elif defined(HAVE_MZ_H)
#include <mz.h>
#include <mz_zip.h>
#include <mz_strm.h>
#include <mz_strm_mem.h>
#endif
/* If we have neither things break, but then I looked everywhere and I still have no fucking idea where minizip put
 * its headers this time. */

struct _GwyZipFile {
    GMappedFile *mfile;
    void *mem_stream;
    void *zip_handle;
};

G_GNUC_UNUSED
static void
gwyminizip_free(GwyZipFile zipfile)
{
    if (zipfile->zip_handle) {
        mz_zip_close(zipfile->zip_handle);
        mz_zip_delete(&zipfile->zip_handle);
    }
    if (zipfile->mem_stream)
        mz_stream_mem_delete(&zipfile->mem_stream);
    if (zipfile->mfile)
        g_mapped_file_free(zipfile->mfile);
    g_free(zipfile);
}

G_GNUC_UNUSED
static void
err_MINIZIP(int32_t status, GError **error)
{
    gchar buf[12];

    g_snprintf(buf, sizeof(buf), -status);
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                _("%s error while reading the zip file: %s."), "Minizip", buf);
}

/* We do not want minizip doing I/O itself, in particular in MS Windows.  Just mmap the input file and give the
 * library a memory stream... */
G_GNUC_UNUSED
static GwyZipFile
gwyzip_open(const gchar *path, GError **error)
{
    struct _GwyZipFile *zipfile;
    GError *err = NULL;
    int32_t status;

    zipfile = g_new0(struct _GwyZipFile, 1);

    if (!(zipfile->mfile = g_mapped_file_new(path, FALSE, &err))) {
        /* err_GET_FILE_CONTENTS() */
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("Cannot read file contents: %s"), err->message);
        g_clear_error(&err);
        gwyminizip_free(zipfile);
        return NULL;
    }

    mz_zip_create(&zipfile->zip_handle);
    mz_stream_mem_create(&zipfile->mem_stream);
    mz_stream_mem_set_buffer(zipfile->mem_stream,
                             g_mapped_file_get_contents(zipfile->mfile),
                             g_mapped_file_get_length(zipfile->mfile));
    mz_stream_open(zipfile->mem_stream, NULL, MZ_OPEN_MODE_READ);
    status = mz_zip_open(zipfile->zip_handle, zipfile->mem_stream, MZ_OPEN_MODE_READ);
    if (status != MZ_OK) {
        err_MINIZIP(status, error);
        gwyminizip_free(zipfile);
        return NULL;
    }

    return zipfile;
}

G_GNUC_UNUSED
static void
gwyzip_close(GwyZipFile zipfile)
{
    gwyminizip_free(zipfile);
}

G_GNUC_UNUSED
static gboolean
gwyzip_next_file(GwyZipFile zipfile, GError **error)
{
    int32_t status;
    if ((status = mz_zip_goto_next_entry(zipfile->zip_handle)) == MZ_OK)
        return TRUE;
    err_MINIZIP(status, error);
    return FALSE;
}

G_GNUC_UNUSED
static gboolean
gwyzip_first_file(GwyZipFile zipfile, GError **error)
{
    int32_t status;
    if ((status = mz_zip_goto_first_entry(zipfile->zip_handle)) == MZ_OK)
        return TRUE;
    err_MINIZIP(status, error);
    return FALSE;
}

/* Pass non-NULL for the attributes you are interested in...
 * XXX: I am not sure if file_info is guaranteed to survive closing the entry, so I am not going to try using it
 * afterwards.  */
G_GNUC_UNUSED
static gboolean
gwyminizip_get_common_file_info(GwyZipFile zipfile, gchar **filename, gsize *uncomp_size, GError **error)
{
    mz_zip_file *file_info = NULL;
    int32_t status;

    status = mz_zip_entry_read_open(zipfile->zip_handle, 0, NULL);
    if (status != MZ_OK) {
        err_MINIZIP(status, error);
        return FALSE;
    }

    status = mz_zip_entry_get_info(zipfile->zip_handle, &file_info);
    if (status != MZ_OK) {
        err_MINIZIP(status, error);
        mz_zip_entry_close(zipfile->zip_handle);
        return FALSE;
    }

    if (filename)
        *filename = g_strdup(file_info->filename);
    if (uncomp_size)
        *uncomp_size = file_info->uncompressed_size;

    /* No minizip code example seems to free file_info in any manner.  So hopefully it points to static storage... */

    return TRUE;
}

/* This function returns the file name as UTF-8.  The others probably don't. */
G_GNUC_UNUSED
static gboolean
gwyzip_get_current_filename(GwyZipFile zipfile, gchar **filename, GError **error)
{
    gchar *fnm;

    if (gwyminizip_get_common_file_info(zipfile, &fnm, NULL, error)) {
        *filename = fnm;
        return TRUE;
    }
    *filename = NULL;
    return FALSE;
}

G_GNUC_UNUSED
static gboolean
gwyzip_locate_file(GwyZipFile zipfile, const gchar *filename, gint casesens, GError **error)
{
    int32_t status;

    /* Negate the last argument; it is called ignore_case. */
    status = mz_zip_locate_entry(zipfile->zip_handle, filename, !casesens);
    if (status != MZ_OK) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("File %s is missing in the zip file."), filename);
        return FALSE;
    }
    return TRUE;
}

G_GNUC_UNUSED
static guchar*
gwyzip_get_file_content(GwyZipFile zipfile, gsize *contentsize, GError **error)
{
    gsize uncsize, remsize;
    guchar *buffer;
    int32_t status;

    if (!gwyminizip_get_common_file_info(zipfile, NULL, &uncsize, error))
        return NULL;

    status = mz_zip_entry_read_open(zipfile->zip_handle, 0, NULL);
    if (status != MZ_OK) {
        err_MINIZIP(status, error);
        return NULL;
    }

    buffer = g_new(guchar, uncsize + 1);
    remsize = uncsize;
    while (remsize) {
        int32_t bytes_to_read = (remsize > (guint)G_MAXINT32 ? G_MAXINT32 : remsize);
        status = mz_zip_entry_read(zipfile->zip_handle, buffer + (uncsize - remsize), bytes_to_read);
        /* When positive, the return value it is the number of bytes read. Otherwise an error code?  Anyway, do not
         * accept zero. */
        if (status <= 0) {
            err_MINIZIP(status, error);
            g_free(buffer);
            return NULL;
        }
        if (status > bytes_to_read) {
            /* XXX: What to do when the library does this? */
            err_MINIZIP(MZ_INTERNAL_ERROR, error);
            g_free(buffer);
            return NULL;
        }
        /* We read at least one byte.  So proceed. */
        remsize -= status;
    }
    buffer[uncsize] = '\0';
    if (contentsize)
        *contentsize = uncsize;

    mz_zip_entry_close(zipfile->zip_handle);

    return buffer;
}

#endif

/****************************************************************************
 *
 * Libzip wrapper
 *
 ****************************************************************************/
#ifdef HAVE_LIBZIP
#define HAVE_GWYZIP 1
#include <zip.h>

/* This is not defined in 0.11 yet (1.0 is required) but we can live without it. */
#ifndef ZIP_RDONLY
#define ZIP_RDONLY 0
#endif

struct _GwyZipFile {
    struct zip *archive;
    guint index;
    guint nentries;
};

G_GNUC_UNUSED
static GwyZipFile
gwyzip_open(const char *path, GError **error)
{
    struct _GwyZipFile *zipfile;
    struct zip *archive;

    if (!(archive = zip_open(path, ZIP_RDONLY, NULL))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("%s cannot open the file as a ZIP file."), "Libzip");
        return NULL;
    }

    zipfile = g_new0(struct _GwyZipFile, 1);
    zipfile->archive = archive;
    zipfile->nentries = zip_get_num_entries(archive, 0);
    return zipfile;
}

G_GNUC_UNUSED
static void
gwyzip_close(GwyZipFile zipfile)
{
    zip_close(zipfile->archive);
    g_free(zipfile);
}

G_GNUC_UNUSED
static gboolean
err_ZIP_NOFILE(GwyZipFile zipfile, GError **error)
{
    if (zipfile->index >= zipfile->nentries) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("%s error while reading the zip file: %s."), "Libzip", _("End of list of files"));
        return TRUE;
    }
    return FALSE;
}

G_GNUC_UNUSED
static void
err_ZIP(GwyZipFile zipfile, GError **error)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                _("%s error while reading the zip file: %s."), "Libzip", zip_strerror(zipfile->archive));
}

G_GNUC_UNUSED
static gboolean
gwyzip_next_file(GwyZipFile zipfile, GError **error)
{
    if (err_ZIP_NOFILE(zipfile, error))
        return FALSE;
    zipfile->index++;
    return !err_ZIP_NOFILE(zipfile, error);
}

G_GNUC_UNUSED
static gboolean
gwyzip_first_file(GwyZipFile zipfile, GError **error)
{
    zipfile->index = 0;
    return !err_ZIP_NOFILE(zipfile, error);
}

G_GNUC_UNUSED
static gboolean
gwyzip_get_current_filename(GwyZipFile zipfile, gchar **filename, GError **error)
{
    const char *filename_buf;

    if (err_ZIP_NOFILE(zipfile, error)) {
        *filename = NULL;
        return FALSE;
    }

    filename_buf = zip_get_name(zipfile->archive, zipfile->index, ZIP_FL_ENC_GUESS);
    if (filename_buf) {
        *filename = g_strdup(filename_buf);
        return TRUE;
    }

    err_ZIP(zipfile, error);
    *filename = NULL;
    return FALSE;
}

G_GNUC_UNUSED
static gboolean
gwyzip_locate_file(GwyZipFile zipfile, const gchar *filename, gint casesens, GError **error)
{
    zip_int64_t i;

    i = zip_name_locate(zipfile->archive, filename, ZIP_FL_ENC_GUESS | (casesens ? 0 : ZIP_FL_NOCASE));
    if (i == -1) {
        err_ZIP(zipfile, error);
        return FALSE;
    }
    zipfile->index = i;
    return TRUE;
}

G_GNUC_UNUSED
static guchar*
gwyzip_get_file_content(GwyZipFile zipfile, gsize *contentsize, GError **error)
{
    struct zip_file *file;
    struct zip_stat zst;
    guchar *buffer;

    if (err_ZIP_NOFILE(zipfile, error))
        return NULL;

    zip_stat_init(&zst);
    if (zip_stat_index(zipfile->archive, zipfile->index, 0, &zst) == -1) {
        err_ZIP(zipfile, error);
        return NULL;
    }

    if (!(zst.valid & ZIP_STAT_SIZE)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("Cannot obtain the uncompressed file size."));
        return NULL;
    }

    gwy_debug("uncompressed_size: %lu", (gulong)zst.size);
    file = zip_fopen_index(zipfile->archive, zipfile->index, 0);
    if (!file) {
        err_ZIP(zipfile, error);
        return NULL;
    }

    buffer = g_new(guchar, zst.size + 1);
    if (zip_fread(file, buffer, zst.size) != zst.size) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("Cannot read file contents."));
        zip_fclose(file);
        g_free(buffer);
        return NULL;
    }
    zip_fclose(file);

    buffer[zst.size] = '\0';
    if (contentsize)
        *contentsize = zst.size;
    return buffer;
}
#endif

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
