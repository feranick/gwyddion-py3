/*
 *  $Id: gwynlfit.c 24758 2022-04-06 12:18:50Z yeti-dn $
 *  Copyright (C) 2000-2003 Martin Siler.
 *  Copyright (C) 2004-2020 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwynlfit.h>
#include "libgwyddion/gwyomp.h"

/* Side step constant for numerical differentiation in gwy_math_nlfit_derive()
 */
#define FitSqrtMachEps  1e-5

/* Constant for decision to stop fitting cycle due to relative difference
 * in residual sum of squares between subsequent steps.
 */
#define EPS 1e-16

/* Lower symmetric part indexing */
/* i (row) MUST be greater or equal than j (column) */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]

typedef struct {
    GwyNLFitter *fitter;
    GwySetFractionFunc set_fraction;
    GwySetMessageFunc set_message;
    GwyNLFitIdxFunc func_idx;
    GwyNLFitIdxDiffFunc diff_idx;
    gboolean approx_geometric;  /* Ignored. */
    gint nparam;
} GwyNLFitterPrivate;

static void                gwy_math_nlfit_init    (GwyNLFitter *nlfit);
static gdouble             gwy_math_nlfit_fit_real(GwyNLFitter *nlfit,
                                                   guint ndata,
                                                   const gdouble *x,
                                                   const gdouble *y,
                                                   const gdouble *weight,
                                                   guint nparam,
                                                   gdouble *param,
                                                   const gboolean *fixed_param,
                                                   const gint *link_map,
                                                   gpointer user_data);
static gdouble             gwy_math_nlfit_residua (GwyNLFitFunc func,
                                                   GwyNLFitIdxFunc func_idx,
                                                   guint ndata,
                                                   const gdouble *x,
                                                   const gdouble *y,
                                                   const gdouble *weight,
                                                   guint nparam,
                                                   const gdouble *param,
                                                   gpointer user_data,
                                                   gdouble *resid,
                                                   gboolean *success);
static GwyNLFitterPrivate* find_private_data      (GwyNLFitter *fitter,
                                                   gboolean do_create);
static void                free_private_data      (GwyNLFitter *fitter);

G_LOCK_DEFINE_STATIC(private_fitter_data);
static GList *private_fitter_data = NULL;     /* Threads: protected by lock */

#if 0
GType
gwy_math_nlfit_get_type(void)
{
    static GType nlfit_type = 0;

    if (G_UNLIKELY(!nlfit_type)) {
        nlfit_type = g_boxed_type_register_static("GwyMathNLFit",
                                                  (GBoxedCopyFunc)gwy_math_nlfit_copy,
                                                  (GBoxedFreeFunc)gwy_math_nlfit_free);
    }

    return nlfit_type;
}
#endif

/**
 * gwy_math_nlfit_new:
 * @func: The fitted function.
 * @diff: The derivative of fitted function.
 *
 * Creates a new Marquardt-Levenberg nonlinear fitter for function with
 * a real-valued independent variable.
 *
 * See gwy_math_nlfit_new_idx() for more complex scenarios.
 *
 * You can use gwy_math_nlfit_diff() computing the derivative numerically,
 * when you do not know the derivatives explicitely.  Since 2.46 passing %NULL
 * as @diff has the same effect, i.e. the fitter will automatically use
 * numerical differentiation.
 *
 * Returns: The newly created fitter.
 **/
GwyNLFitter*
gwy_math_nlfit_new(GwyNLFitFunc func, GwyNLFitDerFunc diff)
{
    GwyNLFitter *nlfit;

    nlfit = g_new0(GwyNLFitter, 1);
    gwy_math_nlfit_init(nlfit);
    nlfit->fmarq = func;
    nlfit->dmarq = diff ? diff : gwy_math_nlfit_diff;

    return nlfit;
}

/**
 * gwy_math_nlfit_new_idx:
 * @func: The fitted function.
 * @diff: The derivative of fitted function.
 *
 * Creates a new Marquardt-Levenberg nonlinear fitter for opaque indexed data.
 *
 * As only the data index is passed to the functions, using this interface
 * permits fitting more complex functions.  The abscissa can be arbitrary,
 * for instance a vector, as it is not seen by the fitter.  Similarly,
 * vector-valued functions can be emulated by mapping tuples of indices to the
 * vector components.
 *
 * You can pass %NULL as @diff to use automatically numerical differentiation
 * when you do not know the derivatives explicitely.  Note that this means you
 * cannot use weighting.  If you want weighting you need to pass your own
 * @diff function that performs the weighting (it can utilise the
 * gwy_math_nlfit_diff_idx() helper).
 *
 * Returns: The newly created fitter.
 *
 * Since: 2.46
 **/
GwyNLFitter*
gwy_math_nlfit_new_idx(GwyNLFitIdxFunc func, GwyNLFitIdxDiffFunc diff)
{
    GwyNLFitterPrivate *priv;
    GwyNLFitter *nlfit;

    nlfit = g_new0(GwyNLFitter, 1);
    gwy_math_nlfit_init(nlfit);
    priv = find_private_data(nlfit, TRUE);
    priv->func_idx = func;
    priv->diff_idx = diff;

    return nlfit;
}

static void
gwy_math_nlfit_init(GwyNLFitter *nlfit)
{
    nlfit->mfi = 1.0;
    nlfit->mdec = 0.4;
    nlfit->minc = 10.0;
    nlfit->mtol = 1e-6;
    nlfit->maxiter = 100;
    nlfit->eval = FALSE;
    nlfit->dispersion = -1;
    nlfit->covar = NULL;
}

/**
 * gwy_math_nlfit_free:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Completely frees a Marquardt-Levenberg nonlinear fitter.
 **/
void
gwy_math_nlfit_free(GwyNLFitter *nlfit)
{
    free_private_data(nlfit);
    g_free(nlfit->covar);
    nlfit->covar = NULL;
    g_free(nlfit);
}

/**
 * gwy_math_nlfit_copy:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Creates a copy of a nonlinear least squares fitter.
 *
 * This function is mostly usefil for language bindings.
 *
 * Returns: The newly created fitter.
 *
 * Since: 2.47
 **/
GwyNLFitter*
gwy_math_nlfit_copy(GwyNLFitter *nlfit)
{
    GwyNLFitter *copy = g_memdup(nlfit, sizeof(GwyNLFitter));
    GwyNLFitterPrivate *priv;
    gint n;

    priv = find_private_data(nlfit, FALSE);
    if (nlfit->covar) {
        g_assert(priv);
        n = priv->nparam;
        copy->covar = g_memdup(nlfit->covar, n*(n+1)/2*sizeof(gdouble));
    }
    if (priv)
        *find_private_data(copy, TRUE) = *priv;

    return copy;
}

/**
 * gwy_math_nlfit_fit:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @ndata: The number of data points in @x, @y.
 * @x: Array of independent variable values.
 * @y: Array of dependent variable values.
 * @nparam: The nuber of parameters.
 * @param: Array of parameters (of size @nparam).  Note the parameters must
 *         be initialized to reasonably near values.
 * @user_data: Pointer that will be passed to the function and derivative
 *             as @user_data.
 *
 * Performs a nonlinear fit of simple function on data.
 *
 * Returns: The final residual sum, a negative number in the case of failure.
 **/
gdouble
gwy_math_nlfit_fit(GwyNLFitter *nlfit,
                   gint ndata,
                   const gdouble *x,
                   const gdouble *y,
                   gint nparam,
                   gdouble *param,
                   gpointer user_data)
{
    return gwy_math_nlfit_fit_real(nlfit, ndata, x, y, NULL,
                                   nparam, param, NULL, NULL, user_data);
}

/**
 * gwy_math_nlfit_fit_full:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @ndata: The number of data points in @x, @y, @weight.
 * @x: Array of independent variable values.
 * @y: Array of dependent variable values.
 * @weight: Array of weights associated to each data point (usually equal to 
 *          inverse squares errors).  Can be %NULL, unit weight is then used
 *          for all data.
 * @nparam: The nuber of parameters.
 * @param: Array of parameters (of size @nparam).  Note the parameters must
 *         be initialized to reasonably near values.
 * @fixed_param: Which parameters should be treated as fixed (set corresponding
 *               element to %TRUE for them).  May be %NULL if all parameters
 *               are variable.
 * @link_map: Map of linked parameters.  One of linked parameters is master,
 *            Values in this array are indices of corresponding master
 *            parameter for each parameter (for independent parameters set
 *            @link_map[i] == i).   May be %NULL if all parameter are
 *            independent.
 * @user_data: Pointer that will be passed to the function and derivative
 *
 * Performs a nonlinear fit of simple function on data, allowing some fixed
 * parameters.
 *
 * Initial values of linked (dependent) parameters are overwritten by master
 * values, their @fixed_param property is ignored and master's property
 * controls whether all are fixed or all variable.
 *
 * Returns: The final residual sum, a negative number in the case of failure.
 **/
gdouble
gwy_math_nlfit_fit_full(GwyNLFitter *nlfit,
                        gint ndata,
                        const gdouble *x,
                        const gdouble *y,
                        const gdouble *weight,
                        gint nparam,
                        gdouble *param,
                        const gboolean *fixed_param,
                        const gint *link_map,
                        gpointer user_data)
{
    return gwy_math_nlfit_fit_real(nlfit, ndata, x, y, weight,
                                   nparam, param, fixed_param, link_map,
                                   user_data);
}

/**
 * gwy_math_nlfit_fit_idx:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @ndata: The number of data points in @x, @y, @weight.
 * @nparam: The nuber of parameters.
 * @param: Array of parameters (of size @nparam).  Note the parameters must
 *         be initialized to reasonably near values.
 * @user_data: Pointer that will be passed to the function and derivative
 *
 * Performs a nonlinear fit of function on opaque indexed data.
 *
 * Returns: The final residual sum, a negative number in the case of failure.
 *
 * Since: 2.46
 **/
gdouble
gwy_math_nlfit_fit_idx(GwyNLFitter *nlfit,
                       guint ndata,
                       guint nparam,
                       gdouble *param,
                       gpointer user_data)
{
    return gwy_math_nlfit_fit_real(nlfit, ndata, NULL, NULL, NULL,
                                   nparam, param, NULL, NULL, user_data);
}

/**
 * gwy_math_nlfit_fit_idx_full:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @ndata: The number of data points in @x, @y, @weight.
 * @nparam: The nuber of parameters.
 * @param: Array of parameters (of size @nparam).  Note the parameters must
 *         be initialized to reasonably near values.
 * @fixed_param: Which parameters should be treated as fixed (set corresponding
 *               element to %TRUE for them).  May be %NULL if all parameters
 *               are variable.
 * @link_map: Map of linked parameters.  One of linked parameters is master,
 *            Values in this array are indices of corresponding master
 *            parameter for each parameter (for independent parameters set
 *            @link_map[i] == i).   May be %NULL if all parameter are
 *            independent.
 * @user_data: Pointer that will be passed to the function and derivative
 *
 * Performs a nonlinear fit of function on opaque indexed data, allowing some
 * fixed parameters.
 *
 * Initial values of linked (dependent) parameters are overwritten by master
 * values, their @fixed_param property is ignored and master's property
 * controls whether all are fixed or all variable.
 *
 * Returns: The final residual sum, a negative number in the case of failure.
 *
 * Since: 2.46
 **/
gdouble
gwy_math_nlfit_fit_idx_full(GwyNLFitter *nlfit,
                            guint ndata,
                            guint nparam,
                            gdouble *param,
                            const gboolean *fixed_param,
                            const gint *link_map,
                            gpointer user_data)
{
    return gwy_math_nlfit_fit_real(nlfit, ndata, NULL, NULL, NULL,
                                   nparam, param, fixed_param, link_map,
                                   user_data);
}

static gdouble
gwy_math_nlfit_fit_real(GwyNLFitter *nlfit,
                        guint ndata,
                        const gdouble *x,
                        const gdouble *y,
                        const gdouble *weight,
                        guint nparam,
                        gdouble *param,
                        const gboolean *fixed_param,
                        const gint *link_map,
                        gpointer user_data)
{
    GwyNLFitterPrivate *priv;
    GwySetFractionFunc set_fraction = NULL;
    GwySetMessageFunc set_message = NULL;
    GwyNLFitFunc func;
    GwyNLFitDerFunc diff;
    GwyNLFitIdxFunc func_idx;
    GwyNLFitIdxDiffFunc diff_idx;
    gdouble mlambda = 1e-4;
    gdouble sumr_new, sumr_best = G_MAXDOUBLE;
    gdouble *v = NULL, *xr = NULL, *w = NULL,
            *saveparam = NULL, *origparam = NULL, *resid = NULL,
            *a = NULL, *save_a = NULL, *covar = NULL, **storage = NULL,
            *param_best = NULL;
    guint *var_param_id = NULL, *lmap = NULL;
    gboolean *fixed = NULL;
    guint covar_size;
    guint i, j;
    guint n_var_param;
    guint nthreads = gwy_omp_max_threads();
    guint miter = 0;
    gint unimproved = 0;
    gboolean end = FALSE;

    g_return_val_if_fail(nlfit, -1.0);
    g_return_val_if_fail(param || !nparam, -1.0); /* handle zero nparam later */

    priv = find_private_data(nlfit, TRUE);
    set_fraction = priv->set_fraction;
    set_message = priv->set_message;
    priv->nparam = 0;
    func = nlfit->fmarq;
    diff = nlfit->dmarq;
    func_idx = priv->func_idx;
    diff_idx = priv->diff_idx;

    GWY_FREE(nlfit->covar);
    nlfit->dispersion = -1.0;

    if (ndata < nparam)
        return -1.0;

    g_return_val_if_fail((x && y && func && diff) || func_idx, -1.0);
    g_return_val_if_fail(!func_idx || (!x && !y && !weight && !func), -1.0);

    if (set_message)
        set_message(_("Fitting..."));
    if (set_fraction)
        set_fraction(0.0);

    /* Calculate square roots of weights because they are easily split into
     * the expressions.  The indexed interface already does it right. */
    if (weight) {
        w = g_new(gdouble, ndata);
        for (i = 0; i < ndata; i++)
            w[i] = sqrt(fmax(weight[i], 0.0));
    }

    /* Use defaults for param specials, if not specified */
    if (!link_map) {
        lmap = g_new(gint, nparam);
        for (i = 0; i < nparam; i++)
            lmap[i] = i;
        link_map = lmap;
    }

    /* Sync slave param values with master */
    origparam = g_memdup(param, nparam*sizeof(gdouble));
    for (i = 0; i < nparam; i++) {
        if (link_map[i] != i)
            param[i] = param[link_map[i]];
    }

    if (nparam == 0)
        goto fail;

    resid = g_new(gdouble, ndata);
    param_best = g_memdup(param, nparam*sizeof(gdouble));
    sumr_new = gwy_math_nlfit_residua(func, func_idx, ndata, x, y, w,
                                      nparam, param, user_data, resid,
                                      &nlfit->eval);
    gwy_debug("initial sumr %.16g", sumr_new);
    if (!nlfit->eval) {
        g_warning("Initial residua evaluation failed");
        sumr_best = -1.0;
        goto fail;
    }

    if (set_fraction && !set_fraction(1.0/(nlfit->maxiter + 1))) {
        gwy_debug("cancelled");
        nlfit->eval = FALSE;
        sumr_best = -2.0;
        goto fail;
    }

    /* find non-fixed parameters and map all -> non-fixed */
    n_var_param = 0;
    var_param_id = g_new(guint, nparam);
    for (i = 0; i < nparam; i++) {
        if (fixed_param && fixed_param[link_map[i]])
            var_param_id[i] = G_MAXUINT;
        else {
            if (link_map[i] == i) {
                var_param_id[i] = n_var_param;
                n_var_param++;
            }
        }
        gwy_debug("var_param_id[%d] = %d", i, var_param_id[i]);
    }
    /* assign master var_param_id to slaves in second pass, as it may have
     * higher id than slave */
    for (i = 0; i < nparam; i++) {
        if (link_map[i] != i)
            var_param_id[i] = var_param_id[link_map[i]];
    }

    if (!n_var_param)
        goto fail;

    /* Resolve which params are fixed, taking links into account.  We
     * cannot modify fixed_param, so create a new array. */
    if (!fixed_param)
        fixed = NULL;
    else {
        fixed = g_new0(gboolean, nparam);
        for (i = 0; i < nparam; i++)
            fixed[i] = fixed_param[link_map[i]];
    }

    covar_size = n_var_param*(n_var_param + 1)/2;

    /* Allocate separate memory block for each thread's data. */
    storage = g_new0(gdouble*, nthreads);
    for (i = 0; i < nthreads; i++)
        storage[i] = g_new(gdouble, n_var_param + covar_size + nparam);
    v = storage[0];
    a = v + n_var_param;
    /* At a + covar_size starts is der[], but we never need in the global
     * scope. */

    xr = g_new(gdouble, n_var_param);
    saveparam = g_new(gdouble, nparam);
    save_a = g_new(gdouble, covar_size);

    /* The actual minizmation */
    do {
        gboolean is_pos_def = FALSE;
        gboolean first_pass = TRUE;
        guint count = 0;
        gboolean eval = TRUE;

        if (unimproved == 0) {
            mlambda *= nlfit->mdec;
            sumr_best = sumr_new;
            gwy_assign(param_best, param, nparam);

            /* J'J and J'r computation */
#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            reduction(&&:eval) \
            private(i,j) \
            shared(diff_idx,func_idx,diff,func,user_data,param,nparam,fixed,link_map,var_param_id,ndata,covar_size,n_var_param,x,w,storage,resid)
#endif
            {
                guint ifrom = gwy_omp_chunk_start(ndata);
                guint ito = gwy_omp_chunk_end(ndata);
                guint tid = gwy_omp_thread_num();
                gdouble *tv = storage[tid];
                gdouble *ta = tv + n_var_param;
                gdouble *der = ta + covar_size;
                gboolean eeval = TRUE;

                gwy_clear(ta, covar_size);
                gwy_clear(tv, n_var_param);

                for (i = ifrom; i < ito; i++) {
                    if (diff_idx) {
                        diff_idx(i, param, fixed, func_idx, user_data, der,
                                 &eeval);
                    }
                    else if (diff) {
                        diff(x[i], nparam, param, fixed, func, user_data, der,
                             &eeval);
                    }
                    else {
                        gwy_math_nlfit_diff_idx(i, nparam, param, fixed,
                                                func_idx, user_data, der,
                                                &eeval);
                    }

                    if (!eeval)
                        break;

                    /* This should be done only for the real-function
                     * interface; but that is also the only case when @w can be
                     * non-NULL. */
                    if (w) {
                        for (j = 0; j < nparam; j++)
                            der[j] *= w[i];
                    }

                    /* Accumulate derivatives by slave parameters in master. */
                    for (j = 0; j < nparam; j++) {
                        if (link_map[j] != j)
                            der[link_map[j]] += der[j];
                    }

                    /* Accumulate the Hessian and gradient. */
                    for (j = 0; j < nparam; j++) {
                        guint jid, diag, k;

                        /* Only variable master parameters matter */
                        if ((jid = var_param_id[j]) == G_MAXUINT
                            || link_map[j] != j)
                            continue;
                        diag = jid*(jid + 1)/2;

                        /* for J'r */
                        tv[jid] += der[j] * resid[i];
                        for (k = 0; k <= j; k++) {   /* for J'J */
                            gint kid = var_param_id[k];

                            if (kid != G_MAXUINT)
                                ta[diag + kid] += der[j] * der[k];
                        }
                    }
                }

                eval = eeval;
            }

            nlfit->eval = eval;
            for (i = 1; i < nthreads; i++) {
                gdouble *st0 = storage[0], *sti = storage[i];

                for (j = 0; j < n_var_param + covar_size; j++)
                    st0[j] += sti[j];
            }

            if (nlfit->eval) {
                gwy_assign(save_a, a, covar_size);
                gwy_assign(saveparam, param, nparam);
            }
            else {
                gwy_debug("eval failed.");
                sumr_best = -1.0;
                break;
            }
        }
        while (!is_pos_def) {
            if (!first_pass)
                gwy_assign(a, save_a, covar_size);
            else
                first_pass = FALSE;

            for (j = 0; j < n_var_param; j++) {
                /* Add diagonal elements */
                guint diag = j*(j + 3)/2;

                /* This used to be there.  But it breaks the scaling because
                 * mfi is just a number while a[] elements scale with the
                 * param derivatives.
                 * a[diag] = save_a[diag]*(1.0 + mlambda) + nlfit->mfi*mlambda;
                 */
                if (G_UNLIKELY(save_a[diag] == 0.0))
                    a[diag] = nlfit->mfi*mlambda;
                else
                    a[diag] = save_a[diag]*(1.0 + mlambda);

                xr[j] = -v[j];
            }
            /* Choleski decompoation J'J in A*/
            is_pos_def = gwy_math_choleski_decompose(n_var_param, a);
            if (!is_pos_def) {
                /* Increase lambda */
                mlambda *= nlfit->minc;
                if (mlambda == 0.0)
                    mlambda = nlfit->mtol;
            }
        }

        gwy_math_choleski_solve(n_var_param, a, xr);

        /* Move master params along the solved gradient */
        for (i = 0; i < nparam; i++) {
            if (var_param_id[i] == G_MAXUINT || link_map[i] != i)
                continue;
            param[i] = saveparam[i] + xr[var_param_id[i]];
            if (fabs(param[i] - saveparam[i]) == 0)
                count++;
        }
        /* Sync slave params with master */
        for (i = 0; i < nparam; i++) {
            if (var_param_id[i] != G_MAXUINT && link_map[i] != i)
                param[i] = param[link_map[i]];
        }
        if (count == n_var_param)
            break;

        /* See what the new residua is. */
        sumr_new = gwy_math_nlfit_residua(func, func_idx, ndata, x, y, w,
                                          nparam, param, user_data, resid,
                                          &nlfit->eval);
        /* Catch failed evaluation even if it's not reported. */
        if (gwy_isinf(sumr_new) || gwy_isnan(sumr_new)) {
            gwy_debug("Inf/NaN encountered");
            nlfit->eval = FALSE;
            sumr_best = -1.0;
            break;
        }

        /* Good, we've finished */
        gwy_debug("sumr change %.16g -> %.16g (%g)",
                  sumr_best, sumr_new, (sumr_new - sumr_best)/sumr_best);
        if (sumr_new == 0
            || (miter > 2 && fabs((sumr_best - sumr_new)/sumr_best) < EPS)) {
            gwy_debug("reached zero residuum or converged");
            end = TRUE;
        }
        /* Overshoot, increase lambda */
        if (!nlfit->eval || sumr_new >= sumr_best) {
            mlambda *= nlfit->minc;
            if (mlambda == 0.0)
                mlambda = nlfit->mtol;
            unimproved++;
        }
        else
            unimproved = 0;

        if (unimproved >= 12) {
            gwy_debug("lack of improvement");
            break;
        }
        if (++miter >= nlfit->maxiter) {
            gwy_debug("maxiter reached");
            break;
        }

        if (set_fraction && !set_fraction((gdouble)miter/nlfit->maxiter)) {
            gwy_debug("cancelled");
            nlfit->eval = FALSE;
            sumr_best = -2.0;
            break;
        }
    } while (!end);

    gwy_assign(param, param_best, nparam);

    /* Parameter errors computation */
    if (nlfit->eval) {
        /* Fix the case of exactly zero derivatives.
         * XXX: Can we do something about the error then? */
        gwy_assign(a, save_a, covar_size);
        for (i = 0; i < n_var_param; i++) {
            guint diag = i*(i + 3)/2;

            if (a[diag] == 0.0)
                save_a[diag] = 1.0;
        }

        /* If we fail just slightly, try again. */
        if (!gwy_math_choleski_invert(n_var_param, save_a)) {
            gwy_assign(save_a, a, covar_size);
            for (i = 0; i < n_var_param; i++) {
                guint diag = i*(i + 3)/2;

                if (a[diag] == 0.0)
                    save_a[diag] = 1.0;
                save_a[diag] *= 1.0001;
            }
        }

        if (gwy_math_choleski_invert(n_var_param, save_a)) {
            /* Stretch the matrix to span over fixed params too.  This is
             * actually a bit silly and callers may want to undo it later. */
            nlfit->covar = covar = g_new(gdouble, nparam*(nparam + 1)/2);
            for (i = 0; i < nparam; i++) {
                gint iid = var_param_id[i];

                for (j = 0; j < i; j++) {
                    gint jid = var_param_id[j];

                    if (iid < 0 || jid < 0)
                        SLi(covar, i, j) = 0.0;   /* Fixed. */
                    else {
                        /* For linked params i > j does not ensure iid > jid. */
                        SLi(covar, i, j) = SLi(save_a,
                                               MAX(iid, jid), MIN(iid, jid));
                    }
                }
                if (iid < 0)
                    SLi(covar, i, j) = 1.0;
                else
                    SLi(covar, i, i) = SLi(save_a, iid, iid);
            }
            nlfit->dispersion = sumr_best/(ndata - n_var_param);
        }
        else {
            /* XXX: else what? */
            gwy_debug("cannot invert covariance matrix");
            sumr_best = -1.0;
            GWY_FREE(nlfit->covar);
        }
    }

    for (i = 0; i < nparam; i++) {
        if (gwy_isinf(param[i]) || gwy_isnan(param[i])) {
            sumr_best = nlfit->dispersion = -1.0;
            GWY_FREE(nlfit->covar);
            gwy_assign(param, origparam, nparam);
            gwy_debug("final params are Inf/NaN");
            break;
        }
    }

    if (nlfit->covar) {
        priv->nparam = nparam;
        covar = nlfit->covar;
        for (i = 0; i < nparam*(nparam + 1)/2; i++) {
             if (gwy_isinf(covar[i]) || gwy_isnan(covar[i])) {
                 priv->nparam = 0;
                 sumr_best = nlfit->dispersion = -1.0;
                 GWY_FREE(nlfit->covar);
                 gwy_assign(param, origparam, nparam);
                 gwy_debug("covariance matrix items are Inf/NaN");
                 break;
             }
        }
    }

fail:
    g_free(param_best);
    g_free(origparam);
    g_free(save_a);
    if (storage) {
        for (i = 0; i < nthreads; i++)
            g_free(storage[i]);
        g_free(storage);
    }
    g_free(saveparam);
    g_free(xr);
    g_free(fixed);
    g_free(var_param_id);
    g_free(resid);
    g_free(w);
    g_free(lmap);

    return sumr_best;
}

/**
 * gwy_math_nlfit_derive:
 * @x: The value to compute the derivative at.
 * @nparam: The nuber of parameters.
 * @param: Array of parameters (of size @nparam).
 * @fixed_param: Which parameters should be treated as fixed (corresponding
 *               entries are set to %TRUE).
 * @func: The fitted function.
 * @user_data: User data to be passed to @func.
 * @der: Array where the put the result to.
 * @success: Set to %TRUE if succeeds, %FALSE on failure.
 *
 * Numerically computes the partial derivatives of a function.
 *
 * This is a legacy name for function gwy_math_nlfit_diff().
 **/
void
gwy_math_nlfit_derive(gdouble x,
                      gint nparam,
                      const gdouble *param,
                      const gboolean *fixed_param,
                      GwyNLFitFunc func,
                      gpointer user_data,
                      gdouble *diff,
                      gboolean *success)
{
    return gwy_math_nlfit_diff(x, nparam, param, fixed_param, func, user_data,
                               diff, success);
}

/**
 * gwy_math_nlfit_diff:
 * @x: The value to compute the derivative at.
 * @nparam: The nuber of parameters.
 * @param: Array of parameters (of size @nparam).
 * @fixed_param: Which parameters should be treated as fixed (corresponding
 *               entries are set to %TRUE).
 * @func: The fitted function.
 * @user_data: User data to be passed to @func.
 * @der: Array where the put the result to.
 * @success: Set to %TRUE if succeeds, %FALSE on failure.
 *
 * Numerically computes the partial derivatives of a function.
 *
 * Since: 2.46
 **/
void
gwy_math_nlfit_diff(gdouble x,
                    gint nparam,
                    const gdouble *param,
                    const gboolean *fixed_param,
                    GwyNLFitFunc func,
                    gpointer user_data,
                    gdouble *der,
                    gboolean *success)
{
    gdouble *param_tmp;
    gdouble hj, left, right;
    gint j;

    param_tmp = g_newa(gdouble, nparam);
    gwy_assign(param_tmp, param, nparam);

    for (j = 0; j < nparam; j++) {
        if (fixed_param && fixed_param[j]) {
            der[j] = 0.0;
            continue;
        }

        hj = fabs(param_tmp[j]) * FitSqrtMachEps;
        if (hj == 0.0)
            hj = FitSqrtMachEps;

        param_tmp[j] -= hj;
        left = func(x, nparam, param_tmp, user_data, success);
        if (!*success)
            return;

        param_tmp[j] += 2 * hj;
        right = func(x, nparam, param_tmp, user_data, success);
        if (!*success)
            return;

        der[j] = (right - left)/2/hj;
        param_tmp[j] = param[j];
    }
}

/**
 * gwy_math_nlfit_diff_idx:
 * @i: Data index from the set {0, 1, 2, ..., ndata-1}.
 * @nparam: The nuber of parameters.
 * @param: Array of parameters (of size @nparam).
 * @fixed_param: Which parameters should be treated as fixed (corresponding
 *               entries are set to %TRUE).
 * @func: The fitted function.
 * @user_data: User data to be passed to @func.
 * @der: Array where the put the result to.
 * @success: Set to %TRUE if succeeds, %FALSE on failure.
 *
 * Numerically computes the partial derivatives of an opaque function.
 *
 * This function cannot be passed as derivative calculation function to
 * gwy_math_nlfit_new_idx() because it is not of the #GwyNLFitIdxDiffFunc.
 * Just pass %NULL to gwy_math_nlfit_new_idx() if you want automatic
 * numerical derivatives.
 *
 * You can employ this function in your own #GwyNLFitIdxDiffFunc function
 * if you need to modify the derivatives somehow, for instance to apply
 * weighting.
 *
 * Since: 2.46
 **/
void
gwy_math_nlfit_diff_idx(guint i,
                        gint nparam,
                        const gdouble *param,
                        const gboolean *fixed_param,
                        GwyNLFitIdxFunc func,
                        gpointer user_data,
                        gdouble *der,
                        gboolean *success)
{
    gdouble *param_tmp;
    gdouble hj, left, right;
    gint j;

    param_tmp = g_newa(gdouble, nparam);
    gwy_assign(param_tmp, param, nparam);

    for (j = 0; j < nparam; j++) {
        if (fixed_param && fixed_param[j]) {
            der[j] = 0.0;
            continue;
        }

        hj = fabs(param_tmp[j]) * FitSqrtMachEps;
        if (hj == 0.0)
            hj = FitSqrtMachEps;

        param_tmp[j] -= hj;
        left = func(i, param_tmp, user_data, success);
        if (!*success)
            return;

        param_tmp[j] += 2 * hj;
        right = func(i, param_tmp, user_data, success);
        if (!*success)
            return;

        der[j] = (right - left)/2/hj;
        param_tmp[j] = param[j];
    }
}

/* NB: we apply weighting to to resid[]. */
static gdouble
gwy_math_nlfit_residua(GwyNLFitFunc func,
                       GwyNLFitIdxFunc func_idx,
                       guint ndata,
                       const gdouble *x,
                       const gdouble *y,
                       const gdouble *weight,
                       guint nparam,
                       const gdouble *param,
                       gpointer user_data,
                       gdouble *resid,
                       gboolean *success)
{
    gdouble s = 0.0;
    gboolean ok = TRUE;

    g_return_val_if_fail(func || func_idx, -1.0);

    if (func_idx) {
#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            reduction(+:s) reduction(&&:ok) \
            shared(func_idx,ndata,resid,param,user_data)
#endif
        {
            guint ifrom = gwy_omp_chunk_start(ndata);
            guint ito = gwy_omp_chunk_end(ndata);
            guint i;

            for (i = ifrom; i < ito; i++) {
                gboolean ook;

                resid[i] = func_idx(i, param, user_data, &ook);
                s += resid[i] * resid[i];
                if (!ook) {
                    ok = FALSE;
                    break;
                }
            }
        }
    }
    else {
#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            reduction(+:s) reduction(&&:ok) \
            shared(func,x,y,ndata,resid,weight,nparam,param,user_data)
#endif
        {
            guint ifrom = gwy_omp_chunk_start(ndata);
            guint ito = gwy_omp_chunk_end(ndata);
            guint i;

            for (i = ifrom; i < ito; i++) {
                gboolean ook;

                resid[i] = func(x[i], nparam, param, user_data, &ook) - y[i];
                if (weight)
                    resid[i] *= weight[i];
                s += resid[i] * resid[i];
                if (!ook) {
                    ok = FALSE;
                    break;
                }
            }
        }
    }
    *success = ok;

    return s;
}

/**
 * gwy_math_nlfit_get_max_iterations:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Returns the maximum number of iterations of nonlinear fitter @nlfit.
 *
 * Returns: The maximum number of iterations.
 **/
gint
gwy_math_nlfit_get_max_iterations(GwyNLFitter *nlfit)
{
    return nlfit->maxiter;
}

/**
 * gwy_math_nlfit_set_max_iterations:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @maxiter: The maximum number of iterations.
 *
 * Sets the maximum number of iterations for nonlinear fitter @nlfit.
 **/
void
gwy_math_nlfit_set_max_iterations(GwyNLFitter *nlfit,
                                  gint maxiter)
{
    g_return_if_fail(maxiter > 0);
    nlfit->maxiter = maxiter;
}

/**
 * gwy_math_nlfit_get_approx_geometric:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Reports if a non-linear fitter performs approximately orthogonal fitting.
 *
 * This function does not do anything useful.
 *
 * Returns: %TRUE if the fitting is approximately orthogonal; %FALSE for
 *          ordinary least squares.
 *
 * Since: 2.47
 **/
gboolean
gwy_math_nlfit_get_approx_geometric(GwyNLFitter *nlfit)
{
    GwyNLFitterPrivate *priv;

    priv = find_private_data(nlfit, FALSE);
    return priv && priv->approx_geometric;
}

/**
 * gwy_math_nlfit_set_approx_geometric:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @setting: %TRUE to approximately orthogonal (geometrical) fitting.
 *
 * Sets a non-linear fitter to perform ordinary or approximately orthogonal
 * fitting.
 *
 * This function does not do anything useful.  Since 2.56 it is no-op.
 *
 * Since: 2.47
 **/
void
gwy_math_nlfit_set_approx_geometric(GwyNLFitter *nlfit,
                                    gboolean setting)
{
    GwyNLFitterPrivate *priv;

    priv = find_private_data(nlfit, TRUE);
    priv->approx_geometric = setting;
}

/**
 * gwy_math_nlfit_succeeded:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Obtains the status of the last fitting.
 *
 * Fitting failure can be (and usually should be) also determined by checking
 * for negative return value of gwy_math_nlfit_fit() or
 * gwy_math_nlfit_fit_full().  This function allows to test it later.
 *
 * Returns: %TRUE if the last fitting suceeded, %FALSE if it failed.
 *
 * Since: 2.7
 **/
gboolean
gwy_math_nlfit_succeeded(GwyNLFitter *nlfit)
{
    if ((!nlfit->covar && nlfit->dispersion >= 0.0)
        || (nlfit->covar && nlfit->dispersion < 0.0)) {
        g_warning("Covar and dispersion do not agree on whether the fit "
                  "was successful.");
        return FALSE;
    }

    return nlfit->covar != NULL;
}

/**
 * gwy_math_nlfit_get_sigma:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @par: Parameter index.
 *
 * Returns the standard deviation of parameter number @par.
 *
 * This function makes sense only after a successful fit and for a free
 * parameter.
 *
 * Returns: The SD of @par-th parameter.
 **/
gdouble
gwy_math_nlfit_get_sigma(GwyNLFitter *nlfit, gint par)
{
    g_return_val_if_fail(nlfit->covar, G_MAXDOUBLE);

    return sqrt(nlfit->dispersion * SLi(nlfit->covar, par, par));
}

/**
 * gwy_math_nlfit_get_dispersion:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Returns the residual sum divided by the number of degrees of freedom.
 *
 * This function can be used only after a successful fit.
 *
 * Returns: The dispersion.
 **/
gdouble
gwy_math_nlfit_get_dispersion(GwyNLFitter *nlfit)
{
    g_return_val_if_fail(nlfit->covar, G_MAXDOUBLE);
    return nlfit->dispersion;
}

/**
 * gwy_math_nlfit_get_nparam:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Gets the number of parameters of a nonlinear fitter.
 *
 * The number of parameters determines the size of covariance matrix obtained
 * by gwy_math_nlfit_get_covar() and valid arguments for
 * gwy_math_nlfit_get_correlations().
 *
 * Returns: The number of parameters for the last fit.  If the last fit was not
 *          successful zero is returned.
 *
 * Since: 2.47
 **/
gint
gwy_math_nlfit_get_nparam(GwyNLFitter *nlfit)
{
    GwyNLFitterPrivate *priv;

    priv = find_private_data(nlfit, FALSE);
    return priv ? priv->nparam : 0;
}

/**
 * gwy_math_nlfit_get_eval:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Gets the state of a nonlinear fitter.
 *
 * Returns: %TRUE if the last fit succeeded, %FALSE if it failed.
 *
 * Since: 2.47
 **/
gboolean
gwy_math_nlfit_get_eval(GwyNLFitter *nlfit)
{
    return nlfit->eval;
}

/**
 * gwy_math_nlfit_get_covar:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 *
 * Gets the covariance matrix of a nonlinear fitter.
 *
 * The returned matrix may be only used between fits.  Any fitting can free and
 * reallocate it.
 *
 * Returns: The covariance matrix, as lower triangular matrix, owned by the
 *          fitter.  The return value will be %NULL if the last fit did not
 *          succeed.
 *
 * Since: 2.47
 **/
const gdouble*
gwy_math_nlfit_get_covar(GwyNLFitter *nlfit)
{
    return nlfit->covar;
}

/**
 * gwy_math_nlfit_get_correlations:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @par1: First parameter index.
 * @par2: Second parameter index.
 *
 * Returns the correlation coefficient between @par1-th and @par2-th parameter.
 *
 * This function can be used only after a successful fit.
 *
 * Returns: The correlation coefficient.
 **/
gdouble
gwy_math_nlfit_get_correlations(GwyNLFitter *nlfit, gint par1, gint par2)
{
    gdouble t;

    g_return_val_if_fail(nlfit->covar, G_MAXDOUBLE);

    if (par1 == par2)
        return 1.0;
    if (par1 < par2)
        GWY_SWAP(gint, par1, par2);

    t = SLi(nlfit->covar, par1, par1) * SLi(nlfit->covar, par2, par2);
    if (t == 0) {
        g_warning("Zero element in covar matrix");
        return G_MAXDOUBLE;
    }

    return SLi(nlfit->covar, par1, par2)/sqrt(t);
}

/**
 * gwy_math_nlfit_set_callbacks:
 * @nlfit: A Marquardt-Levenberg nonlinear fitter.
 * @set_fraction: Function that sets fraction to output (or %NULL).
 * @set_message: Function that sets message to output (or %NULL).
 *
 * Sets callbacks reporting a non-linear least squares fitter progress.
 *
 * Since: 2.46
 **/
void
gwy_math_nlfit_set_callbacks(GwyNLFitter *fitter,
                             GwySetFractionFunc set_fraction,
                             GwySetMessageFunc set_message)
{
    GwyNLFitterPrivate *priv;

    priv = find_private_data(fitter, TRUE);
    priv->set_fraction = set_fraction;
    priv->set_message = set_message;
}

static GwyNLFitterPrivate*
find_private_data(GwyNLFitter *fitter, gboolean do_create)
{
    GwyNLFitterPrivate *priv;
    GList *l;

    G_LOCK(private_fitter_data);
    for (l = private_fitter_data; l; l = g_list_next(l)) {
        priv = (GwyNLFitterPrivate*)l->data;
        if (priv->fitter == fitter) {
            G_UNLOCK(private_fitter_data);
            return priv;
        }
    }

    if (!do_create) {
        G_UNLOCK(private_fitter_data);
        return NULL;
    }

    priv = g_new0(GwyNLFitterPrivate, 1);
    priv->fitter = fitter;
    private_fitter_data = g_list_prepend(private_fitter_data, priv);
    G_UNLOCK(private_fitter_data);

    return priv;
}

static void
free_private_data(GwyNLFitter *fitter)
{
    GwyNLFitterPrivate *priv;
    GList *l;

    G_LOCK(private_fitter_data);
    for (l = private_fitter_data; l; l = g_list_next(l)) {
        priv = (GwyNLFitterPrivate*)l->data;
        if (priv->fitter == fitter) {
            private_fitter_data = g_list_delete_link(private_fitter_data, l);
            g_free(priv);
            break;
        }
    }
    G_UNLOCK(private_fitter_data);
}

/**
 * gwy_math_nlfit_map_free_params:
 * @fixed_param: Which parameters are fixed.
 * @n: Length of @fixed_param array.
 * @id_map: Parameter id map to fill.
 *
 * Creates a map from free parameter ids to all-parameter ids.
 *
 * This is a helper function for reduction of full parameter vectors and
 * covariance matrices to only free parameters.
 *
 * The array @id_map must at least as many elements as there are free
 * parameters.  However, it is usually better to just pass an array of size @n
 * because the number of free parameters is what this function calculates.
 *
 * Returns: The number of free parameters.  This is the number of ids filled
 *          in @id_map.
 *
 * Since: 2.50
 **/
gint
gwy_math_nlfit_map_free_params(const gboolean *fixed_param,
                               gint n,
                               gint *id_map)
{
    gint i, j;

    if (!n)
        return 0;

    g_return_val_if_fail(fixed_param, 0);
    g_return_val_if_fail(id_map, 0);

    for (i = j = 0; i < n; i++) {
        if (!fixed_param[i])
            id_map[j++] = i;
    }

    return j;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwynlfit
 * @title: GwyNLFitter
 * @short_description: Marquardt-Levenberg nonlinear least square fitter
 * @see_also: #GwyNLFitPreset
 *
 * A new Marquardt-Levenberg nonlinear least square fitter can be created with
 * gwy_math_nlfit_new(), specifying the function to fit (as #GwyNLFitFunc) and
 * its derivative (as #GwyNLFitDerFunc). For functions for whose analytic
 * derivative is not available or very impractical, gwy_math_nlfit_derive()
 * (computing the derivative numerically) can be used instead.
 *
 * A fitter can be then repeatedly used on different data either in
 * gwy_math_nlfit_fit(), or gwy_math_nlfit_fit_full() when there are some
 * fixed or linked parameters.  Arbitrary additional (non-fitting) parameters
 * can be passed to the fited function in @user_data.
 *
 * After a successfull fit additional fit information can be obtained with
 * gwy_math_nlfit_get_dispersion(), gwy_math_nlfit_get_correlations(),
 * gwy_math_nlfit_get_sigma(). Note these functions may be used only after a
 * successfull fit. When a fitter is no longer needed, it should be freed with
 * gwy_math_nlfit_free().
 *
 * Several common functions are also available as fitting presets that can be
 * fitted with gwy_nlfit_preset_fit().  See #GwyNLFitPreset for details.
 **/

/**
 * GwyNLFitFunc:
 * @x: The value to compute the function at.
 * @nparam: The number of parameters (size of @param).
 * @param: Parameters.
 * @user_data: User data as passed to gwy_math_nlfit_fit().
 * @success: Set to %TRUE if succeeds, %FALSE on failure.
 *
 * Fitting function type for real-valued independent variables.
 *
 * Returns: The function value at @x.
 */

/**
 * GwyNLFitDerFunc:
 * @x: x-data as passed to gwy_math_nlfit_fit().
 * @nparam: The number of parameters (size of @param).
 * @param: Parameters.
 * @fixed_param: Which parameters should be treated as fixed (corresponding
 *               entries are set to %TRUE).
 * @func: The fitted function.
 * @user_data: User data as passed to gwy_math_nlfit_fit().
 * @der: Array where the @nparam partial derivatives by each parameter are
 *       to be stored.  Derivatives by fixed parameters are ignored so the
 *       function may set corresponding elements or not, whichever is more
 *       convenient.
 * @success: Set to %TRUE if succeeds, %FALSE on failure.
 *
 * Fitting function partial derivative type for real-valued independent
 * variables.
 */

/**
 * GwyNLFitIdxFunc:
 * @i: Data index from the set {0, 1, 2, ..., ndata-1}.
 * @param: Parameters.
 * @user_data: User data as passed to gwy_math_nlfit_fit().
 * @success: Set to %TRUE if succeeds, %FALSE on failure.
 *
 * Fitting function type for opaque indexed data.
 *
 * Note that unlike #GwyNLFitFunc which returns just the function value this
 * function must return the <emphasis>difference</emphasis> between the
 * function value and fitted data value (that is function minus data point).
 * When opaque data are fitted the fitter does not know what the data values
 * are.
 *
 * The function must take care of weighting.  The difference should be
 * multiplied by the inverse of the (unsquared) estimated error of the
 * @i-th data point.  Not multiplying by anything correspond to using the
 * default unit weights.
 *
 * Returns: Difference between the function value and data in the @i-th data
 * point.
 *
 * Since: 2.46
 **/

/**
 * GwyNLFitIdxDiffFunc:
 * @i: Data index from the set {0, 1, 2, ..., ndata-1}.
 * @param: Parameters.
 * @fixed_param: Which parameters should be treated as fixed (corresponding
 *               entries are set to %TRUE).  It may be %NULL is no parameters
 *               are fixed.  The function must set derivatives by fixed
 *               parameters to zero.
 * @func: The fitted function.
 * @user_data: User data as passed to gwy_math_nlfit_fit().
 * @der: Array where the @nparam partial derivatives by each parameter are
 *       to be stored.  Derivatives by fixed parameters are ignored so the
 *       function may set corresponding elements or not, whichever is more
 *       convenient.
 * @success: Set to %TRUE if succeeds, %FALSE on failure.
 *
 * Fitting function partial derivatives type for opaque indexed data.
 *
 * The function must take care of weighting.  The derivatives should be
 * multiplied by the inverse of the (unsquared) estimated error of the
 * @i-th data point.  Not multiplying by anything correspond to using the
 * default unit weights.
 *
 * Since: 2.46
 **/

/**
 * GwyNLFitter:
 * @fmarq: Evaluates the fitted function.
 * @dmarq: Evaluates derivatives of the fitted function.
 * @maxiter: Maximum number of iteration.
 * @eval: %TRUE if last evaluation succeeded.
 * @covar: Covariance matrix (set upon successful fit).
 * @dispersion: Mean residual sum of squares per point, set to -1 on failure.
 * @mfi: Lambda parameter is multiplied by it.  Probably keep at 1.
 * @mdec: Decrease of lambda parameter after an unsuccessful step.
 * @minc: Increase of lambda parameter after a successful step.
 * @mtol: If lambda parameter becomes zero it is set to this value.
 *
 * Non-linear least-squares fitter.
 *
 * The fields should be considered private.
 *
 * Use gwy_math_nlfit_get_eval(), gwy_math_nlfit_get_covar() and
 * gwy_math_nlfit_get_dispersion() and gwy_math_nlfit_succeeded() to examine
 * the fitter state.
 *
 * Use gwy_math_nlfit_set_max_iterations() to set the maximum iteration.
 *
 * In rare occasions you might want modify @mdec or @minc. The rest is better
 * left untouched.
 */

/**
 * GwyNLFitParamFlags:
 * @GWY_NLFIT_PARAM_ANGLE: Parameter is an angle.  It is defined in radians and
 *                         unitless but you may wish to convert it from/to
 *                         degrees in the user interface.  It is consdered
 *                         periodic and always reduced to the range [0, 2π).
 * @GWY_NLFIT_PARAM_ABSVAL: Parameter is a width or distance that is by
 *                          definition positive.  Only its absolute value
 *                          matters and it is always made non-negative.
 *
 * Type of fitting parameter properties.
 *
 * Since: 2.47
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
