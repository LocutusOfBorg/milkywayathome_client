/*
Copyright 2008, 2009 Travis Desell, Dave Przybylo, Nathan Cole,
Boleslaw Szymanski, Heidi Newberg, Carlos Varela, Malik Magdon-Ismail
and Rensselaer Polytechnic Institute.

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

#ifdef _WIN32
#include <float.h>
#endif

#include "milkyway.h"
#include "milkyway_priv.h"

inline static void atBound(double* angle, /* MODIFIED -- the angle to bound in radians */
                           double min,    /* IN -- inclusive minimum value */
                           double max     /* IN -- exclusive maximum value */
                          )
{
    while (*angle < min)
        *angle += D2PI;

    while (*angle >= max)
        *angle -= D2PI;
}

inline static void atBound2(double* theta, /* MODIFIED -- the -M_PI_2 to M_PI_2 angle */
                            double* phi    /* MODIFIED -- the 0 to D2PI angle */
                            )
{
    atBound(theta, -M_PI, M_PI);
    if (fabs(*theta) > M_PI_2)
    {
        *theta = M_PI - *theta;
        *phi += M_PI;
    }
    atBound(theta, -M_PI, M_PI);
    atBound(phi, 0.0, D2PI);
    if (fabs(*theta) == M_PI_2)
        *phi = 0.0;
    return;
}

inline static double slaDrange(double angle)
{
    double w = dmod(angle, D2PI);
    return ( fabs(w) < M_PI ) ? w : w - dsign(D2PI, angle);
}

inline static double slaDranrm(double angle)
{
    double w = dmod(angle, D2PI);
    return (w >= 0.0) ? w : w + D2PI;
}

inline static void slaDcc2s(vector v, double* a, double* b)
{
    double r = sqrt(sqr(X(v)) + sqr(Y(v)));

    *a = ( r != 0.0 ) ? atan2( Y(v), X(v) ) : 0.0;
    *b = ( Z(v) != 0.0 ) ? atan2( Z(v), r ) : 0.0;
}

//vickej2 for sgr stripes, the great circles are defined thus:
//sgr stripes run parallel to sgr longitude lines, centered on lamda=2.5*wedge number
//with mu=0 at the sgr equator and increasing in the +z direction (increasing from the equator with beta)
//and nu=0 at the center and increasing in the -y direction (inversely to lamda)
//in this manner an equatorial stripe of standard coordinate conventions is created.
inline static void gcToSgr( double mu, double nu, int wedge, double* lamda, double* beta )
{
    double x = cos(mu) * cos(nu);
    double y = -sin(nu);
    double z = sin(mu) * cos(nu);

    *lamda = atan2(y, x);
    *lamda = r2d(*lamda);
    *lamda += 2.5 * wedge;
    if (*lamda < 0)
        *lamda += 360.0;

    *beta = asin(z);
    *beta = r2d(*beta);

    return;
}


inline static void slaDcs2c(vector v, double a, double b)
{
    const double cosb = cos(b);
    X(v) = cos(a) * cosb;
    Y(v) = sin(a) * cosb;
    Z(v) = sin(b);
}

inline static void slaDmxv(const double dm[3][3], vector va, vector vb)
{
    unsigned int i, j;
    double w;
    vector vw;

    /* Matrix dm * vector va -> vector vw */
    for ( j = 0; j < 3; j++ )
    {
        w = 0.0;
        for ( i = 0; i < 3; i++ )
        {
            w += dm[j][i] * va[i];
        }
        vw[j] = w;
    }

    /* Vector vw -> vector vb */
    for ( j = 0; j < 3; j++ )
        vb[j] = vw[j];
}

inline static void slaEqgal( double dr, double dd, double* dl, double* db )
{
    vector v1;
    vector v2;

    static const double rmat[3][3] =
        {
            { -0.054875539726, -0.873437108010, -0.483834985808 },
            {  0.494109453312, -0.444829589425,  0.746982251810 },
            { -0.867666135858, -0.198076386122,  0.455983795705 }
        };


    /* Spherical to Cartesian */
    slaDcs2c(v1, dr, dd);

    /* Equatorial to Galactic */
    slaDmxv(rmat, v1, v2);

    /* Cartesian to spherical */
    slaDcc2s(v2, dl, db);

    /* Express in conventional ranges */
    *dl = slaDranrm(*dl);
    *db = slaDrange(*db);
}



typedef struct
{
    double rot11, rot12, rot13;
    double rot21, rot22, rot23;
    double rot31, rot32, rot33;
} SGR_TO_GAL_CONSTANTS;

/* CHECKME: This mess needs testing, but I don't think it's actually used. */
static void init_sgr_to_gal_constants(SGR_TO_GAL_CONSTANTS* sgc)
{
    const double radpdeg = M_PI / M_PI;
    const double phi = (M_PI + 3.75) * radpdeg;
    const double theta = (M_PI_2 - 13.46) * radpdeg;
    const double psi = (M_PI + 14.111534) * radpdeg;

    const double sintsq = sqr(sin(theta));  /* sin^2(theta), cos^2(theta) */
    const double costsq = sqr(cos(theta));

    const double cosphisq = sqr(cos(phi));  /* sin(phi), cos(phi) */
    const double sinphisq = sqr(sin(phi));

    const double cospsisq = sqr(cos(psi));  /* sin^2(psi), cos^2(psi) */
    const double sinpsisq = sqr(sin(psi));

    const double sint = sin(theta);  /* sin(theta), cos(theta) */
    const double cost = cos(theta);

    const double sinphi = sin(phi);  /* sin(phi), cos(phi) */
    const double cosphi = cos(phi);

    const double sinpsi = sin(psi);  /* sin(psi), cos(psi) */
    const double cospsi = cos(psi);

    sgc->rot11 = -(  cost * sinphi * sinpsi
                   - costsq * cosphi * cospsi
                   - cospsi * sintsq * cosphi)
                            /
                  (  cospsisq * cosphisq * costsq
                   + cospsisq * cosphisq * sintsq
                   + costsq * sinpsisq * sinphisq
                   + sinpsisq * cosphisq * costsq
                   + sinpsisq * cosphisq * sintsq
                   + costsq * cospsisq * sinphisq
                   + sintsq * sinphisq * cospsisq
                   + sintsq * sinphisq * sinpsisq);

    sgc->rot12 = -(  cost * sinphi * cospsi
                   + costsq * cosphi * sinpsi
                   + sinpsi * sintsq * cosphi)
                           /
                 (  cospsisq * cosphisq * costsq
                   + cospsisq * cosphisq * sintsq
                   + costsq * sinpsisq * sinphisq
                   + sinpsisq * cosphisq * costsq
                   + sinpsisq * cosphisq * sintsq
                   + costsq * cospsisq * sinphisq
                   + sintsq * sinphisq * cospsisq
                   + sintsq * sinphisq * sinpsisq);

    sgc->rot13 = (sint * sinphi)
                      /
        (costsq * sinphisq + cosphisq * costsq + cosphisq * sintsq + sintsq * sinphisq);

    sgc->rot21 = (  cost * cosphi * sinpsi
                  + costsq * cospsi * sinphi
                  + cospsi * sintsq * sinphi)
                         /
                (  cospsisq * cosphisq * costsq
                  + cospsisq * cosphisq * sintsq
                  + costsq * sinpsisq * sinphisq
                  + sinpsisq * cosphisq * costsq
                  + sinpsisq * cosphisq * sintsq
                  + costsq * cospsisq * sinphisq
                  + sintsq * sinphisq * cospsisq
                  + sintsq * sinphisq * sinpsisq);

    sgc->rot22 = -(  -cost * cosphi * cospsi
                   + costsq * sinpsi * sinphi
                   + sinpsi * sintsq * sinphi)
                             /
                  (  cospsisq * cosphisq * costsq
                   + cospsisq * cosphisq * sintsq
                   + costsq * sinpsisq * sinphisq
                   + sinpsisq * cosphisq * costsq
                   + sinpsisq * cosphisq * sintsq
                   + costsq * cospsisq * sinphisq
                   + sintsq * sinphisq * cospsisq
                   + sintsq * sinphisq * sinpsisq);

    sgc->rot23 = -(sint * cosphi)
                        /
                  (  costsq * sinphisq
                   + cosphisq * costsq
                   + cosphisq * sintsq
                   + sintsq * sinphisq);

    sgc->rot31 = (sinpsi * sint)
                        /
                 (  cospsisq * costsq
                  + sinpsisq * sintsq
                  + cospsisq * sintsq
                  + sinpsisq * costsq);

    sgc->rot32 = (cospsi * sint)
                        /
                 (  cospsisq * costsq
                  + sinpsisq * sintsq
                  + cospsisq * sintsq
                  + sinpsisq * costsq);

    sgc->rot33 = cost / (costsq + sintsq);
}

//mathematic reversal of majewski's defined rotations for lbr->sgr conversion
inline static void sgrToGal(double lamda, double beta, double* l, double* b)
{
    double x2 = 0.0, y2 = 0.0;

    SGR_TO_GAL_CONSTANTS _sgc;  /* FIXME: Shouldn't be done each call */
    init_sgr_to_gal_constants(&_sgc);
    SGR_TO_GAL_CONSTANTS* sgc = &_sgc;

    if (beta > M_PI_2)
    {
        beta = M_PI_2 - (beta - M_PI_2);
        lamda += M_PI;
        if (lamda > D2PI)
        {
            lamda -= D2PI;
        }
    }
    if (beta < -M_PI_2)
    {
        beta = -M_PI_2 - (beta + M_PI_2);
        lamda += M_PI;
        if (lamda > D2PI)
        {
            lamda -= D2PI;
        }
    }
    if (lamda < 0)
    {
        lamda += D2PI;
    }

    beta += M_PI_2;

    double z2 = cos(beta);

    if (lamda == 0.0)
    {
        x2 = sin(beta);
        y2 = 0.0;
    }
    else if (lamda < M_PI_2)
    {
        x2 = sqrt((1.0 - cos(beta) * cos(beta)) / (1.0 + tan(lamda) * tan(lamda)));
        y2 = x2 * tan(lamda);
    }
    else if (lamda == M_PI_2)
    {
        x2 = 0.0;
        y2 = sin(beta);

    }
    else if (lamda < M_PI)
    {
        y2 = sqrt((1.0 - cos(beta) * cos(beta)) / (1.0 / (tan(lamda) * tan(lamda)) + 1));
        x2 = y2 / tan(lamda);

    }
    else if (lamda == D2PI)
    {
        x2 = -sin(beta);
        y2 = 0;
    }
    else if (lamda < PI_3_2)
    {
        x2 = sqrt((1.0 - cos(beta) * cos(beta)) / (1.0 + tan(lamda) * tan(lamda)));
        y2 = x2 * tan(lamda);
        x2 = -x2;
        y2 = -y2;

    }
    else if (lamda == PI_3_2)
    {
        x2 = 0;
        y2 = -sin(beta);

    }
    else if (lamda < D2PI)
    {
        x2 = sqrt((1.0 - cos(beta) * cos(beta)) / (1.0 + tan(lamda) * tan(lamda)));
        y2 = x2 * tan(lamda);

    }
    else if (lamda == D2PI)
    {
        lamda = d2r(lamda);
        x2 = sin(beta);
        y2 = 0.0;
    }

    double x1 = sgc->rot11 * x2 + sgc->rot12 * y2 + sgc->rot13 * z2;
    double y1 = sgc->rot21 * x2 + sgc->rot22 * y2 + sgc->rot23 * z2;
    double z1 = sgc->rot31 * x2 + sgc->rot32 * y2 + sgc->rot33 * z2;

    if (z1 > 1)
    {
        *l = 0.0;
        *b = M_PI_2;
    }
    else
    {
        *b = asin(z1);
        *l = atan2(y1, x1);
        if (*l < 0.0)
            *l += D2PI;
    }

    return;
}

/* (ra, dec) in degrees */
inline static RA_DEC atGCToEq(
    double amu,  /* IN -- mu in radians */
    double anu,  /* IN -- nu in radians */
    double ainc  /* IN -- inclination in radians */
    )
{
    RA_DEC radec;
    double anode = d2r(NODE_GC_COORDS);

    /* Rotation */
    const double x2 = cos(amu - anode) * cos(anu);
    const double y2 = sin(amu - anode) * cos(anu);
    const double z2 = sin(anu);
    const double x1 = x2;
    const double y1 = y2 * cos(ainc) - z2 * sin(ainc);
    const double z1 = y2 * sin(ainc) + z2 * cos(ainc);


    radec.ra = atan2(y1, x1) + anode;
    radec.dec = asin(z1);

    atBound2(&radec.dec, &radec.ra);
    return radec;
}

/* Convert sun-centered lbr (degrees) into galactic xyz coordinates. */
void lbr2xyz(const double* lbr, vector xyz)
{
    double zp, d;
/*
    TODO: Use radians to begin with
    const double bsin = sin(B(lbr));
    const double lsin = sin(L(lbr));
    const double bcos = cos(B(lbr));
    const double lcos = cos(L(lbr));
*/

    const double bsin = sin(d2r(B(lbr)));
    const double lsin = sin(d2r(L(lbr)));
    const double bcos = cos(d2r(B(lbr)));
    const double lcos = cos(d2r(L(lbr)));

    Z(xyz) = R(lbr) * bsin;
    zp = R(lbr) * bcos;
    d = sqrt( sqr(sun_r0) + sqr(zp) - 2.0 * sun_r0 * zp * lcos);
    X(xyz) = (sqr(zp) - sqr(sun_r0) - sqr(d)) / (2.0 * sun_r0);
    Y(xyz) = zp * lsin;
}

inline static void atEqToGal (
    double ra,      /* IN -- ra in radians */
    double dec,     /* IN -- dec in radians */
    double* glong,  /* OUT -- Galactic longitude in radians */
    double* glat    /* OUT -- Galactic latitude in radians */
)
{
    /* Use SLALIB to do the actual conversion */
    slaEqgal(ra, dec, glong, glat);
    atBound2(glat, glong);
    return;
}

/* Get eta for the given wedge. */
inline static double wedge_eta(int wedge)
{
    return wedge * d2r(stripeSeparation) - d2r(57.5) - (wedge > 46 ? M_PI : 0.0);
}

/* Get inclination for the given wedge. */
inline static double wedge_incl(int wedge)
{
    return wedge_eta(wedge) + d2r(surveyCenterDec);
}

/* Convert GC coordinates (mu, nu) into l and b for the given wedge. */
void gc2lb( int wedge, double mu, double nu, double* l, double* b )
{
    RA_DEC radec = atGCToEq(d2r(mu), d2r(nu), wedge_incl(wedge));
    atEqToGal(radec.ra, radec.dec, l, b);
    *l = r2d(*l);
    *b = r2d(*b);
}

/* FIXME: This is almost certainly broken, but I don't think it's used  */
void gc2sgr( int wedge, double mu, double nu, double* l, double* b )
{
    fprintf(stderr, "Don't use me\n");
    mw_finish(EXIT_FAILURE);

    double lamda, beta;
    gcToSgr(wedge, d2r(mu), d2r(nu), &lamda, &beta);
    sgrToGal(lamda, beta, l, b);
    *l = r2d(*l);
    *b = r2d(*b);
}

