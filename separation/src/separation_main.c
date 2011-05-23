/*
Copyright 2008-2010 Travis Desell, Dave Przybylo, Nathan Cole, Matthew
Arsenault, Boleslaw Szymanski, Heidi Newberg, Carlos Varela, Malik
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

#include "separation.h"
#include "milkyway_util.h"
#include "milkyway_cpp_util.h"
#include "io_util.h"
#include <popt.h>


#define DEFAULT_ASTRONOMY_PARAMETERS "astronomy_parameters.txt"
#define DEFAULT_STAR_POINTS "stars.txt"

typedef struct
{
    char* star_points_file;
    char* ap_file;  /* astronomy parameters */
    char* separation_outfile;
    char* referenceFile;
    int debugBOINC;
    int do_separation;
    int setSeed;
    int separationSeed;
    int cleanupCheckpoint;
    int ignoreCheckpoint;  /* Ignoring checkpoint is not the same as disabling GPU checkpoints */
    int usePlatform;
    int useDevNumber;  /* Choose CL platform and device */
    int nonResponsive;  /* FIXME: Make this go away */
    unsigned int numChunk;  /* Also this */
    double responsivenessFactor;
    double targetFrequency;
    int pollingMode;
    int printVersion;
    int disableGPUCheckpointing;

    MWPriority processPriority;
    int setPriority;

    /* Force between normal, SSE2, SSE3 paths */
    int forceNoIntrinsics;
    int forceX87;
    int forceSSE2;
    int forceSSE3;

    int verbose;
} SeparationFlags;

/* Process priority to use for GPU version */
#ifndef _WIN32
  #define DEFAULT_GPU_PRIORITY 0
#else
  #define DEFAULT_GPU_PRIORITY MW_PRIORITY_NORMAL
#endif /* _WIN32 */

#define DEFAULT_POLLING_MODE 1
#define DEFAULT_RESPONSIVENESS_FACTOR 1.0
#define DEFAULT_TARGET_FREQUENCY 30.0
#define DEFAULT_DISABLE_GPU_CHECKPOINTING 0


#define EMPTY_SEPARATION_FLAGS { NULL, NULL, NULL, NULL, FALSE, FALSE, FALSE,  \
                                 0, FALSE, FALSE, 0, 0, 0, 0,                  \
                                 DEFAULT_RESPONSIVENESS_FACTOR,                \
                                 DEFAULT_TARGET_FREQUENCY,                     \
                                 DEFAULT_POLLING_MODE,                         \
                                 DEFAULT_DISABLE_GPU_CHECKPOINTING,            \
                                 0, 0, FALSE,                                  \
                                 FALSE, FALSE, FALSE, FALSE, FALSE             \
                               }
#define SEED_ARGUMENT (1 << 1)
#define PRIORITY_ARGUMENT (1 << 2)


static void printVersion(int boincTag)
{
    char versionStr[2048];

    snprintf(versionStr, sizeof(versionStr), "%s %u.%u %s %s %s%s%s%s",
             SEPARATION_APP_NAME,
             SEPARATION_VERSION_MAJOR, SEPARATION_VERSION_MINOR,
             SEPARATION_SYSTEM_NAME,
             ARCH_STRING,
             PRECSTRING,
             DENORMAL_STRING,
             SEPARATION_SPECIAL_STR,
             SEPARATION_SPECIAL_LIBM_STR);

    if (boincTag)
        warn("<search_application> %s </search_application>\n", versionStr);
    else
    {
        warn("%s %s\n",
             versionStr,
             BOINC_APPLICATION ? "BOINC" : "");
    }
}

static void freeSeparationFlags(SeparationFlags* sf)
{
    free(sf->star_points_file);
    free(sf->ap_file);
    free(sf->separation_outfile);
    free(sf->referenceFile);
}

/* Use hardcoded names if files not specified */
static void setDefaultFiles(SeparationFlags* sf)
{
    stringDefault(sf->star_points_file, DEFAULT_STAR_POINTS);
    stringDefault(sf->ap_file, DEFAULT_ASTRONOMY_PARAMETERS);
}

static void setCommonFlags(CLRequest* clr, const SeparationFlags* sf)
{
    clr->forceNoIntrinsics = sf->forceNoIntrinsics;
    clr->forceX87 = sf->forceX87;
    clr->forceSSE2 = sf->forceSSE2;
    clr->forceSSE3 = sf->forceSSE3;
    clr->verbose = sf->verbose;
}

#if SEPARATION_OPENCL

static void getCLReqFromFlags(CLRequest* clr, const SeparationFlags* sf)
{
    clr->platform = sf->usePlatform;
    clr->devNum = sf->useDevNumber;
    clr->nonResponsive = sf->nonResponsive;
    clr->numChunk = sf->numChunk;
    clr->enableCheckpointing = !sf->disableGPUCheckpointing;
    setCommonFlags(clr, sf);
}

#elif SEPARATION_CAL

static void getCLReqFromFlags(CLRequest* clr, const SeparationFlags* sf)
{
    clr->devNum = sf->useDevNumber;
    clr->responsivenessFactor = sf->responsivenessFactor;
    clr->targetFrequency = sf->targetFrequency <= 0.01 ? DEFAULT_TARGET_FREQUENCY : sf->targetFrequency;
    clr->pollingMode = sf->pollingMode;
    clr->enableCheckpointing = !sf->disableGPUCheckpointing;

    setCommonFlags(clr, sf);
}

#else

static void getCLReqFromFlags(CLRequest* clr, const SeparationFlags* sf)
{
    setCommonFlags(clr, sf);
}

#endif /* SEFPARATION_OPENCL */


/* Returns the newly allocated array of parameters */
static real* parseParameters(int argc, const char** argv, unsigned int* paramnOut, SeparationFlags* sfOut)
{
    poptContext context;
    int argRead;
    real* parameters = NULL;
    static unsigned int numParams;
    static int server_params = 0;
    static const char** rest;
    const char** argvCopy;
    static SeparationFlags sf = EMPTY_SEPARATION_FLAGS;

    static const struct poptOption options[] =
    {
        {
            "astronomy-parameter-file", 'a',
            POPT_ARG_STRING, &sf.ap_file,
            0, "Astronomy parameter file", NULL
        },

        {
            "star-points-file", 's',
            POPT_ARG_STRING, &sf.star_points_file,
            0, "Star points files", NULL
        },

        {
            "output", 'o',
            POPT_ARG_STRING, &sf.separation_outfile,
            0, "Output file for separation (enables separation)", NULL
        },

        {
            "seed", 'e',
            POPT_ARG_INT, &sf.separationSeed,
            SEED_ARGUMENT, "Seed for random number generator", NULL
        },

        {
            "ignore-checkpoint", 'i',
            POPT_ARG_NONE, &sf.ignoreCheckpoint,
            0, "Ignore the checkpoint file", NULL
        },

        {
            "cleanup-checkpoint", 'c',
            POPT_ARG_NONE, &sf.cleanupCheckpoint,
            0, "Delete checkpoint on successful", NULL
        },

        {
            "debug-boinc", 'g',
            POPT_ARG_NONE, &sf.debugBOINC,
            0, "Init BOINC with debugging. No effect if not built with BOINC_APPLICATION", NULL
        },

        {
            "process-priority", 'b',
            POPT_ARG_INT, &sf.processPriority,
         #ifndef _WIN32
            PRIORITY_ARGUMENT, "Set process nice value (-20 to 20)", NULL
         #else
            PRIORITY_ARGUMENT, "Set process priority class. Set priority class 0 (lowest) to 4 (highest)", NULL
         #endif /* _WIN32 */
        },

      #if SEPARATION_OPENCL || SEPARATION_CAL
        {
            "device", 'd',
            POPT_ARG_INT, &sf.useDevNumber,
            0, "Device number passed by boinc to use", NULL
        },

        {
            "num-chunk", 'u',
            POPT_ARG_INT, &sf.numChunk,
            0, "Manually set number of chunks per GPU iteration", NULL
        },

        {
            "responsiveness-factor", 'r',
            POPT_ARG_DOUBLE, &sf.responsivenessFactor,
            0, "Responsiveness factor for GPU", NULL
        },

        {
            "gpu-target-frequency", 'q',
            POPT_ARG_DOUBLE, &sf.targetFrequency,
            0, "Target frequency for GPU tasks" , NULL
        },

        {
            "gpu-polling-mode", 'm',
            POPT_ARG_INT, &sf.pollingMode,
            0, "Interval for polling GPU (< 0 for busy wait, 0 for calCtxWaitForEvents(), > 1 sets interval in ms)" , NULL
        },

        {
            "gpu-disable-checkpointing", 'k',
            POPT_ARG_NONE, &sf.disableGPUCheckpointing,
            0, "Disable checkpointing with GPUs" , NULL
        },

      #endif /* SEPARATION_OPENCL || SEPARATION_CAL */

      #if SEPARATION_OPENCL
        {
            "platform", 'l',
            POPT_ARG_INT, &sf.usePlatform,
            0, "CL Platform to use", NULL
        },

        {
            "verbose", '\0',
            POPT_ARG_NONE, &sf.verbose,
            0, "Print some extra debugging information", NULL
        },

      #endif /* SEPARATION_OPENCL */

        {
            "force-no-intrinsics", '\0',
            POPT_ARG_NONE, &sf.forceNoIntrinsics,
            0, "Use old default path", NULL
        },

        {
            "force-x87", '\0',
            POPT_ARG_NONE, &sf.forceX87,
            0, "Force to use x87 path (ignored if x86_64)", NULL
        },

        {
            "force-sse2", '\0',
            POPT_ARG_NONE, &sf.forceSSE2,
            0, "Force to use SSE2 path", NULL
        },

        {
            "force-sse3", '\0',
            POPT_ARG_NONE, &sf.forceSSE3,
            0, "Force to use SSE3 path", NULL
        },

        {
            "version", 'v',
            POPT_ARG_NONE, &sf.printVersion,
            0, "Print version information", NULL
        },

        {
            "p", 'p',
            POPT_ARG_NONE, &server_params,
            0, "Unused dummy argument to satisfy primitive arguments the server sends", NULL
        },

        {
            "np", '\0',
            POPT_ARG_INT | POPT_ARGFLAG_ONEDASH, &numParams,
            0, "Unused dummy argument to satisfy primitive arguments the server sends", NULL
        },

        POPT_AUTOHELP
        POPT_TABLEEND
    };

    /* Workaround for BOINC arguments being appended */
    argvCopy = mwFixArgv(argc, argv);
    context = poptGetContext(argv[0], argc, argvCopy ? argvCopy : argv, options, POPT_CONTEXT_POSIXMEHARDER);

    if (argc < 2)
    {
        poptPrintUsage(context, stderr, 0);
        poptFreeContext(context);
        exit(EXIT_FAILURE);
    }

    argRead = mwReadArguments(context);
    if (argRead < 0)
    {
        poptPrintHelp(context, stderr, 0);
        poptFreeContext(context);
        freeSeparationFlags(&sf);
        free(argvCopy);
        exit(EXIT_FAILURE);
    }

    if (sf.printVersion)
    {
        printVersion(FALSE);
        exit(EXIT_SUCCESS);
    }

    sf.setSeed = argRead & SEED_ARGUMENT; /* Check if these flags were used */
    sf.setPriority = argRead & PRIORITY_ARGUMENT;

    sf.do_separation = (sf.separation_outfile && strcmp(sf.separation_outfile, ""));
    if (!sf.do_separation) /* Skip server arguments for just running separation */
    {
        rest = poptGetArgs(context);
        parameters = mwReadRestArgs(rest, numParams, paramnOut);
        if (!parameters)
        {
            warn("Failed to read server arguments\n");
            freeSeparationFlags(&sf);
            poptFreeContext(context);
            free(argvCopy);
            exit(EXIT_FAILURE);
        }
    }

    free(argvCopy);
    poptFreeContext(context);
    setDefaultFiles(&sf);

    if (sf.do_separation)
        prob_ok_init(sf.separationSeed, sf.setSeed);

    *sfOut = sf;
    return parameters;
}

static IntegralArea* prepareParameters(const SeparationFlags* sf,
                                       AstronomyParameters* ap,
                                       BackgroundParameters* bgp,
                                       Streams* streams,
                                       const real* parameters,
                                       const int number_parameters)
{
    IntegralArea* ias;
    int badNumberParameters;

    ias = readParameters(sf->ap_file, ap, bgp, streams);
    if (!ias)
    {
        warn("Error reading astronomy parameters from file '%s'\n", sf->ap_file);
        return NULL;
    }

    badNumberParameters = number_parameters < 1;
    if (badNumberParameters && !sf->do_separation)
    {
        warn("Error reading parameters: Bad number of parameters from the command line\n");
        mwFreeA(ias);
        freeStreams(streams);
        return NULL;
    }

    if (!sf->do_separation)
    {
        if (setParameters(ap, bgp, streams, parameters, number_parameters))
        {
            mwFreeA(ias);
            freeStreams(streams);
            return NULL;
        }
    }

    return ias;
}

static int worker(const SeparationFlags* sf, const real* parameters, const int number_parameters)
{
    AstronomyParameters ap;
    BackgroundParameters bgp = EMPTY_BACKGROUND_PARAMETERS;
    Streams streams = EMPTY_STREAMS;
    IntegralArea* ias = NULL;
    StreamConstants* sc = NULL;
    SeparationResults* results = NULL;
    int rc;
    CLRequest clr;

    memset(&ap, 0, sizeof(ap));

    getCLReqFromFlags(&clr, sf);

    ias = prepareParameters(sf, &ap, &bgp, &streams, parameters, number_parameters);
    if (!ias)
    {
        warn("Failed to read parameters\n");
        return 1;
    }

    rc = setAstronomyParameters(&ap, &bgp);
    if (rc)
    {
        warn("Failed to set astronomy parameters\n");
        mwFreeA(ias);
        freeStreams(&streams);
        return 1;
    }

    setExpStreamWeights(&ap, &streams);
    sc = getStreamConstants(&ap, &streams);
    if (!sc)
    {
        warn("Failed to get stream constants\n");
        mwFreeA(ias);
        freeStreams(&streams);
        return 1;
    }

    results = newSeparationResults(ap.number_streams);
    rc = evaluate(results, &ap, ias, &streams, sc, sf->star_points_file,
                  &clr, sf->do_separation, sf->ignoreCheckpoint, sf->separation_outfile);
    if (rc)
        warn("Failed to calculate likelihood\n");

    printSeparationResults(results, ap.number_streams);

    if (sf->referenceFile)
        rc |= verifySeparationResults(sf->referenceFile, results, ap.number_streams);

    mwFreeA(ias);
    mwFreeA(sc);
    freeStreams(&streams);
    freeSeparationResults(results);

    return rc;
}

static int separationInit(const char* appName, int debugBOINC, MWPriority priority, int setPriority)
{
    int rc;

  #if DISABLE_DENORMALS
    mwDisableDenormalsSSE();
  #endif

    rc = mwBoincInit(appName, debugBOINC);
    if (rc)
        return rc;

    /* For GPU versions, default to using a higher process priority if not set */
  #if SEPARATION_OPENCL || SEPARATION_CAL
    if (!setPriority && mwSetProcessPriority(DEFAULT_GPU_PRIORITY))
        return 1;
  #endif

    /* If a  priority was specified, use that */
    if (setPriority && mwSetProcessPriority(priority))
        return 1;

  #if (SEPARATION_CAL || SEPARATION_OPENCL) && defined(_WIN32)
    /* We need to increase timer resolution to prevent big slowdown on windows when CPU is loaded. */
    if (mwSetTimerMinResolution())
        return 1;
  #endif /* SEPARATION_CAL && defined(_WIN32) */

    return 0;
}


static int separationSpecialCleanup()
{
  #if SEPARATION_CAL && defined(_WIN32)
    mwResetTimerResolution();
  #endif /* SEPARATION_CAL && defined(_WIN32) */

    return 0;
}


#ifdef MILKYWAY_IPHONE_APP
  #define main _iphone_main
#endif

int main(int argc, const char* argv[])
{
    int rc;
    SeparationFlags sf = EMPTY_SEPARATION_FLAGS;
    real* parameters;
    unsigned int number_parameters;

    parameters = parseParameters(argc, argv, &number_parameters, &sf);
    if (!parameters && !sf.do_separation)
    {
        warn("Could not parse parameters from the command line\n");
        exit(EXIT_FAILURE);
    }

    rc = separationInit(argv[0], sf.debugBOINC, sf.processPriority, sf.setPriority);
    if (rc)
        return rc;

    rc = worker(&sf, parameters, number_parameters);

    freeSeparationFlags(&sf);
    free(parameters);

  #if !SEPARATION_OPENCL
    if (!sf.ignoreCheckpoint && sf.cleanupCheckpoint && rc == 0)
    {
        mw_report("Removing checkpoint file '%s'\n", CHECKPOINT_FILE);
        mw_remove(CHECKPOINT_FILE);
    }
  #endif

  #if BOINC_APPLICATION
    printVersion(TRUE);
    mw_finish(rc);
  #endif

    separationSpecialCleanup();

    return rc;
}

