/*
 *  $Id: quazarnpic.c 24468 2021-11-05 12:50:23Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
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

/**
 * [FILE-MAGIC-USERGUIDE]
 * Quazar NPIC
 * .npic
 * Read[1]
 * [1] The import module is unfinished due to the lack of documentation,
 * testing files and/or people willing to help with the testing.  If you can
 * help please contact us.
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Avoding clash with a standard file format.
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

/* The eight zeros is a region we do not compare.  Not sure the FRAME content is stable. */
#define MAGIC "\x80\x04\x95\x00\x00\x00\x00\x00\x00\x00\x00\x8c\x15numpy.core.multiarray"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EXTENSION ".npic"

typedef enum {
/* {{{ */
    OPCODE_MARK = '(',
    OPCODE_EMPTY_TUPLE = ')',
    OPCODE_EMPTY_LIST = ']',
    OPCODE_EMPTY_DICT = '}',
    OPCODE_STOP = '.',
    OPCODE_POP = '0',
    OPCODE_POP_MARK = '1',
    OPCODE_DUP = '2',
    OPCODE_APPEND = 'a',
    OPCODE_BINBYTES = 'B',
    OPCODE_BUILD = 'b',
    OPCODE_SHORT_BINBYTES = 'C',
    OPCODE_GLOBAL = 'c',
    OPCODE_DICT = 'd',
    OPCODE_APPENDS = 'e',
    OPCODE_FLOAT = 'F',
    OPCODE_BINFLOAT = 'G',
    OPCODE_GET = 'g',
    OPCODE_BINGET = 'h',
    OPCODE_INT = 'I',
    OPCODE_INST = 'i',
    OPCODE_BININT = 'J',
    OPCODE_LONG_BINGET = 'j',
    OPCODE_BININT1 = 'K',
    OPCODE_BININT2 = 'M',
    OPCODE_LONG = 'L',
    OPCODE_LIST = 'l',
    OPCODE_NONE = 'N',
    OPCODE_OBJ = 'o',
    OPCODE_PERSID = 'P',
    OPCODE_PUT = 'p',
    OPCODE_BINPERSID = 'Q',
    OPCODE_BINPUT = 'q',
    OPCODE_REDUCE = 'R',
    OPCODE_LONG_BINPUT = 'r',
    OPCODE_STRING = 'S',
    OPCODE_SETITEM = 's',
    OPCODE_BINSTRING = 'T',
    OPCODE_TUPLE = 't',
    OPCODE_SHORT_BINSTRING = 'U',
    OPCODE_SETITEMS = 'u',
    OPCODE_BINUNICODE = 'X',
    OPCODE_UNICODE = 'V',
    OPCODE_PROTO = 0x80,
    OPCODE_NEWOBJ = 0x81,
    OPCODE_EXT1 = 0x82,
    OPCODE_EXT2 = 0x83,
    OPCODE_EXT4 = 0x84,
    OPCODE_TUPLE1 = 0x85,
    OPCODE_TUPLE2 = 0x86,
    OPCODE_TUPLE3 = 0x87,
    OPCODE_LONG1 = 0x8a,
    OPCODE_LONG4 = 0x8b,
    OPCODE_FRAME = 0x95,
    OPCODE_NEWTRUE = 0x88,
    OPCODE_NEWFALSE = 0x89,
    OPCODE_SHORT_BINUNICODE = 0x8c,
    OPCODE_BINUNICODE8 = 0x8d,
    OPCODE_BINBYTES8 = 0x8e,
    OPCODE_EMPTY_SET = 0x8f,
    OPCODE_ADDITEMS = 0x90,
    OPCODE_FROZENSET = 0x91,
    OPCODE_NEWOBJ_EX = 0x92,
    OPCODE_STACK_GLOBAL = 0x93,
    OPCODE_MEMOIZE = 0x94,
    /* }}} */
} PickleOpcodeType;

typedef enum {
    /* {{{ */
    ARG_NONE,
    ARG_BYTES1,
    ARG_BYTES4,
    ARG_BYTES8,
    ARG_DECIMALNL_LONG,
    ARG_DECIMALNL_SHORT,
    ARG_FLOAT8,
    ARG_FLOATNL,
    ARG_INT4,
    ARG_LONG1,
    ARG_LONG4,
    ARG_STRING1,
    ARG_STRING4,
    ARG_STRINGNL,
    ARG_STRINGNL_NOESCAPE,
    ARG_STRINGNL_NOESCAPE_PAIR,
    ARG_UINT1,
    ARG_UINT2,
    ARG_UINT4,
    ARG_UINT8,
    ARG_UNICODESTRING1,
    ARG_UNICODESTRING4,
    ARG_UNICODESTRING8,
    ARG_UNICODESTRINGNL,
    /* }}} */
} PickleArgType;

typedef enum {
    /* {{{ */
    STACK_VOID,
    STACK_ANYOBJECT,
    STACK_ANYOBJECT2,
    STACK_ANYOBJECT3,
    STACK_MARKOBJECT,
    STACK_MARKOBJECT_ANYOBJECT_STACKSLICE,
    STACK_MARKOBJECT_STACKSLICE,
    STACK_PYBOOL,
    STACK_PYBYTES,
    STACK_PYBYTES_OR_STR,
    STACK_PYDICT,
    STACK_PYDICT_ANYOBJECT2,
    STACK_PYDICT_MARKOBJECT_STACKSLICE,
    STACK_PYFLOAT,
    STACK_PYFROZENSET,
    STACK_PYINT,
    STACK_PYINTEGER_OR_BOOL,
    STACK_PYLIST,
    STACK_PYLIST_ANYOBJECT,
    STACK_PYLIST_MARKOBJECT_STACKSLICE,
    STACK_PYNONE,
    STACK_PYSET,
    STACK_PYSET_MARKOBJECT_STACKSLICE,
    STACK_PYTUPLE,
    STACK_PYUNICODE,
    STACK_PYUNICODE_PYUNICODE,
    /* }}} */
} PickleStackType;

typedef enum {
    OBJECT_NONE,
    OBJECT_MARK,
    OBJECT_BOOL,
    OBJECT_INT,
    OBJECT_FLOAT,
    OBJECT_STRING,  /* UTF-8. */
    OBJECT_BYTES,
    OBJECT_SEQ,     /* List, tuple, we do not care. */
    OBJECT_DICT,
    OBJECT_SET,
    OBJECT_GLOBAL,  /* (module, class) pair created by the GLOBAL opcodes. */
    OBJECT_REDUCE,  /* (callable, seq) pair created by REDUCE -- which we obviously do not execute. */
    OBJECT_OBJECT,  /* (seq, reduce) pair created by BUILD -- the finished object. */
    OBJECT_ANY      /* not a real type, just for querying */
} PickleObjectType;

typedef struct {
    guchar opcode;
    guchar protocol : 4;
    PickleArgType argtype : 6;
    PickleStackType stack_before : 6;
    PickleStackType stack_after : 6;
    const gchar *name;
} PickleOpcode;

typedef union {
    gboolean b;
    gint64 i;
    gdouble d;
    gchar *s;   /* String or bytes. */
    /* Only for objects. */
    GPtrArray *a;  /* Any sequence of PickleObjects.  For dicts it contains interleaved keys and values. */
    /* Kind-of only used for args. */
    guint64 u;
} PickleValue;

typedef struct {
    PickleValue v;
    gsize len;
    PickleArgType type;
    gboolean free_s;
} PickleArg;

typedef struct {
    PickleValue v;
    PickleObjectType type;
    gsize len;
    guint refcount;
} PickleObject;

typedef struct {
    gdouble xstep;
    gdouble ystep;
    gdouble xcal;
    gdouble ycal;
    gdouble zcal;
    const gchar *xcalunit;
    const gchar *zcalunit;
    GPtrArray *channel_names;
} NpicFileInfo;

static const PickleOpcode opcodes[] = {
/* {{{ */
    { OPCODE_ADDITEMS,         4, ARG_NONE,                   STACK_PYSET_MARKOBJECT_STACKSLICE,     STACK_PYSET,             "ADDITEMS",         },
    { OPCODE_APPEND,           0, ARG_NONE,                   STACK_PYLIST_ANYOBJECT,                STACK_PYLIST,            "APPEND",           },
    { OPCODE_APPENDS,          1, ARG_NONE,                   STACK_PYLIST_MARKOBJECT_STACKSLICE,    STACK_PYLIST,            "APPENDS",          },
    { OPCODE_BINBYTES,         3, ARG_BYTES4,                 STACK_VOID,                            STACK_PYBYTES,           "BINBYTES",         },
    { OPCODE_BINBYTES8,        4, ARG_BYTES8,                 STACK_VOID,                            STACK_PYBYTES,           "BINBYTES8",        },
    { OPCODE_BINFLOAT,         1, ARG_FLOAT8,                 STACK_VOID,                            STACK_PYFLOAT,           "BINFLOAT",         },
    { OPCODE_BINGET,           1, ARG_UINT1,                  STACK_VOID,                            STACK_ANYOBJECT,         "BINGET",           },
    { OPCODE_BININT1,          1, ARG_UINT1,                  STACK_VOID,                            STACK_PYINT,             "BININT1",          },
    { OPCODE_BININT,           1, ARG_INT4,                   STACK_VOID,                            STACK_PYINT,             "BININT",           },
    { OPCODE_BININT2,          1, ARG_UINT2,                  STACK_VOID,                            STACK_PYINT,             "BININT2",          },
    { OPCODE_BINPERSID,        1, ARG_NONE,                   STACK_ANYOBJECT,                       STACK_ANYOBJECT,         "BINPERSID",        },
    { OPCODE_BINPUT,           1, ARG_UINT1,                  STACK_VOID,                            STACK_VOID,              "BINPUT",           },
    { OPCODE_BINSTRING,        1, ARG_STRING4,                STACK_VOID,                            STACK_PYBYTES_OR_STR,    "BINSTRING",        },
    { OPCODE_BINUNICODE,       1, ARG_UNICODESTRING4,         STACK_VOID,                            STACK_PYUNICODE,         "BINUNICODE",       },
    { OPCODE_BINUNICODE8,      4, ARG_UNICODESTRING8,         STACK_VOID,                            STACK_PYUNICODE,         "BINUNICODE8",      },
    { OPCODE_BUILD,            0, ARG_NONE,                   STACK_ANYOBJECT2,                      STACK_ANYOBJECT,         "BUILD",            },
    { OPCODE_DICT,             0, ARG_NONE,                   STACK_MARKOBJECT_STACKSLICE,           STACK_PYDICT,            "DICT",             },
    { OPCODE_DUP,              0, ARG_NONE,                   STACK_ANYOBJECT,                       STACK_ANYOBJECT2,        "DUP",              },
    { OPCODE_EMPTY_DICT,       1, ARG_NONE,                   STACK_VOID,                            STACK_PYDICT,            "EMPTY_DICT",       },
    { OPCODE_EMPTY_LIST,       1, ARG_NONE,                   STACK_VOID,                            STACK_PYLIST,            "EMPTY_LIST",       },
    { OPCODE_EMPTY_SET,        4, ARG_NONE,                   STACK_VOID,                            STACK_PYSET,             "EMPTY_SET",        },
    { OPCODE_EMPTY_TUPLE,      1, ARG_NONE,                   STACK_VOID,                            STACK_PYTUPLE,           "EMPTY_TUPLE",      },
    { OPCODE_EXT1,             2, ARG_UINT1,                  STACK_VOID,                            STACK_ANYOBJECT,         "EXT1",             },
    { OPCODE_EXT2,             2, ARG_UINT2,                  STACK_VOID,                            STACK_ANYOBJECT,         "EXT2",             },
    { OPCODE_EXT4,             2, ARG_INT4,                   STACK_VOID,                            STACK_ANYOBJECT,         "EXT4",             },
    { OPCODE_FLOAT,            0, ARG_FLOATNL,                STACK_VOID,                            STACK_PYFLOAT,           "FLOAT",            },
    { OPCODE_FRAME,            4, ARG_UINT8,                  STACK_VOID,                            STACK_VOID,              "FRAME",            },
    { OPCODE_FROZENSET,        4, ARG_NONE,                   STACK_MARKOBJECT_STACKSLICE,           STACK_PYFROZENSET,       "FROZENSET",        },
    { OPCODE_GET,              0, ARG_DECIMALNL_SHORT,        STACK_VOID,                            STACK_ANYOBJECT,         "GET",              },
    { OPCODE_GLOBAL,           0, ARG_STRINGNL_NOESCAPE_PAIR, STACK_VOID,                            STACK_ANYOBJECT,         "GLOBAL",           },
    { OPCODE_INST,             0, ARG_STRINGNL_NOESCAPE_PAIR, STACK_MARKOBJECT_STACKSLICE,           STACK_ANYOBJECT,         "INST",             },
    { OPCODE_INT,              0, ARG_DECIMALNL_SHORT,        STACK_VOID,                            STACK_PYINTEGER_OR_BOOL, "INT",              },
    { OPCODE_LIST,             0, ARG_NONE,                   STACK_MARKOBJECT_STACKSLICE,           STACK_PYLIST,            "LIST",             },
    { OPCODE_LONG,             0, ARG_DECIMALNL_LONG,         STACK_VOID,                            STACK_PYINT,             "LONG",             },
    { OPCODE_LONG1,            2, ARG_LONG1,                  STACK_VOID,                            STACK_PYINT,             "LONG1",            },
    { OPCODE_LONG4,            2, ARG_LONG4,                  STACK_VOID,                            STACK_PYINT,             "LONG4",            },
    { OPCODE_LONG_BINGET,      1, ARG_UINT4,                  STACK_VOID,                            STACK_ANYOBJECT,         "LONG_BINGET",      },
    { OPCODE_LONG_BINPUT,      1, ARG_UINT4,                  STACK_VOID,                            STACK_VOID,              "LONG_BINPUT",      },
    { OPCODE_MARK,             0, ARG_NONE,                   STACK_VOID,                            STACK_MARKOBJECT,        "MARK",             },
    { OPCODE_MEMOIZE,          4, ARG_NONE,                   STACK_ANYOBJECT,                       STACK_ANYOBJECT,         "MEMOIZE",          },
    { OPCODE_NEWFALSE,         2, ARG_NONE,                   STACK_VOID,                            STACK_PYBOOL,            "NEWFALSE",         },
    { OPCODE_NEWOBJ,           2, ARG_NONE,                   STACK_ANYOBJECT2,                      STACK_ANYOBJECT,         "NEWOBJ",           },
    { OPCODE_NEWOBJ_EX,        4, ARG_NONE,                   STACK_ANYOBJECT3,                      STACK_ANYOBJECT,         "NEWOBJ_EX",        },
    { OPCODE_NEWTRUE,          2, ARG_NONE,                   STACK_VOID,                            STACK_PYBOOL,            "NEWTRUE",          },
    { OPCODE_NONE,             0, ARG_NONE,                   STACK_VOID,                            STACK_PYNONE,            "NONE",             },
    { OPCODE_OBJ,              1, ARG_NONE,                   STACK_MARKOBJECT_ANYOBJECT_STACKSLICE, STACK_ANYOBJECT,         "OBJ",              },
    { OPCODE_PERSID,           0, ARG_STRINGNL_NOESCAPE,      STACK_VOID,                            STACK_ANYOBJECT,         "PERSID",           },
    { OPCODE_POP,              0, ARG_NONE,                   STACK_ANYOBJECT,                       STACK_VOID,              "POP",              },
    { OPCODE_POP_MARK,         1, ARG_NONE,                   STACK_MARKOBJECT_STACKSLICE,           STACK_VOID,              "POP_MARK",         },
    { OPCODE_PROTO,            2, ARG_UINT1,                  STACK_VOID,                            STACK_VOID,              "PROTO",            },
    { OPCODE_PUT,              0, ARG_DECIMALNL_SHORT,        STACK_VOID,                            STACK_VOID,              "PUT",              },
    { OPCODE_REDUCE,           0, ARG_NONE,                   STACK_ANYOBJECT2,                      STACK_ANYOBJECT,         "REDUCE",           },
    { OPCODE_SETITEM,          0, ARG_NONE,                   STACK_PYDICT_ANYOBJECT2,               STACK_PYDICT,            "SETITEM",          },
    { OPCODE_SETITEMS,         1, ARG_NONE,                   STACK_PYDICT_MARKOBJECT_STACKSLICE,    STACK_PYDICT,            "SETITEMS",         },
    { OPCODE_SHORT_BINBYTES,   3, ARG_BYTES1,                 STACK_VOID,                            STACK_PYBYTES,           "SHORT_BINBYTES",   },
    { OPCODE_SHORT_BINSTRING,  1, ARG_STRING1,                STACK_VOID,                            STACK_PYBYTES_OR_STR,    "SHORT_BINSTRING",  },
    { OPCODE_SHORT_BINUNICODE, 4, ARG_UNICODESTRING1,         STACK_VOID,                            STACK_PYUNICODE,         "SHORT_BINUNICODE", },
    { OPCODE_STACK_GLOBAL,     4, ARG_NONE,                   STACK_PYUNICODE_PYUNICODE,             STACK_ANYOBJECT,         "STACK_GLOBAL",     },
    { OPCODE_STOP,             0, ARG_NONE,                   STACK_ANYOBJECT,                       STACK_VOID,              "STOP",             },
    { OPCODE_STRING,           0, ARG_STRINGNL,               STACK_VOID,                            STACK_PYBYTES_OR_STR,    "STRING",           },
    { OPCODE_TUPLE,            0, ARG_NONE,                   STACK_MARKOBJECT_STACKSLICE,           STACK_PYTUPLE,           "TUPLE",            },
    { OPCODE_TUPLE1,           2, ARG_NONE,                   STACK_ANYOBJECT,                       STACK_PYTUPLE,           "TUPLE1",           },
    { OPCODE_TUPLE2,           2, ARG_NONE,                   STACK_ANYOBJECT2,                      STACK_PYTUPLE,           "TUPLE2",           },
    { OPCODE_TUPLE3,           2, ARG_NONE,                   STACK_ANYOBJECT3,                      STACK_PYTUPLE,           "TUPLE3",           },
    { OPCODE_UNICODE,          0, ARG_UNICODESTRINGNL,        STACK_VOID,                            STACK_PYUNICODE,         "UNICODE",          },
/* }}} */
};

#ifdef DEBUG
/* {{{ */
static const GwyEnum argtype_names[] = {
    { "NONE",                   ARG_NONE,                   },
    { "BYTES1",                 ARG_BYTES1,                 },
    { "BYTES4",                 ARG_BYTES4,                 },
    { "BYTES8",                 ARG_BYTES8,                 },
    { "DECIMALNL_LONG",         ARG_DECIMALNL_LONG,         },
    { "DECIMALNL_SHORT",        ARG_DECIMALNL_SHORT,        },
    { "FLOAT8",                 ARG_FLOAT8,                 },
    { "FLOATNL",                ARG_FLOATNL,                },
    { "INT4",                   ARG_INT4,                   },
    { "LONG1",                  ARG_LONG1,                  },
    { "LONG4",                  ARG_LONG4,                  },
    { "STRING1",                ARG_STRING1,                },
    { "STRING4",                ARG_STRING4,                },
    { "STRINGNL",               ARG_STRINGNL,               },
    { "STRINGNL_NOESCAPE",      ARG_STRINGNL_NOESCAPE,      },
    { "STRINGNL_NOESCAPE_PAIR", ARG_STRINGNL_NOESCAPE_PAIR, },
    { "UINT1",                  ARG_UINT1,                  },
    { "UINT2",                  ARG_UINT2,                  },
    { "UINT4",                  ARG_UINT4,                  },
    { "UINT8",                  ARG_UINT8,                  },
    { "UNICODESTRING1",         ARG_UNICODESTRING1,         },
    { "UNICODESTRING4",         ARG_UNICODESTRING4,         },
    { "UNICODESTRING8",         ARG_UNICODESTRING8,         },
    { "UNICODESTRINGNL",        ARG_UNICODESTRINGNL,        },
};

static const GwyEnum objtype_names[] = {
    { "NONE",   OBJECT_NONE,   },
    { "MARK",   OBJECT_MARK,   },
    { "BOOL",   OBJECT_BOOL,   },
    { "INT",    OBJECT_INT,    },
    { "FLOAT",  OBJECT_FLOAT,  },
    { "STRING", OBJECT_STRING, },
    { "BYTES",  OBJECT_BYTES,  },
    { "SEQ",    OBJECT_SEQ,    },
    { "DICT",   OBJECT_DICT,   },
    { "SET",    OBJECT_SET,    },
    { "GLOBAL", OBJECT_GLOBAL, },
    { "REDUCE", OBJECT_REDUCE, },
    { "OBJECT", OBJECT_OBJECT, },
};
/* }}} */
#endif

static gboolean      module_register(void);
static gint          npic_detect    (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* npic_load      (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Quazar data files stored as Python pickles v4."),
    "Yeti <yeti@gwyddion.net>",
    "0.2",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, quazarnpic)

static gboolean
module_register(void)
{
    gwy_file_func_register("quazarnpic",
                           N_("Quazar Python-pickled data (.npic)"),
                           (GwyFileDetectFunc)&npic_detect,
                           (GwyFileLoadFunc)&npic_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
npic_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    static const gchar *paramnames[] = {
        "StepSize", "ImageSize", "ImageSizeUnit", "NoOfChannels", "ChannelNames", "Instrument",
        "XCalibration", "XCalibrationUnit", "YCalibration", "YCalibrationUnit", "ZCalibration", "ZCalibrationUnit",
    };
    const gchar *p = fileinfo->head;
    guint i, nfound;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->buffer_len < MAGIC_SIZE)
        return 0;

    if (memcmp(p, MAGIC, 3) || memcmp(p + 3 + 8, MAGIC + 3 + 8, MAGIC_SIZE - 3 - 8))
        return 0;

    for (i = nfound = 0; i < G_N_ELEMENTS(paramnames); i++) {
        if (gwy_memmem(fileinfo->tail, fileinfo->buffer_len, paramnames[i], strlen(paramnames[i])))
            nfound++;
        /* If it seems we are not finding any, bail out. */
        if (nfound < (i+1)/2 && nfound < MAX(i, 2)-2)
            return 0;
    }

    return 50 + 48*nfound/G_N_ELEMENTS(paramnames);
}

static void
err_INVALID_STACK(GError **error, guint op)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Invalid stack state for opcode %02x."), op);
}

static gboolean
read_arg(const guchar **p, gsize *size, PickleArg *arg)
{
    if (arg->type == ARG_NONE)
        return TRUE;
    else if (arg->type == ARG_UINT1) {
        if (*size < 1)
            return FALSE;
        arg->v.u = **p;
        gwy_debug("UINT1 value %lu", (gulong)arg->v.u);
        (*p)++;
        (*size)--;
        return TRUE;
    }
    else if (arg->type == ARG_UINT2) {
        if (*size < 2)
            return FALSE;
        arg->v.u = gwy_get_guint16_le(p);
        gwy_debug("UINT2 value %lu", (gulong)arg->v.u);
        *size -= 2;
        return TRUE;
    }
    else if (arg->type == ARG_UINT4) {
        if (*size < 4)
            return FALSE;
        arg->v.u = gwy_get_guint32_le(p);
        gwy_debug("UINT4 value %lu", (gulong)arg->v.u);
        *size -= 4;
        return TRUE;
    }
    else if (arg->type == ARG_INT4) {
        if (*size < 4)
            return FALSE;
        arg->v.i = gwy_get_gint32_le(p);
        gwy_debug("INT4 value %ld", (glong)arg->v.i);
        *size -= 4;
        return TRUE;
    }
    else if (arg->type == ARG_UINT8) {
        if (*size < 8)
            return FALSE;
        arg->v.u = gwy_get_gint64_le(p);
        gwy_debug("UINT8 value %lu", (gulong)arg->v.u);
        *size -= 8;
        return TRUE;
    }
    else if (arg->type == ARG_FLOAT8) {
        if (*size < 8)
            return FALSE;
        /* NB: Really.  Integers are little endian, but floats are big endian. */
        arg->v.d = gwy_get_gdouble_be(p);
        gwy_debug("FLOAT8 value %g", arg->v.d);
        *size -= 8;
        return TRUE;
    }
    else if (arg->type == ARG_UNICODESTRING1 || arg->type == ARG_UNICODESTRING4 || arg->type == ARG_UNICODESTRING8
             || arg->type == ARG_BYTES1 || arg->type == ARG_BYTES4 || arg->type == ARG_BYTES8) {
        if (arg->type == ARG_UNICODESTRING1 || arg->type == ARG_BYTES1) {
            if (*size < 1)
                return FALSE;
            arg->len = **p;
            (*p)++;
            (*size)--;
        }
        else if (arg->type == ARG_UNICODESTRING4 || arg->type == ARG_BYTES4) {
            if (*size < 4)
                return FALSE;
            arg->len = gwy_get_guint32_le(p);
            *size -= 4;
        }
        else {
            if (*size < 8)
                return FALSE;
            arg->len = gwy_get_guint64_le(p);
            *size -= 8;
        }
        if (*size < arg->len)
            return FALSE;
        arg->free_s = TRUE;
        arg->v.s = g_new(gchar, arg->len+1);
        memcpy(arg->v.s, *p, arg->len);
        arg->v.s[arg->len] = '\0';
        if (arg->type == ARG_UNICODESTRING1 || arg->type == ARG_UNICODESTRING4 || arg->type == ARG_UNICODESTRING8) {
            gwy_debug("STRING value %s", arg->v.s);
        }
        else {
            gwy_debug("BYTES value of length %lu", (gulong)arg->len);
        }
        *p += arg->len;
        *size -= arg->len;
        return TRUE;
    }
    /* This should only be the newline formats which are silly and long which we have no way of representing anyway
     * (unless they are used for normal-sized integers). */
    g_warning("Argument type %u not implemented.", arg->type);
    return FALSE;
}

static void
free_arg(PickleArg *arg)
{
    if (arg->free_s)
        g_free(arg->v.s);
    gwy_clear(arg, 1);
}

static PickleObject*
new_object(PickleObjectType type, PickleArg *arg)
{
    PickleObject *obj = g_slice_new0(PickleObject);
    obj->refcount = 1;
    obj->type = type;
    if (arg) {
        obj->v = arg->v;
        obj->len = arg->len;
        arg->free_s = FALSE;
    }
    else if (type == OBJECT_SEQ || type == OBJECT_DICT || type == OBJECT_SET
             || type == OBJECT_GLOBAL || type == OBJECT_REDUCE || type == OBJECT_OBJECT)
        obj->v.a = g_ptr_array_new();

    return obj;
}

/* Actually, unref it.  Usually this means freeing. */
static void
free_object(PickleObject *obj)
{
    PickleObjectType type = obj->type;

    g_assert(obj->refcount);
    if (--obj->refcount)
        return;

    if (type == OBJECT_STRING || type == OBJECT_BYTES)
        g_free(obj->v.s);
    else if (type == OBJECT_SEQ || type == OBJECT_DICT || type == OBJECT_SET
             || type == OBJECT_GLOBAL || type == OBJECT_REDUCE || type == OBJECT_OBJECT) {
        GPtrArray *a = obj->v.a;
        guint i, n = a->len;

        for (i = 0; i < n; i++)
            free_object(g_ptr_array_index(a, i));
        g_ptr_array_free(a, TRUE);
    }
    g_slice_free(PickleObject, obj);
}

static guint
append_to_seq(GPtrArray *stack, guint objpos, gboolean has_mark, PickleObjectType expected_type)
{
    guint i, slen = stack->len;
    PickleObject *obj;
    GPtrArray *a;

    g_return_val_if_fail(objpos < G_MAXUINT-4, 0);
    obj = g_ptr_array_index(stack, objpos);
    g_assert(obj->type == expected_type);
    a = obj->v.a;
    for (i = objpos+1 + !!has_mark; i < slen; i++)
        g_ptr_array_add(a, g_ptr_array_index(stack, i));
    g_ptr_array_set_size(stack, objpos+1);

    return objpos+1;
}

static PickleObject*
make_seq(GPtrArray *stack, guint nitems, PickleObjectType type)
{
    PickleObject *obj;
    guint i;

#ifdef DEBUG
    gwy_debug("make sequence (of type %s) from %u top stack items",
              gwy_enum_to_string(type, objtype_names, G_N_ELEMENTS(objtype_names)), nitems);
#endif
    g_return_val_if_fail(stack->len >= nitems, NULL);
    obj = new_object(type, NULL);
    for (i = stack->len - nitems; i < stack->len; i++)
        g_ptr_array_add(obj->v.a, g_ptr_array_index(stack, i));
    g_ptr_array_set_size(stack, stack->len - nitems);

    return obj;
}

static gboolean
check_stack_before(GPtrArray *stack, PickleStackType stack_before, guint *markpos)
{
    PickleObject *obj1, *obj2, *obj3;
    guint i, n = stack->len;

#ifdef DEBUG2
    {
        GString *str = g_string_new(NULL);

        for (i = 0; i < n; i++) {
            obj1 = g_ptr_array_index(stack, i);
            if (i)
                g_string_append(str, ", ");
            g_string_append(str, gwy_enum_to_string(obj1->type, objtype_names, G_N_ELEMENTS(objtype_names)));
        }
        gwy_debug("stack (%s)", str->str);
        g_string_free(str, TRUE);
    }
#endif

    if (markpos)
        *markpos = G_MAXUINT;

    if (stack_before == STACK_VOID)
        return TRUE;
    if (stack_before == STACK_ANYOBJECT)
        return n >= 1;
    if (stack_before == STACK_ANYOBJECT2)
        return n >= 2;
    if (stack_before == STACK_ANYOBJECT3)
        return n >= 3;

    if (!n)
        return FALSE;

    obj1 = g_ptr_array_index(stack, n-1);
    /* Handle specific fixed types. */
    if (stack_before == STACK_PYUNICODE_PYUNICODE) {
        if (n < 2)
            return FALSE;
        obj2 = g_ptr_array_index(stack, n-2);
        return obj1->type == OBJECT_STRING && obj2->type == OBJECT_STRING;
    }
    if (stack_before == STACK_PYLIST_ANYOBJECT) {
        if (n < 2)
            return FALSE;
        obj2 = g_ptr_array_index(stack, n-2);
        return obj2->type == OBJECT_SEQ;
    }
    if (stack_before == STACK_PYDICT_ANYOBJECT2) {
        if (n < 3)
            return FALSE;
        obj3 = g_ptr_array_index(stack, n-3);
        return obj3->type == OBJECT_DICT;
    }

    /* Now only stack types requiring a mark remain. */
    for (i = n; i; i--) {
        obj2 = g_ptr_array_index(stack, i-1);
        if (obj2->type == OBJECT_MARK)
            break;
    }
    if (!i)
        return FALSE;
    if (markpos)
        *markpos = i-1;

    if (stack_before == STACK_MARKOBJECT_STACKSLICE)
        return TRUE;
    if (stack_before == STACK_MARKOBJECT_ANYOBJECT_STACKSLICE)
        return n - i >= 1;

    /* Now we have only stack types which need something before the mark. */
    if (i < 2)
        return FALSE;

    obj3 = g_ptr_array_index(stack, i-2);
    if (stack_before == STACK_PYLIST_MARKOBJECT_STACKSLICE)
        return obj3->type == OBJECT_SEQ;
    if (stack_before == STACK_PYSET_MARKOBJECT_STACKSLICE)
        return obj3->type == OBJECT_SET;

    /* This one is a special case.  It updates a dictionary, so the items must be (key, value) pairs, i.e. even. */
    if (stack_before == STACK_PYDICT_MARKOBJECT_STACKSLICE) {
        if (obj3->type != OBJECT_DICT)
            return FALSE;
        if ((n - i) % 2)
            return FALSE;
        return TRUE;
    }

    g_return_val_if_reached(FALSE);
}

static PickleObject*
read_one_object(const guchar **p, gsize size, GError **error)
{
    GPtrArray *stack, *memoized;
    PickleObject *retval = NULL;
    PickleArg arg;
    guint i, protocol = 0;

    gwy_debug("starting to read a new object");
    stack = g_ptr_array_new();
    memoized = g_ptr_array_new();
    gwy_clear(&arg, 1);
    while (TRUE) {
        const PickleOpcode *opcode;
        PickleObject *obj = NULL;
        PickleArgType type;
        guint op, markpos, slen = stack->len;

        free_arg(&arg);
        if (size < 1) {
            err_TRUNCATED_PART(error, "object");
            goto fail;
        }
        op = **p;
        (*p)++;
        size--;
        for (i = 0; i < G_N_ELEMENTS(opcodes); i++) {
            opcode = opcodes + i;
            if (opcode->opcode == op)
                break;
        }
        if (i == G_N_ELEMENTS(opcodes)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Uknown opcode 0x%02x encountered"), op);
            goto fail;
        }

#ifdef DEBUG
        gwy_debug("opcode %s(%02x) (proto=%u, args=%d(%s), before=%d after=%d)",
                  opcode->name, op, opcode->protocol,
                  opcode->argtype, gwy_enum_to_string(opcode->argtype, argtype_names, G_N_ELEMENTS(argtype_names)),
                  opcode->stack_before, opcode->stack_after);
#endif
        arg.type = type = opcode->argtype;
        if (!read_arg(p, &size, &arg)) {
            err_TRUNCATED_PART(error, opcode->name);
            goto fail;
        }

        if (!check_stack_before(stack, opcode->stack_before, &markpos)) {
            err_INVALID_STACK(error, op);
            goto fail;
        }

        if (op == OPCODE_STOP)
            break;

        if (op == OPCODE_MEMOIZE) {
            g_assert(slen);
            gwy_debug("memoize stack top as %u", slen-1);
            g_ptr_array_add(memoized, g_ptr_array_index(stack, slen-1));
        }
        else if (op == OPCODE_BINGET || op == OPCODE_LONG_BINGET) {
            i = arg.v.u;
            g_assert(i < memoized->len);
            obj = g_ptr_array_index(memoized, i);
            obj->refcount++;
            gwy_debug("get memoized item #%u", i);
        }
        else if (op == OPCODE_POP) {
            free_object(g_ptr_array_index(stack, slen-1));
            g_ptr_array_set_size(stack, slen-1);
            slen = stack->len;
        }
        /* TODO: OPCODE_DUP: must implement object duplication */
        /* TODO: OPCODE_POP_MARK: must implement object freeing, then just pop everything up to the mark */
        /* TODO: OPCODE_BINPUT: this allows cyclic references, do not like it */
        else if (op == OPCODE_PROTO) {
            protocol = MAX(arg.v.u, protocol);
            gwy_debug("protocol %u (->%u)", (guint)arg.v.u, protocol);
        }
        else if (op == OPCODE_FRAME) {
            gwy_debug("frame %lu", (gulong)arg.v.u);
        }
        else if (op == OPCODE_MARK)
            obj = new_object(OBJECT_MARK, NULL);
        else if (op == OPCODE_NONE)
            obj = new_object(OBJECT_NONE, NULL);
        else if (op == OPCODE_NEWFALSE || op == OPCODE_NEWTRUE) {
            obj = new_object(OBJECT_BOOL, NULL);
            obj->v.b = (op == OPCODE_NEWTRUE);
        }
        else if (op == OPCODE_INT || op == OPCODE_BININT || op == OPCODE_BININT1 || op == OPCODE_BININT2
                 || op == OPCODE_LONG || op == OPCODE_LONG1 || op == OPCODE_LONG4)
            obj = new_object(OBJECT_INT, &arg);
        else if (op == OPCODE_FLOAT || op == OPCODE_BINFLOAT)
            obj = new_object(OBJECT_FLOAT, &arg);
        else if (op == OPCODE_STRING || op == OPCODE_BINSTRING || op == OPCODE_BINUNICODE || op == OPCODE_UNICODE
                 || op == OPCODE_SHORT_BINUNICODE || op == OPCODE_BINUNICODE8)
            obj = new_object(OBJECT_STRING, &arg);
        else if (op == OPCODE_BINBYTES || op == OPCODE_SHORT_BINBYTES || op == OPCODE_BINBYTES8)
            obj = new_object(OBJECT_BYTES, &arg);
        else if (op == OPCODE_EMPTY_LIST || op == OPCODE_EMPTY_TUPLE)
            obj = new_object(OBJECT_SEQ, NULL);
        else if (op == OPCODE_EMPTY_DICT)
            obj = new_object(OBJECT_DICT, NULL);
        else if (op == OPCODE_EMPTY_SET)
            obj = new_object(OBJECT_SET, NULL);
        else if (op == OPCODE_TUPLE1)
            obj = make_seq(stack, 1, OBJECT_SEQ);
        else if (op == OPCODE_TUPLE2)
            obj = make_seq(stack, 2, OBJECT_SEQ);
        else if (op == OPCODE_TUPLE3)
            obj = make_seq(stack, 3, OBJECT_SEQ);
        else if (op == OPCODE_TUPLE || op == OPCODE_FROZENSET) {
            g_assert(markpos != G_MAXUINT);
            obj = make_seq(stack, slen-1 - markpos, op == OPCODE_FROZENSET ? OBJECT_SET : OBJECT_SEQ);
            g_ptr_array_set_size(stack, markpos);
            slen = stack->len;
        }
        else if (op == OPCODE_DICT) {
            g_assert(markpos != G_MAXUINT);
            /* This is not checked in check_stack_before() because the stack type is non-specific so it does not know
             * we need pairs. */
            if ((slen-1 - markpos) % 2) {
                err_INVALID_STACK(error, op);
                goto fail;
            }
            obj = make_seq(stack, slen-1 - markpos, OBJECT_DICT);
            g_ptr_array_set_size(stack, markpos);
            slen = stack->len;
        }
        else if (op == OPCODE_APPEND)
            slen = append_to_seq(stack, slen-2, FALSE, OBJECT_SEQ);
        else if (op == OPCODE_APPENDS)
            slen = append_to_seq(stack, markpos-1, TRUE, OBJECT_SEQ);
        else if (op == OPCODE_ADDITEMS)
            slen = append_to_seq(stack, markpos-1, TRUE, OBJECT_SET);
        else if (op == OPCODE_SETITEM)
            slen = append_to_seq(stack, slen-3, FALSE, OBJECT_DICT);
        else if (op == OPCODE_SETITEMS)
            slen = append_to_seq(stack, markpos-1, TRUE, OBJECT_DICT);
        else if (op == OPCODE_STACK_GLOBAL)
            obj = make_seq(stack, 2, OBJECT_GLOBAL);
        else if (op == OPCODE_REDUCE)
            obj = make_seq(stack, 2, OBJECT_REDUCE);
        else if (op == OPCODE_BUILD)
            obj = make_seq(stack, 2, OBJECT_OBJECT);
        else {
            /* There are classes, extensions and various stuff we hope to never see here.  But they can mess up our
             * simple-minded reconstruction if they show up. */
            g_warning("opcode %s(%02x) is unhandled", opcode->name, op);
        }

        if (obj) {
#ifdef DEBUG
            gwy_debug("push %s to stack", gwy_enum_to_string(obj->type, objtype_names, G_N_ELEMENTS(objtype_names)));
#endif
            g_ptr_array_add(stack, obj);
        }
    }

    if (stack->len != 1) {
        err_INVALID_STACK(error, OPCODE_STOP);
        goto fail;
    }

    /* We have exactly one object on stack.  Make it survive. */
    retval = g_ptr_array_index(stack, 0);
    retval->refcount++;

fail:
    free_arg(&arg);
    for (i = 0; i < stack->len; i++)
        free_object(g_ptr_array_index(stack, i));
    g_ptr_array_free(stack, TRUE);
    g_ptr_array_free(memoized, TRUE);

    return retval;
}

#ifdef DEBUG
static void dump_object(PickleObject *obj, GString *str);

static void
dump_object(PickleObject *obj, GString *str)
{
    gboolean top_level = !str, do_recurse = FALSE;
    guint i, len;

    if (top_level)
        str = g_string_new(NULL);
    else
        g_string_append(str, "    ");

    len = str->len;
    g_string_append(str, gwy_enum_to_string(obj->type, objtype_names, G_N_ELEMENTS(objtype_names)));
    if (obj->type == OBJECT_NONE || obj->type == OBJECT_MARK) {
    }
    else if (obj->type == OBJECT_BOOL)
        g_string_append_printf(str, "(%s)", obj->v.b ? "True" : "False");
    else if (obj->type == OBJECT_INT)
        g_string_append_printf(str, "(%ld)", obj->v.i);
    else if (obj->type == OBJECT_FLOAT)
        g_string_append_printf(str, "(%g)", obj->v.d);
    else if (obj->type == OBJECT_STRING)
        g_string_append_printf(str, "('%s')", obj->v.s);
    else if (obj->type == OBJECT_BYTES)
        g_string_append_printf(str, "(len=%u)", (guint)obj->len);
    else {
        g_string_append_printf(str, "(nitems=%u)", obj->v.a->len);
        do_recurse = TRUE;
    }

    gwy_debug("%s", str->str);
    if (do_recurse) {
        g_string_truncate(str, len);
        for (i = 0; i < obj->v.a->len; i++)
            dump_object(g_ptr_array_index(obj->v.a, i), str);
    }

    if (top_level)
        g_string_free(str, TRUE);
    else
        g_string_truncate(str, len-4);
}
#endif

/* Find a member object in a sequence-like object by index, type, or both. */
static PickleObject*
get_object_from_seq(PickleObject *parent, gint idx, PickleObjectType type)
{
    GPtrArray *a;
    PickleObject *obj;
    guint i;

    if (parent->type != OBJECT_OBJECT && parent->type != OBJECT_REDUCE && parent->type != OBJECT_GLOBAL
        && parent->type != OBJECT_SEQ)
        return NULL;

    a = parent->v.a;
    if (idx >= 0) {
        if ((guint)idx >= a->len)
            return NULL;
        obj = g_ptr_array_index(a, idx);
        if (type != OBJECT_ANY && obj->type != type)
            return NULL;
#ifdef DEBUG
        gwy_debug("found %s at index %d",
                  gwy_enum_to_string(obj->type, objtype_names, G_N_ELEMENTS(objtype_names)), idx);
#endif
        return obj;
    }

    /* A bit silly. */
    if (type == OBJECT_ANY)
        return a->len ? g_ptr_array_index(a, 0) : NULL;

    for (i = 0; i < a->len; i++) {
        obj = g_ptr_array_index(a, i);
        if (obj->type == type) {
#ifdef DEBUG
            gwy_debug("found %s at index %u",
                      gwy_enum_to_string(obj->type, objtype_names, G_N_ELEMENTS(objtype_names)), i);
#endif
            return obj;
        }
    }
    return NULL;
}

static gboolean
check_global(PickleObject *obj, const gchar *module, const gchar *type)
{
    PickleObject *objmodule, *objtype;

    if (!obj || obj->type != OBJECT_GLOBAL)
        return FALSE;

    g_return_val_if_fail(obj->v.a->len == 2, FALSE);
    if (!(objmodule = get_object_from_seq(obj, 0, OBJECT_STRING))
        || !(objtype = get_object_from_seq(obj, 1, OBJECT_STRING)))
        return FALSE;

    return gwy_strequal(objmodule->v.s, module) && gwy_strequal(objtype->v.s, type);
}

/*
 * OBJECT(nitems=2)
 *     REDUCE(nitems=2)
 *         GLOBAL(nitems=2)
 *             STRING('numpy.core.multiarray')
 *             STRING('_reconstruct')
 *         SEQ(nitems=3)
 *             GLOBAL(nitems=2)
 *                 STRING('numpy')
 *                 STRING('ndarray')
 *             SEQ(nitems=1)
 *                 INT(0)
 *             BYTES(len=1)
 *     SEQ(nitems=5)
 *         INT(1)
 *         SEQ(nitems=2)
 *             INT(256)
 *             INT(256)
 *         OBJECT(nitems=2)
 *             REDUCE(nitems=2)
 *                 GLOBAL(nitems=2)
 *                     STRING('numpy')
 *                     STRING('dtype')
 *                 SEQ(nitems=3)
 *                     STRING('f4')
 *                     INT(0)
 *                     INT(1)
 *             SEQ(nitems=8)
 *                 INT(3)
 *                 STRING('<')
 *                 NONE
 *                 NONE
 *                 NONE
 *                 INT(-1)
 *                 INT(-1)
 *                 INT(0)
 *         BOOL(False)
 *         BYTES(len=262144)
 */
static GwyDataField*
extract_image(PickleObject *root)
{
    GwyDataField *field;
    PickleObject *obj, *obj2, *obj3;
    gint xres, yres;
    GwyByteOrder byteorder;
    GwyRawDataType datatype;
    const gchar *dtype;

    if (root->type != OBJECT_OBJECT)
        return NULL;

    /* Check the type. */
    if (!(obj = get_object_from_seq(root, 0, OBJECT_REDUCE))
        || !(obj = get_object_from_seq(obj, 1, OBJECT_SEQ))
        || !(obj = get_object_from_seq(obj, 0, OBJECT_GLOBAL))
        || !check_global(obj, "numpy", "ndarray"))
        return NULL;
    gwy_debug("is numpy.ndarray");

    if (!(obj = get_object_from_seq(root, 1, OBJECT_SEQ)))
        return NULL;

    /* Extract array dimensions. */
    if (!(obj2 = get_object_from_seq(obj, 1, OBJECT_SEQ)) || obj2->v.a->len != 2)
        return NULL;

    if (!(obj3 = get_object_from_seq(obj2, 0, OBJECT_INT)))
        return NULL;
    yres = obj3->v.i;

    if (!(obj3 = get_object_from_seq(obj2, 1, OBJECT_INT)))
        return NULL;
    xres = obj3->v.i;

    gwy_debug("xres %d, yres %d", xres, yres);
    if (err_DIMENSION(NULL, xres) || err_DIMENSION(NULL, yres))
        return NULL;

    /* Check/extract the data type. */
    if (!(obj = get_object_from_seq(obj, 2, OBJECT_OBJECT)))
        return NULL;

    if (!(obj2 = get_object_from_seq(obj, 0, OBJECT_REDUCE)))
        return NULL;
    if (!(obj3 = get_object_from_seq(obj2, 0, OBJECT_GLOBAL)) || !check_global(obj3, "numpy", "dtype"))
        return NULL;
    gwy_debug("found numpy.dtype");
    if (!(obj3 = get_object_from_seq(obj2, 1, OBJECT_SEQ)) || !(obj3 = get_object_from_seq(obj3, 0, OBJECT_STRING)))
        return NULL;
    dtype = obj3->v.s;
    gwy_debug("dtype %s", dtype);
    if (gwy_strequal(dtype, "f4"))
        datatype = GWY_RAW_DATA_FLOAT;
    else if (gwy_strequal(dtype, "f8"))
        datatype = GWY_RAW_DATA_DOUBLE;
    else
        return NULL;

    /* Extract the byte order. */
    if (!(obj2 = get_object_from_seq(obj, 1, OBJECT_SEQ)) || !(obj3 = get_object_from_seq(obj2, 1, OBJECT_STRING)))
        return NULL;
    gwy_debug("byteorder %s", obj3->v.s);
    byteorder = (gwy_strequal(obj3->v.s, ">") ? GWY_BYTE_ORDER_BIG_ENDIAN : GWY_BYTE_ORDER_LITTLE_ENDIAN);

    /* Extract the data. */
    if (!(obj = get_object_from_seq(root, 1, OBJECT_SEQ)) || !(obj = get_object_from_seq(obj, -1, OBJECT_BYTES)))
        return NULL;

    gwy_debug("%lu bytes", (gulong)obj->len);
    if (obj->len != gwy_raw_data_size(datatype)*xres*yres)
        return NULL;

    gwy_debug("creating data field");
    field = gwy_data_field_new(xres, yres, xres, yres, FALSE);
    gwy_convert_raw_data(obj->v.s, xres*yres, 1, datatype, byteorder, gwy_data_field_get_data(field), 1.0, 0.0);

    return field;
}

static void
gather_units(gpointer hkey, G_GNUC_UNUSED gpointer hvalue, gpointer user_data)
{
    GPtrArray *a = (GPtrArray*)user_data;
    GQuark quark = GPOINTER_TO_UINT(hkey);
    GwyContainer *meta = g_ptr_array_index(a, 0);
    GString *str = g_ptr_array_index(a, 1);
    const gchar *strkey = g_quark_to_string(quark);

    if (!g_str_has_suffix(strkey, "Unit"))
        return;

    g_string_assign(str, strkey);
    g_string_truncate(str, str->len-4);
    if (!gwy_container_contains_by_name(meta, str->str))
        return;

    g_ptr_array_add(a, (gpointer)strkey);
}

/*
 * DICT(nitems=78)
 *     STRING('Direction')
 *     INT(2)
 *     STRING('StepSize')
 *     SEQ(nitems=2)
 *         FLOAT(4.2728)
 *         FLOAT(4.2728)
 *     STRING('ImageSize')
 *     SEQ(nitems=2)
 *         INT(256)
 *         INT(256)
 *     STRING('ImageSizeUnit')
 *     STRING('pixels')
 *     STRING('NoOfChannels')
 *     INT(1)
 *     STRING('ChannelNames')
 *     SEQ(nitems=1)
 *         STRING('CC Mode')
 *     STRING('XOffset')
 *     FLOAT(-146.1)
 *     STRING('XOffsetUnit')
 *     STRING('nm')
 *     STRING('YOffset')
 *     FLOAT(465.6)
 *     STRING('YOffsetUnit')
 *     STRING('nm')
 *     STRING('XCalibration')
 *     FLOAT(18)
 *     STRING('XCalibrationUnit')
 *     STRING('nm/V')
 *     STRING('YCalibration')
 *     FLOAT(18)
 *     STRING('YCalibrationUnit')
 *     STRING('nm/V')
 *     STRING('ZCalibration')
 *     FLOAT(5)
 *     STRING('ZCalibrationUnit')
 *     STRING('nm/V')
 *
 * This is an abridged version.  There are many more fields.
 */
static GwyContainer*
extract_metadata(PickleObject *root, NpicFileInfo *info)
{
    PickleObject *keyobj, *valobj, *obj;
    GwyContainer *meta;
    const gchar *key;
    GPtrArray *a;
    GString *str;
    guint i, j, len;

    if (root->type != OBJECT_DICT)
        return NULL;

    a = root->v.a;
    len = a->len;
    meta = gwy_container_new();
    str = g_string_new(NULL);
    for (i = 0; i+1 < len; i += 2) {
        keyobj = g_ptr_array_index(a, i);
        valobj = g_ptr_array_index(a, i+1);
        if (keyobj->type != OBJECT_STRING)
            continue;


        key = keyobj->v.s;
        if (valobj->type == OBJECT_STRING) {
            gwy_container_set_const_string_by_name(meta, key, valobj->v.s);
            if (gwy_strequal(key, "XCalibrationUnit"))
                info->xcalunit = valobj->v.s;
            else if (gwy_strequal(key, "ZCalibrationUnit"))
                info->zcalunit = valobj->v.s;
        }
        else if (valobj->type == OBJECT_BOOL)
            gwy_container_set_const_string_by_name(meta, key, valobj->v.b ? "True" : "False");
        else if (valobj->type == OBJECT_INT)
            gwy_container_set_string_by_name(meta, key, g_strdup_printf("%ld", (glong)valobj->v.i));
        else if (valobj->type == OBJECT_FLOAT) {
            gwy_container_set_string_by_name(meta, key, g_strdup_printf("%g", valobj->v.d));
            if (gwy_strequal(key, "XCalibration"))
                info->xcal = fabs(valobj->v.d);
            else if (gwy_strequal(key, "YCalibration"))
                info->ycal = fabs(valobj->v.d);
            else if (gwy_strequal(key, "ZCalibration"))
                info->zcal = fabs(valobj->v.d);
        }
        else if (valobj->type == OBJECT_SEQ) {
            if (gwy_strequal(key, "StepSize") && valobj->v.a->len == 2) {
                if (!(obj = get_object_from_seq(valobj, 0, OBJECT_FLOAT)))
                    continue;
                info->xstep = obj->v.d;
                if (!(obj = get_object_from_seq(valobj, 1, OBJECT_FLOAT)))
                    continue;
                info->ystep = obj->v.d;
                gwy_container_set_string_by_name(meta, "StepSizeX", g_strdup_printf("%g", info->xstep));
                gwy_container_set_string_by_name(meta, "StepSizeY", g_strdup_printf("%g", info->ystep));
                gwy_debug("steps %g, %g", info->xstep, info->ystep);
            }
            else if (gwy_strequal(key, "ImageSize") && valobj->v.a->len == 2) {
                /* Ignore.  Use the ndarray size. */
            }
            else if (gwy_strequal(key, "ChannelNames")) {
                for (j = 0; j < valobj->v.a->len; j++) {
                    if ((obj = get_object_from_seq(valobj, j, OBJECT_STRING))) {
                        gwy_debug("channel[%u] = %s", j, obj->v.s);
                        g_ptr_array_add(info->channel_names, obj->v.s);
                        g_string_assign(str, key);
                        g_string_append_printf(str, "[%u]", j);
                        gwy_container_set_const_string_by_name(meta, str->str, obj->v.s);
                    }
                }
            }
            else {
                gwy_debug("Unhandled sequence-like metadata %s.", key);
            }
        }
        else {
            gwy_debug("Unhandled metadata %s of type %d.", key, valobj->type);
        }
    }

    /* Merge FooUnit into Foo. */
    a = g_ptr_array_new();
    g_ptr_array_add(a, meta);
    g_ptr_array_add(a, str);
    gwy_container_foreach(meta, NULL, gather_units, a);
    for (i = 2; i < a->len; i++) {
        const gchar *unit, *value;

        g_string_assign(str, g_ptr_array_index(a, i));
        unit = gwy_container_get_string_by_name(meta, str->str);
        g_string_truncate(str, str->len-4);
        value = gwy_container_get_string_by_name(meta, str->str);

        gwy_container_set_string_by_name(meta, str->str, g_strconcat(value, " ", unit, NULL));
        gwy_container_remove_by_name(meta, g_ptr_array_index(a, i));
    }

    g_string_free(str, TRUE);
    g_ptr_array_free(a, TRUE);

    return meta;
}

static GwyContainer*
npic_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    NpicFileInfo info;
    GwyContainer *container = NULL, *tmpmeta, *meta = NULL;
    GPtrArray *fields = NULL;
    GwyDataField *field;
    guchar *buffer = NULL;
    const guchar *p;
    const gchar *dir;
    gchar *s;
    PickleObject *obj, *metaobj = NULL;
    GwySIUnit *unit, *volt;
    GError *err = NULL;
    gsize size;
    gint power10;
    gdouble q;
    guint i;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&info, 1);
    info.xcal = info.ycal = info.zcal = info.xstep = info.ystep = 1.0;
    fields = g_ptr_array_new();
    info.channel_names = g_ptr_array_new();
    p = buffer;
    while (p - buffer < size) {
        if (!(obj = read_one_object(&p, size - (p - buffer), error)))
            goto fail;
#ifdef DEBUG
        dump_object(obj, NULL);
#endif
        if ((field = extract_image(obj)))
            g_ptr_array_add(fields, field);
        else if (!meta && (meta = extract_metadata(obj, &info))) {
            metaobj = obj;
            metaobj->refcount++;
        }
        free_object(obj);
    }

    if (!fields->len || !meta) {
        err_NO_DATA(error);
        goto fail;
    }

    container = gwy_container_new();
    unit = gwy_si_unit_new(NULL);
    volt = gwy_si_unit_new("V");
    for (i = 0; i < fields->len; i++) {
        field = g_ptr_array_index(fields, i);

        gwy_si_unit_set_from_string_parse(unit, info.xcalunit, &power10);
        gwy_si_unit_multiply(unit, volt, unit);
        q = pow10(power10);
        gwy_data_field_set_xreal(field, q*info.xstep * info.xcal/100.0 * gwy_data_field_get_xres(field));
        gwy_data_field_set_yreal(field, q*info.ystep * info.ycal/100.0 * gwy_data_field_get_yres(field));
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(field), unit);
        gwy_container_set_object(container, gwy_app_get_data_key_for_id(i), field);

        gwy_si_unit_set_from_string_parse(unit, info.zcalunit, &power10);
        gwy_si_unit_multiply(unit, volt, unit);
        q = pow10(power10);
        /* The minus is strange and I cannot find any negative calibration factor or something, but it seems to be
         * the only way to match screenshots. */
        gwy_data_field_multiply(field, -q*info.zcal/100.0);
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), unit);

        gwy_container_set_object(container, gwy_app_get_data_key_for_id(i), field);

        dir = (i%2 ? "[Backward]" : "[Forward]");
        s = NULL;
        if (i/2 < info.channel_names->len)
            s = g_strconcat(g_ptr_array_index(info.channel_names, i/2), " ", dir, NULL);
        else if (gwy_app_channel_title_fall_back(container, i))
            s = g_strconcat(gwy_container_get_string(container, gwy_app_get_data_title_key_for_id(i)), " ", dir, NULL);
        if (s)
            gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(i), s);

        tmpmeta = gwy_container_duplicate(meta);
        gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(i), tmpmeta);
        g_object_unref(tmpmeta);
    }
    g_object_unref(unit);

fail:
    free_object(metaobj);
    GWY_OBJECT_UNREF(meta);
    for (i = 0; i < fields->len; i++)
        GWY_OBJECT_UNREF(g_ptr_array_index(fields, i));
    g_ptr_array_free(fields, TRUE);
    g_ptr_array_free(info.channel_names, TRUE);

    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
