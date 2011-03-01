/*
Copyright (C) 2011  Matthew Arsenault

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


#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "nbody_types.h"
#include "lua_type_marshal.h"
#include "lua_nbodyctx.h"
#include "show.h"

#include "milkyway_util.h"

static const NBodyCtx defaultNBodyCtx =
{
    /* Grr lack of C99 named struct initializers in MSVC */
    /* .pot             */  EMPTY_POTENTIAL,
    /* .nbody           */  0,
    /* .timestep        */  0.0,
    /* .time_evolve     */  0.0,
    /* .orbit_timestep  */  0.0,
    /* time_orbit       */  0.0,

    /* .headline        */  NULL,
    /* .outfilename     */  NULL,
    /* .histogram       */  NULL,
    /* .histout         */  NULL,
    /* .outfile         */  NULL,

    /* .freqout         */  0.0,
    /* .theta           */  0.0,
    /* .eps2            */  0.0,

    /* .tree_rsize      */  4.0,
    /* .sunGCDist       */  8.0,
    /* .criterion       */  NewCriterion,
    /* .seed            */  0,
    /* .usequad         */  TRUE,
    /* .allowIncest     */  FALSE,

    /* .outputCartesian */  FALSE,
    /* .outputBodies    */  FALSE,
    /* .outputHistogram */  FALSE,
    /* .cp_filename     */  NULL,
    /* .cp_resolved     */  ""
};


static const MWEnumAssociation criterionOptions[] =
{
    { "NewCriterion", NewCriterion },
    { "Exact",        Exact        },
    { "BH86",         BH86         },
    { "SW93",         SW93         },
    END_MW_ENUM_ASSOCIATION
};

static int getCriterionT(lua_State* luaSt, void* v)
{
    return pushEnum(luaSt, criterionOptions, *(criterion_t*) v);
}

static int setCriterionT(lua_State* luaSt, void* v)
{
    warn("Set criterion\n");
    *(criterion_t*) v = checkEnum(luaSt, criterionOptions, -1);
    return 0;
}

NBodyCtx* checkNBodyCtx(lua_State* luaSt, int index)
{
    return (NBodyCtx*) mw_checknamedudata(luaSt, index, NBODY_CTX);
}

int pushNBodyCtx(lua_State* luaSt, const NBodyCtx* ctx)
{
    NBodyCtx* lctx;

    lctx = (NBodyCtx*)lua_newuserdata(luaSt, sizeof(NBodyCtx));

    luaL_getmetatable(luaSt, NBODY_CTX);
    lua_setmetatable(luaSt, -2);

    *lctx = *ctx;

    return 0;
}

static int createNBodyCtx(lua_State* luaSt)
{
    pushNBodyCtx(luaSt, &defaultNBodyCtx);
    return 1;
}

static int destroyNBodyCtx(lua_State* luaSt)
{
    NBodyCtx* ctx;

    ctx = (NBodyCtx*) lua_touserdata(luaSt, 1);
    printf("Goodbye: %f\n", ctx->timestep);
    return 0;
}

static int toStringNBodyCtx(lua_State* luaSt)
{
    NBodyCtx* ctx;
    char* str;

    ctx = checkNBodyCtx(luaSt, 1);
    str = showNBodyCtx(ctx);
    lua_pushstring(luaSt, str);
    free(str);

    return 1;
}

static const luaL_reg metaMethodsNBodyCtx[] =
{
    { "__gc",       destroyNBodyCtx  },
    { "__tostring", toStringNBodyCtx },
    { NULL, NULL }
};

static const luaL_reg methodsNBodyCtx[] =
{
    { "create",   createNBodyCtx },
    { NULL, NULL }
};

static const Xet_reg_pre gettersNBodyCtx[] =
{
    { "nbody",           getInt,        offsetof(NBodyCtx, nbody)           },
    { "timestep",        getNumber,     offsetof(NBodyCtx, timestep)        },
    { "time_evolve",     getNumber,     offsetof(NBodyCtx, time_orbit)      },
    { "orbit_timestep",  getNumber,     offsetof(NBodyCtx, orbit_timestep)  },
    { "time_orbit",      getNumber,     offsetof(NBodyCtx, time_orbit)      },
    { "freqOut",         getNumber,     offsetof(NBodyCtx, freqOut)         },
    { "theta",           getNumber,     offsetof(NBodyCtx, theta)           },
    { "eps2",            getNumber,     offsetof(NBodyCtx, eps2)            },
    { "tree_rsize",      getNumber,     offsetof(NBodyCtx, tree_rsize)      },
    { "sunGCDist",       getNumber,     offsetof(NBodyCtx, sunGCDist)       },
    { "criterion",       getCriterionT, offsetof(NBodyCtx, criterion)       },
    { "seed",            getLong,       offsetof(NBodyCtx, seed)            },
    { "useQuad",         getBool,       offsetof(NBodyCtx, usequad)         },
    { "allowIncest",     getBool,       offsetof(NBodyCtx, allowIncest)     },
    { "outputCartesian", getBool,       offsetof(NBodyCtx, outputCartesian) },
    { "outputBodies",    getBool,       offsetof(NBodyCtx, outputBodies)    },
    { "outputHistogram", getBool,       offsetof(NBodyCtx, outputHistogram) },
    { NULL, NULL, 0 }
};

static const Xet_reg_pre settersNBodyCtx[] =
{
    { "timestep",        setNumber,     offsetof(NBodyCtx, timestep)        },
    { "time_evolve",     setNumber,     offsetof(NBodyCtx, time_orbit)      },
    { "orbit_timestep",  setNumber,     offsetof(NBodyCtx, orbit_timestep)  },
    { "time_orbit",      setNumber,     offsetof(NBodyCtx, time_orbit)      },
    { "freqOut",         setNumber,     offsetof(NBodyCtx, freqOut)         },
    { "theta",           setNumber,     offsetof(NBodyCtx, theta)           },
    { "eps2",            setNumber,     offsetof(NBodyCtx, eps2)            },
    { "tree_rsize",      setNumber,     offsetof(NBodyCtx, tree_rsize)      },
    { "sunGCDist",       setNumber,     offsetof(NBodyCtx, sunGCDist)       },
    { "criterion",       setCriterionT, offsetof(NBodyCtx, criterion)       },
    { "seed",            setLong,       offsetof(NBodyCtx, seed)            },
    { "useQuad",         setBool,       offsetof(NBodyCtx, usequad)         },
    { "allowIncest",     setBool,       offsetof(NBodyCtx, allowIncest)     },
    { "outputCartesian", setBool,       offsetof(NBodyCtx, outputCartesian) },
    { "outputBodies",    setBool,       offsetof(NBodyCtx, outputBodies)    },
    { "outputHistogram", setBool,       offsetof(NBodyCtx, outputHistogram) },
    { NULL, NULL, 0 }
};

int registerNBodyCtx(lua_State* luaSt)
{
    return registerStruct(luaSt,
                          NBODY_CTX,
                          gettersNBodyCtx,
                          settersNBodyCtx,
                          metaMethodsNBodyCtx,
                          methodsNBodyCtx);
}

