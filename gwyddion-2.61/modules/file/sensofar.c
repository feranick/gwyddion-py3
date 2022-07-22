/*
 *  $Id: sensofar.c 20781 2018-01-31 09:41:03Z yeti-dn $
 *  Copyright (C) 2004-2017 David Necas (Yeti), Petr Klapetek, Jan Horak.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, xhorak@gmail.com.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */
/* TODO: xyz maps, metadata */

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-sensofar-spm">
 *   <comment>Sensofar PLu data</comment>
 *   <glob pattern="*.plu"/>
 *   <glob pattern="*.PLU"/>
 *   <glob pattern="*.apx"/>
 *   <glob pattern="*.APX"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Sensofar PLu
 * .plu, .apx
 * Read
 **/

#include "config.h"
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "get.h"
#include "err.h"

#define Micrometer (1e-6)

enum {
    DATE_SIZE    = 128,
    COMMENT_SIZE = 256,
    HEADER_SIZE  = 500,
    LOST_PIXELS  = 1000001,
};

typedef enum {
    MES_IMATGE                 = 0,
    MES_PERFIL                 = 1,
    MES_MULTIPERFIL            = 2,
    MES_TOPO                   = 3,
    MES_COORD_MULTIPLE_PROFILE = 4,
    MES_GRUIX                  = 5,
    MES_CUSTOM                 = 6,
    MES_COORD_TOPO_MAP         = 7,
    MES_COORD_THICKNESS_MAP    = 8,
} MeasurementType;

typedef enum {
    ALG_INTEN            = 0,
    ALG_GRADIENT         = 1,
    ALG_PSI              = 2,
    ALG_VSI              = 3,
    ALG_EPSI             = 4,
    ALG_THICK_CONFOCAL   = 5,
    ALG_THICK_INTERF     = 6,
    ALG_FOCUS_VARIATION1 = 7,
    ALG_TRACKING         = 8,
    ALG_SCANNING         = 9,
    ALG_FOCUS_VARIATION2 = 10,
    ALG_VSI_SNR          = 11,
    ALG_VSI_CM           = 12,
    ALG_CSSS             = 13,
    ALG_CSDS             = 14,
    ALG_CSQS             = 15,
    ALG_RP               = 16,
} AcquisitionAlgorithm;

typedef enum {
    ACQ_T_NORMAL    = 0,
    ACQ_T_STITCHING = 1,
} AcquisitionMethodTopo;

typedef enum {
    ACQ_IM_CONVENCIONAL  = 0,
    ACQ_IM_CONFOCAL      = 1,
    ACQ_IM_EXTENDED      = 2,
    ACQ_IM_CONFOCALCOLOR = 3,
} AcquisitionMethodImatge;

typedef enum {
    ACQ_P_1LINIA  = 0,
    ACQ_P_NLINIA  = 1,
} AcquisitionMethodPerfil;

typedef enum {
    ACQ_MP_NORMAL    = 0,
    ACQ_MP_STITCHING = 1,
} AcquisitionMethodMultiPerfil;

typedef enum {
    ACQ_MAP_PROFILE_0           = 0,
    ACQ_MAP_PROFILES_0_90       = 1,
    ACQ_MAP_MULTIPLE_PROFILES_A = 2,
    ACQ_MAP_GRID                = 3,
} AcquisitionMethodMap;

typedef enum {
    OBJ_DESCONEGUT           = 0,
    OBJ_SLWD_20X             = 1,
    OBJ_SLWD_50X             = 2,
    OBJ_SLWD_100X            = 3,
    OBJ_EPI_20X              = 4,
    OBJ_EPI_50X              = 5,
    OBJ_EPI_10X              = 6,
    OBJ_EPI_100X             = 7,
    OBJ_ELWD_10X             = 8,
    OBJ_ELWD_20X             = 9,
    OBJ_ELWD_50X             = 10,
    OBJ_ELWD_100X            = 11,
    OBJ_TI_2_5X              = 12,
    OBJ_TI_5X                = 13,
    OBJ_DI_10X               = 14,
    OBJ_DI_20X               = 15,
    OBJ_DI_50X               = 16,
    OBJ_EPI_5X               = 17,
    OBJ_EPI_150X             = 18,
    OBJ_EPI_50X_PLANAPO      = 19,
    OBJ_EPI_1_5X             = 20,
    OBJ_EPI_2_5X             = 21,
    OBJ_EPI_100X_PLANAPO     = 22,
    OBJ_EPI_200X             = 23,
    OBJ_WATER_10X            = 24,
    OBJ_WATER_20X            = 25,
    OBJ_WATER_150X           = 26,
    OBJ_CRLCD_20X_ELWD       = 27,
    OBJ_CRLCD_20X            = 28,
    OBJ_CRLCD_50X            = 29,
    OBJ_CRLCD_100X_A         = 30,
    OBJ_CRLCD_100X_B         = 31,
    OBJ_EPI_2_5X_LEICA       = 32,
    OBJ_EPI_5X_LEICA         = 33,
    OBJ_EPI_10X_LEICA        = 34,
    OBJ_EPI_20X_LEICA        = 35,
    OBJ_EPI_50X_LEICA        = 36,
    OBJ_EPI_50X_0_9_LEICA    = 37,
    OBJ_EPI_100X_LEICA       = 38,
    OBJ_EPI_150X_LEICA       = 39,
    OBJ_LWD_10X_LEICA        = 40,
    OBJ_LWD_20X_LEICA        = 41,
    OBJ_LWD_50X_LEICA        = 42,
    OBJ_LWD_100X_LEICA       = 43,
    OBJ_MICHELSON_5X_LEICA   = 44,
    OBJ_MIRAU_10X_LEICA      = 45,
    OBJ_MIRAU_20X_LEICA      = 46,
    OBJ_MIRAU_50X_LEICA      = 47,
    OBJ_LINNIK_EPI_20X_NIKON = 48,
    OBJ_DI_100X              = 49,
    OBJ_EPI_1_25X_LEICA      = 50,
    OBJ_EPI_20X_LNA_LEICA    = 51,
    OBJ_EPI_40X_LEICA        = 52,
    OBJ_EPI_50X_LNA_LEICA    = 53,
    OBJ_EPI_100X_HNA_LEICA   = 54,
    OBJ_WATER_20X_LEICA      = 55,
    OBJ_WATER_40X_LEICA      = 56,
    OBJ_WATER_63X_LEICA      = 57,
    OBJ_CRLCD_20X_LEICA      = 58,
    OBJ_CRLCD_40X_LEICA      = 59,
    OBJ_MIRAU_5X_SR_LEICA    = 60,
    OBJ_MIRAU_10X_SR_        = 61,
    OBJ_MIRAU_20X_SR_        = 62,
    OBJ_MIRAU_50X_SR_        = 63,
    OBJ_MIRAU_100X_SR_       = 64,
    OBJ_EPI_50X_0_8_LEICA    = 72,
    OBJ_EPI_100X_0_9_LEICA   = 73,
    OBJ_EPI_1X_v35           = 74,
    OBJ_EPI_2_5X_v35         = 75,
    OBJ_EPI_5X_v35           = 76,
    OBJ_EPI_10X_v35          = 77,
    OBJ_EPI_20X_v35          = 78,
    OBJ_EPI_50X_v35          = 79,
    OBJ_EPI_100X_v35         = 80,
    OBJ_EPI_150X_v35         = 81,
    OBJ_ELWD_20X_v35         = 82,
    OBJ_ELWD_50X_v35         = 83,
    OBJ_ELWD_100X_v35        = 84,
    OBJ_SLWD_10X_v35         = 85,
    OBJ_SLWD_20X_v35         = 86,
    OBJ_SLWD_50X_v35         = 87,
    OBJ_SLWD_100X_v35        = 88,
    OBJ_WATER_60X_v35        = 89,
    OBJ_EPI_50X_v50          = 90,
    OBJ_EPI_100X_v50         = 91,
    OBJ_EPI_150X_v50         = 92,
} ObjectiveType;

typedef enum {
    AREA_128         = 0,
    AREA_256         = 1,
    AREA_512         = 2,
    AREA_MAX         = 3,  /* According to hardware below. */
    AREA_L256        = 4,
    AREA_L128        = 5,
    AREA_COORDINATES = 6,
} AreaType;

typedef enum {
    /* 768x576 */
    HWCONF_PLU             = 0,
    HWCONF_PLU_2300_XGA    = 1,
    HWCONF_PLU_2300_XGA_T5 = 2,
    HWCONF_PLU_2300_SXGA   = 3,
    HWCONF_PLU_3300        = 4,
    HWCONF_DCM_3D          = 5,
    HWCONF_PLU_NEOX        = 6,
    HWCONF_DCM_3D_R2       = 7,
    HWCONF_PLU_APEX_P      = 8,
    HWCONF_PLU_NEOX_R2     = 9,
    /* 1360x1024 */
    HWCONF_DCM_3D_R3       = 10,
    HWCONF_PLU_APEX        = 11,
} HardwareConfiguration;

typedef enum {
    FORMAT_VERSION_2000  = 0x00,
    FORMAT_VERSION_2013  = 0xfa,
    FORMAT_VERSION_2012  = 0xfb,
    FORMAT_VERSION_2011B = 0xfc,
    FORMAT_VERSION_2011  = 0xfd,
    FORMAT_VERSION_2010A = 0xfe,
    FORMAT_VERSION_2006  = 0xff,
} FormatVersion;

typedef struct {
    gint name;
    gint value;
} GwyFlatEnum;

typedef struct {
    gchar str[DATE_SIZE];
    time_t t;
} SensofarDate;

typedef struct {
    gint32 xres_area;
    gint32 yres_area;
    gint32 xres;
    gint32 yres;
    gint32 na;
    gdouble incr_z;
    gdouble range;
    gint32 n_planes;
    gint32 tpc_umbral_F;
} SensofarFOVScanSettings;

typedef struct {
    gdouble tracking_range;
    gdouble tracking_speed;
    gint32 tracking_direction;
    gdouble tracking_threshold;
    gdouble tracking_min_angle;
    gint32 confocal_scan_type;
    gdouble confocal_scan_range;
    gdouble confocal_speed_factor;
    gdouble confocal_threshold;
    guchar reserved[4];
} SensofarPointScanSettings;

typedef struct {
    MeasurementType type;
    AcquisitionAlgorithm algorithm;
    guint method;  /* Some of the Method enums, depending on MeasurementType. */
    ObjectiveType objective;
    AreaType area_type;
    /* For AREA_COORDINATES it is point; otherwise it is fov. */
    union {
        SensofarFOVScanSettings fov;
        SensofarPointScanSettings point;
    } settings;
    gboolean restore;
    guint num_layers;
    FormatVersion version;
    HardwareConfiguration config_hardware;
    guint num_images;
    guint reserved;
    gint32 factor_delmacio;
} SensofarConfigMesura;

typedef struct  {
    guint32 yres;
    guint32 xres;
    guint32 N_tall;
    gdouble dy_multip;
    gdouble mppx;
    gdouble mppy;
    /* (x_0, y_0, z_0) is used as the origin also for XYZ data (maps), but
     * not the rest of the struct. */
    gdouble x_0;
    gdouble y_0;
    gdouble mpp_tall;
    gdouble z_0;
} SensofarCalibratEixos_Arxiu;

typedef struct {
    SensofarDate date;
    gchar user_comment[COMMENT_SIZE];
    SensofarCalibratEixos_Arxiu axes_config;
    SensofarConfigMesura measure_config;
} SensofarDataDesc;

static gboolean      module_register       (void);
static gint          sensofar_detect       (const GwyFileDetectInfo *fileinfo,
                                            gboolean only_name);
static GwyContainer* sensofar_load         (const gchar *filename,
                                            GwyRunType mode,
                                            GError **error);
static gboolean      read_calibration_block(const guchar **p,
                                            gsize size,
                                            SensofarCalibratEixos_Arxiu *axes_config,
                                            GError **error);
static gboolean      read_config_mesura    (const guchar **p,
                                            gsize size,
                                            SensofarConfigMesura *measure_config,
                                            GError **error);
static gboolean      read_float_data_field (SensofarDataDesc *data_desc,
                                            guint nrgb,
                                            const gchar *filename,
                                            GwyContainer *container,
                                            guint *channelno,
                                            const guchar **p,
                                            gsize size,
                                            GError **error);
static gboolean      read_rgb_data_field   (SensofarDataDesc *data_desc,
                                            const gchar *filename,
                                            GwyContainer *container,
                                            guint *channelno,
                                            const guchar **p,
                                            gsize size,
                                            GError **error);
static gboolean      read_rgb_data         (SensofarDataDesc *data_desc,
                                            guint xres,
                                            guint yres,
                                            const gchar *filename,
                                            GwyContainer *container,
                                            guint *channelno,
                                            const guchar **p,
                                            gsize size,
                                            GError **error);
static gboolean      read_profiles         (SensofarDataDesc *data_desc,
                                            GwyContainer *container,
                                            guint *channelno,
                                            const guchar **p,
                                            gsize size,
                                            GError **error);
static void          add_image_meta        (SensofarDataDesc *data_desc,
                                            GwyContainer *data,
                                            guint channelno);
static gboolean      parses_as_date        (const gchar *str);

#ifdef GWY_RELOC_SOURCE
static const GwyEnum versions[] = {
    { "2000",  FORMAT_VERSION_2000,  },
    { "2013",  FORMAT_VERSION_2013,  },
    { "2012",  FORMAT_VERSION_2012,  },
    { "2011B", FORMAT_VERSION_2011B, },
    { "2011",  FORMAT_VERSION_2011,  },
    { "2010A", FORMAT_VERSION_2010A, },
    { "2006",  FORMAT_VERSION_2006,  },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit versions[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar versions_name[] =
    "2000\0002013\0002012\0002011B\0002011\0002010A\0002006";

static const GwyFlatEnum versions[] = {
    { 0, FORMAT_VERSION_2000 },
    { 5, FORMAT_VERSION_2013 },
    { 10, FORMAT_VERSION_2012 },
    { 15, FORMAT_VERSION_2011B },
    { 21, FORMAT_VERSION_2011 },
    { 26, FORMAT_VERSION_2010A },
    { 32, FORMAT_VERSION_2006 },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum meas_types[] = {
    { "Confocal Image",               MES_IMATGE,                 },
    { "Profile",                      MES_PERFIL,                 },
    { "Multiple profile",             MES_MULTIPERFIL,            },
    { "Topography",                   MES_TOPO,                   },
    { "Coordinates Multiple Profile", MES_COORD_MULTIPLE_PROFILE, },
    { "Single Point Thickness",       MES_GRUIX,                  },
    { "Custom Application",           MES_CUSTOM,                 },
    { "Coordinates Topography Map",   MES_COORD_TOPO_MAP,         },
    { "Coordinates Thickness Map",    MES_COORD_THICKNESS_MAP,    },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit meas_types[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar meas_types_name[] =
    "Confocal Image\000Profile\000Multiple profile\000Topography\000Coordin"
    "ates Multiple Profile\000Single Point Thickness\000Custom Application"
    "\000Coordinates Topography Map\000Coordinates Thickness Map";

static const GwyFlatEnum meas_types[] = {
    { 0, MES_IMATGE },
    { 15, MES_PERFIL },
    { 23, MES_MULTIPERFIL },
    { 40, MES_TOPO },
    { 51, MES_COORD_MULTIPLE_PROFILE },
    { 80, MES_GRUIX },
    { 103, MES_CUSTOM },
    { 122, MES_COORD_TOPO_MAP },
    { 149, MES_COORD_THICKNESS_MAP },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum acq_methods_topo[] = {
    { "Topography",          ACQ_T_NORMAL,    },
    { "Extended Topography", ACQ_T_STITCHING, },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit acq_methods_topo[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar acq_methods_topo_name[] =
    "Topography\000Extended Topography";

static const GwyFlatEnum acq_methods_topo[] = {
    { 0, ACQ_T_NORMAL },
    { 11, ACQ_T_STITCHING },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum acq_methods_imatge[] = {
    { "Conventional Image", ACQ_IM_CONVENCIONAL,  },
    { "Confocal Image",     ACQ_IM_CONFOCAL,      },
    { "Extended Image",     ACQ_IM_EXTENDED,      },
    { "Confocal RGB",       ACQ_IM_CONFOCALCOLOR, },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit acq_methods_imatge[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar acq_methods_imatge_name[] =
    "Conventional Image\000Confocal Image\000Extended Image\000Confocal RGB";

static const GwyFlatEnum acq_methods_imatge[] = {
    { 0, ACQ_IM_CONVENCIONAL },
    { 19, ACQ_IM_CONFOCAL },
    { 34, ACQ_IM_EXTENDED },
    { 49, ACQ_IM_CONFOCALCOLOR },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum acq_methods_perfil[] = {
    { "Single Profile",   ACQ_P_1LINIA },
    { "Extended Profile", ACQ_P_NLINIA },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit acq_methods_perfil[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar acq_methods_perfil_name[] =
    "Single Profile\000Extended Profile";

static const GwyFlatEnum acq_methods_perfil[] = {
    { 0, ACQ_P_1LINIA },
    { 15, ACQ_P_NLINIA },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum acq_methods_multiperfil[] = {
    { "Multiple Profile",          ACQ_MP_NORMAL,    },
    { "Extended Multiple Profile", ACQ_MP_STITCHING, },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit acq_methods_multiperfil[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar acq_methods_multiperfil_name[] =
    "Multiple Profile\000Extended Multiple Profile";

static const GwyFlatEnum acq_methods_multiperfil[] = {
    { 0, ACQ_MP_NORMAL },
    { 17, ACQ_MP_STITCHING },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum acq_methods_map[] = {
    { "0° profile method",              ACQ_MAP_PROFILE_0,           },
    { "0° and 90° method",              ACQ_MAP_PROFILES_0_90,       },
    { "Multiple profiles at any angle", ACQ_MAP_MULTIPLE_PROFILES_A, },
    { "Map using Grid",                 ACQ_MAP_GRID,                },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit acq_methods_map[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar acq_methods_map_name[] =
    "0° profile method\0000° and 90° method\000Multiple profiles at any "
    "angle\000Map using Grid";

static const GwyFlatEnum acq_methods_map[] = {
    { 0, ACQ_MAP_PROFILE_0 },
    { 19, ACQ_MAP_PROFILES_0_90 },
    { 39, ACQ_MAP_MULTIPLE_PROFILES_A },
    { 70, ACQ_MAP_GRID },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum algorithms[] = {
    { "Confocal Intensity",                        ALG_INTEN,            },
    { "Confocal Gradient",                         ALG_GRADIENT,         },
    { "Interferometric PSI",                       ALG_PSI,              },
    { "Interferometric VSI",                       ALG_VSI,              },
    { "Interferometric ePSI",                      ALG_EPSI,             },
    { "Confocal thickness",                        ALG_THICK_CONFOCAL,   },
    { "Interferometric thickness",                 ALG_THICK_INTERF,     },
    { "Focus Variation",                           ALG_FOCUS_VARIATION1, },
    { "Tracking & confocal (to measure apex)",     ALG_TRACKING,         },
    { "Confocal only (PLu apex system)",           ALG_SCANNING,         },
    { "Focus Variation",                           ALG_FOCUS_VARIATION2, },
    { "Interferometric VSI Smart Noise Reduction", ALG_VSI_SNR,          },
    { "Interferometric VSI Centre of Mass",        ALG_VSI_CM,           },
    { "Confocal Coarse Shift Single Sampling",     ALG_CSSS,             },
    { "Confocal Coarse Shift Double Sampling",     ALG_CSDS,             },
    { "Confocal Coarse Shift Quadrupe Sampling",   ALG_CSQS,             },
    { "Confocal Random Points",                    ALG_RP,               },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit algorithms[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar algorithms_name[] =
    "Confocal Intensity\000Confocal Gradient\000Interferometric PSI\000Inte"
    "rferometric VSI\000Interferometric ePSI\000Confocal thickness\000Inter"
    "ferometric thickness\000Focus Variation\000Tracking & confocal (to mea"
    "sure apex)\000Confocal only (PLu apex system)\000Focus Variation\000In"
    "terferometric VSI Smart Noise Reduction\000Interferometric VSI Centre "
    "of Mass\000Confocal Coarse Shift Single Sampling\000Confocal Coarse Sh"
    "ift Double Sampling\000Confocal Coarse Shift Quadrupe Sampling\000Conf"
    "ocal Random Points";

static const GwyFlatEnum algorithms[] = {
    { 0, ALG_INTEN },
    { 19, ALG_GRADIENT },
    { 37, ALG_PSI },
    { 57, ALG_VSI },
    { 77, ALG_EPSI },
    { 98, ALG_THICK_CONFOCAL },
    { 117, ALG_THICK_INTERF },
    { 143, ALG_FOCUS_VARIATION1 },
    { 159, ALG_TRACKING },
    { 197, ALG_SCANNING },
    { 229, ALG_FOCUS_VARIATION2 },
    { 245, ALG_VSI_SNR },
    { 287, ALG_VSI_CM },
    { 322, ALG_CSSS },
    { 360, ALG_CSDS },
    { 398, ALG_CSQS },
    { 438, ALG_RP },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum area_types[] = {
    { "128×128 pixels",                      AREA_128,         },
    { "256×256 pixels",                      AREA_256,         },
    { "512×512 pixels",                      AREA_512,         },
    { "Camera rows × Camera columns pixels", AREA_MAX,         },
    { "256 × Col. CCD columns pixels",       AREA_L256,        },
    { "128 × Col. CCD columns pixels",       AREA_L128,        },
    { "Coordinates",                         AREA_COORDINATES, },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit area_types[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar area_types_name[] =
    "128×128 pixels\000256×256 pixels\000512×512 pixels\000Camera rows ×"
    " Camera columns pixels\000256 × Col. CCD columns pixels\000128 × Col"
    ". CCD columns pixels\000Coordinates";

static const GwyFlatEnum area_types[] = {
    { 0, AREA_128 },
    { 16, AREA_256 },
    { 32, AREA_512 },
    { 48, AREA_MAX },
    { 85, AREA_L256 },
    { 116, AREA_L128 },
    { 147, AREA_COORDINATES },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum config_hardwares[] = {
    { "PLµ",                       HWCONF_PLU,             },
    { "PLµ 2300, XGA (2003)",      HWCONF_PLU_2300_XGA,    },
    { "PLµ 2300, XGA T5 (2004)",   HWCONF_PLU_2300_XGA_T5, },
    { "PLµ 2300, SXGA (2006)",     HWCONF_PLU_2300_SXGA,   },
    { "PLµ 3300 (2006)",           HWCONF_PLU_3300,        },
    { "DCM 3D (2008)",             HWCONF_DCM_3D,          },
    { "PLu Neox (2009)",           HWCONF_PLU_NEOX,        },
    { "DCM 3D rev 2 (2009)",       HWCONF_DCM_3D_R2,       },
    { "PLu Apex prototype (2010)", HWCONF_PLU_APEX_P,      },
    { "S neox (2013)",             HWCONF_PLU_NEOX_R2,     },
    { "DCM8 (2013)",               HWCONF_DCM_3D_R3,       },
    { "PLu Apex (2012)",           HWCONF_PLU_APEX,        },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit config_hardwares[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar config_hardwares_name[] =
    "PLµ\000PLµ 2300, XGA (2003)\000PLµ 2300, XGA T5 (2004)\000PLµ 2300"
    ", SXGA (2006)\000PLµ 3300 (2006)\000DCM 3D (2008)\000PLu Neox (2009)"
    "\000DCM 3D rev 2 (2009)\000PLu Apex prototype (2010)\000S neox (2013)"
    "\000DCM8 (2013)\000PLu Apex (2012)";

static const GwyFlatEnum config_hardwares[] = {
    { 0, HWCONF_PLU },
    { 5, HWCONF_PLU_2300_XGA },
    { 27, HWCONF_PLU_2300_XGA_T5 },
    { 52, HWCONF_PLU_2300_SXGA },
    { 75, HWCONF_PLU_3300 },
    { 92, HWCONF_DCM_3D },
    { 106, HWCONF_PLU_NEOX },
    { 122, HWCONF_DCM_3D_R2 },
    { 142, HWCONF_PLU_APEX_P },
    { 168, HWCONF_PLU_NEOX_R2 },
    { 182, HWCONF_DCM_3D_R3 },
    { 194, HWCONF_PLU_APEX },
};
#endif /* }}} */

#ifdef GWY_RELOC_SOURCE
static const GwyEnum objectives[] = {
    { "Unknown",                                                OBJ_DESCONEGUT,           },
    { "Nikon CFI Fluor Plan EPI SLWD 20x",                      OBJ_SLWD_20X,             },
    { "Nikon CFI Fluor Plan EPI SLWD 50x",                      OBJ_SLWD_50X,             },
    { "Nikon CFI Fluor Plan EPI SLWD 100x",                     OBJ_SLWD_100X,            },
    { "Nikon CFI Fluor Plan EPI 20x",                           OBJ_EPI_20X,              },
    { "Nikon CFI Fluor Plan EPI 50x",                           OBJ_EPI_50X,              },
    { "Nikon CFI Fluor Plan EPI 10x",                           OBJ_EPI_10X,              },
    { "Nikon CFI Fluor Plan EPI 100x",                          OBJ_EPI_100X,             },
    { "Nikon CFI Fluor Plan EPI ELWD 10x",                      OBJ_ELWD_10X,             },
    { "Nikon CFI Fluor Plan EPI ELWD 20x",                      OBJ_ELWD_20X,             },
    { "Nikon CFI Fluor Plan EPI ELWD 50x",                      OBJ_ELWD_50X,             },
    { "Nikon CFI Fluor Plan EPI ELWD 100x",                     OBJ_ELWD_100X,            },
    { "Nikon CFI Plan Interferential 2.5X",                     OBJ_TI_2_5X,              },
    { "Nikon CFI Plan Interferential 5X T",                     OBJ_TI_5X,                },
    { "Nikon CFI Plan Interferential 10X",                      OBJ_DI_10X,               },
    { "Nikon CFI Plan Interferential 20X",                      OBJ_DI_20X,               },
    { "Nikon CFI Plan Interferential 50X",                      OBJ_DI_50X,               },
    { "Nikon CFI Fluor Plan EPI 5X",                            OBJ_EPI_5X,               },
    { "Nikon CFI Fluor Plan EPI 150X",                          OBJ_EPI_150X,             },
    { "Nikon CFI Fluor Plan Apo EPI 50X",                       OBJ_EPI_50X_PLANAPO,      },
    { "Nikon CFI Fluor Plan EPI 1.5X",                          OBJ_EPI_1_5X,             },
    { "Nikon CFI Fluor Plan EPI 2.5X",                          OBJ_EPI_2_5X,             },
    { "Nikon CFI Fluor Plan Apo EPI 100X",                      OBJ_EPI_100X_PLANAPO,     },
    { "Nikon CFI Fluor Plan EPI 200X",                          OBJ_EPI_200X,             },
    { "Nikon CFI Plan Water Immersion 10X",                     OBJ_WATER_10X,            },
    { "Nikon CFI Plan Water Immersion 20X",                     OBJ_WATER_20X,            },
    { "Nikon CFI Plan Water Immersion 150X",                    OBJ_WATER_150X,           },
    { "Nikon CFI Plan EPI CR ELWD 10X",                         OBJ_CRLCD_20X_ELWD,       },
    { "Nikon CFI Plan EPI CR 20X",                              OBJ_CRLCD_20X,            },
    { "Nikon CFI Plan EPI CR 50X",                              OBJ_CRLCD_50X,            },
    { "Nikon CFI Plan EPI CR 100X A",                           OBJ_CRLCD_100X_A,         },
    { "Nikon CFI Plan EPI CR 100X B",                           OBJ_CRLCD_100X_B,         },
    { "Leica HCX FL Plan 2.5X",                                 OBJ_EPI_2_5X_LEICA,       },
    { "Leica HC PL Fluotar EPI 5X",                             OBJ_EPI_5X_LEICA,         },
    { "Leica HC PL Fluotar EPI 10X",                            OBJ_EPI_10X_LEICA,        },
    { "Leica HC PL Fluotar EPI 20X",                            OBJ_EPI_20X_LEICA,        },
    { "Leica HC PL Fluotar EPI 50X",                            OBJ_EPI_50X_LEICA,        },
    { "Leica HC PL Fluotar EPI 50X HNA",                        OBJ_EPI_50X_0_9_LEICA,    },
    { "Leica HC PL Fluotar EPI 100X",                           OBJ_EPI_100X_LEICA,       },
    { "Leica HC PL Fluotar EPI 50X",                            OBJ_EPI_150X_LEICA,       },
    { "Leica N Plan EPI LWD 10X",                               OBJ_LWD_10X_LEICA,        },
    { "Leica N Plan EPI LWD 20X",                               OBJ_LWD_20X_LEICA,        },
    { "Leica HCX PL Fluotar LWD 50X",                           OBJ_LWD_50X_LEICA,        },
    { "Leica HCX PL Fluotar LWD 100X",                          OBJ_LWD_100X_LEICA,       },
    { "Leica HC PL Fluotar – Interferential Michelson MR 5X", OBJ_MICHELSON_5X_LEICA,   },
    { "Leica HC PL Fluotar – Interferential Mirau MR 10X",    OBJ_MIRAU_10X_LEICA,      },
    { "Leica N PLAN H - Interferential Mirau MR 20X",           OBJ_MIRAU_20X_LEICA,      },
    { "Leica N PLAN H -Interferential Mirau MR 50X",            OBJ_MIRAU_50X_LEICA,      },
    { "Nikon Interferential Linnik EPI 20X",                    OBJ_LINNIK_EPI_20X_NIKON, },
    { "Nikon CFI Plan Interferential 100X DI",                  OBJ_DI_100X,              },
    { "Leica HCX PL FLUOTAR 1.25X",                             OBJ_EPI_1_25X_LEICA,      },
    { "Leica N PLAN EPI 20X",                                   OBJ_EPI_20X_LNA_LEICA,    },
    { "Leica N PLAN EPI 40X",                                   OBJ_EPI_40X_LEICA,        },
    { "Leica N PLAN L 50X",                                     OBJ_EPI_50X_LNA_LEICA,    },
    { "Leica PL APO 100X",                                      OBJ_EPI_100X_HNA_LEICA,   },
    { "Leica HCX APO L U-V-I 20X",                              OBJ_WATER_20X_LEICA,      },
    { "Leica HCX APO L U-V-I 40X",                              OBJ_WATER_40X_LEICA,      },
    { "Leica HCX APO L U-V-I 63X",                              OBJ_WATER_63X_LEICA,      },
    { "Leica HCX PL FLUOTAR 20X",                               OBJ_CRLCD_20X_LEICA,      },
    { "Leica N PLAN L 40X",                                     OBJ_CRLCD_40X_LEICA,      },
    { "Leica Interferential Mirau SR 5X",                       OBJ_MIRAU_5X_SR_LEICA,    },
    { "Leica Interferential Mirau SR 10X",                      OBJ_MIRAU_10X_SR_,        },
    { "Leica Interferential Mirau SR 20X",                      OBJ_MIRAU_20X_SR_,        },
    { "Leica Interferential Mirau SR 50X",                      OBJ_MIRAU_50X_SR_,        },
    { "Leica Interferential Mirau SR 100X",                     OBJ_MIRAU_100X_SR_,       },
    { "Leica HC PL Fluotar EPI 50X 0.8",                        OBJ_EPI_50X_0_8_LEICA,    },
    { "Leica HC PL Fluotar EPI 100X 0.9",                       OBJ_EPI_100X_0_9_LEICA,   },
    { "Nikon CFI T Plan EPI 1X",                                OBJ_EPI_1X_v35,           },
    { "Nikon CFI T Plan EPI 2.5X",                              OBJ_EPI_2_5X_v35,         },
    { "Nikon CFI TU Plan Fluor EPI 5X",                         OBJ_EPI_5X_v35,           },
    { "Nikon CFI TU Plan Fluor EPI 10X",                        OBJ_EPI_10X_v35,          },
    { "Nikon CFI TU Plan Fluor EPI 20X",                        OBJ_EPI_20X_v35,          },
    { "Nikon CFI LU Plan Fluor EPI 50X",                        OBJ_EPI_50X_v35,          },
    { "Nikon CFI TU Plan Fluor EPI 100X",                       OBJ_EPI_100X_v35,         },
    { "Nikon CFI EPI Plan Apo 150X",                            OBJ_EPI_150X_v35,         },
    { "Nikon CFI T Plan EPI ELWD 20X (AV 3.5)",                 OBJ_ELWD_20X_v35,         },
    { "Nikon CFI T Plan EPI ELWD 50X (AV 3.5)",                 OBJ_ELWD_50X_v35,         },
    { "Nikon CFI T Plan EPI ELWD 100X (AV 3.5)",                OBJ_ELWD_100X_v35,        },
    { "Nikon CFI T Plan EPI SLWD 10X (AV 3.5)",                 OBJ_SLWD_10X_v35,         },
    { "Nikon CFI T Plan EPI SLWD 20X (AV 3.5)",                 OBJ_SLWD_20X_v35,         },
    { "Nikon CFI T Plan EPI SLWD 50X (AV 3.5)",                 OBJ_SLWD_50X_v35,         },
    { "Nikon CFI T Plan EPI SLWD 100X (AV 3.5)",                OBJ_SLWD_100X_v35,        },
    { "Nikon CFI Fluor Water Immersion 63X",                    OBJ_WATER_60X_v35,        },
    { "Nikon CFI TU Plan Fluor EPI 50X",                        OBJ_EPI_50X_v50,          },
    { "Nikon CFI TU Plan Apo EPI 100X",                         OBJ_EPI_100X_v50,         },
    { "Nikon CFI TU Plan Apo EPI 150X",                         OBJ_EPI_150X_v50,         },
};
#else  /* {{{ */
/* This code block was GENERATED by flatten.py.
   When you edit objectives[] data above,
   re-run flatten.py SOURCE.c. */
static const gchar objectives_name[] =
    "Unknown\000Nikon CFI Fluor Plan EPI SLWD 20x\000Nikon CFI Fluor Plan E"
    "PI SLWD 50x\000Nikon CFI Fluor Plan EPI SLWD 100x\000Nikon CFI Fluor P"
    "lan EPI 20x\000Nikon CFI Fluor Plan EPI 50x\000Nikon CFI Fluor Plan EP"
    "I 10x\000Nikon CFI Fluor Plan EPI 100x\000Nikon CFI Fluor Plan EPI ELW"
    "D 10x\000Nikon CFI Fluor Plan EPI ELWD 20x\000Nikon CFI Fluor Plan EPI"
    " ELWD 50x\000Nikon CFI Fluor Plan EPI ELWD 100x\000Nikon CFI Plan Inte"
    "rferential 2.5X\000Nikon CFI Plan Interferential 5X T\000Nikon CFI Pla"
    "n Interferential 10X\000Nikon CFI Plan Interferential 20X\000Nikon CFI"
    " Plan Interferential 50X\000Nikon CFI Fluor Plan EPI 5X\000Nikon CFI F"
    "luor Plan EPI 150X\000Nikon CFI Fluor Plan Apo EPI 50X\000Nikon CFI Fl"
    "uor Plan EPI 1.5X\000Nikon CFI Fluor Plan EPI 2.5X\000Nikon CFI Fluor "
    "Plan Apo EPI 100X\000Nikon CFI Fluor Plan EPI 200X\000Nikon CFI Plan W"
    "ater Immersion 10X\000Nikon CFI Plan Water Immersion 20X\000Nikon CFI "
    "Plan Water Immersion 150X\000Nikon CFI Plan EPI CR ELWD 10X\000Nikon C"
    "FI Plan EPI CR 20X\000Nikon CFI Plan EPI CR 50X\000Nikon CFI Plan EPI "
    "CR 100X A\000Nikon CFI Plan EPI CR 100X B\000Leica HCX FL Plan 2.5X"
    "\000Leica HC PL Fluotar EPI 5X\000Leica HC PL Fluotar EPI 10X\000Leica"
    " HC PL Fluotar EPI 20X\000Leica HC PL Fluotar EPI 50X\000Leica HC PL F"
    "luotar EPI 50X HNA\000Leica HC PL Fluotar EPI 100X\000Leica HC PL Fluo"
    "tar EPI 50X\000Leica N Plan EPI LWD 10X\000Leica N Plan EPI LWD 20X"
    "\000Leica HCX PL Fluotar LWD 50X\000Leica HCX PL Fluotar LWD 100X\000L"
    "eica HC PL Fluotar – Interferential Michelson MR 5X\000Leica HC PL F"
    "luotar – Interferential Mirau MR 10X\000Leica N PLAN H - Interferent"
    "ial Mirau MR 20X\000Leica N PLAN H -Interferential Mirau MR 50X\000Nik"
    "on Interferential Linnik EPI 20X\000Nikon CFI Plan Interferential 100X"
    " DI\000Leica HCX PL FLUOTAR 1.25X\000Leica N PLAN EPI 20X\000Leica N P"
    "LAN EPI 40X\000Leica N PLAN L 50X\000Leica PL APO 100X\000Leica HCX AP"
    "O L U-V-I 20X\000Leica HCX APO L U-V-I 40X\000Leica HCX APO L U-V-I 63"
    "X\000Leica HCX PL FLUOTAR 20X\000Leica N PLAN L 40X\000Leica Interfere"
    "ntial Mirau SR 5X\000Leica Interferential Mirau SR 10X\000Leica Interf"
    "erential Mirau SR 20X\000Leica Interferential Mirau SR 50X\000Leica In"
    "terferential Mirau SR 100X\000Leica HC PL Fluotar EPI 50X 0.8\000Leica"
    " HC PL Fluotar EPI 100X 0.9\000Nikon CFI T Plan EPI 1X\000Nikon CFI T "
    "Plan EPI 2.5X\000Nikon CFI TU Plan Fluor EPI 5X\000Nikon CFI TU Plan F"
    "luor EPI 10X\000Nikon CFI TU Plan Fluor EPI 20X\000Nikon CFI LU Plan F"
    "luor EPI 50X\000Nikon CFI TU Plan Fluor EPI 100X\000Nikon CFI EPI Plan"
    " Apo 150X\000Nikon CFI T Plan EPI ELWD 20X (AV 3.5)\000Nikon CFI T Pla"
    "n EPI ELWD 50X (AV 3.5)\000Nikon CFI T Plan EPI ELWD 100X (AV 3.5)\000"
    "Nikon CFI T Plan EPI SLWD 10X (AV 3.5)\000Nikon CFI T Plan EPI SLWD 20"
    "X (AV 3.5)\000Nikon CFI T Plan EPI SLWD 50X (AV 3.5)\000Nikon CFI T Pl"
    "an EPI SLWD 100X (AV 3.5)\000Nikon CFI Fluor Water Immersion 63X\000Ni"
    "kon CFI TU Plan Fluor EPI 50X\000Nikon CFI TU Plan Apo EPI 100X\000Nik"
    "on CFI TU Plan Apo EPI 150X";

static const GwyFlatEnum objectives[] = {
    { 0, OBJ_DESCONEGUT },
    { 8, OBJ_SLWD_20X },
    { 42, OBJ_SLWD_50X },
    { 76, OBJ_SLWD_100X },
    { 111, OBJ_EPI_20X },
    { 140, OBJ_EPI_50X },
    { 169, OBJ_EPI_10X },
    { 198, OBJ_EPI_100X },
    { 228, OBJ_ELWD_10X },
    { 262, OBJ_ELWD_20X },
    { 296, OBJ_ELWD_50X },
    { 330, OBJ_ELWD_100X },
    { 365, OBJ_TI_2_5X },
    { 400, OBJ_TI_5X },
    { 435, OBJ_DI_10X },
    { 469, OBJ_DI_20X },
    { 503, OBJ_DI_50X },
    { 537, OBJ_EPI_5X },
    { 565, OBJ_EPI_150X },
    { 595, OBJ_EPI_50X_PLANAPO },
    { 628, OBJ_EPI_1_5X },
    { 658, OBJ_EPI_2_5X },
    { 688, OBJ_EPI_100X_PLANAPO },
    { 722, OBJ_EPI_200X },
    { 752, OBJ_WATER_10X },
    { 787, OBJ_WATER_20X },
    { 822, OBJ_WATER_150X },
    { 858, OBJ_CRLCD_20X_ELWD },
    { 889, OBJ_CRLCD_20X },
    { 915, OBJ_CRLCD_50X },
    { 941, OBJ_CRLCD_100X_A },
    { 970, OBJ_CRLCD_100X_B },
    { 999, OBJ_EPI_2_5X_LEICA },
    { 1022, OBJ_EPI_5X_LEICA },
    { 1049, OBJ_EPI_10X_LEICA },
    { 1077, OBJ_EPI_20X_LEICA },
    { 1105, OBJ_EPI_50X_LEICA },
    { 1133, OBJ_EPI_50X_0_9_LEICA },
    { 1165, OBJ_EPI_100X_LEICA },
    { 1194, OBJ_EPI_150X_LEICA },
    { 1222, OBJ_LWD_10X_LEICA },
    { 1247, OBJ_LWD_20X_LEICA },
    { 1272, OBJ_LWD_50X_LEICA },
    { 1301, OBJ_LWD_100X_LEICA },
    { 1331, OBJ_MICHELSON_5X_LEICA },
    { 1386, OBJ_MIRAU_10X_LEICA },
    { 1438, OBJ_MIRAU_20X_LEICA },
    { 1483, OBJ_MIRAU_50X_LEICA },
    { 1527, OBJ_LINNIK_EPI_20X_NIKON },
    { 1563, OBJ_DI_100X },
    { 1601, OBJ_EPI_1_25X_LEICA },
    { 1628, OBJ_EPI_20X_LNA_LEICA },
    { 1649, OBJ_EPI_40X_LEICA },
    { 1670, OBJ_EPI_50X_LNA_LEICA },
    { 1689, OBJ_EPI_100X_HNA_LEICA },
    { 1707, OBJ_WATER_20X_LEICA },
    { 1733, OBJ_WATER_40X_LEICA },
    { 1759, OBJ_WATER_63X_LEICA },
    { 1785, OBJ_CRLCD_20X_LEICA },
    { 1810, OBJ_CRLCD_40X_LEICA },
    { 1829, OBJ_MIRAU_5X_SR_LEICA },
    { 1862, OBJ_MIRAU_10X_SR_ },
    { 1896, OBJ_MIRAU_20X_SR_ },
    { 1930, OBJ_MIRAU_50X_SR_ },
    { 1964, OBJ_MIRAU_100X_SR_ },
    { 1999, OBJ_EPI_50X_0_8_LEICA },
    { 2031, OBJ_EPI_100X_0_9_LEICA },
    { 2064, OBJ_EPI_1X_v35 },
    { 2088, OBJ_EPI_2_5X_v35 },
    { 2114, OBJ_EPI_5X_v35 },
    { 2145, OBJ_EPI_10X_v35 },
    { 2177, OBJ_EPI_20X_v35 },
    { 2209, OBJ_EPI_50X_v35 },
    { 2241, OBJ_EPI_100X_v35 },
    { 2274, OBJ_EPI_150X_v35 },
    { 2302, OBJ_ELWD_20X_v35 },
    { 2341, OBJ_ELWD_50X_v35 },
    { 2380, OBJ_ELWD_100X_v35 },
    { 2420, OBJ_SLWD_10X_v35 },
    { 2459, OBJ_SLWD_20X_v35 },
    { 2498, OBJ_SLWD_50X_v35 },
    { 2537, OBJ_SLWD_100X_v35 },
    { 2577, OBJ_WATER_60X_v35 },
    { 2613, OBJ_EPI_50X_v50 },
    { 2645, OBJ_EPI_100X_v50 },
    { 2676, OBJ_EPI_150X_v50 },
};
#endif /* }}} */

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Sensofar PLu file format, version 2000 or newer."),
    "Jan Hořák <xhorak@gmail.com>, Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Jan Hořák",
    "2008",
};

GWY_MODULE_QUERY2(module_info, sensofar)

static gboolean
module_register(void)
{
    gwy_file_func_register("sensofar",
                           N_("Sensofar PLu files (.plu, .apx)"),
                           (GwyFileDetectFunc)&sensofar_detect,
                           (GwyFileLoadFunc)&sensofar_load,
                           NULL,
                           NULL);
    return TRUE;
}

static gint
sensofar_detect(const GwyFileDetectInfo *fileinfo,
                gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, ".plu") ? 20 : 0;

    /* Byte 490 is the version. */
    if (fileinfo->file_size >= HEADER_SIZE + 12
        && fileinfo->buffer_len >= HEADER_SIZE && parses_as_date(fileinfo->head)
        && (fileinfo->head[490] == 0 || (guint)(fileinfo->head[490]) >= 0xf0))
        return 85;

    return 0;
}

static GwyContainer*
sensofar_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    SensofarDataDesc data_desc;
    SensofarConfigMesura *measure_config;
    MeasurementType meas_type;
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    const guchar *p;
    gboolean imatge_is_grey;
    guint i, channelno;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (size < HEADER_SIZE + 12) {
        err_TRUNCATED_HEADER(error);
        goto fail;
    }

    /* Date block */
    p = buffer;
    memcpy(&data_desc.date.str, p, DATE_SIZE);
    data_desc.date.str[DATE_SIZE-1] = '\0';
    p += DATE_SIZE;
    data_desc.date.t = gwy_get_guint32_le(&p);

    /* Comment block */
    memcpy(&data_desc.user_comment, p, COMMENT_SIZE);
    data_desc.user_comment[COMMENT_SIZE-1] = '\0';
    p += COMMENT_SIZE;

    if (!read_calibration_block(&p, size - (p - buffer), &data_desc.axes_config,
                            error))
        goto fail;

    measure_config = &data_desc.measure_config;
    if (!read_config_mesura(&p, size - (p - buffer), measure_config, error))
        goto fail;

    gwy_debug("Format version=%d, date=<%s>",
              measure_config->version,
              data_desc.date.str);
    gwy_debug("Data type=%d, num_layers=%d, num_images=%d",
              measure_config->type,
              measure_config->num_layers,
              measure_config->num_images);
    gwy_debug("Res xres=%d, yres=%d",
              measure_config->settings.fov.xres,
              measure_config->settings.fov.yres);
    gwy_debug("Acquisition method=%d, algorithm=%d",
              measure_config->method,
              measure_config->algorithm);

    if (!measure_config->num_layers) {
        err_NO_DATA(error);
        goto fail;
    }
    meas_type = measure_config->type;
    imatge_is_grey = (measure_config->version == FORMAT_VERSION_2000
                      || measure_config->version == FORMAT_VERSION_2006);

    container = gwy_container_new();
    channelno = 0;

    for (i = 0; i < measure_config->num_layers; i++) {
        if (meas_type == MES_TOPO) {
            if (!read_float_data_field(&data_desc, measure_config->num_images,
                                       filename, container, &channelno,
                                       &p, size - (p - buffer), error))
                goto fail;
        }
        else if (imatge_is_grey && meas_type == MES_IMATGE) {
            if (!read_float_data_field(&data_desc, 0,
                                       filename, container, &channelno,
                                       &p, size - (p - buffer), error))
                goto fail;
        }
        else if (!imatge_is_grey && meas_type == MES_IMATGE) {
            if (!read_rgb_data_field(&data_desc, filename,
                                     container, &channelno,
                                     &p, size - (p - buffer), error))
                goto fail;
        }
        else if (meas_type == MES_PERFIL
                 || meas_type == MES_GRUIX
                 || meas_type == MES_MULTIPERFIL) {
            if (!read_profiles(&data_desc, container, &channelno,
                               &p, size - (p - buffer), error))
                goto fail;
        }
        else {
            err_DATA_TYPE(error, measure_config->type);
            goto fail;
        }
    }

    /* Since v2011B there is some additional information after the data,
     * in particular operator and saple name.  The specs does not seem agree
     * with reality though. */
    gwy_debug("remaining data length: %lu", (gulong)(size - (p - buffer)));

    return container;

fail:
    gwy_file_abandon_contents(buffer, size, NULL);
    GWY_OBJECT_UNREF(container);
    return NULL;
}

static gboolean
read_calibration_block(const guchar **p, gsize size,
                       SensofarCalibratEixos_Arxiu *axes_config,
                       GError **error)
{
    if (size < 40) {
        err_TRUNCATED_PART(error, "tCalibratEixos_Arxiu");
        return FALSE;
    }

    /* Calbration block */
    axes_config->yres = gwy_get_guint32_le(p);
    axes_config->xres = gwy_get_guint32_le(p);
    axes_config->N_tall = gwy_get_guint32_le(p);
    axes_config->dy_multip = gwy_get_gfloat_le(p);
    axes_config->mppx = gwy_get_gfloat_le(p);
    axes_config->mppy = gwy_get_gfloat_le(p);
    axes_config->x_0 = gwy_get_gfloat_le(p);
    axes_config->y_0 = gwy_get_gfloat_le(p);
    axes_config->mpp_tall = gwy_get_gfloat_le(p);
    axes_config->z_0 = gwy_get_gfloat_le(p);

    return TRUE;
}

static gboolean
read_config_mesura(const guchar **p, gsize size,
                   SensofarConfigMesura *measure_config,
                   GError **error)
{
    if (size < 72) {
        err_TRUNCATED_PART(error, "tConfigMesura");
        return FALSE;
    }

    /* Measurement block */
    measure_config->type = gwy_get_guint32_le(p);
    measure_config->algorithm = gwy_get_guint32_le(p);
    measure_config->method = gwy_get_guint32_le(p);
    measure_config->objective = gwy_get_guint32_le(p);
    measure_config->area_type = gwy_get_guint32_le(p);
    if (measure_config->area_type == AREA_COORDINATES) {
        SensofarPointScanSettings *settings = &measure_config->settings.point;

        settings->tracking_range = gwy_get_gfloat_le(p);
        settings->tracking_speed = gwy_get_gfloat_le(p);
        settings->tracking_direction = gwy_get_guint32_le(p);
        settings->tracking_threshold = gwy_get_gfloat_le(p);
        settings->tracking_min_angle = gwy_get_gfloat_le(p);
        settings->confocal_scan_type = gwy_get_guint32_le(p);
        settings->confocal_scan_range = gwy_get_gfloat_le(p);
        settings->confocal_speed_factor = gwy_get_gfloat_le(p);
        settings->confocal_threshold = gwy_get_gfloat_le(p);
        get_CHARS(settings->reserved, p, 4);
    }
    else {
        SensofarFOVScanSettings *settings = &measure_config->settings.fov;

        settings->xres_area = gwy_get_guint32_le(p);
        settings->yres_area = gwy_get_guint32_le(p);
        settings->xres = gwy_get_guint32_le(p);
        settings->yres = gwy_get_guint32_le(p);
        settings->na = gwy_get_guint32_le(p);
        settings->incr_z = gwy_get_gdouble_le(p);
        settings->range = gwy_get_gfloat_le(p);
        settings->n_planes = gwy_get_guint32_le(p);
        settings->tpc_umbral_F = gwy_get_guint32_le(p);
    }
    measure_config->restore = gwy_get_gboolean8(p);
    measure_config->num_layers = *((*p)++);
    measure_config->version = *((*p)++);
    measure_config->config_hardware = *((*p)++);
    measure_config->num_images = *((*p)++);
    measure_config->reserved = *((*p)++);
    *p += 2; // struct padding
    measure_config->factor_delmacio = gwy_get_guint32_le(p);

    return TRUE;
}

static gboolean
read_float_data_field(SensofarDataDesc *data_desc, guint nrgb,
                      const gchar *filename,
                      GwyContainer *container, guint *channelno,
                      const guchar **p, gsize size,
                      GError **error)
{
    GwyDataField *dfield, *mfield;
    guint xres, yres, i, j, mcount;
    gdouble *data, *mdata;
    const guchar *buf = *p;
    GQuark quark;

    yres = gwy_get_guint32_le(p);
    xres = gwy_get_guint32_le(p);
    gwy_debug("Data size: %dx%d", xres, yres);
    if (err_SIZE_MISMATCH(error, (xres*yres + 2)*sizeof(gfloat),
                          size - 2*sizeof(guint32), FALSE))
        return FALSE;
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return FALSE;
    if (!((data_desc->axes_config.mppx
           = fabs(data_desc->axes_config.mppx)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        data_desc->axes_config.mppx = 1.0;
    }
    if (!((data_desc->axes_config.mppy
           = fabs(data_desc->axes_config.mppy)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        data_desc->axes_config.mppy = 1.0;
    }

    dfield = gwy_data_field_new(xres, yres,
                                data_desc->axes_config.mppx * xres * Micrometer,
                                data_desc->axes_config.mppy * yres * Micrometer,
                                FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");

    mfield = gwy_data_field_new_alike(dfield, FALSE);
    gwy_data_field_fill(mfield, 1.0);

    /* In older files we can get here with both TOPO and IMGTE types. */
    if (data_desc->measure_config.type == MES_TOPO)
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "m");

    data = gwy_data_field_get_data(dfield);
    mdata = gwy_data_field_get_data(mfield);
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gdouble v = gwy_get_gfloat_le(p);
            if (v == LOST_PIXELS)
                mdata[i*xres + j] = 0.0;
            else
                data[i*xres + j] = v;
        }
    }
    *p += 2*sizeof(gfloat);   /* Data min and data max. */

    if (data_desc->measure_config.type == MES_TOPO)
        gwy_data_field_multiply(dfield, Micrometer);

    gwy_debug("Offset: %g %g",
              data_desc->axes_config.x_0, data_desc->axes_config.y_0);
    //FIXME: offset later, support of offset determined by version?
    //gwy_data_field_set_xoffset(d, pow10(power10)*data_desc.axes_config.x_0);
    //gwy_data_field_set_yoffset(d, pow10(power10)*data_desc.axes_config.y_0);

    mcount = gwy_app_channel_remove_bad_data(dfield, mfield);

    quark = gwy_app_get_data_key_for_id(*channelno);
    gwy_container_set_object(container, quark, dfield);
    if (mcount) {
        quark = gwy_app_get_mask_key_for_id(*channelno);
        gwy_container_set_object(container, quark, mfield);
    }
    gwy_app_channel_title_fall_back(container, *channelno);
    add_image_meta(data_desc, container, *channelno);
    gwy_file_channel_import_log_add(container, *channelno, NULL, filename);
    (*channelno)++;

    g_object_unref(mfield);
    g_object_unref(dfield);

    if (!nrgb)
        return TRUE;

    size -= (*p - buf);
    for (i = 0; i < nrgb; i++) {
        if (!read_rgb_data(data_desc, xres, yres, filename,
                           container, channelno, p, size,
                           error))
            return FALSE;
        size -= 3*xres*yres;
    }

    return TRUE;
}

static gboolean
read_rgb_data_field(SensofarDataDesc *data_desc,
                    const gchar *filename,
                    GwyContainer *container, guint *channelno,
                    const guchar **p, gsize size,
                    GError **error)
{
    guint xres, yres;

    if (size < 2*sizeof(guint32)) {
        err_TRUNCATED_PART(error, "RGB data");
        return FALSE;
    }
    yres = gwy_get_guint32_le(p);
    xres = gwy_get_guint32_le(p);
    gwy_debug("Data size: %dx%d", xres, yres);
    return read_rgb_data(data_desc, xres, yres, filename,
                         container, channelno, p, size - 2*sizeof(guint32),
                         error);
}

static gboolean
read_rgb_data(SensofarDataDesc *data_desc,
              guint xres, guint yres,
              const gchar *filename,
              GwyContainer *container, guint *channelno,
              const guchar **p, gsize size,
              GError **error)
{
    GwyDataField *rfield, *gfield, *bfield;
    guint k;
    gdouble *rdata, *gdata, *bdata;
    gboolean is_grey;
    GQuark quark;

    if (err_SIZE_MISMATCH(error, xres*yres*3, size, FALSE))
        return FALSE;
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return FALSE;
    if (!((data_desc->axes_config.mppx
           = fabs(data_desc->axes_config.mppx)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        data_desc->axes_config.mppx = 1.0;
    }
    if (!((data_desc->axes_config.mppy
           = fabs(data_desc->axes_config.mppy)) > 0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        data_desc->axes_config.mppy = 1.0;
    }

    rfield = gwy_data_field_new(xres, yres,
                                data_desc->axes_config.mppx * xres * Micrometer,
                                data_desc->axes_config.mppy * yres * Micrometer,
                                FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(rfield), "m");

    gfield = gwy_data_field_new_alike(rfield, FALSE);
    bfield = gwy_data_field_new_alike(rfield, FALSE);

    rdata = gwy_data_field_get_data(rfield);
    gdata = gwy_data_field_get_data(gfield);
    bdata = gwy_data_field_get_data(bfield);
    is_grey = TRUE;

    for (k = 0; k < xres*yres; k++) {
        rdata[k] = *((*p)++);
        gdata[k] = *((*p)++);
        bdata[k] = *((*p)++);
        if (rdata[k] != gdata[k] || gdata[k] != bdata[k])
            is_grey = FALSE;
    }

    gwy_debug("Offset: %g %g",
              data_desc->axes_config.x_0, data_desc->axes_config.y_0);
    //FIXME: offset later, support of offset determined by version?
    //gwy_data_field_set_xoffset(d, pow10(power10)*data_desc.axes_config.x_0);
    //gwy_data_field_set_yoffset(d, pow10(power10)*data_desc.axes_config.y_0);

    /* Do not create three images when they are all the same. */
    if (is_grey) {
        quark = gwy_app_get_data_key_for_id(*channelno);
        gwy_container_set_object(container, quark, rfield);
        quark = gwy_app_get_data_palette_key_for_id(*channelno);
        gwy_container_set_const_string(container, quark, "Gray");
        quark = gwy_app_get_data_title_key_for_id(*channelno);
        gwy_container_set_const_string(container, quark, "Gray");
        add_image_meta(data_desc, container, *channelno);
        gwy_file_channel_import_log_add(container, *channelno, NULL, filename);
        (*channelno)++;
    }
    else {
        quark = gwy_app_get_data_key_for_id(*channelno);
        gwy_container_set_object(container, quark, rfield);
        quark = gwy_app_get_data_palette_key_for_id(*channelno);
        gwy_container_set_const_string(container, quark, "RGB-Red");
        quark = gwy_app_get_data_title_key_for_id(*channelno);
        gwy_container_set_const_string(container, quark, "Red");
        add_image_meta(data_desc, container, *channelno);
        gwy_file_channel_import_log_add(container, *channelno, NULL, filename);
        (*channelno)++;

        quark = gwy_app_get_data_key_for_id(*channelno);
        gwy_container_set_object(container, quark, gfield);
        quark = gwy_app_get_data_palette_key_for_id(*channelno);
        gwy_container_set_const_string(container, quark, "RGB-Green");
        quark = gwy_app_get_data_title_key_for_id(*channelno);
        gwy_container_set_const_string(container, quark, "Green");
        add_image_meta(data_desc, container, *channelno);
        gwy_file_channel_import_log_add(container, *channelno, NULL, filename);
        (*channelno)++;

        quark = gwy_app_get_data_key_for_id(*channelno);
        gwy_container_set_object(container, quark, bfield);
        quark = gwy_app_get_data_palette_key_for_id(*channelno);
        gwy_container_set_const_string(container, quark, "RGB-Blue");
        quark = gwy_app_get_data_title_key_for_id(*channelno);
        gwy_container_set_const_string(container, quark, "Blue");
        add_image_meta(data_desc, container, *channelno);
        gwy_file_channel_import_log_add(container, *channelno, NULL, filename);
        (*channelno)++;
    }
    g_object_unref(bfield);
    g_object_unref(gfield);
    g_object_unref(rfield);

    return TRUE;
}

static gboolean
read_profiles(SensofarDataDesc *data_desc,
              GwyContainer *container, guint *channelno,
              const guchar **p, gsize size,
              GError **error)
{
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    guint xres, yres, i, j, n;
    GwySIUnit *units = NULL;
    gdouble *xdata, *ydata;
    gchar *s;
    GQuark quark;
    gdouble dx;

    /* The yres is present and currectly set to 1 for MES_PERFIL and MES_GRUIX
     * so we can process single and multiple profiles the same. */
    yres = gwy_get_guint32_le(p);
    xres = gwy_get_guint32_le(p);
    gwy_debug("Data size: %dx%d", xres, yres);
    if (err_SIZE_MISMATCH(error, (xres*yres + 2)*sizeof(gfloat),
                          size - 2*sizeof(guint32), FALSE))
        return FALSE;
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return FALSE;
    if (!((data_desc->axes_config.mppx
           = fabs(data_desc->axes_config.mppx)) > 0)) {
        g_warning("Real x size is 0.0, fixing to 1.0");
        data_desc->axes_config.mppx = 1.0;
    }

    xdata = g_new(gdouble, xres);
    ydata = g_new(gdouble, xres);
    dx = data_desc->axes_config.mppx * Micrometer;

    gmodel = gwy_graph_model_new();
    g_object_set(gmodel, "title", _("Profile"), NULL);

    units = gwy_si_unit_new("m"); /* values are in µm only */
    g_object_set(gmodel, "si-unit-x", units, NULL);
    g_object_unref(units);

    units = gwy_si_unit_new("m"); /* values are in µm only */
    g_object_set(gmodel, "si-unit-y", units, NULL);
    g_object_unref(units);

    for (i = 0; i < yres; i++) {
        for (j = n = 0; j < xres; j++) {
            gdouble v = gwy_get_gfloat_le(p);
            if (v != LOST_PIXELS) {
                xdata[n] = dx*j;
                ydata[n] = v*Micrometer;
                n++;
            }
        }

        if (!n)
            continue;

        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, n);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(i),
                     NULL);
        if (yres == 1)
            g_object_set(gcmodel, "description", _("Profile"), NULL);
        else {
            s = g_strdup_printf(_("Profile %u"), i+1);
            g_object_set(gcmodel, "description", s, NULL);
            g_free(s);
        }
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }
    *p += 2*sizeof(gfloat);   /* Data min and data max. */

    g_free(xdata);
    g_free(ydata);

    if (!gwy_graph_model_get_n_curves(gmodel)) {
        g_object_unref(gmodel);
        err_NO_DATA(error);
        return FALSE;
    }

    quark = gwy_app_get_graph_key_for_id(*channelno);
    gwy_container_set_object(container, quark, gmodel);
    g_object_unref(gmodel);

    (*channelno)++;

    return TRUE;
}

static const gchar*
gwy_flat_enum_to_string(gint enumval,
                        guint nentries,
                        const GwyFlatEnum *table,
                        const gchar *names)
{
    gint j;

    for (j = 0; j < nentries; j++) {
        if (enumval == table[j].value)
            return names + table[j].name;
    }

    return NULL;
}

static void
set_meta_flat_enum(GwyContainer *meta, guint value, const gchar *name,
                   const GwyFlatEnum *table, const gchar *strings, guint n)
{
    const gchar *s;

    if ((s = gwy_flat_enum_to_string(value, n, table, strings)))
        gwy_container_set_const_string_by_name(meta, name, s);
}

static void
add_image_meta(SensofarDataDesc *data_desc,
               GwyContainer *data, guint channelno)
{
    SensofarCalibratEixos_Arxiu *axes_config = &data_desc->axes_config;
    SensofarConfigMesura *measure_config = &data_desc->measure_config;
    SensofarFOVScanSettings *fov = &measure_config->settings.fov;
    GwyContainer *meta;
    GQuark quark;

    meta = gwy_container_new();
    gwy_container_set_const_string_by_name(meta, "Date", data_desc->date.str);
    if (strlen(data_desc->user_comment)) {
        gwy_container_set_const_string_by_name(meta, "Comment",
                                               data_desc->user_comment);
    }
    gwy_container_set_string_by_name(meta, "X0",
                                     g_strdup_printf("%g µm",
                                                     axes_config->x_0));
    gwy_container_set_string_by_name(meta, "Y0",
                                     g_strdup_printf("%g µm",
                                                     axes_config->y_0));
    gwy_container_set_string_by_name(meta, "Z0",
                                     g_strdup_printf("%g µm",
                                                     axes_config->z_0));
    gwy_container_set_string_by_name(meta, "Number of FOVs",
                                     g_strdup_printf("%d", fov->na));
    gwy_container_set_string_by_name(meta, "Dz step",
                                     g_strdup_printf("%g µm",
                                                     fov->incr_z));
    gwy_container_set_string_by_name(meta, "Scan Z range",
                                     g_strdup_printf("%g µm",
                                                     fov->range));
    gwy_container_set_string_by_name(meta, "Number of planes",
                                     g_strdup_printf("%d", fov->n_planes));
    gwy_container_set_string_by_name(meta, "Acquisition threshold",
                                     g_strdup_printf("%d %%",
                                                     fov->tpc_umbral_F));
    gwy_container_set_string_by_name(meta, "Number of layers",
                                     g_strdup_printf("%d",
                                                     measure_config->num_layers));
    gwy_container_set_string_by_name(meta, "Decimation factor",
                                     g_strdup_printf("%d",
                                                     measure_config->factor_delmacio));

    set_meta_flat_enum(meta, measure_config->version, "Format version",
                       versions, versions_name, G_N_ELEMENTS(versions));
    set_meta_flat_enum(meta, measure_config->type, "Measurement type",
                       meas_types, meas_types_name, G_N_ELEMENTS(meas_types));
    set_meta_flat_enum(meta, measure_config->algorithm, "Algorithm",
                       algorithms, algorithms_name, G_N_ELEMENTS(algorithms));
    set_meta_flat_enum(meta, measure_config->objective, "Objective",
                       objectives, objectives_name, G_N_ELEMENTS(objectives));
    set_meta_flat_enum(meta, measure_config->area_type, "Area type",
                       area_types, area_types_name, G_N_ELEMENTS(area_types));
    set_meta_flat_enum(meta, measure_config->area_type, "Area type",
                       area_types, area_types_name, G_N_ELEMENTS(area_types));

    /* Field incorrect in older versions. */
    if (measure_config->version != FORMAT_VERSION_2000
        && measure_config->version <= FORMAT_VERSION_2012) {
        set_meta_flat_enum(meta, measure_config->config_hardware,
                           "Hardware configurations",
                           config_hardwares, config_hardwares_name,
                           G_N_ELEMENTS(config_hardwares));
    }

    if (measure_config->type == MES_IMATGE) {
        set_meta_flat_enum(meta, measure_config->method, "Acquisition method",
                           acq_methods_imatge, acq_methods_imatge_name,
                           G_N_ELEMENTS(acq_methods_imatge));
    }
    else if (measure_config->type == MES_PERFIL) {
        set_meta_flat_enum(meta, measure_config->method, "Acquisition method",
                           acq_methods_perfil, acq_methods_perfil_name,
                           G_N_ELEMENTS(acq_methods_perfil));
    }
    else if (measure_config->type == MES_MULTIPERFIL) {
        set_meta_flat_enum(meta, measure_config->method, "Acquisition method",
                           acq_methods_multiperfil,
                           acq_methods_multiperfil_name,
                           G_N_ELEMENTS(acq_methods_multiperfil));
    }
    else if (measure_config->type == MES_IMATGE) {
        set_meta_flat_enum(meta, measure_config->method, "Acquisition method",
                           acq_methods_topo, acq_methods_topo_name,
                           G_N_ELEMENTS(acq_methods_topo));
    }
    if (measure_config->type == MES_COORD_MULTIPLE_PROFILE
        || measure_config->type == MES_COORD_TOPO_MAP
        || measure_config->type == MES_COORD_THICKNESS_MAP) {
        set_meta_flat_enum(meta, measure_config->method, "Acquisition method",
                           acq_methods_map, acq_methods_map_name,
                           G_N_ELEMENTS(acq_methods_map));
    }

    quark = gwy_app_get_data_meta_key_for_id(channelno);
    gwy_container_set_object(data, quark, meta);
    g_object_unref(meta);
}

/* File starts with date, try to parse it.
 * FIXME: this is stupid */
static gboolean
parses_as_date(const gchar *str)
{
    char day_name[4], month_name[4];
    int month_day, hour, min, sec, year;

    if (str[24] != '\0' && !g_ascii_isspace(str[24]))
        return FALSE;

    if (sscanf(str, "%3s %3s %u %u:%u:%u %u",
               day_name, month_name, &month_day, &hour, &min, &sec, &year) != 7)
        return FALSE;

    if (strlen(day_name) != 3 || strlen(month_name) != 3)
        return FALSE;

    if (!gwy_stramong(day_name,
                      "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", NULL))
        return FALSE;

    if (!gwy_stramong(month_name,
                      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL))
        return FALSE;

    return TRUE;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
