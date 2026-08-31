// cpu_driver.cpp — CPU car driver for the Android port.
//
// The 1999 PC source drop predates the finished CPU racing AI: ai_car.cpp is
// a 5KB stub (CAI_ResetCar is empty, CPU players get NO control handler and a
// NULL conhandler — the car spawns with physics but nothing drives it). The
// finished 127KB AI only exists in the newer Xbox tree, which is too far
// diverged to drop in wholesale.
//
// This is a port of that retail AI's racing model onto the PC structures.
// What the retail code (rvsource/Xbox/Src/ai_car.cpp + ainode.cpp) actually
// does, verified by reading it:
//
//   * CAI_State_Race sets controls.dy = -CTRL_RANGE_MAX unconditionally —
//     FULL THROTTLE, ALWAYS. Its brake-zone branch is disabled (`if (0)`),
//     and CAI_CalcBrakingParameters reads AINODE_LINKINFO::speed, a field
//     that is never written anywhere in the retail tree. The authored
//     RacingLineSpeed/CentreSpeed values are editor-only: the AI never reads
//     them. Cornering comes from steering + the car physics, not braking.
//
//   * AIN_FindNodeAhead walks the node links by distance and returns an
//     INTERPOLATED point on the racing line (lerp across each node's edge
//     pair by its RacingLine value, then lerp between the two nodes), giving
//     a smooth continuous target rather than a node centre.
//
//   * CAI_GetAngleToRacingLine flattens both the target vector and the car's
//     forward vector to the XZ plane, returning cos(angle) plus which side of
//     the target line the car points.
//
//   * CAI_ProcessCarSteering: sqrt(1 - cos) * (90 / fullLockAngle) * 0.5,
//     clamped to 1, scaled to full stick and signed by that side value.
//
//   * Look-ahead: 600 units + 0.1 * current speed while racing.
//
// Per-driver variety follows retail's approach of biasing the racing-line
// parameter per car (retail randomises it per car in the bronze cup and
// blends toward an OvertakingLine, a field our 1999 nodes do not carry).
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
#include "weapon.h"
#include "Wheel.h"

// per-player state, indexed by player slot
static REAL sStuckTime[MAX_NUM_PLAYERS];
static REAL sReverseTime[MAX_NUM_PLAYERS];
static REAL sAirTime[MAX_NUM_PLAYERS];
static REAL sWrongWayTime[MAX_NUM_PLAYERS];
static REAL sDirSign[MAX_NUM_PLAYERS];   // 0 = uninitialised, +1/-1 = race-direction sense

// Race-forward successor of a node. Retail can just take Next[route] because
// its data guarantees link orientation; the 1999 node graph does not, so pick
// the neighbour that advances furthest along the node's race direction. That
// direction is built the same way the game's own wrong-way check builds it
// (UpdateCarFinishDist): norm = (-RVec.z, RVec.y, RVec.x) points AGAINST the
// race direction, so forward is its negation.
static AINODE *RaceForwardNode(AINODE *node, REAL dirSign, long slot)
{
    VEC fwdDir;
    fwdDir.v[X] =  node->RVec.v[Z] * dirSign;
    fwdDir.v[Y] = -node->RVec.v[Y] * dirSign;
    fwdDir.v[Z] = -node->RVec.v[X] * dirSign;

    AINODE *cand[4] = { node->Next[0], node->Next[1], node->Prev[0], node->Prev[1] };
    AINODE *best = NULL;
    REAL bestAlong = 0.0f;

    for (long c = 0; c < 4; c++) {
        if (!cand[c]) continue;
        VEC step;
        SubVector(&cand[c]->Centre, &node->Centre, &step);
        REAL along = VecDotVec(&step, &fwdDir);
        // at a fork, each driver leans toward a different branch
        if (c < 2 && c == (slot & 1)) along *= 1.2f;
        if (along > bestAlong) { bestAlong = along; best = cand[c]; }
    }
    return best;
}

// point on a node's racing line, biased across the track by `bias`
static void RacingLinePoint(AINODE *node, REAL bias, VEC *out)
{
    REAL t = node->RacingLine + bias;
    if (t < 0.05f) t = 0.05f;
    if (t > 0.95f) t = 0.95f;
    out->v[X] = node->Node[0].Pos.v[X] + (node->Node[1].Pos.v[X] - node->Node[0].Pos.v[X]) * t;
    out->v[Y] = node->Node[0].Pos.v[Y] + (node->Node[1].Pos.v[Y] - node->Node[0].Pos.v[Y]) * t;
    out->v[Z] = node->Node[0].Pos.v[Z] + (node->Node[1].Pos.v[Z] - node->Node[0].Pos.v[Z]) * t;
}

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

    // retail biases the racing line per car (frand(0.25) in the bronze cup);
    // deterministic per grid slot here so the field spreads over the road
    const REAL lineBias = ((REAL)((slot * 37) % 100) / 100.0f - 0.5f) * 0.30f;
    const REAL lookScale = 0.9f + (REAL)((slot * 53) % 20) / 100.0f;   // 0.90..1.09

    REAL speedCur = VecLen(&car->Body->Centre.Vel);
    REAL fwdSpeed = VecDotVec(&car->Body->Centre.Vel, &car->Body->Centre.WMatrix.mv[L]);
    REAL absSpeed = (fwdSpeed < 0) ? -fwdSpeed : fwdSpeed;

    // ---------------------------------------------------------------- recovery

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

    // backing out of a wall / stuck spot — staggered per car so a clump
    // disperses instead of shuffling in lockstep
    if (sReverseTime[slot] > 0.0f) {
        sReverseTime[slot] -= TimeStep;
        Control->dy = CTRL_RANGE_MAX;   // reverse
        Control->dx = (signed char)((slot & 1) ? 90 : -90);
        return;
    }

    // ------------------------------------------ target on the racing line
    // (AIN_FindNodeAhead, ported)

    long idx = player->CarAI.FinishDistNode;
    if (idx < 0 || idx >= AiNodeNum)
        return;
    AINODE *node = &AiNode[idx];

    // retail look-ahead: 600 + 0.1 * speed while racing
    REAL lookAhead = (600.0f + speedCur * 0.1f) * lookScale;

    AINODE *succ = RaceForwardNode(node, sDirSign[slot], slot);
    if (!succ)
        return;

    // distance already travelled along the current link (retail: distAlongNode)
    VEC linkVec, toCar;
    SubVector(&succ->Centre, &node->Centre, &linkVec);
    REAL linkDist = VecLen(&linkVec);
    if (linkDist < 1.0f)
        return;
    SubVector(&car->Body->Centre.Pos, &node->Centre, &toCar);
    REAL distAlong = VecDotVec(&toCar, &linkVec) / linkDist;
    if (distAlong < 0.0f) distAlong = 0.0f;
    if (distAlong > linkDist) distAlong = linkDist;

    REAL dist = lookAhead + distAlong;

    for (long iter = 0; iter < 64; iter++) {
        dist -= linkDist;
        if (dist < 0.0f)
            break;
        AINODE *nextSucc = RaceForwardNode(succ, sDirSign[slot], slot);
        if (!nextSucc)
            break;
        node = succ;
        succ = nextSucc;
        SubVector(&succ->Centre, &node->Centre, &linkVec);
        linkDist = VecLen(&linkVec);
        if (linkDist < 1.0f)
            break;
    }

    // interpolate the racing line across the link we landed in
    REAL t = (dist + linkDist) / linkDist;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    VEC c0, c1, destPos;
    RacingLinePoint(node, lineBias, &c0);
    RacingLinePoint(succ, lineBias, &c1);
    destPos.v[X] = c0.v[X] + (c1.v[X] - c0.v[X]) * t;
    destPos.v[Y] = c0.v[Y] + (c1.v[Y] - c0.v[Y]) * t;
    destPos.v[Z] = c0.v[Z] + (c1.v[Z] - c0.v[Z]) * t;
    player->CarAI.CurNode = node;

    // ------------------------------------------------------- angle to line
    // (CAI_GetAngleToRacingLine, ported — everything flattened to XZ)

    VEC fVec, rVec, carFwd;

    fVec.v[X] = destPos.v[X] - car->Body->Centre.Pos.v[X];
    fVec.v[Y] = 0.0f;
    fVec.v[Z] = destPos.v[Z] - car->Body->Centre.Pos.v[Z];
    if (VecLen(&fVec) < 0.001f)
        return;
    NormalizeVector(&fVec);

    rVec.v[X] =  fVec.v[Z];
    rVec.v[Y] =  0.0f;
    rVec.v[Z] = -fVec.v[X];

    carFwd.v[X] = car->Body->Centre.WMatrix.m[LX];
    carFwd.v[Y] = 0.0f;
    carFwd.v[Z] = car->Body->Centre.WMatrix.m[LZ];
    if (VecLen(&carFwd) < 0.001f)
        return;
    NormalizeVector(&carFwd);

    REAL side = VecDotVec(&rVec, &carFwd);
    REAL cosAngle = VecDotVec(&fVec, &carFwd);
    REAL absCos = (cosAngle < 0) ? -cosAngle : cosAngle;

    // ------------------------------------------------------------ avoidance
    // retail overtakes by blending toward an OvertakingLine (absent from the
    // 1999 node data), so nudge the aim off-line around a blocker instead
    REAL avoid = 0.0f;
    int blocked = 0;
    {
        PLAYER *other;
        for (other = PLR_PlayerHead; other; other = other->next) {
            if (other == player || !other->car.Body) continue;
            VEC od;
            SubVector(&other->car.Body->Centre.Pos, &car->Body->Centre.Pos, &od);
            REAL ofwd = VecDotVec(&od, &car->Body->Centre.WMatrix.mv[L]);
            if (ofwd < 0.0f || ofwd > 300.0f) continue;
            REAL oside = VecDotVec(&od, &car->Body->Centre.WMatrix.mv[R]);
            if (oside > 90.0f || oside < -90.0f) continue;
            avoid += ((oside >= 0.0f) ? -1.0f : 1.0f) * (1.0f - ofwd / 300.0f);
            if (ofwd < 140.0f) blocked = 1;
        }
        if (avoid >  1.0f) avoid =  1.0f;
        if (avoid < -1.0f) avoid = -1.0f;
    }

    // ------------------------------------------------------------- steering
    // (CAI_ProcessCarSteering, ported)
    //   angle = sqrt(1 - |cos|) * (90 / fullLockAngle) * 0.5, clamped to 1

    REAL fullLockAngle = fabsf(car->Wheel[0].SteerRatio * 360.0f);
    REAL steerConvert = (fullLockAngle > 1.0f) ? (90.0f / fullLockAngle) : 2.5f;

    REAL angle = 1.0f - absCos;
    if (angle < 0.0f) angle = 0.0f;
    angle = (REAL)sqrtf(angle);
    angle *= steerConvert;
    angle *= 0.5f;
    if (angle > 1.0f) angle = 1.0f;

    REAL steer = (REAL)CTRL_RANGE_MAX * angle;
    if (side >= 0.0f)
        steer = -steer;

    // blend in the avoidance nudge
    steer += avoid * (REAL)CTRL_RANGE_MAX * 0.45f;
    if (steer >  (REAL)CTRL_RANGE_MAX) steer =  (REAL)CTRL_RANGE_MAX;
    if (steer < -(REAL)CTRL_RANGE_MAX) steer = -(REAL)CTRL_RANGE_MAX;
    Control->dx = (signed char)(long)steer;

    // ------------------------------------------------------------- throttle
    // retail: FULL THROTTLE, always (CAI_State_Race). The only lifts here are
    // the ones retail gets from its recovery states: pointing the wrong way,
    // or sitting on someone's bumper.

    if (cosAngle < -0.30f) {
        // target is behind us — three-point turn (retail: reverse-correct states)
        Control->dx = (signed char)((side >= 0.0f) ? CTRL_RANGE_MAX : -CTRL_RANGE_MAX);
        Control->dy = CTRL_RANGE_MAX;
    } else if (blocked && absSpeed > 500.0f) {
        Control->dy = 0;                 // ease off behind a car
    } else {
        Control->dy = -CTRL_RANGE_MAX;   // flat out
    }

    // -------------------------------------------------------------- weapons
    // rockets wait for a lock, everything else fires when held; FIRE is pulsed
    // so the edge-triggered input registers repeatedly for multi-shot packs
    static long sFirePulse = 0;
    sFirePulse++;
    if (player->PickupNum > 0) {
        int needsLock = (player->PickupType == PICKUP_FIREWORK ||
                         player->PickupType == PICKUP_FIREWORKPACK);
        if ((!needsLock || player->PickupTarget) && ((sFirePulse >> 4) & 1))
            Control->digital |= CTRL_FIRE;
    }

    // ------------------------------------------- direction self-calibration
    // if the game's own wrong-way detector says we're persistently racing
    // backwards at speed, our race-direction sense is inverted — flip it
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

    // stuck: throttle on but barely moving -> back out
    if (absSpeed < 48.0f && Control->dy < 0) {
        sStuckTime[slot] += TimeStep;
        if (sStuckTime[slot] > 1.2f) {
            sStuckTime[slot] = 0.0f;
            sReverseTime[slot] = 0.7f + (REAL)((slot * 29) % 70) / 100.0f;
        }
    } else {
        sStuckTime[slot] = 0.0f;
    }

    // heartbeat to logcat (~every 2s at 60fps)
    static long sBeat = 0;
    if ((sBeat++ % 120) == 0) {
        __android_log_print(4 /*INFO*/, "revolt-ai",
                            "cpu slot %ld: node %ld dx %d dy %d speed %.0f cos %.2f side %.2f",
                            slot, (long)(node - AiNode), (int)Control->dx,
                            (int)Control->dy, (double)speedCur, (double)cosAngle,
                            (double)side);
    }
}
