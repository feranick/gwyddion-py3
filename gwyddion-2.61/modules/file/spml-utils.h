#ifndef __SPML_UTILS_H
#define __SPML_UTILS_H
/*
 * ============================================================================
 *
 *        Filename:  spml-utils.c
 *
 *     Description:  ZLIB stream inflation, Base64 decoding, data types reading
 *                   functions
 *
 *         Version:  0.1
 *         Created:  20.02.2006 20:16:13 CET
 *        Revision:  none
 *        Compiler:  gcc
 *
 *          Author:  Jan Horak (xhorak@gmail.com),
 *         Company:
 *
 * ============================================================================
 */
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <assert.h>


#define CHUNK 16000             /*/< size of input/output buffer for zlib inflate in bytes */
/**
 * Extra possible input data formats in SPML (in addition to GwyRawDataType) -- these are not implemented anyway.
 */
enum {
    STRING = -1,
    UNKNOWN_DATAFORMAT = -666,
};

/**
 * Possible data coding in SPML
 */
typedef enum {
    UNKNOWN_CODING, ZLIB_COMPR_BASE64, BASE64, HEX, ASCII, BINARY
} codingTypes;

/**
 * Structure contain information about one datachannel group.
 * Each datachannel group has unique name and it contain
 * list of relevant datachannels.
 */
typedef struct {
    xmlChar *name;
    GList *datachannels;
} dataChannelGroup;

/**
 Structure to hold info about zlib stream inflating
 @param strm holds info for inflate function
 @param in input buffer
 @param out output buffer
 Each zlib_stream must be initialized before used byt inflate_init
 and destroyed by inflate_destroy
*/
typedef struct {
    z_stream strm;
    char in[CHUNK];
    char out[CHUNK];
} zlib_stream;


/* ZLIB INFLATING FUNCTIONS */
/* ------------------------ */

/**
 Initialization of zlib inflating
 @return Z_OK when initialization succeeded
*/
static int
inflate_init(zlib_stream * zstr)
{
    int ret;

    zstr->strm.zalloc = Z_NULL;
    zstr->strm.zfree = Z_NULL;
    zstr->strm.opaque = Z_NULL;
    zstr->strm.avail_in = 0;
    zstr->strm.next_in = Z_NULL;

    ret = inflateInit(&(zstr->strm));
    return ret;
}

/**
 Set input buffer to decompress
 @param in_buf pointer to input buffer
 @param count number of bytes in input buffer
 @return -1 when input buffer is too long to fit into in buffer
 (is greater than CHUNK define )
*/
static int
inflate_set_in_buffer(zlib_stream * zstr, char *in_buf, int count)
{
    if (count > CHUNK) {
        g_warning("Input buffer is too long (%d). Maximum size is %d.\n", count,
                  CHUNK);
        return -1;
    }
    if (count == 0)
        return 0;
    zstr->strm.avail_in = count;

    memcpy(&(zstr->in), in_buf, count);
    zstr->strm.next_in = zstr->in;
    /*zstr->strm.next_in = in_buf;  better to copy input buffer */

    return 0;
}

/**
 Run inflation of buffer
 Run inflation of input buffer which was previously set by inflate_set_in_buffer
 and store it to output buffer.
 */
static int
inflate_get_out_buffer(zlib_stream * zstr, GArray ** out_buf)
{
    int count;
    int ret;

    /* run inflate() on input until output buffer not full */
    do {
        zstr->strm.avail_out = CHUNK;
        zstr->strm.next_out = zstr->out;
        ret = inflate(&(zstr->strm), Z_NO_FLUSH);
        assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
        switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                return ret;
        }
        count = CHUNK - zstr->strm.avail_out;
        *out_buf = g_array_append_vals(*out_buf, zstr->out, count);
        if (*out_buf == NULL) {
            g_warning("Zlib inflate: output buffer wasn't written "
                      "to dynamic array.");
            return Z_ERRNO;
        }
    } while (zstr->strm.avail_out == 0);

    return ret;
}

/**
 Dispose zlib inflate structure
*/
static void
inflate_destroy(zlib_stream * zstr)
{
    (void)inflateEnd(&(zstr->strm));
}

/**
 * Inflate content of in_buf to out_buf
 * @param in_buf pointer to GArray of chars which contain whole zlib stream to inflate
 * @param out_buf pointer to pointer to GArray of chars which will be set to inflated
 * stream, it is dynamically allocated so it must be freed by caller.
 * @return -1 when in_buf is not complete zlib compressed array or any other error
 * when unpacking
 */
static int
inflate_dynamic_array(GArray * in_buf, GArray ** out_buf)
{
    int i;
    int ret = 0;
    zlib_stream zstr;
    char *pCh = in_buf->data;

    inflate_init(&zstr);
    *out_buf = g_array_new(FALSE, FALSE, sizeof(char));
    for (i = 0; (i + CHUNK) < in_buf->len; i += CHUNK) {
        if (inflate_set_in_buffer(&zstr, pCh + i, CHUNK) != 0) {
            ret = -1;
            break;
        }
        if (inflate_get_out_buffer(&zstr, out_buf) != Z_OK) {
            g_warning("Cannot inflate zlib compression. Be sure it is "
                      "a compressed stream.");
            ret = -1;
            break;
        }
    }
    if (ret == 0) {
        /* inflate the rest of buffer */
        if (inflate_set_in_buffer(&zstr, pCh + i, in_buf->len - i) != 0) {
            ret = -1;
        }
        if (inflate_get_out_buffer(&zstr, out_buf) != Z_STREAM_END) {
            g_warning("Cannot inflate zlib compression. Be sure it is "
                      "a compressed stream.");
            ret = -1;
        }
    }
    inflate_destroy(&zstr);
    return ret;

}

/**
 * Decode input buffer in BASE64 encoding to out_buf
 * @param in_buf pointer to input buffer
 * out_buf is created in dynamic memory must be deallocated by caller.
 */
static void
decode_b64(const gchar *in_buf, GArray **out_buf)
{
    guchar *mem;
    gsize out_len;

    mem = g_base64_decode(in_buf, &out_len);
    *out_buf = g_array_sized_new(FALSE, FALSE, sizeof(char), out_len);
    g_array_append_vals(*out_buf, mem, out_len);
    g_free(mem);
}

/* SPML utils */
/*------------ */

static gint
get_data_format(char *value)
{
    static const GwyEnum formats[] = {
        { "FLOAT32", GWY_RAW_DATA_FLOAT,  },
        { "FLOAT64", GWY_RAW_DATA_DOUBLE, },
        { "INT8",    GWY_RAW_DATA_SINT8,  },
        { "UINT8",   GWY_RAW_DATA_UINT8,  },
        { "INT16",   GWY_RAW_DATA_SINT16, },
        { "UINT16",  GWY_RAW_DATA_UINT16, },
        { "INT32",   GWY_RAW_DATA_SINT32, },
        { "UINT32",  GWY_RAW_DATA_UINT32, },
        { "STRING",  STRING,              },
    };
    gint ret = UNKNOWN_DATAFORMAT;

    if (!value)
        g_warning("SPML: Unknown dataformat for datachannel.");
    else {
        if ((ret = gwy_string_to_enum(value, formats, G_N_ELEMENTS(formats))) == -1) {
            g_warning("SPML: Data coding for datachannel not recognized.");
            ret = UNKNOWN_DATAFORMAT;
        }
        gwy_debug("Dataformat read.");
        g_free(value);
    }
    return ret;
}

static codingTypes
get_data_coding(char *value)
{
    static const GwyEnum codings[] = {
        { "ZLIB-COMPR-BASE64", ZLIB_COMPR_BASE64, },
        { "BASE64",            BASE64,            },
        { "HEX",               HEX,               },
        { "ASCII",             ASCII,             },
        { "BINARY",            BINARY,            },
    };
    codingTypes ret = UNKNOWN_CODING;

    if (!value)
        g_warning("SPML: Unknown coding type for datachannel.");
    else {
        if ((ret = gwy_string_to_enum(value, codings, G_N_ELEMENTS(codings))) == -1) {
            g_warning("SPML: Data coding for datachannel not recognized.");
            ret = UNKNOWN_CODING;
        }
        gwy_debug("coding read.");
        g_free(value);
    }
    return ret;
}

static GwyByteOrder
get_byteorder(char *value)
{
    GwyByteOrder ret = GWY_BYTE_ORDER_NATIVE;

    if (value) {
        if (strcmp(value, "BIG-ENDIAN") == 0) {
            ret = GWY_BYTE_ORDER_BIG_ENDIAN;
        }
        else if (strcmp(value, "LITTLE-ENDIAN") == 0) {
            ret = GWY_BYTE_ORDER_LITTLE_ENDIAN;
        }
        else {
            g_warning("Byte order for datachannel not recognized.");
        }
        gwy_debug("byteorder read.");
        g_free(value);
    }
    else {
        g_warning("SPML: Unknown byteorder of datachannel.");
    }
    return ret;
}

#endif
/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
