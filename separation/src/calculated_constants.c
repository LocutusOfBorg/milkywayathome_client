/*
 *  Copyright (c) 2008-2010 Travis Desell, Nathan Cole, Dave Przybylo
 *  Copyright (c) 2008-2010 Boleslaw Szymanski, Heidi Newberg
 *  Copyright (c) 2008-2010 Carlos Varela, Malik Magdon-Ismail
 *  Copyright (c) 2008-2011 Rensselaer Polytechnic Institute
 *  Copyright (c) 2010-2011 Matthew Arsenault
 *
 *  This file is part of Milkway@Home.
 *
 *  Milkway@Home is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "separation_constants.h"
#include "calculated_constants.h"
#include "milkyway_util.h"
#include "milkyway_math.h"
#include "coordinates.h"
#include "gauss_legendre.h"
#include "integrals.h"

static inline mwvector streamA(const StreamParameters* parameters)
{
    mwvector a;
    X(a) = mw_sin(parameters->theta) * mw_cos(parameters->phi);
    Y(a) = mw_sin(parameters->theta) * mw_sin(parameters->phi);
    Z(a) = mw_cos(parameters->theta);
    W(a) = 0.0;
    return a;
}

static inline mwvector streamC(const AstronomyParameters* ap, int wedge, real mu, real r)
{
    LB lb;
    mwvector lbr;

    lb = gc2lb(wedge, mu, 0.0);

    L(lbr) = LB_L(lb);
    B(lbr) = LB_B(lb);
    R(lbr) = r;
    W(lbr) = 0.0;
    return lbr2xyz(ap, lbr);
}

/*

  background_parameters[0] = alpha = 1.0
  background_parameters[3] = delta = 1.0


  background_parameters[1] = q
  background_parameters[2] = r0

  stream_weight = epsilon

  stream_parameters[0] = mu
  stream_parameters[1] = r
  stream_parameters[2] = theta
  stream_parameters[3] = phi
  stream_parameters[4] = sigma

 */

int setAstronomyParameters(AstronomyParameters* ap, const BackgroundParameters* bgp)
{
    ap->alpha = bgp->alpha;
    ap->q     = bgp->q;

    ap->r0    = bgp->r0;
    ap->delta = bgp->delta;

    ap->q_inv = inv(ap->q);
    ap->q_inv_sqr = inv(sqr(ap->q));

    ap->aux_bg_profile = (bgp->a != 0.0) || (bgp->b != 0.0) || (bgp->c != 0.0);
    ap->bg_a = bgp->a;
    ap->bg_b = bgp->b;
    ap->bg_c = bgp->c;

    if (ap->convolve == 0 || ap->convolve > MAX_CONVOLVE || !mwEven(ap->convolve))
    {
        mw_printf("convolve (%u) must be > 0, <= 256 and even\n", ap->convolve);
        return 1;
    }

 //   ap->coeff = 1.0 / (stdev * SQRT_2PI);
    ap->alpha_delta3 = 3.0 - ap->alpha + ap->delta;

    ap->exp_background_weight = mw_exp(bgp->epsilon);
    ap->fast_h_prob = (ap->alpha == 1.0 && ap->delta == 1.0);

    ap->sun_r0 = const_sun_r0;
    ap->m_sun_r0 = -ap->sun_r0;

    return 0;
}

void setExpStreamWeights(const AstronomyParameters* ap, Streams* streams)
{
    int i;

    streams->sumExpWeights = ap->exp_background_weight;
    for (i = 0; i < streams->number_streams; i++)
    {
        streams->parameters[i].epsilonExp = mw_exp(streams->parameters[i].epsilon);
        streams->sumExpWeights += streams->parameters[i].epsilonExp;
    }

    streams->sumExpWeights *= 0.001;
}

StreamConstants* getStreamConstants(const AstronomyParameters* ap, const Streams* streams)
{
    int i;
    StreamConstants* sc;
    real stream_sigma;
    real sigma_sq2;

    sc = (StreamConstants*) mwMallocA(streams->number_streams * sizeof(StreamConstants));

    for (i = 0; i < streams->number_streams; ++i)
    {
        stream_sigma = streams->parameters[i].sigma;

        if (stream_sigma == 0.0)
        {
            mw_printf("stream sigma 0.0 is invalid\n");
            mwFreeA(sc);
            return NULL;
        }

        sc[i].large_sigma = (stream_sigma > SIGMA_LIMIT || stream_sigma < -SIGMA_LIMIT);
        sigma_sq2 = 2.0 * sqr(stream_sigma);

        sc[i].sigma_sq2_inv = 1.0 / sigma_sq2;

        sc[i].a = streamA(&streams->parameters[i]);
        sc[i].c = streamC(ap,
                          ap->wedge,
                          streams->parameters[i].mu,
                          streams->parameters[i].r);
    }

    return sc;
}

void freeStreamGauss(StreamGauss sg)
{
    mwFreeA(sg.dx);
    mwFreeA(sg.qgaus_W);
}

StreamGauss getStreamGauss(int convolve)
{
    int i;
    StreamGauss sg;
    real* qgaus_X;

    qgaus_X = (real*) mwMallocA(sizeof(real) * convolve);
    sg.qgaus_W = (real*) mwMallocA(sizeof(real) * convolve);

    gaussLegendre(-1.0, 1.0, qgaus_X, sg.qgaus_W, convolve);

    sg.dx = (real*) mwMallocA(sizeof(real) * convolve);

    /*Using old (single-sided gaussian stdev = 0.6) to spread points.  This is a small simplification when using 
    modfit, but does not cause any problems since it is parameter independent.  The weights will be calculated 
    later based on the two-sided gaussian.*/

    for (i = 0; i < convolve; ++i)
    {
        sg.dx[i] = 3.0 * stdev * qgaus_X[i];
    }

    mwFreeA(qgaus_X);

    return sg;
}

NuConstants* prepareNuConstants(unsigned int nu_steps, real nu_step_size, real nu_min)
{
    unsigned int i;
    real tmp1, tmp2;
    NuConstants* nu_consts;

    nu_consts = (NuConstants*) mwMallocA(sizeof(NuConstants) * nu_steps);

    for (i = 0; i < nu_steps; ++i)
    {
        nu_consts[i].nu = nu_min + (i * nu_step_size);

        tmp1 = d2r(90.0 - nu_consts[i].nu - nu_step_size);
        tmp2 = d2r(90.0 - nu_consts[i].nu);

        nu_consts[i].id = mw_cos(tmp1) - mw_cos(tmp2);
        nu_consts[i].nu += 0.5 * nu_step_size;
    }

    return nu_consts;
}

NuId calcNuStep(const IntegralArea* ia, const unsigned int nu_step)
{
    NuId nuid;
    real tmp1, tmp2;

    nuid.nu = ia->nu_min + (nu_step * ia->nu_step_size);

    tmp1 = d2r(90.0 - nuid.nu - ia->nu_step_size);
    tmp2 = d2r(90.0 - nuid.nu);

    nuid.id = mw_cos(tmp1) - mw_cos(tmp2);
    nuid.nu += 0.5 * ia->nu_step_size;

    return nuid;
}

LBTrig* precalculateLBTrig(const AstronomyParameters* ap,
                           const IntegralArea* ia,
                           int transpose)
{
    unsigned int i, j, idx;
    LBTrig* lbts;
    NuId nuid;
    LB lb;
    real mu;

    lbts = (LBTrig*) mwMallocA(sizeof(LBTrig) * ia->nu_steps * ia->mu_steps);

    for (i = 0; i < ia->nu_steps; ++i)
    {
        nuid = calcNuStep(ia, i);
        for (j = 0; j < ia->mu_steps; ++j)
        {
            mu = ia->mu_min + (((real) j + 0.5) * ia->mu_step_size);
            lb = gc2lb(ap->wedge, mu, nuid.nu);
            idx = transpose ? j * ia->nu_steps + i : i * ia->mu_steps + j;
            lbts[idx] = lb_trig(lb);
        }
    }

    return lbts;
}

