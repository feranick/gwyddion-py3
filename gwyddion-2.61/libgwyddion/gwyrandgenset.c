/*
 *  $Id: gwyrandgenset.c 23775 2021-05-24 21:01:14Z yeti-dn $
 *  Copyright (C) 2014-2021 David Necas (Yeti).
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

#include "gwymacros.h"
#include "gwymath.h"
#include "gwythreads.h"
#include "gwyrandgenset.h"
#include "gwyomp.h"

#define GWY_SQRT6 2.449489742783178098197284074705

typedef struct {
    GRand *rng;
    gdouble spare_gauss;
    gboolean have_spare_gauss;
    guint spare_bits_exp;
    guint32 spare_exp;
} GwyRandGen;

struct _GwyRandGenSet {
    guint n;
    GwyRandGen *rngs;
};

/**
 * gwy_rand_gen_set_new:
 * @n: The number of generators.
 *
 * Creates a new set of pseudorandom number generators.
 *
 * The generators are initialised to random states.
 *
 * Returns: A new set of pseudorandom number generators.
 *
 * Since: 2.37
 **/
GwyRandGenSet*
gwy_rand_gen_set_new(guint n)
{
    GwyRandGenSet *rngset;
    guint i;

    rngset = g_slice_new(GwyRandGenSet);
    rngset->rngs = g_new0(GwyRandGen, n);
    rngset->n = n;
    for (i = 0; i < n; i++)
        rngset->rngs[i].rng = g_rand_new();

    return rngset;
}

/**
 * gwy_rand_gen_set_init:
 * @rngset: A set of pseudorandom number generators.
 * @seed: The seed used to initialise the generators.
 *
 * Initialises a set of pseudorandom number generators using an integer seed.
 *
 * Since: 2.37
 **/
void
gwy_rand_gen_set_init(GwyRandGenSet *rngset,
                      guint seed)
{
    guint i;

    for (i = 0; i < rngset->n; i++) {
        g_rand_set_seed(rngset->rngs[i].rng, seed + i);
        rngset->rngs[i].have_spare_gauss = FALSE;
        rngset->rngs[i].spare_bits_exp = FALSE;
    }
}

/**
 * gwy_rand_gen_set_free:
 * @rngset: A set of pseudorandom number generators.
 *
 * Destroys a set of pseudorandom number generators.
 *
 * If you obtained individual generators using gwy_rand_gen_set_rng() you may not use them any more after calling this
 * function.
 *
 * Since: 2.37
 **/
void
gwy_rand_gen_set_free(GwyRandGenSet *rngset)
{
    guint i;

    for (i = 0; i < rngset->n; i++)
        g_rand_free(rngset->rngs[i].rng);
    g_free(rngset->rngs);
    g_slice_free(GwyRandGenSet, rngset);
}

/**
 * gwy_rand_gen_set_rng:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 *
 * Obtains a single generator from a set of pseudorandom number generators.
 *
 * The generator can be used to produce random numbers in any way for which you find the provided methods
 * insufficient.  However, if you reseed it manually, number sequence stability will be broken because sampling
 * functions may keep persistent information between calls.
 *
 * Returns: A pseudorandom number generator from @rngset.
 *
 * Since: 2.37
 **/
GRand*
gwy_rand_gen_set_rng(GwyRandGenSet *rngset,
                     guint i)
{
    g_return_val_if_fail(rngset, NULL);
    g_return_val_if_fail(i < rngset->n, NULL);
    return rngset->rngs[i].rng;
}

/**
 * gwy_rand_gen_set_range:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 * @lower: Lower limit of the range.
 * @upper: Upper limit of the range.
 *
 * Samples from a uniform distribution over given interval using one generator from a pseudorandom number generator
 * set.
 *
 * The generated number always lies inside the interval, neither endpoint value is ever returned.  Note if there are
 * no representable real numbers between @lower and @upper this function will never terminate.  You must ensure @upper
 * is sufficiently larger than @lower.
 *
 * Returns: A pseudorandom number.
 *
 * Since: 2.37
 **/
gdouble
gwy_rand_gen_set_range(GwyRandGenSet *rngset,
                       guint i,
                       gdouble lower,
                       gdouble upper)
{
    GwyRandGen *randgen = rngset->rngs + i;
    gdouble x;

    do {
        x = (upper - lower)*g_rand_double(randgen->rng) + lower;
    } while (G_UNLIKELY(x <= lower || x >= upper));

    return x;
}

/**
 * gwy_rand_gen_set_uniform:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 * @sigma: Rms of the distribution.
 *
 * Samples from a centered uniform distribution using one generator from a pseudorandom number generator set.
 *
 * The mean value of the distribution is zero, the rms value is given by @sigma.
 *
 * Returns: A pseudorandom number.
 *
 * Since: 2.37
 **/
gdouble
gwy_rand_gen_set_uniform(GwyRandGenSet *rngset,
                         guint i,
                         gdouble sigma)
{
    GwyRandGen *randgen = rngset->rngs + i;
    gdouble x;

    do {
        x = g_rand_double(randgen->rng);
    } while (G_UNLIKELY(x == 0.0));

    return (2.0*x - 1.0)*GWY_SQRT3*sigma;
}

/**
 * gwy_rand_gen_set_gaussian:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 * @sigma: Rms of the distribution.
 *
 * Samples from a centered Gaussian distribution using one generator from a pseudorandom number generator set.
 *
 * The mean value of the distribution is zero, the rms value is given by @sigma.
 *
 * Returns: A pseudorandom number.
 *
 * Since: 2.37
 **/
gdouble
gwy_rand_gen_set_gaussian(GwyRandGenSet *rngset,
                          guint i,
                          gdouble sigma)
{
    GwyRandGen *randgen = rngset->rngs + i;
    gdouble x, y, w;

    if (randgen->have_spare_gauss) {
        randgen->have_spare_gauss = FALSE;
        return sigma*randgen->spare_gauss;
    }

    do {
        x = -1.0 + 2.0*g_rand_double(randgen->rng);
        y = -1.0 + 2.0*g_rand_double(randgen->rng);
        w = x*x + y*y;
    } while (w >= 1.0 || G_UNLIKELY(w == 0.0));

    w = sqrt(-2.0*log(w)/w);
    randgen->spare_gauss = y*w;
    randgen->have_spare_gauss = TRUE;

    return sigma*x*w;
}

/**
 * gwy_rand_gen_set_exponential:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 * @sigma: Rms of the distribution.
 *
 * Samples from a centered exponential distribution using one generator from a pseudorandom number generator set.
 *
 * The mean value of the distribution is zero, the rms value is given by @sigma.
 *
 * Returns: A pseudorandom number.
 *
 * Since: 2.37
 **/
gdouble
gwy_rand_gen_set_exponential(GwyRandGenSet *rngset,
                             guint i,
                             gdouble sigma)
{
    GwyRandGen *randgen = rngset->rngs + i;
    gdouble x;
    gboolean sign;

    x = g_rand_double(randgen->rng);
    /* This is how we get exact 0.0 at least sometimes */
    if (G_UNLIKELY(x == 0.0))
        return 0.0;

    if (!randgen->spare_bits_exp) {
        randgen->spare_exp = g_rand_int(randgen->rng);
        randgen->spare_bits_exp = 32;
    }

    sign = randgen->spare_exp & 1;
    randgen->spare_exp >>= 1;
    randgen->spare_bits_exp--;

    if (sign)
        return -sigma/G_SQRT2*log(x);
    else
        return sigma/G_SQRT2*log(x);
}

/**
 * gwy_rand_gen_set_triangular:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 * @sigma: Rms of the distribution.
 *
 * Samples from a centered triangular distribution using one generator from a pseudorandom number generator set.
 *
 * The mean value of the distribution is zero, the rms value is given by @sigma.
 *
 * Returns: A pseudorandom number.
 *
 * Since: 2.37
 **/
gdouble
gwy_rand_gen_set_triangular(GwyRandGenSet *rngset,
                            guint i,
                            gdouble sigma)
{
    GwyRandGen *randgen = rngset->rngs + i;
    gdouble x;

    do {
        x = g_rand_double(randgen->rng);
    } while (G_UNLIKELY(x == 0.0));

    return (x <= 0.5 ? sqrt(2.0*x) - 1.0 : 1.0 - sqrt(2.0*(1.0 - x)))*sigma*GWY_SQRT6;
}

/**
 * gwy_rand_gen_set_multiplier:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 * @range: Half-range of the distribution.
 *
 * Samples from a multiplier distribution using one generator from a pseudorandom number generator set.
 *
 * The multiplier distribution is triangular distribution centered at 1, with values from [1-@range, 1+@range].
 *
 * Returns: A pseudorandom number.
 *
 * Since: 2.37
 **/
gdouble
gwy_rand_gen_set_multiplier(GwyRandGenSet *rngset,
                            guint i,
                            gdouble range)
{
    GRand *rng;

    rng = rngset->rngs[i].rng;
    return 1.0 + range*(g_rand_double(rng) - g_rand_double(rng));
}

/**
 * gwy_rand_gen_set_double:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 *
 * Samples uniform distribution over [0,1) using one generator from a pseudorandom number generator set.
 *
 * Returns: A pseudorandom number.
 *
 * Since: 2.37
 **/
gdouble
gwy_rand_gen_set_double(GwyRandGenSet *rngset,
                        guint i)
{
    return g_rand_double(rngset->rngs[i].rng);
}

/**
 * gwy_rand_gen_set_int:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 *
 * Samples a 32bit integer using a generator from a pseudorandom number generator set.
 *
 * Returns: A pseudorandom number.
 *
 * Since: 2.37
 **/
guint32
gwy_rand_gen_set_int(GwyRandGenSet *rngset,
                     guint i)
{
    return g_rand_int(rngset->rngs[i].rng);
}

/**
 * gwy_rand_gen_set_choose_shuffle:
 * @rngset: A set of pseudorandom number generators.
 * @i: Index of a generator from the set.
 * @n: Total number of possible indices.
 * @nchoose: Number of values to choose (at most equal to @n).  It is permitted to pass zero; the function returns
 *           %NULL then.
 *
 * Chooses randomly a subset of indices, in random order.
 *
 * The function creates an array containing integers from the set {0, 1, 2, ..., n-1}, each at most once, in random
 * order.
 *
 * To generate a permutation, simply pass @nchoose equal to @n.
 *
 * Returns: A newly allocated array of @nchoose integers.
 *
 * Since: 2.46
 **/
guint*
gwy_rand_gen_set_choose_shuffle(GwyRandGenSet *rngset,
                                guint i,
                                guint n,
                                guint nchoose)
{
    GRand *rng;
    guint *indices;
    guint j, k;

    g_return_val_if_fail(rngset, NULL);
    g_return_val_if_fail(i < rngset->n, NULL);
    g_return_val_if_fail(nchoose <= n, NULL);
    if (!nchoose)
        return NULL;

    rng = rngset->rngs[i].rng;
    /* XXX: This is not a good theoretical threshold.  We should do some benchmarking because rng is a comparatively
     * slow operation. */
    if (nchoose < (guint)sqrt(n)) {
        /* Generate indices directly and check for repetition. */
        indices = g_new(guint, nchoose);

        for (k = 0; k < nchoose; k++) {
            gboolean found;

            do {
                indices[k] = g_rand_int_range(rng, 0, n);
                found = FALSE;
                for (j = 0; j < k; j++) {
                    if (indices[j] == indices[k]) {
                        found = TRUE;
                        break;
                    }
                }
            } while (found);
        }
    }
    else {
        /* Use Knuth's shuffling algorithm (truncated to @nchoose). */
        indices = g_new(guint, n);

        for (k = 0; k < n; k++)
            indices[k] = k;

        for (k = 0; k < nchoose; k++) {
            j = g_rand_int_range(rng, 0, n-k);
            if (G_LIKELY(j)) {
                GWY_SWAP(guint, indices[k], indices[k+j]);
            }
        }

        indices = g_renew(guint, indices, nchoose);
    }

    return indices;
}

/**
 * gwy_rand_gen_set_fill_doubles:
 * @rngset: A set of pseudorandom number generators.
 * @random_numbers: Array to be filled with random numbers.
 * @n: How many numbers to generated.
 *
 * Fill an array with random doubles from a pseudorandom number generator set
 *
 * If @rngset has @N generators then the array is split into @N equally sized blocks (at least approximately) and
 * each block is filled from a different generator.  The filling of individual blocks is run multi-threaded (if
 * parallelisation is enabled).  The result does not depend on the number of threads, allowing parallelisation of
 * random number generation.
 *
 * Note that the number of generators @N must be a fixed constant for reproducible results.  It must not depend on the
 * actual number of processor cores or a similar variable quantity.
 *
 * Since: 2.59
 **/
void
gwy_rand_gen_set_fill_doubles(GwyRandGenSet *rngset,
                              gdouble *random_numbers,
                              gint n)
{
    guint ngenerators;

    g_return_if_fail(rngset);
    g_return_if_fail(!n || random_numbers);
    ngenerators = rngset->n;

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(random_numbers,n,rngset,ngenerators)
#endif
    {
        guint irfrom = gwy_omp_chunk_start(ngenerators);
        guint irto = gwy_omp_chunk_end(ngenerators);
        guint ir, ifrom, ito, i;
        GRand *rng;

        for (ir = irfrom; ir < irto; ir++) {
            rng = gwy_rand_gen_set_rng(rngset, ir);
            ifrom = ir*n/ngenerators;
            ito = (ir + 1)*n/ngenerators;
            for (i = ifrom; i < ito; i++)
                random_numbers[i] = g_rand_double(rng);
        }
    }
}

/**
 * gwy_rand_gen_set_fill_ints:
 * @rngset: A set of pseudorandom number generators.
 * @random_numbers: Array to be filled with random numbers.
 * @n: How many numbers to generated.
 *
 * Fill an array with random integers from a pseudorandom number generator set
 *
 * See gwy_rand_gen_set_fill_doubles() for discussion and caveats.
 *
 * Since: 2.59
 **/
void
gwy_rand_gen_set_fill_ints(GwyRandGenSet *rngset,
                           guint32 *random_numbers,
                           gint n)
{
    guint ngenerators;

    g_return_if_fail(rngset);
    g_return_if_fail(!n || random_numbers);
    ngenerators = rngset->n;

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(random_numbers,n,rngset,ngenerators)
#endif
    {
        guint irfrom = gwy_omp_chunk_start(ngenerators);
        guint irto = gwy_omp_chunk_end(ngenerators);
        guint ir, ifrom, ito, i;
        GRand *rng;

        for (ir = irfrom; ir < irto; ir++) {
            rng = gwy_rand_gen_set_rng(rngset, ir);
            ifrom = ir*n/ngenerators;
            ito = (ir + 1)*n/ngenerators;
            for (i = ifrom; i < ito; i++)
                random_numbers[i] = g_rand_int(rng);
        }
    }
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyrandgenset
 * @title: GwyRandGenSet
 * @short_description: Set of random number generators
 *
 * #GwyRandGenSet represents a set of pseudoradnom number generators initialised together, but each producing
 * a different sequence of numbers. This is useful when you use pseudorandom number generators to optionally randomize
 * several different things.  Using a common generator would require always generating exactly the same number of
 * random numbers, even for quantities you do not want randomized, in order to keep the random number sequences
 * stable.
 *
 * #GwyRandGenSet also provides functions sample a few common distributions such as Gaussian or exponential.  It
 * should be noted that the individual sampling functions may advance the generator state differently.  This means
 * |[
 * x = gwy_rand_gen_set_gaussian(rngset, 0, 1.0);
 * y = gwy_rand_gen_set_gaussian(rngset, 0, 1.0);
 * ]|
 * may produce a different value of @y than
 * |[
 * x = gwy_rand_gen_set_exponential(rngset, 0, 1.0);
 * y = gwy_rand_gen_set_gaussian(rngset, 0, 1.0);
 * ]|
 * even if the initial generator state was the same.
 **/

/**
 * GwyRandGenSet:
 *
 * #GwyRandGenSet is an opaque data structure and should be only manipulated with the functions below.
 *
 * Since: 2.37
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
