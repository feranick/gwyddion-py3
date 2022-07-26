/*
 *  $Id: gwyfdcurvepreset.c 24520 2021-11-13 18:45:09Z klapetek $
 *  Copyright (C) 2000-2003 Martin Siler.
 *  Copyright (C) 2004 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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

#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libgwyddion/gwyfdcurvepreset.h>
#include "gwyddioninternal.h"

static GwyFDCurvePreset*
gwy_fd_curve_preset_new_static(const GwyNLFitPresetBuiltin *data);

G_DEFINE_TYPE(GwyFDCurvePreset, gwy_fd_curve_preset, GWY_TYPE_NLFIT_PRESET)


/******************* sszanette ********************************/
static gdouble
sszanette_func(gdouble x,
               G_GNUC_UNUSED gint n_param,
               const gdouble *b,
               G_GNUC_UNUSED gpointer user_data,
               gboolean *fres)
{
    /*xc, R, H */
    *fres = TRUE;
    return b[1] -b[3]/6*(b[2]*b[2]*b[2] * (b[2]+2*(x-b[0])))/((x-b[0])*(x-b[0]) * pow(((x-b[0]) + b[2]), 3));
}

static void
sszanette_guess(gint n_dat,
                const gdouble *x,
                const gdouble *y,
                gdouble *param,
                gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 20e-9;
    param[3] = 2e-21;
    *fres = TRUE;
}

/******************* pyrzanette ********************************/
static gdouble
pyrzanette_func(gdouble x,
                G_GNUC_UNUSED gint n_param,
                const gdouble *b,
                G_GNUC_UNUSED gpointer user_data,
                gboolean *fres)
{
    /*xc, R, H, gamma*/
    *fres = TRUE;
    return b[1] -2*b[2]*(tan(b[3])*tan(b[3]))/3/G_PI/(x-b[0]);
}

static void
pyrzanette_guess(gint n_dat,
                 const gdouble *x,
                 const gdouble *y,
                 gdouble *param,
                 gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/100;
    param[2] = 2e-20;
    param[3] = 0.5;
    *fres = TRUE;
}


/******************* tpyrzanette ********************************/
static gdouble
tpyrzanette_func(gdouble x,
                 G_GNUC_UNUSED gint n_param,
                 const gdouble *b,
                 G_GNUC_UNUSED gpointer user_data,
                 gboolean *fres)
{
    /*xc, R, H, gamma, L, */
    *fres = TRUE;
    return b[1] - 2*b[2]*b[4]*b[4]/(x-b[0])*(x-b[0])*(x-b[0])
           * (1 + (tan(b[3])*(x-b[0]))/b[4]
              + (tan(b[3])*(x-b[0])*tan(b[3])*(x-b[0]))/b[4]/b[4]);
}

static void
tpyrzanette_guess(gint n_dat,
                  const gdouble *x,
                  const gdouble *y,
                  gdouble *param,
                  gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/100;
    param[2] = 2e-20;
    param[3] = 0.5;
    param[4] = 20e-9;
    *fres = TRUE;
}


/******************* sphcapella ********************************/
static gdouble
sphcapella_func(gdouble x,
                G_GNUC_UNUSED gint n_param,
                const gdouble *b,
                G_GNUC_UNUSED gpointer user_data,
                gboolean *fres)
{
    /*xc, R, H */
    *fres = TRUE;
    return b[1] -b[3]*b[2]/6/(x-b[0])/(x-b[0]);
}

static void
sphcapella_guess(gint n_dat,
                 const gdouble *x,
                 const gdouble *y,
                 gdouble *param,
                 gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 20e-9;
    param[3] = 2e-21;

    *fres = TRUE;
}

/******************* sphsphcapella ********************************/
static gdouble
sphsphcapella_func(gdouble x,
                   G_GNUC_UNUSED gint n_param,
                   const gdouble *b,
                   G_GNUC_UNUSED gpointer user_data,
                   gboolean *fres)
{
    /*xc, R, H */
    *fres = TRUE;
    return b[1] -b[4]*b[3]*b[2]/6/(x-b[0])/(x-b[0])/(b[2]+b[3]) ;
}

static void
sphsphcapella_guess(gint n_dat,
                    const gdouble *x,
                    const gdouble *y,
                    gdouble *param,
                    gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 20e-9;
    param[3] = 20e-9;
    param[4] = 2e-21;

    *fres = TRUE;
}

/******************* conecapella ********************************/
static gdouble
conecapella_func(gdouble x,
                 G_GNUC_UNUSED gint n_param,
                 const gdouble *b,
                 G_GNUC_UNUSED gpointer user_data,
                 gboolean *fres)
{
    /*xc, R, H */
    *fres = TRUE;
    return b[1] -tan(b[2])*tan(b[2])*b[3]/6/(x-b[0]) ;
}

static void
conecapella_guess(gint n_dat,
                  const gdouble *x,
                  const gdouble *y,
                  gdouble *param,
                  gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 0.5;
    param[3] = 200e-21;

    *fres = TRUE;
}

/******************* cylindercapella ********************************/
static gdouble
cylindercapella_func(gdouble x,
                     G_GNUC_UNUSED gint n_param,
                     const gdouble *b,
                     G_GNUC_UNUSED gpointer user_data,
                     gboolean *fres)
{
    /*xc, R, H */
    *fres = TRUE;
    return b[1] -b[3]*b[2]*b[2]/6/(x-b[0])/(x-b[0])/(x-b[0]) ;
}

static void
cylindercapella_guess(gint n_dat,
                      const gdouble *x,
                      const gdouble *y,
                      gdouble *param,
                      gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 20e-9;
    param[3] = 2e-23;

    *fres = TRUE;
}

/******************* paraboloidcapella ********************************/
static gdouble
parcapella_func(gdouble x,
                G_GNUC_UNUSED gint n_param,
                const gdouble *b,
                G_GNUC_UNUSED gpointer user_data,
                gboolean *fres)
{
    /*xc, R, H */
    *fres = TRUE;
    return b[1] -b[4]*b[2]*b[2]/b[3]/12/(x-b[0])/(x-b[0]) ;
}

static void
parcapella_guess(gint n_dat,
                 const gdouble *x,
                 const gdouble *y,
                 gdouble *param,
                 gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 20e-9;
    param[3] = 150e-9;
    param[4] = 2e-21;

    *fres = TRUE;
}


/******************* hertz spherical ********************************/
static gdouble
hertzsph_func(gdouble x,
              G_GNUC_UNUSED gint n_param,
              const gdouble *param,
              G_GNUC_UNUSED gpointer user_data,
              gboolean *fres)
{
    /*xc, R, E, nu */
    double xr = (param[0] - x);
    *fres = TRUE;
    if (xr > 0)
        return 4.0*param[2]/3.0/(1-param[3]*param[3])*sqrt(param[1]*xr*xr*xr);
    else
        return 0;
}

static void
hertzsph_guess(gint n_dat,
               const gdouble *x,
               G_GNUC_UNUSED const gdouble *y,
               gdouble *param,
               gboolean *fres)
{
    gint i;
    gdouble ymin = G_MAXDOUBLE;
    gdouble xmin = 0;

    for (i = 0; i < n_dat; i++) {
        if (y[i] < ymin) {
            ymin = y[i];
            xmin = x[i];
        }
    }

    param[0] = xmin;
    param[1] = 20e-9;
    param[2] = 5e7;
    param[3] = 0.25;

    *fres = TRUE;
}

static GwySIUnit*
hertzsph_get_units(G_GNUC_UNUSED GwyNLFitPreset *preset,
                   guint param,
                   GwySIUnit *siunit_x,
                   G_GNUC_UNUSED GwySIUnit *siunit_y)
{
    switch (param) {
        case 0:
        return gwy_si_unit_duplicate(siunit_x);
        break;

        case 1:
        return gwy_si_unit_new("m");
        break;

        case 2:
        return gwy_si_unit_new("Pa");
        break;

        case 3:
        return gwy_si_unit_new(NULL);
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }
}


/******************* hertz spherical with fixed thin film correction********************************/
static gdouble
hertzsphhfix_func(gdouble x,
                  G_GNUC_UNUSED gint n_param,
                  const gdouble *param,
                  G_GNUC_UNUSED gpointer user_data,
                  gboolean *fres)
{
    /*xc, R, E, nu, h */
    double xr = (param[0] - x);
    double xi = sqrt(param[1]*xr)/param[4];
    double alpha = -(1.2876 - 1.4678*param[4] + 1.3442*param[4]*param[4])/(1-param[4]);
    double beta = (0.6387 - 1.0277*param[4] + 1.5164*param[4]*param[4])/(1-param[4]);
    double xp = xi/M_PI;

    double fc = 1 - 2*alpha*xp + 4*alpha*alpha*xp*xp
                - 8*(alpha*alpha*alpha + 4*M_PI*M_PI*beta/15)*xp*xp*xp
                + 16*(alpha*alpha*alpha + 3*M_PI*M_PI*beta/5)*xp*xp*xp*xp;

    *fres = TRUE;
    if (xr > 0)
        return 4.0*param[2]/3.0/(1-param[3]*param[3])*sqrt(param[1]*xr*xr*xr)*fc;
    else
        return 0;
}

static void
hertzsphhfix_guess(gint n_dat,
                   const gdouble *x,
                   G_GNUC_UNUSED const gdouble *y,
                   gdouble *param,
                   gboolean *fres)
{
    gint i;
    gdouble ymin = G_MAXDOUBLE;
    gdouble xmin = 0;

    for (i = 0; i < n_dat; i++) {
        if (y[i] < ymin) {
            ymin = y[i];
            xmin = x[i];
        }
    }

    param[0] = xmin;
    param[1] = 20e-9;
    param[2] = 5e7;
    param[3] = 0.25;
    param[4] = 100e-9;

    *fres = TRUE;
}

static GwySIUnit*
hertzsphfilm_get_units(G_GNUC_UNUSED GwyNLFitPreset *preset,
                       guint param,
                       GwySIUnit *siunit_x,
                       G_GNUC_UNUSED GwySIUnit *siunit_y)
{
    switch (param) {
        case 0:
        return gwy_si_unit_duplicate(siunit_x);
        break;

        case 1:
        return gwy_si_unit_new("m");
        break;

        case 2:
        return gwy_si_unit_new("Pa");
        break;

        case 3:
        return gwy_si_unit_new(NULL);
        break;

        case 4:
        return gwy_si_unit_new("m");
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }
}


/******************* hertz spherical with free thin film correction********************************/
static gdouble
hertzsphhfree_func(gdouble x,
                   G_GNUC_UNUSED gint n_param,
                   const gdouble *param,
                   G_GNUC_UNUSED gpointer user_data,
                   gboolean *fres)
{
    /*xc, R, E, nu, h */
    double xr = (param[0] - x);
    double xi = sqrt(param[1]*xr)/param[4];
    double alpha = -0.347*(3 - 2*param[4])/(1-param[4]);
    double beta = -0.056*(5 - 2*param[4])/(1-param[4]);
    double xp = xi/M_PI;

    double fc = 1 - 2*alpha*xp + 4*alpha*alpha*xp*xp
                - 8*(alpha*alpha*alpha + 4*M_PI*M_PI*beta/15)*xp*xp*xp
                + 16*(alpha*alpha*alpha + 3*M_PI*M_PI*beta/5)*xp*xp*xp*xp;

    *fres = TRUE;
    if (xr > 0)
        return 4.0*param[2]/3.0/(1-param[3]*param[3])*sqrt(param[1]*xr*xr*xr)*fc;
    else
        return 0;
}

static void
hertzsphhfree_guess(gint n_dat,
                    const gdouble *x,
                    G_GNUC_UNUSED const gdouble *y,
                    gdouble *param,
                    gboolean *fres)
{
    gint i;
    gdouble ymin = G_MAXDOUBLE;
    gdouble xmin = 0;

    for (i = 0; i < n_dat; i++) {
        if (y[i] < ymin) {
            ymin = y[i];
            xmin = x[i];
        }
    }

    param[0] = xmin;
    param[1] = 20e-9;
    param[2] = 5e7;
    param[3] = 0.25;
    param[4] = 100e-9;

    *fres = TRUE;
}

/******************* DMT  ********************************/
static gdouble
dmt_func(gdouble x,
         G_GNUC_UNUSED gint n_param,
         const gdouble *param,
         G_GNUC_UNUSED gpointer user_data,
         gboolean *fres)
{
    /*xc, F_ad, R, E, nu*/
    double xr = (param[0] - x); //this should be deformation and should be already entered only positive
    *fres = TRUE;
    if (xr > 0)
        return 4.0*param[3]/3.0/(1-param[4]*param[4])*sqrt(param[2]*xr*xr*xr) + param[1];
    else
        return param[1];
}

static void
dmt_guess(gint n_dat,
          const gdouble *x,
          const gdouble *y,
          gdouble *param,
          gboolean *fres)
{
    gint i;
    gdouble ymin = G_MAXDOUBLE;
    gdouble xmin = 0;

    for (i = 0; i < n_dat; i++) {
        if (y[i] < ymin) {
            ymin = y[i];
            xmin = x[i];
        }
    }
    param[0] = xmin;
    param[1] = ymin;
    param[2] = 20e-9;
    param[3] = 5e7;
    param[4] = 0.25;

    *fres = TRUE;
}

static GwySIUnit*
dmt_get_units(G_GNUC_UNUSED GwyNLFitPreset *preset,
              guint param,
              GwySIUnit *siunit_x,
              GwySIUnit *siunit_y)
{
    switch (param) {
        case 0:
        return gwy_si_unit_duplicate(siunit_x);
        break;

        case 1:
        return gwy_si_unit_duplicate(siunit_y);
        break;

        case 2:
        return gwy_si_unit_new("m");
        break;

        case 3:
        return gwy_si_unit_new("Pa");
        break;

        case 4:
        return gwy_si_unit_new(NULL);
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }
}



/******************* Sneddon conical ********************************/
static gdouble
sneddon_func(gdouble x,
             G_GNUC_UNUSED gint n_param,
             const gdouble *param,
             G_GNUC_UNUSED gpointer user_data,
             gboolean *fres)
{
    /*xc, alpha, E, nu*/
    double xr = (param[0] - x); //this should be deformation and should be already entered only positive
    *fres = TRUE;
    if (xr > 0)
        return 2.0*param[2]/M_PI/(1-param[3]*param[3])*tan(param[1])*xr*xr;
    else
        return 0;
}

static void
sneddon_guess(gint n_dat,
              const gdouble *x,
              const gdouble *y,
              gdouble *param,
              gboolean *fres)
{
    gint i;
    gdouble ymin = G_MAXDOUBLE;
    gdouble xmin = 0;

    for (i = 0; i < n_dat; i++) {
        if (y[i] < ymin) {
            ymin = y[i];
            xmin = x[i];
        }
    }
    param[0] = xmin;
    param[1] = 0.25;
    param[2] = 5e7;
    param[3] = 0.25;

    *fres = TRUE;
}

static GwySIUnit*
sneddon_get_units(G_GNUC_UNUSED GwyNLFitPreset *preset,
                  guint param,
                  GwySIUnit *siunit_x,
                  G_GNUC_UNUSED GwySIUnit *siunit_y)
{
    switch (param) {
        case 0:
        return gwy_si_unit_duplicate(siunit_x);
        break;

        case 1:
        return gwy_si_unit_new("rad");
        break;

        case 2:
        return gwy_si_unit_new("Pa");
        break;

        case 3:
        return gwy_si_unit_new(NULL);
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }
}

/******************* Sneddon conical with free film correction ********************************/
static gdouble
sneddonhfree_func(gdouble x,
             G_GNUC_UNUSED gint n_param,
             const gdouble *param,
             G_GNUC_UNUSED gpointer user_data,
             gboolean *fres)
{
    /*xc, alpha, E, nu, h*/
    double xr = (param[0] - x); //this should be deformation and should be already entered only positive
    double zeta = 0.388;
    double fc = 1 + zeta*2*tan(param[1])*xr/(M_PI*M_PI*param[4]) 
              + 16*zeta*zeta*tan(param[1])*tan(param[1])*xr*xr/(param[4]*param[4]);

    *fres = TRUE;
    if (xr > 0)
        return 2.0*param[2]/M_PI/(1-param[3]*param[3])*tan(param[1])*xr*xr*fc;
    else
        return 0;
}

static void
sneddonhfree_guess(gint n_dat,
              const gdouble *x,
              const gdouble *y,
              gdouble *param,
              gboolean *fres)
{
    gint i;
    gdouble ymin = G_MAXDOUBLE;
    gdouble xmin = 0;

    for (i = 0; i < n_dat; i++) {
        if (y[i] < ymin) {
            ymin = y[i];
            xmin = x[i];
        }
    }
    param[0] = xmin;
    param[1] = 0.25;
    param[2] = 5e7;
    param[3] = 0.25;
    param[4] = 100e-9;

    *fres = TRUE;
}

static GwySIUnit*
sneddonhfree_get_units(G_GNUC_UNUSED GwyNLFitPreset *preset,
                  guint param,
                  GwySIUnit *siunit_x,
                  G_GNUC_UNUSED GwySIUnit *siunit_y)
{
    switch (param) {
        case 0:
        return gwy_si_unit_duplicate(siunit_x);
        break;

        case 1:
        return gwy_si_unit_new("rad");
        break;

        case 2:
        return gwy_si_unit_new("Pa");
        break;

        case 3:
        return gwy_si_unit_new(NULL);
        break;

        case 4:
        return gwy_si_unit_new("m");
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }
}

/******************* Sneddon conical with fixed film correction ********************************/
static gdouble
sneddonhfix_func(gdouble x,
             G_GNUC_UNUSED gint n_param,
             const gdouble *param,
             G_GNUC_UNUSED gpointer user_data,
             gboolean *fres)
{
    /*xc, alpha, E, nu, h*/
    double xr = (param[0] - x); //this should be deformation and should be already entered only positive
    double zeta = 1.7795;
    double fc = 1 + zeta*2*tan(param[1])*xr/(M_PI*M_PI*param[4]) 
              + 16*zeta*zeta*tan(param[1])*tan(param[1])*xr*xr/(param[4]*param[4]);

    *fres = TRUE;
    if (xr > 0)
        return 2.0*param[2]/M_PI/(1-param[3]*param[3])*tan(param[1])*xr*xr*fc;
    else
        return 0;
}

static void
sneddonhfix_guess(gint n_dat,
              const gdouble *x,
              const gdouble *y,
              gdouble *param,
              gboolean *fres)
{
    gint i;
    gdouble ymin = G_MAXDOUBLE;
    gdouble xmin = 0;

    for (i = 0; i < n_dat; i++) {
        if (y[i] < ymin) {
            ymin = y[i];
            xmin = x[i];
        }
    }
    param[0] = xmin;
    param[1] = 0.25;
    param[2] = 5e7;
    param[3] = 0.25;
    param[4] = 100e-9;

    *fres = TRUE;
}

static GwySIUnit*
sneddonhfix_get_units(G_GNUC_UNUSED GwyNLFitPreset *preset,
                  guint param,
                  GwySIUnit *siunit_x,
                  G_GNUC_UNUSED GwySIUnit *siunit_y)
{
    switch (param) {
        case 0:
        return gwy_si_unit_duplicate(siunit_x);
        break;

        case 1:
        return gwy_si_unit_new("rad");
        break;

        case 2:
        return gwy_si_unit_new("Pa");
        break;

        case 3:
        return gwy_si_unit_new(NULL);
        break;

        case 4:
        return gwy_si_unit_new("m");
        break;

        default:
        g_return_val_if_reached(NULL);
        break;
    }
}


/******************* sphtiptap ********************************/
static gdouble
sphtiptap_func(gdouble x,
               G_GNUC_UNUSED gint n_param,
               const gdouble *b,
               G_GNUC_UNUSED gpointer user_data,
               gboolean *fres)
{
    /*xc, R, H., xc */
    *fres = TRUE;
    return b[1] - b[3]*b[2]/6/((x-b[0])-b[4])/((x-b[0])-b[4]);
}

static void
sphtiptap_guess(gint n_dat,
                const gdouble *x,
                const gdouble *y,
                gdouble *param,
                gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 20e-9;
    param[3] = 2e-21;
    param[4] = 0;
    *fres = TRUE;
}

#if 0
/******************* sphxu ********************************/
static gdouble
sphxu_func(gdouble x,
           G_GNUC_UNUSED gint n_param,
           const gdouble *b,
           G_GNUC_UNUSED gpointer user_data,
           gboolean *fres)
{
     /*xc, R, H, sigma */
    *fres = TRUE;
    return b[1] - b[3]*b[2]/12*(1/(x-b[0])/(x-b[0]) - 1/15*pow(b[4], 6)/pow((x-b[0]), 8));
}

static void
sphxu_guess(gint n_dat,
            const gdouble *x,
            const gdouble *y,
            gdouble *param,
            gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
            xmin = x[i];
        if (x[i] > xmax)
            xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 20e-9;
    param[3] = 2e-21;
    param[4] = 1;
    *fres = TRUE;
}

/******************* sphcappakarinen ********************************/
static gdouble
sphcappakarinen_func(gdouble x,
                     G_GNUC_UNUSED gint n_param,
                     const gdouble *b,
                     G_GNUC_UNUSED gpointer user_data,
                     gboolean *fres)
{
    /*R, gamma, theta1, theta2*/
    *fres = TRUE;
    return  b[1] - 2*b[2]*G_PI*b[0]*(cos(b[3]) + cos(b[4]));
}

static void
sphcappakarinen_guess(gint n_dat,
                      const gdouble *x,
                      const gdouble *y,
                      gdouble *param,
                      gboolean *fres)
{
    gint i;

    param[0] = 0;
    param[1] = 0;

    for (i = 0; i < n_dat; i++)
        param[1] += y[i]/n_dat;
}

/******************* spheastman ********************************/
static gdouble
sphcapeastman_func(gdouble x,
                   G_GNUC_UNUSED gint n_param,
                   const gdouble *b,
                   G_GNUC_UNUSED gpointer user_data,
                   gboolean *fres)
{
    /* xc, R, gamma, theta, d*/
    *fres = TRUE;
    return b[1] - 2*b[3]*G_PI*b[2]*cos(b[4])/(1+(x-b[0])/b[5]);
}

static void
sphcapeastman_guess(gint n_dat,
                    const gdouble *x,
                    const gdouble *y,
                    gdouble *param,
                    gboolean *fres)
{
    gint i;

    param[0] = 0;
    param[1] = 0;

    for (i = 0; i < n_dat; i++)
        param[1] += y[i]/n_dat;

    *fres = TRUE;
}

/******************* sphcapheinz ********************************/
static gdouble
sphcapheinz_func(gdouble x,
                 G_GNUC_UNUSED gint n_param,
                 const gdouble *b,
                 G_GNUC_UNUSED gpointer user_data,
                 gboolean *fres)
{
    /*R, gamma, theta*/
    *fres = TRUE;
    return b[1] - 4*b[2]*G_PI*b[0]*cos(b[3]);
}

static void
sphcapheinz_guess(gint n_dat,
                  const gdouble *x,
                  const gdouble *y,
                  gdouble *param,
                  gboolean *fres)
{
    gint i;

    param[0] = 0;
    param[1] = 0;

    for (i = 0; i < n_dat; i++)
        param[1] += y[i]/n_dat;
}

/******************* sphesheinz ********************************/
static gdouble
sphesheinz_func(gdouble x,
                G_GNUC_UNUSED gint n_param,
                const gdouble *b,
                G_GNUC_UNUSED gpointer user_data,
                gboolean *fres)
{
    /*xc, R, sigma1, sigma2, epsilon, debye, lambda*/
    *fres = TRUE;
    return  b[1] - 4*G_PI*b[2]*b[7]*b[3]*b[4]/b[5]*exp(-(x-b[0])/b[6]) ;
}

static void
sphesheinz_guess(gint n_dat,
                 const gdouble *x,
                 const gdouble *y,
                 gdouble *param,
                 gboolean *fres)
{
    gint i;

    param[0] = 0;
    param[1] = 0;

    for (i = 0; i < n_dat; i++)
        param[1] += y[i]/n_dat;
}

/******************* hsphhertz ********************************/
static gdouble
hsphhertz_func(gdouble x,
               G_GNUC_UNUSED gint n_param,
               const gdouble *b,
               G_GNUC_UNUSED gpointer user_data,
               gboolean *fres)
{
    /*xc, yc, R, E, nu*/
    *fres = TRUE;
    return  b[1] + (b[0] - x)*sqrt(b[0] - x)*2*b[3]*sqrt(b[2])/(3*(1-b[4]*b[4]));
}

static void
hsphhertz_guess(gint n_dat,
                const gdouble *x,
                const gdouble *y,
                gdouble *param,
                gboolean *fres)
{
    gint i;

    param[0] = 0;
    param[1] = 0;

    for (i = 0; i < n_dat; i++)
        param[1] += y[i]/n_dat;
}

/******************* hertz paraboloid ********************************/
static gdouble
hertzpar_func(gdouble x,
              G_GNUC_UNUSED gint n_param,
              const gdouble *b,
              G_GNUC_UNUSED gpointer user_data,
              gboolean *fres)
{
    /*xc, R, E */
    *fres = TRUE;
    if ((b[0]-x) > 0)
        return 1.3333333*b[2]*sqrt(b[1]*(b[0]-x)*(b[0]-x)*(b[0]-x));
    else
        return 0;
}

static void
hertzpar_guess(gint n_dat,
               const gdouble *x,
               G_GNUC_UNUSED const gdouble *y,
               gdouble *param,
               gboolean *fres)
{
    gint i;
    gdouble xmax = x[n_dat - 1];

    for (i = 1; i < n_dat; i++) {
        if (x[i] > xmax)
            xmax = x[i];
    }
    param[0] = xmax;
    param[1] = 10e-9;
    param[2] = 10e6;

    *fres = TRUE;
}

#endif

/******************* electrostatic sphere capella ********************************/
/*static gdouble
esphcapella_func(gdouble x,
                G_GNUC_UNUSED gint n_param,
                const gdouble *b,
                G_GNUC_UNUSED gpointer user_data,
                gboolean *fres)
{
    *fres = TRUE;
    return b[1] -G_PI*8.8541878176e-12*b[2]*b[2]*b[3]*b[3]/(x-b[0])/(x-b[0] + b[2]) ;
}

static void
esphcapella_guess(gint n_dat,
                 const gdouble *x,
                 const gdouble *y,
                 gdouble *param,
                 gboolean *fres)
{
    gint i;
    gdouble xmin = x[0], xmax = x[n_dat - 1];

    param[1] = y[0]/n_dat;

    for (i = 1; i < n_dat; i++) {
        if (x[i] < xmin)
           xmin = x[i];
        if (x[i] > xmax)
           xmax = x[i];
        param[1] += y[i]/n_dat;
    }
    param[0] = xmin - (xmax-xmin)/20;
    param[2] = 20e-9;
    param[3] = 1;

    *fres = TRUE;
}
*/

/************************** presets ****************************/


/*xc, R, H, gamma*/
/*
static const GwyNLFitParam argento_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "H", 1, 1, },
    { "gamma", 0, 0, },
};
*/

/*xc, R, H, gamma, h1, L*/
/*
static const GwyNLFitParam parzanette_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "H", 1, 1, },
    { "gamma", 0, 0, },
    { "h1", 1, 0, },
    { "L", 1, 0, },
};
*/

static const GwyNLFitParam sszanette_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "H", 1, 1, },
};


static const GwyNLFitParam pyrzanette_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "H", 1, 1, },
    { "gamma", 0, 0, },
};

static const GwyNLFitParam tpyrzanette_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "H", 1, 1, },
    { "gamma", 0, 0, },
    { "L", 1, 1, },
};

static const GwyNLFitParam sphcapella_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "H", 1, 1, },
};

static const GwyNLFitParam sphsphcapella_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "R1", 1, 0, },
    { "R2", 1, 0, },
    { "H", 1, 1, },
};

static const GwyNLFitParam conecapella_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "theta", 0, 0, },
    { "H", 1, 1, },
};

static const GwyNLFitParam parcapella_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "l_xy", 1, 0, },
    { "l_z", 1, 0, },
    { "H", 1, 1, },
};

static const GwyNLFitParam sphtiptap_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "H", 1, 1, },
    { "xi", 1, 0, },
};

static const GwyNLFitParam hertzsph_params[] = {
    { "xc", 1, 0, },
    { "R", 1, 0, },
    { "E", -2, 1, },
    { "ν", 0, 0, },
};

static const GwyNLFitParam hertzsphhfix_params[] = {
    { "xc", 1, 0, },
    { "R", 1, 0, },
    { "E", -2, 1, },
    { "ν", 0, 0, },
    { "h", 1, 0, },
};

static const GwyNLFitParam hertzsphhfree_params[] = {
    { "xc", 1, 0, },
    { "R", 1, 0, },
    { "E", -2, 1, },
    { "ν", 0, 0, },
    { "h", 1, 0, },
};

static const GwyNLFitParam dmt_params[] = {
    { "xc", 1, 0, },
    { "Fad", 0, 1, },
    { "R", 1, 0, },
    { "E", -2, 1, },
    { "ν", 0, 0, },
};

static const GwyNLFitParam sneddon_params[] = {
    { "xc", 1, 0, },
    { "α", 0, 0, },
    { "E", -2, 1, },
    { "ν", 0, 0, },
};

static const GwyNLFitParam sneddonhfix_params[] = {
    { "xc", 1, 0, },
    { "α", 0, 0, },
    { "E", -2, 1, },
    { "ν", 0, 0, },
    { "h", 1, 0, },
};

static const GwyNLFitParam sneddonhfree_params[] = {
    { "xc", 1, 0, },
    { "α", 0, 0, },
    { "E", -2, 1, },
    { "ν", 0, 0, },
    { "h", 1, 0, },
};



/*
static const GwyNLFitParam hertzpar_params[] = {
    { "xc", 1, 0, },
    { "R", -1, 0, },
    { "Er", -1, 1, },
};
static const GwyNLFitParam sphxu_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "H", 1, 1, },
    { "sigma", 1, 0, },
};

static const GwyNLFitParam sphcappakarinen_params[] = {
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "gamma", 0, 0, },
    { "theta1", 1, 0, },
    { "theta2", 1, 0, },
};

static const GwyNLFitParam sphcapeastman_params[] = {
    { "xc", 0, 1, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "gamma", 0, 0, },
    { "theta", 1, 0, },
    { "d", 1, 0, },
};

static const GwyNLFitParam sphcapheinz_params[] = {
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "gamma", 0, 0, },
    { "theta", 1, 0, },

};

static const GwyNLFitParam sphesheinz_params[] = {
    { "xc", 0, 1, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "sigma1", 1, 0, },
    { "sigma2", 1, 0, },
    { "epsilon", 1, 0, },
    { "debye", 1, 0, },
    { "lambda", 1, 0, },
};

static const GwyNLFitParam esphcapella_params[] = {
    { "xc", 1, 0, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "V", 1, 0, },
};

static const GwyNLFitParam hsphhertz_params[] = {
    { "xc", 0, 1, },
    { "yc", 0, 1, },
    { "R", 1, 0, },
    { "E", 1, 0, },
    { "ν", 1, 0, },
};
*/


static const GwyNLFitPresetBuiltin fitting_presets[] = {
    {
        N_("vdW: semisphere"),
        "<i>f</i>(<i>x</i>) "
        "= -<i>H</i>/6 (<i>R</i><sup>3</sup>(<i>R</i>+2(<i>x</i>-<i>x<sub>c</sub></i>)))"
        "/((<i>x</i>-<i>x<sub>c</sub></i>)<sup>2</sup>((<i>x</i>-<i>x<sub>c</sub></i>)"
        " + <i>R</i>)<sup>3</sup>)",
        &sszanette_func,
        NULL,
        &sszanette_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sszanette_params),
        sszanette_params,
    },
     {
        N_("vdW: pyramide"),
        "<i>f</i>(<i>x</i>) "
        "= -2<i>H</i> (tan(<i>γ</i>)<sup>2</sup>)/3/Pi/(<i>x</i>-<i>x<sub>c</sub></i>) ",
        &pyrzanette_func,
        NULL,
        &pyrzanette_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(pyrzanette_params),
        pyrzanette_params,
    },
     {
        N_("vdW: truncated pyramid"),
        "<i>f</i>(<i>x</i>) "
        "= -2HL<sup>2</sup>/(x-xc)<sup>3</sup> * (1 + (tan(γ)(x-xc))/L + (tan(γ)(x-xc))<sup>2</sup>)/L<sup>2</sup>)",
        &tpyrzanette_func,
        NULL,
        &tpyrzanette_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(tpyrzanette_params),
        tpyrzanette_params,
    },
     {
        N_("vdW: sphere"),
        "<i>f</i>(<i>x</i>) "
        "= -<i>HR</i>/6/(<i>x</i>-<i>x<sub>c</sub></i>)<sup>2</sup> ",
        &sphcapella_func,
        NULL,
        &sphcapella_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphcapella_params),
        sphcapella_params,
    },
     {
        N_("vdW: offset sphere"),
        "<i>f</i>(<i>x</i>) "
        "= -<i>HR</i>/6/((<i>x</i>-<i>x<sub>c</sub></i>)-<i>ξ</i>)<sup>2</sup>",
        &sphtiptap_func,
        NULL,
        &sphtiptap_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphtiptap_params),
        sphtiptap_params,
    },
     {
        N_("vdW: two spheres"),
        "<i>f</i>(<i>x</i>) "
        "= -<i>HR<sub>1</sub>R<sub>2</sub></i>/6/(<i>x</i>-<i>x<sub>c</sub></i>)(R<sub>1</sub>"
        "+ R<sub>2</sub>)<sup>2</sup> ",
        &sphsphcapella_func,
        NULL,
        &sphsphcapella_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphsphcapella_params),
        sphsphcapella_params,
    },
     {
        N_("vdW: cone"),
        "<i>f</i>(<i>x</i>) "
        "= -<i>H tan<sup>2</sup>(theta)</i>/6/(<i>x</i>-<i>x<sub>c</sub></i>)",
        &conecapella_func,
        NULL,
        &conecapella_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(conecapella_params),
        conecapella_params,
    },
     {
        N_("vdW: cylinder"),
        "<i>f</i>(<i>x</i>) "
        "= -<i>HR<sup>2</sup></i>/6/(<i>x</i>-<i>x<sub>c</sub></i>)<sup>3</sup> ",
        &cylindercapella_func,
        NULL,
        &cylindercapella_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphcapella_params),
        sphcapella_params,
    },
     {
        N_("vdW: paraboloid"),
        "<i>f</i>(<i>x</i>) "
        "= -<i>Hl<sub>xy</sub><sup>2</sup></i>/12/(<i>x</i>-<i>x<sub>c</sub></i>)<sup>2</sup> ",
        &parcapella_func,
        NULL,
        &parcapella_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(parcapella_params),
        parcapella_params,
    },
     {
        N_("Hertz: spherical"),
        "<i>f</i>(<i>x</i>) "
        "= 4/3 E/(1-ν<sup>2</sup>) √(R(<i>x</i>-<i>x<sub>c</sub></i>)<sup>3</sup>) ",
        &hertzsph_func,
        NULL,
        &hertzsph_guess,
        &hertzsph_get_units,
        NULL,
        G_N_ELEMENTS(hertzsph_params),
        hertzsph_params,
    },
     {
        N_("Hertz: spherical, fixed film"),
        "<i>f</i>(<i>x</i>) "
        "= 4/3 E/(1-ν<sup>2</sup>) √(R(<i>x</i>-<i>x<sub>c</sub></i>)<sup>3</sup>)f<sub>c,fix</sub> ",
        &hertzsphhfix_func,
        NULL,
        &hertzsphhfix_guess,
        &hertzsphfilm_get_units,
        NULL,
        G_N_ELEMENTS(hertzsphhfix_params),
        hertzsphhfix_params,
    },
     {
        N_("Hertz: spherical, free film"),
        "<i>f</i>(<i>x</i>) "
        "= 4/3 E/(1-ν<sup>2</sup>) √(R(<i>x</i>-<i>x<sub>c</sub></i>)<sup>3</sup>)f<sub>c,free</sub> ",
        &hertzsphhfree_func,
        NULL,
        &hertzsphhfree_guess,
        &hertzsphfilm_get_units,
        NULL,
        G_N_ELEMENTS(hertzsphhfree_params),
        hertzsphhfree_params,
    },
      {
        N_("DMT: spherical"),
        "<i>f</i>(<i>x</i>) "
        "= 4/3 E/(1-ν<sup>2</sup>) √(R(<i>x</i>-<i>x<sub>c</sub></i>)<sup>3</sup>) + F<sub>ad</sub>",
        &dmt_func,
        NULL,
        &dmt_guess,
        &dmt_get_units,
        NULL,
        G_N_ELEMENTS(dmt_params),
        dmt_params,
    },
    {
        N_("Sneddon: conical"),
        "<i>f</i>(<i>x</i>) "
        "= 2/π E/(1-ν<sup>2</sup>) tan(α) (<i>x</i>-<i>x<sub>c</sub></i>)<sup>2</sup>",
        &sneddon_func,
        NULL,
        &sneddon_guess,
        &sneddon_get_units,
        NULL,
        G_N_ELEMENTS(sneddon_params),
        sneddon_params,
    },
    {
        N_("Sneddon: conical, fixed film"),
        "<i>f</i>(<i>x</i>) "
        "= 2/π E/(1-ν<sup>2</sup>) tan(α) (<i>x</i>-<i>x<sub>c</sub></i>)<sup>2</sup>f<sub>c,fix</sub>",
        &sneddonhfix_func,
        NULL,
        &sneddonhfix_guess,
        &sneddonhfix_get_units,
        NULL,
        G_N_ELEMENTS(sneddonhfix_params),
        sneddonhfix_params,
    },
     {
        N_("Sneddon: conical, free film"),
        "<i>f</i>(<i>x</i>) "
        "= 2/π E/(1-ν<sup>2</sup>) tan(α) (<i>x</i>-<i>x<sub>c</sub></i>)<sup>2</sup>f<sub>c,free</sub>",
        &sneddonhfree_func,
        NULL,
        &sneddonhfree_guess,
        &sneddonhfree_get_units,
        NULL,
        G_N_ELEMENTS(sneddonhfree_params),
        sneddonhfree_params,
    },
        /*    {
        "vdW: sphere3",
        "<i>f</i>(<i>x</i>) "
        "= -<i>HR</i>/12 (1/(<i>x</i>-<i>x<sub>c</sub></i>)<sup>2</sup>"
        " - 1/15 <i>σ</i><sup>6</sup>/(<i>x</i>-<i>x<sub>c</sub></i>)<sup>8</sup>) ",
        &sphxu_func,
        NULL,
        &sphxu_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphxu_params),
        sphxu_params,
    },*/
/*      {
        "sphcappakarinen",
        "<i>f</i>(<i>x</i>) "
        "= -2 γ Pi R (cos(theta1) + cos(theta2))",
        &sphcappakarinen_func,
        NULL,
        &sphcappakarinen_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphcappakarinen_params),
        sphcappakarinen_params,
    },
      {
        "sphcapeastman",
        "<i>f</i>(<i>x</i>) "
        "= -2 γ Pi R cos(theta)/(1+(x-xc)/d)",
        &sphcapeastman_func,
        NULL,
        &sphcapeastman_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphcapeastman_params),
        sphcapeastman_params,
    },
      {
        "sphcapheinz",
        "<i>f</i>(<i>x</i>) "
        "= -4 γ Pi R cos(theta)",
        &sphcapheinz_func,
        NULL,
        &sphcapheinz_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphcapheinz_params),
        sphcapheinz_params,
    },
      {
        "sphesheinz",
        "<i>f</i>(<i>x</i>) "
        "= -4 Pi R lambda σ1 σ2/epsilon exp(-(x-xc)/debye)",
        &sphesheinz_func,
        NULL,
        &sphesheinz_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(sphesheinz_params),
        sphesheinz_params,
    },*/
    /* {
        "electrostatic: sphere",
        "<i>f</i>(<i>x</i>) "
        "= -<i>Pi e<sub>0</sub>(VR)<sup>2</sup></i>/(<i>x</i>-<i>x<sub>c</sub></i>)"
        "/(<i>x</i>-<i>x<sub>c</sub></i> + R) ",
        &esphcapella_func,
        NULL,
        &esphcapella_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(esphcapella_params),
        esphcapella_params,
    },*/
/*      {
        "hsphhertz",
        "<i>f</i>(<i>x</i>) "
        "= (xc - x)<sup>3/2</sup>2 E sqrt(R)/(3 (1-nu<sup>2</sup>))",
        &hsphhertz_func,
        NULL,
        &hsphhertz_guess,
        NULL,
        NULL,
        G_N_ELEMENTS(hsphhertz_params),
        hsphhertz_params,
    },*/
};


static void
gwy_fd_curve_preset_class_init(GwyFDCurvePresetClass *klass)
{
    GwyResourceClass *parent_class, *res_class = GWY_RESOURCE_CLASS(klass);

    parent_class = GWY_RESOURCE_CLASS(gwy_fd_curve_preset_parent_class);
    res_class->item_type = *gwy_resource_class_get_item_type(parent_class);

    res_class->item_type.type = G_TYPE_FROM_CLASS(klass);
    res_class->name = "fdcurvepresets";
    res_class->inventory = gwy_inventory_new(&res_class->item_type);
    gwy_inventory_forget_order(res_class->inventory);
}

static void
gwy_fd_curve_preset_init(G_GNUC_UNUSED GwyFDCurvePreset *preset)
{
}

static GwyFDCurvePreset*
gwy_fd_curve_preset_new_static(const GwyNLFitPresetBuiltin *data)
{
    GwyNLFitPreset *preset;

    preset = g_object_new(GWY_TYPE_FD_CURVE_PRESET, "is-const", TRUE, NULL);
    preset->builtin = data;
    g_string_assign(GWY_RESOURCE(preset)->name, data->name);

    return (GwyFDCurvePreset*)preset;
}

void
_gwy_fd_curve_preset_class_setup_presets(void)
{
    GwyResourceClass *klass;
    GwyFDCurvePreset *preset;
    guint i;

    /* Force class instantiation, this function is called before it's first
     * referenced. */
    klass = g_type_class_ref(GWY_TYPE_FD_CURVE_PRESET);

    for (i = 0; i < G_N_ELEMENTS(fitting_presets); i++) {
        preset = gwy_fd_curve_preset_new_static(fitting_presets + i);
        gwy_inventory_insert_item(klass->inventory, preset);
        g_object_unref(preset);
    }
    gwy_inventory_restore_order(klass->inventory);

    /* The presets added a reference so we can safely unref it again */
    g_type_class_unref(klass);
}

/**
 * gwy_fd_curve_presets:
 *
 * Gets inventory with all the FD curve presets.
 *
 * Returns: FD curve preset inventory.
 *
 * Since: 2.7
 **/
GwyInventory*
gwy_fd_curve_presets(void)
{
    return
    GWY_RESOURCE_CLASS(g_type_class_peek(GWY_TYPE_FD_CURVE_PRESET))->inventory;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyfdcurvepreset
 * @title: GwyFDCurvePreset
 * @short_description: Force-distance curve fitting presets
 * @see_also: #GwyNLFitPreset
 *
 * Force-distance curve fitting presets are a particular subtype of non-linear
 * fitting presets.  They have their own class and inventory, but they are
 * functionally identical to #GwyNLFitPreset<!-- -->s.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
