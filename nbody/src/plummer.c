/* Copyright (c) 1993, 2001 Joshua E. Barnes, Honolulu, HI.
   Copyright 2010 Matthew Arsenault, Travis Desell, Boleslaw
Szymanski, Heidi Newberg, Carlos Varela, Malik Magdon-Ismail and
Rensselaer Polytechnic Institute.

This file is part of Milkway@Home.

Milkyway@Home is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Milkyway@Home is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Milkyway@Home.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "nbody_priv.h"
#include "nbody_util.h"
#include "dSFMT.h"

static real unitRandom(dsfmt_t* dsfmtState)
{
    return xrandom(dsfmtState, -1.0, 1.0);
}

static mwvector randomVec(dsfmt_t* dsfmtState)
{
    /* pick from unit cube */
    mwvector vec;

    X(vec) = unitRandom(dsfmtState);
    Y(vec) = unitRandom(dsfmtState);
    Z(vec) = unitRandom(dsfmtState);
    W(vec) = 0.0;

    return vec;
}

/* pickshell: pick a random point on a sphere of specified radius. */
static inline mwvector pickshell(dsfmt_t* dsfmtState, real rad)
{
    real rsq, rsc;
    mwvector vec;

    do                      /* pick point in NDIM-space */
    {
        vec = randomVec(dsfmtState);
        rsq = mw_sqrv(vec);         /* compute radius squared */
    }
    while (rsq > 1.0);              /* reject if outside sphere */

    rsc = rad / mw_sqrt(rsq);         /* compute scaling factor */
    mw_incmulvs(vec, rsc);            /* rescale to radius given */

    return vec;
}

static void printPlummer(const NBodyCtx* ctx, mwvector rshift, mwvector vshift)
{
    fprintf(ctx->outfile,
            "<plummer_r> %.14g %.14g %.14g </plummer_r>\n"
            "<plummer_v> %.14g %.14g %.14g </plummer_v>\n",
            X(rshift), Y(rshift), Z(rshift),
            X(vshift), Y(vshift), Z(vshift));
}

/* generatePlummer: generate Plummer model initial conditions for test
 * runs, scaled to units such that M = -4E = G = 1 (Henon, Hegge,
 * etc).  See Aarseth, SJ, Henon, M, & Wielen, R (1974) Astr & Ap, 37,
 * 183.
 */
void generatePlummer(NBodyCtx* ctx, unsigned int modelIdx, bodyptr bodytab)
{
    bodyptr p, endp;
    real rsc, vsc, r, v, x, y;
    mwvector scaledrshift = ZERO_VECTOR;
    mwvector scaledvshift = ZERO_VECTOR;
    mwvector cmr          = ZERO_VECTOR;
    mwvector cmv          = ZERO_VECTOR;

    dsfmt_t dsfmtState;
    real rnd;

    DwarfModel* model = &ctx->models[modelIdx];
    InitialConditions* ic = &model->initialConditions;

    const real rnbody = (real) model->nbody;
    const real mass   = model->mass;
    const real mpp    = mass / rnbody;     /* mass per particle */

    /* The coordinates to shift the plummer sphere by */
    mwvector rshift = ic->position;
    mwvector vshift = ic->velocity;

    dsfmt_init_gen_rand(&dsfmtState, ctx->seed);

    printPlummer(ctx, rshift, vshift);

    rsc = model->scale_radius;              /* set length scale factor */
    vsc = mw_sqrt(model->mass / rsc);       /* and recip. speed scale */

    scaledrshift = mw_mulvs(rshift, rsc);   /* Multiply shift by scale factor */
    scaledvshift = mw_mulvs(vshift, vsc);   /* Multiply shift by scale factor */

    endp = bodytab + model->nbody;
    for (p = bodytab; p < endp; ++p)   /* loop over particles */
    {
        Type(p) = BODY(modelIdx);    /* tag as a body belonging to this model */
        Mass(p) = mpp;               /* set masses equal */

        /* returns [0, 1) */
        rnd = (real) dsfmt_genrand_close_open(&dsfmtState);

        /* pick r in struct units */

        r = 1.0 / mw_sqrt(mw_pow(rnd, -2.0 / 3.0) - 1.0);

        Pos(p) = pickshell(&dsfmtState, rsc * r);     /* pick scaled position */
        mw_incaddv(Pos(p), rshift);      /* move the position */
        mw_incaddv(cmr, Pos(p));         /* add to running sum */

        do                      /* select from fn g(x) */
        {
            x = xrandom(&dsfmtState, 0.0, 1.0);      /* for x in range 0:1 */
            y = xrandom(&dsfmtState, 0.0, 0.1);      /* max of g(x) is 0.092 */
        }   /* using von Neumann tech */
        while (y > -cube(x - 1.0) * sqr(x) * cube(x + 1.0) * mw_sqrt(1.0 - sqr(x)));

        v = M_SQRT2 * x / mw_sqrt(mw_sqrt(1.0 + sqr(r)));   /* find v in struct units */
        Vel(p) = pickshell(&dsfmtState, vsc * v);        /* pick scaled velocity */
        mw_incaddv(Vel(p), vshift);      /* move the velocity */
        mw_incaddv(cmv, Vel(p));         /* add to running sum */
    }

    mw_incdivs(cmr, rnbody);      /* normalize cm coords */
    mw_incdivs(cmv, rnbody);
}


