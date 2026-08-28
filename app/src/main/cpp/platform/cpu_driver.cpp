// cpu_driver.cpp — CPU car driver for the Android port.
//
// The 1999 PC source drop predates the finished CPU racing AI: ai_car.cpp is
// a 5KB stub (CAI_ResetCar is empty, CPU players get NO control handler and a
// NULL conhandler — the car spawns with physics but nothing drives it). The
// finished 127KB AI only exists in the newer Xbox tree, which is too far
// diverged to drop in.
//
// This is a compact replacement that drives through the SAME control pipeline
// a human uses (CTRL dx/dy -> CON_LocalCarControl): follow the level's AI-node
// racing line (loaded by ainode.cpp from the level's .fan data), steering at a
// speed-scaled look-ahead point, easing the throttle through corners, with
// simple reverse-out-of-trouble and flip-upright recovery.
//
// Hooked up by ANDROID_PORT edits in ctrlread.cpp (CTRL_TYPE_CPU case) and
// player.cpp (PLAYER_CPU gets CON_LocalCarControl as conhandler).

#include <stddef.h>   // before game headers (long=int define, see windows.h)
#include <math.h>
#include <android/log.h>

#include "revolt.h"
#include "main.h"
#include "geom.h"
#include "particle.h"
#include "NewColl.h"
#include "Body.h"
#include "car.h"
#include "ctrlread.h"
#include "object.h"
#include "control.h"
#include "player.h"
#include "ainode.h"
#include "timing.h"
#include "gameloop.h"

// per-player recovery state, indexed by player slot
static REAL sStuckTime[MAX_NUM_PLAYERS];
static REAL sReverseTime[MAX_NUM_PLAYERS];
static REAL sAirTime[MAX_NUM_PLAYERS];
static REAL sWrongWayTime[MAX_NUM_PLAYERS];
static REAL sDirSign[MAX_NUM_PLAYERS];   // 0 = uninitialised, +1/-1 = race-direction sense

void CRD_CpuInput(CTRL *Control)
{
    // the handler only receives the CTRL, which is embedded in the PLAYER
    PLAYER *player = (PLAYER *)((char *)Control - offsetof(PLAYER, controls));
    CAR *car = &player->car;

    Control->dx = 0;
    Control->dy = 0;
    Control->digital = 0;

    if (!AiNodeNum || !car->Body)
        return;

    long slot = (long)(player - Players);
    if (slot < 0 || slot >= MAX_NUM_PLAYERS)
        return;
    if (sDirSign[slot] == 0.0f)
        sDirSign[slot] = 1.0f;

    REAL fwdSpeed = VecDotVec(&car->Body->Centre.Vel, &car->Body->Centre.WMatrix.mv[L]);
    REAL absSpeed = (fwdSpeed < 0) ? -fwdSpeed : fwdSpeed;

    // flipped / airborne too long -> right the car (idigital edge fires once)
    if (car->NWheelFloorContacts == 0) {
        sAirTime[slot] += TimeStep;
        if (sAirTime[slot] > 2.5f) {
            sAirTime[slot] = 0.0f;
            Control->digital = CTRL_RESET;
            return;
        }
    } else {
        sAirTime[slot] = 0.0f;
    }

    // backing out of a wall / stuck spot
    if (sReverseTime[slot] > 0.0f) {
        sReverseTime[slot] -= TimeStep;
        Control->dy = CTRL_RANGE_MAX;   // reverse
        return;
    }

    // look-ahead point on the racing line, further ahead the faster we go
    REAL lookahead = 384.0f + absSpeed * 0.3f;
    if (lookahead > 1536.0f) lookahead = 1536.0f;

    // AIN_GetForwardNode is dead in this drop (AIN_NearestNode is commented
    // out and always returns NULL). Instead start from CarAI.FinishDistNode —
    // the nearest-node tracker UpdateCarFinishDist maintains every frame —
    // and walk race-forward (= FinishDist decreasing, see UpdateCarFinishDist)
    // until the node is a look-ahead length away from the car.
    long idx = player->CarAI.FinishDistNode;
    if (idx < 0 || idx >= AiNodeNum)
        return;
    AINODE *node = &AiNode[idx];

    for (long iter = 0; iter < 48; iter++) {
        VEC toNode;
        SubVector(&node->Centre, &car->Body->Centre.Pos, &toNode);
        REAL nd2 = toNode.v[X] * toNode.v[X] + toNode.v[Y] * toNode.v[Y]
                 + toNode.v[Z] * toNode.v[Z];
        if (nd2 >= lookahead * lookahead)
            break;

        // race-forward at this node, from the same construction as the game's
        // wrong-way check (UpdateCarFinishDist): norm = (-RVec.z, RVec.y,
        // RVec.x) points AGAINST the race direction, so forward is -norm
        VEC fwdDir;
        fwdDir.v[X] = node->RVec.v[Z] * sDirSign[slot];
        fwdDir.v[Y] = -node->RVec.v[Y] * sDirSign[slot];
        fwdDir.v[Z] = -node->RVec.v[X] * sDirSign[slot];

        // neighbor that advances furthest along the race direction
        AINODE *cand[4] = { node->Next[0], node->Next[1], node->Prev[0], node->Prev[1] };
        AINODE *fwd = NULL;
        REAL best = 0.0f;
        for (long c = 0; c < 4; c++) {
            if (!cand[c]) continue;
            VEC step;
            SubVector(&cand[c]->Centre, &node->Centre, &step);
            REAL along = VecDotVec(&step, &fwdDir);
            if (along > best) { best = along; fwd = cand[c]; }
        }
        if (!fwd)
            break;
        node = fwd;
    }
    player->CarAI.CurNode = node;

    // target = the node's racing-line point (lerp across the track edges)
    VEC target, d;
    REAL t = node->RacingLine;
    target.v[X] = node->Node[0].Pos.v[X] + (node->Node[1].Pos.v[X] - node->Node[0].Pos.v[X]) * t;
    target.v[Y] = node->Node[0].Pos.v[Y] + (node->Node[1].Pos.v[Y] - node->Node[0].Pos.v[Y]) * t;
    target.v[Z] = node->Node[0].Pos.v[Z] + (node->Node[1].Pos.v[Z] - node->Node[0].Pos.v[Z]) * t;

    // target bearing in car space
    SubVector(&target, &car->Body->Centre.Pos, &d);
    REAL right = VecDotVec(&d, &car->Body->Centre.WMatrix.mv[R]);
    REAL fwd   = VecDotVec(&d, &car->Body->Centre.WMatrix.mv[L]);
    REAL angle = (REAL)atan2f(right, fwd);   // 0 = dead ahead, + = to the right
    REAL absAngle = (angle < 0) ? -angle : angle;

    // target essentially behind us: three-point turn — reverse with mirrored
    // steering so the nose swings toward the target (skip stuck logic; this
    // IS the recovery)
    if (absAngle > 2.2f) {
        Control->dx = (signed char)((angle > 0) ? -CTRL_RANGE_MAX : CTRL_RANGE_MAX);
        Control->dy = CTRL_RANGE_MAX;   // reverse
        return;
    }

    // steering: full lock when ~35 degrees off the line
    REAL steer = angle * (127.0f / 0.6f);
    if (steer > 127.0f) steer = 127.0f;
    if (steer < -127.0f) steer = -127.0f;
    Control->dx = (signed char)(long)steer;

    // throttle: flat out on the straights, ease through corners,
    // creep at full lock when the target is far off the nose
    if (absAngle > 1.0f)
        Control->dy = -(CTRL_RANGE_MAX / 4);
    else if (absAngle > 0.5f)
        Control->dy = -(CTRL_RANGE_MAX / 2);
    else if (absAngle > 0.25f)
        Control->dy = -(CTRL_RANGE_MAX * 3 / 4);
    else
        Control->dy = -CTRL_RANGE_MAX;

    // self-calibration: if the game's own wrong-way detector says we're
    // persistently racing backwards while driving forward at speed, our
    // direction sense is inverted for this track — flip it
    if (player->CarAI.WrongWay && fwdSpeed > 200.0f) {
        sWrongWayTime[slot] += TimeStep;
        if (sWrongWayTime[slot] > 1.5f) {
            sWrongWayTime[slot] = 0.0f;
            sDirSign[slot] = -sDirSign[slot];
            __android_log_print(4 /*INFO*/, "revolt-ai",
                                "cpu slot %ld: wrong way — flipping direction sense to %.0f",
                                slot, (double)sDirSign[slot]);
        }
    } else {
        sWrongWayTime[slot] = 0.0f;
    }

    // heartbeat to logcat (~every 2s at 60fps) so remote debugging can see us
    static long sBeat = 0;
    if ((sBeat++ % 120) == 0) {
        __android_log_print(4 /*INFO*/, "revolt-ai",
                            "cpu slot %ld: node %ld dx %d dy %d speed %.0f angle %.2f",
                            slot, (long)(node - AiNode), (int)Control->dx,
                            (int)Control->dy, (double)fwdSpeed, (double)angle);
    }

    // stuck: throttle on but barely moving -> back out for a second
    if (absSpeed < 48.0f && Control->dy < 0) {
        sStuckTime[slot] += TimeStep;
        if (sStuckTime[slot] > 2.0f) {
            sStuckTime[slot] = 0.0f;
            sReverseTime[slot] = 1.0f;
        }
    } else {
        sStuckTime[slot] = 0.0f;
    }
}
