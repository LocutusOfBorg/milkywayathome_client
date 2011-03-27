/*
Copyright 2008-2010 Travis Desell, Matthew Arsenault, Dave Przybylo,
Nathan Cole, Boleslaw Szymanski, Heidi Newberg, Carlos Varela, Malik
Magdon-Ismail and Rensselaer Polytechnic Institute.

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

#include "milkyway_util.h"
#include "separation.h"
#include "evaluation_state.h"


static char resolvedCheckpointPath[2048];


void initializeIntegral(Integral* integral, unsigned int number_streams)
{
    integral->background_integral = 0.0;
    integral->stream_integrals = (real*) mwCallocA(number_streams, sizeof(real));
    integral->probs = (Kahan*) mwCallocA(number_streams, sizeof(Kahan));
}

static void initializeState(const AstronomyParameters* ap, EvaluationState* es)
{
    unsigned int i;

    es->current_integral = 0;
    es->number_streams = ap->number_streams;

    es->number_integrals = ap->number_integrals;
    es->integrals = (Integral*) mwMalloc(sizeof(Integral) * ap->number_integrals);

    for (i = 0; i < ap->number_integrals; i++)
        initializeIntegral(&es->integrals[i], ap->number_streams);
}

EvaluationState* newEvaluationState(const AstronomyParameters* ap)
{
    EvaluationState* es;

    es = mwCalloc(1, sizeof(EvaluationState));
    initializeState(ap, es);

    return es;
}

void copyEvaluationState(EvaluationState* esDest, const EvaluationState* esSrc)
{
    unsigned int i;

    assert(esDest && esSrc);

    *esDest = *esSrc;
    for (i = 0; i < esSrc->number_integrals; ++i)
        esDest->integrals[i] = esSrc->integrals[i];
}

static void freeIntegral(Integral* i)
{
    mwFreeA(i->stream_integrals);
    mwFreeA(i->probs);
}

void freeEvaluationState(EvaluationState* es)
{
    unsigned int i;

    for (i = 0; i < es->number_integrals; ++i)
        freeIntegral(&es->integrals[i]);
    free(es->integrals);
    free(es);
}

void printEvaluationState(const EvaluationState* es)
{
    Integral* i;
    unsigned int j;

    printf("evaluation-state {\n"
           "  nu_step           = %u\n"
           "  mu_step          = %u\n"
           "  current_integral = %u\n"
           "  sum              = { %.20g, %.20g }\n",
           es->nu_step,
           es->mu_step,
           es->current_integral,
           es->sum.sum,
           es->sum.correction);

    for (i = es->integrals; i < es->integrals + es->number_integrals; ++i)
    {
        printf("integral: background_integral = %g\n", i->background_integral);
        printf("Stream integrals = ");
        for (j = 0; j < es->number_streams; ++j)
            printf("  %g, ", i->stream_integrals[j]);
        printf("Probs = ");
        for (j = 0; j < es->number_streams; ++j)
            printf(" { %g, %g },", i->probs[j].sum, i->probs[j].correction);
        printf("\n");
    }
    printf("\n");
}

static const char checkpoint_header[] = "separation_checkpoint";
static const char checkpoint_tail[] = "end_checkpoint";


static int readState(FILE* f, EvaluationState* es)
{
    Integral* i;
    char str_buf[sizeof(checkpoint_header) + 1];

    fread(str_buf, sizeof(checkpoint_header), 1, f);
    if (strncmp(str_buf, checkpoint_header, sizeof(str_buf)))
    {
        warn("Failed to find header in checkpoint file\n");
        return 1;
    }

    fread(&es->current_integral, sizeof(es->current_integral), 1, f);
    fread(&es->nu_step, sizeof(es->nu_step), 1, f);
    fread(&es->mu_step, sizeof(es->mu_step), 1, f);
    fread(&es->sum, sizeof(es->sum), 1, f);

    for (i = es->integrals; i < es->integrals + es->number_integrals; ++i)
    {
        fread(&i->background_integral, sizeof(i->background_integral), 1, f);
        fread(i->stream_integrals, sizeof(real), es->number_streams, f);
        fread(i->probs, sizeof(Kahan), es->number_streams, f);
    }

    fread(str_buf, sizeof(checkpoint_tail), 1, f);
    if (strncmp(str_buf, checkpoint_tail, sizeof(str_buf)))
    {
        warn("Failed to find tail in checkpoint file\n");
        return 1;
    }

    return 0;
}

int readCheckpoint(EvaluationState* es)
{
    int rc;
    FILE* f;

    f = mwOpenResolved(CHECKPOINT_FILE, "rb");
    if (!f)
    {
        perror("Opening checkpoint");
        return 1;
    }

    rc = readState(f, es);
    if (rc)
        warn("Failed to read state\n");

    fclose(f);

    return rc;
}

static inline void writeState(FILE* f, const EvaluationState* es)
{
    Integral* i;
    const Integral* endi = es->integrals + es->number_integrals;

    fwrite(checkpoint_header, sizeof(checkpoint_header), 1, f);

    fwrite(&es->current_integral, sizeof(es->current_integral), 1, f);
    fwrite(&es->nu_step, sizeof(es->nu_step), 1, f);
    fwrite(&es->mu_step, sizeof(es->mu_step), 1, f);
    fwrite(&es->sum, sizeof(es->sum), 1, f);

    for (i = es->integrals; i < endi; ++i)
    {
        fwrite(&i->background_integral, sizeof(i->background_integral), 1, f);
        fwrite(i->stream_integrals, sizeof(real), es->number_streams, f);
        fwrite(i->probs, sizeof(Kahan), es->number_streams, f);
    }

    fwrite(checkpoint_tail, sizeof(checkpoint_tail), 1, f);
}



#if !SEPARATION_OPENCL

int resolveCheckpoint()
{
    int rc;

    rc = mw_resolve_filename(CHECKPOINT_FILE, resolvedCheckpointPath, sizeof(resolvedCheckpointPath));
    if (rc)
        warn("Error resolving checkpoint file '%s': %d\n", CHECKPOINT_FILE, rc);
    return rc;
}

int writeCheckpoint(const EvaluationState* es)
{
    FILE* f;

    /* Avoid corrupting the checkpoint file by writing to a temporary file, and moving that */
    f = mw_fopen(CHECKPOINT_FILE_TMP, "wb");
    if (!f)
    {
        perror("Opening checkpoint temp");
        return 1;
    }

    writeState(f, es);
    fclose(f);

    if (mw_rename(CHECKPOINT_FILE_TMP, resolvedCheckpointPath))
    {
        perror("Failed to update checkpoint file");
        return 1;
    }

    return 0;
}

int maybeResume(EvaluationState* es)
{
    if (mw_file_exists(resolvedCheckpointPath))
    {
        mw_report("Checkpoint exists. Attempting to resume from it\n");

        if (readCheckpoint(es))
        {
            mw_report("Reading checkpoint failed\n");
            mw_remove(CHECKPOINT_FILE);
            return 1;
        }
        else
            mw_report("Successfully resumed checkpoint\n");
    }

    return 0;
}

#else /* SEPARATION_OPENCL */

int resolveCheckpoint()
{
    return 0;
}

int writeCheckpoint(const EvaluationState* es)
{
    return 0;
}

int maybeResume(EvaluationState* es)
{
    return 0;
}

#endif /* !SEPARATION_OPENCL */


