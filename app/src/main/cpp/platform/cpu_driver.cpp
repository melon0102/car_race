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
#include "Units.h"
#include "posnode.h"
#include "main.h"

// ---------------------------------------------------------------- retail data
// AI node priority types (rvsource/Xbox/Src/ainode.h enum order — the retail
// .fan files carry these in each node's Priority byte; the authored slow-down
// zones are how the retail AI brakes for corners)

#define AIN_TYPE_SLOWDOWN_25        4
#define AIN_TYPE_TITLESCR_SLOWDOWN  7
#define AIN_TYPE_OFFTHROTTLE        13
#define AIN_TYPE_OFFTHROTTLEPETROL  14
#define AIN_TYPE_SLOWDOWN_15        16
#define AIN_TYPE_SLOWDOWN_20        17
#define AIN_TYPE_SLOWDOWN_30        18

// retail MEDIUM-difficulty catch-up (ai_car.cpp gCatchUp[DIFFICULTY_MEDIUM]):
// cars far behind 1st get up to +4mph of top speed, cars far ahead lose up
// to 2mph — the subtle rubber band that keeps the pack together

#define CATCHUP_LEN_SPEEDUP   (200.0f * 7.0f)
#define CATCHUP_LEN_SLOWDOWN  (200.0f * 10.0f)
static const REAL kCatchUpSpeedUp[4]  = { MPH2OGU_SPEED * 1, MPH2OGU_SPEED * 2, MPH2OGU_SPEED * 3, MPH2OGU_SPEED * 4 };
static const REAL kCatchUpSlowDown[3] = { -MPH2OGU_SPEED * 0, -MPH2OGU_SPEED * 1, -MPH2OGU_SPEED * 2 };

// retail under/oversteer response (ai_car.cpp gCarUnderOverSteerTable's
// generic row — the per-car table was disabled in retail too): countersteer
// into a slide and lift the throttle, the single most human-looking habit

#define UOS_UNDER_THRESHOLD  150.0f
#define UOS_UNDER_RANGE      1500.0f
#define UOS_UNDER_FRONT      450.0f
#define UOS_UNDER_REAR       335.0f
#define UOS_UNDER_MAX        0.95f
#define UOS_OVER_THRESHOLD   300.0f
#define UOS_OVER_RANGE       1000.0f
#define UOS_OVER_MAX         1.0f
#define UOS_OVER_ACC_THRESH  50.0f
#define UOS_OVER_ACC_RANGE   500.0f

#define OVERTAKE_MIN_TIME    4.0f     // retail OVERTAKE_MIN_TIME

// per-player state, indexed by player slot
static REAL sStuckTime[MAX_NUM_PLAYERS];
static REAL sReverseTime[MAX_NUM_PLAYERS];
static REAL sAirTime[MAX_NUM_PLAYERS];
static REAL sWrongWayTime[MAX_NUM_PLAYERS];
static REAL sDirSign[MAX_NUM_PLAYERS];   // 0 = uninitialised, +1/-1 = race-direction sense
static char sOvertake[MAX_NUM_PLAYERS];      // retail bOvertake — on the overtaking line?
static REAL sOvertakeTime[MAX_NUM_PLAYERS];  // retail timeOvertake
static REAL sCurLine[MAX_NUM_PLAYERS];       // retail curRacingLine — blended line bias 0..1
static long sAiNodeIdx[MAX_NUM_PLAYERS];     // this driver's own current AINODE (CarAI.FinishDistNode
static char sAiNodeInit[MAX_NUM_PLAYERS];    // belongs to the posnode chain — a different graph)

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

// racing line with the retail overtaking-line blend: the overtaking line is
// the authored line mirrored to the other side of the track centre
// (rvsource/Xbox/Src/ainode.cpp computes it exactly this way when the node
// data doesn't carry one), and curRacingLine slides between them at retail's
// 0.1/s so the car drifts across the road rather than snapping
static void LinePoint(AINODE *node, REAL bias, REAL overtakeBlend, VEC *out)
{
    REAL rl = node->RacingLine;
    REAL ol = (rl <= 0.5f) ? rl + (1.0f - rl) * 0.5f : rl * 0.5f;
    REAL t = rl + (ol - rl) * overtakeBlend + bias;
    if (t < 0.05f) t = 0.05f;
    if (t > 0.95f) t = 0.95f;
    out->v[X] = node->Node[0].Pos.v[X] + (node->Node[1].Pos.v[X] - node->Node[0].Pos.v[X]) * t;
    out->v[Y] = node->Node[0].Pos.v[Y] + (node->Node[1].Pos.v[Y] - node->Node[0].Pos.v[Y]) * t;
    out->v[Z] = node->Node[0].Pos.v[Z] + (node->Node[1].Pos.v[Z] - node->Node[0].Pos.v[Z]) * t;
}

// posnode race progress in track units (bigger = further round the race)
static REAL RaceProgress(PLAYER *p)
{
    return ((REAL)p->car.Laps - p->CarAI.FinishDistPanel - (REAL)p->CarAI.BackTracking) * PosTotalDist;
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

    // flipped / airborne too long -> right the car (idigital edge fires once);
    // while genuinely airborne, retail's CAI_S_IN_THE_AIR holds the wheels
    // straight at full throttle instead of steering at nothing
    if (car->NWheelFloorContacts == 0) {
        sAirTime[slot] += TimeStep;
        if (sAirTime[slot] > 2.5f) {
            sAirTime[slot] = 0.0f;
            Control->digital = CTRL_RESET;
            return;
        }
        if (car->NWheelsInContact < 3) {
            Control->dx = 0;
            Control->dy = -CTRL_RANGE_MAX;
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

    // ------------------------------------------------- opponents + overtaking
    // (retail CAI_State_Race: within 500 race-units of the car in front, hop
    // onto the opposite line to theirs and hold it for OVERTAKE_MIN_TIME; the
    // close-range nudge below handles the actual wheel-to-wheel moment)

    REAL avoid = 0.0f;
    int blocked = 0;
    {
        PLAYER *other, *front = NULL;
        REAL myProg = RaceProgress(player), frontGap = 0.0f;

        for (other = PLR_PlayerHead; other; other = other->next) {
            if (other == player || !other->car.Body) continue;
            if (other->type != PLAYER_CPU && other->type != PLAYER_LOCAL) continue;

            VEC od;
            SubVector(&other->car.Body->Centre.Pos, &car->Body->Centre.Pos, &od);
            REAL ofwd = VecDotVec(&od, &car->Body->Centre.WMatrix.mv[L]);
            if (ofwd >= 0.0f && ofwd <= 300.0f) {
                REAL oside = VecDotVec(&od, &car->Body->Centre.WMatrix.mv[R]);
                if (oside <= 90.0f && oside >= -90.0f) {
                    avoid += ((oside >= 0.0f) ? -1.0f : 1.0f) * (1.0f - ofwd / 300.0f);
                    if (ofwd < 140.0f) blocked = 1;
                }
            }

            REAL gap = RaceProgress(other) - myProg;
            if (gap > 0.0f && (!front || gap < frontGap)) {
                front = other;
                frontGap = gap;
            }
        }
        if (avoid >  1.0f) avoid =  1.0f;
        if (avoid < -1.0f) avoid = -1.0f;

        if (front && frontGap < 500.0f) {
            long fslot = (long)(front - Players);
            sOvertake[slot] = (fslot >= 0 && fslot < MAX_NUM_PLAYERS) ? !sOvertake[fslot] : 1;
            sOvertakeTime[slot] = OVERTAKE_MIN_TIME;
        } else if (sOvertakeTime[slot] > 0.0f) {
            sOvertakeTime[slot] -= TimeStep;
        } else {
            sOvertake[slot] = 0;
        }

        // slide between the lines at retail's 0.1/s
        REAL dstLine = sOvertake[slot] ? 1.0f : 0.0f;
        if (sCurLine[slot] < dstLine) {
            sCurLine[slot] += TimeStep * 0.1f;
            if (sCurLine[slot] > dstLine) sCurLine[slot] = dstLine;
        } else if (sCurLine[slot] > dstLine) {
            sCurLine[slot] -= TimeStep * 0.1f;
            if (sCurLine[slot] < dstLine) sCurLine[slot] = dstLine;
        }
    }

    // ------------------------------------------ target on the racing line
    // (AIN_FindNodeAhead, ported)

    // track our own current AI node: seed from the nearest node, then walk
    // one link per frame toward whichever neighbour is closer (the same
    // scheme retail's node tracking and our posnode port use)
    if (!sAiNodeInit[slot]) {
        // (AIN_NearestNode is a commented-out stub in the 1999 tree — it
        // always returns NULL. Seed with a direct scan instead.)
        REAL best = 1e30f;
        long bi = -1;
        for (long ni = 0; ni < AiNodeNum; ni++) {
            VEC dv;
            SubVector(&AiNode[ni].Centre, &car->Body->Centre.Pos, &dv);
            REAL d2 = VecDotVec(&dv, &dv);
            if (d2 < best) { best = d2; bi = ni; }
        }
        if (bi < 0)
            return;
        sAiNodeIdx[slot] = bi;
        sAiNodeInit[slot] = 1;
    }
    if (sAiNodeIdx[slot] < 0 || sAiNodeIdx[slot] >= AiNodeNum)
        return;
    AINODE *node = &AiNode[sAiNodeIdx[slot]];

    {
        VEC dv;
        SubVector(&node->Centre, &car->Body->Centre.Pos, &dv);
        REAL best = VecDotVec(&dv, &dv);
        AINODE *bestNode = node;

        for (long li = 0; li < MAX_AINODE_LINKS; li++) {
            AINODE *cand[2] = { node->Next[li], node->Prev[li] };
            for (long ci = 0; ci < 2; ci++) {
                if (!cand[ci]) continue;
                SubVector(&cand[ci]->Centre, &car->Body->Centre.Pos, &dv);
                REAL d2 = VecDotVec(&dv, &dv);
                if (d2 < best) { best = d2; bestNode = cand[ci]; }
            }
        }
        node = bestNode;
        sAiNodeIdx[slot] = (long)(node - AiNode);
    }

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
    LinePoint(node, lineBias, sCurLine[slot], &c0);
    LinePoint(succ, lineBias, sCurLine[slot], &c1);
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

    // ------------------------------------------- authored slow-down zones
    // (retail CAI_ProcessCall: track designers marked braking sections with
    // node Priority types — the retail AI's actual corner braking. The 1999
    // shim ran flat out through all of them.)

    switch (node->Priority) {
        case AIN_TYPE_SLOWDOWN_15:
            if (speedCur > MPH2OGU_SPEED * 15) Control->dy = CTRL_RANGE_MAX;
            break;
        case AIN_TYPE_SLOWDOWN_20:
            if (speedCur > MPH2OGU_SPEED * 20) Control->dy = CTRL_RANGE_MAX;
            break;
        case AIN_TYPE_SLOWDOWN_25:
            if (speedCur > MPH2OGU_SPEED * 25) Control->dy = CTRL_RANGE_MAX;
            break;
        case AIN_TYPE_SLOWDOWN_30:
            if (speedCur > MPH2OGU_SPEED * 30) Control->dy = CTRL_RANGE_MAX;
            break;
        case AIN_TYPE_TITLESCR_SLOWDOWN:
            if (speedCur > car->TopSpeed * 0.25f) Control->dy = CTRL_RANGE_MAX;
            break;
        case AIN_TYPE_OFFTHROTTLEPETROL:
        case AIN_TYPE_OFFTHROTTLE:
            if (speedCur > MPH2OGU_SPEED * 20) Control->dy = 0;
            else Control->dy = (signed char)(-CTRL_RANGE_MAX / 2);
            break;
    }

    // -------------------------------------- under/oversteer correction
    // (retail CAI_ProcessUnderOverSteer: compare the sideways velocity at the
    // front and rear axles — a sliding rear means countersteer toward the
    // slide and lift; a ploughing front means unwind the lock and lift)

    if (car->NWheelFloorContacts >= 3) {
        VEC pos, fVel, rVel;
        REAL frontDot, rearDot, uos;

        VecMinusVec(&car->Wheel[FL].CentrePos, &car->Body->Centre.Pos, &pos);
        BodyPointVel(car->Body, &pos, &fVel);
        frontDot = VecDotVec(&car->Wheel[FL].Axes.mv[R], &fVel);

        VecMinusVec(&car->Wheel[BL].CentrePos, &car->Body->Centre.Pos, &pos);
        BodyPointVel(car->Body, &pos, &rVel);
        rearDot = VecDotVec(&car->Wheel[BL].Axes.mv[R], &rVel);

        REAL absFront = (frontDot < 0) ? -frontDot : frontDot;
        REAL absRear  = (rearDot  < 0) ? -rearDot  : rearDot;

        if ((frontDot >= 0) == (rearDot >= 0)) {
            uos = absRear - absFront - UOS_OVER_THRESHOLD;                  // oversteer?
            if (uos > 0.0f) {
                uos /= UOS_OVER_RANGE;
                if (uos > UOS_OVER_MAX) uos = UOS_OVER_MAX;
            } else {
                uos = absFront + absRear - UOS_UNDER_THRESHOLD;             // understeer?
                uos = (uos > 0.0f) ? -(uos / UOS_UNDER_RANGE) : 0.0f;
                if (uos < -UOS_UNDER_MAX) uos = -UOS_UNDER_MAX;
            }
        } else {
            uos = absFront + absRear - UOS_OVER_THRESHOLD;                  // rear snapping out
            if (uos > 0.0f) {
                uos /= UOS_OVER_RANGE;
                if (uos > UOS_OVER_MAX) uos = UOS_OVER_MAX;
            } else {
                uos = 0.0f;
            }
        }

        if (uos < 0.0f) {
            // understeer: add more lock and lift unless both axles wash out
            long add = (long)(-uos * CTRL_RANGE_MAX);
            long dx = Control->dx + ((Control->dx >= 0) ? add : -add);
            if (dx >  CTRL_RANGE_MAX) dx =  CTRL_RANGE_MAX;
            if (dx < -CTRL_RANGE_MAX) dx = -CTRL_RANGE_MAX;
            Control->dx = (signed char)dx;
            if (absFront <= UOS_UNDER_FRONT || absRear <= UOS_UNDER_REAR)
                Control->dy = (signed char)(-CTRL_RANGE_MAX * (1.0f + uos));
        } else if (uos > 0.0f) {
            // oversteer: countersteer with the slide
            long dx = (long)((car->SteerAngle + ((frontDot < 0.0f) ? -uos : uos)) * CTRL_RANGE_MAX);
            if (dx >  CTRL_RANGE_MAX) dx =  CTRL_RANGE_MAX;
            if (dx < -CTRL_RANGE_MAX) dx = -CTRL_RANGE_MAX;
            Control->dx = (signed char)dx;

            REAL v = (absFront > absRear) ? absFront : absRear;
            v -= UOS_OVER_ACC_THRESH;
            if (v > 0.0f) {
                v /= UOS_OVER_ACC_RANGE;
                if (v > 1.5f) v = 1.5f;
                Control->dy = (signed char)((1.0f - v) * -CTRL_RANGE_MAX);
            }
        }
    }

    // ---------------------------------------------------------- catch-up
    // (retail CAI_CatchUp + control.cpp consumer, MEDIUM difficulty: top
    // speed drifts a few mph either way with the gap to the human's car)

    if (Players[0].car.Body && player != &Players[0]) {
        REAL gap = RaceProgress(&Players[0]) - RaceProgress(player);
        REAL mod = 0.0f;

        if (gap > CATCHUP_LEN_SPEEDUP) {
            long step = (long)(gap / CATCHUP_LEN_SPEEDUP);
            if (step > 3) step = 3;
            mod = kCatchUpSpeedUp[step];
        } else if (gap < -CATCHUP_LEN_SLOWDOWN) {
            long step = (long)(-gap / CATCHUP_LEN_SLOWDOWN);
            if (step > 3) step = 3;
            mod = kCatchUpSlowDown[step - 1 < 0 ? 0 : (step - 1 > 2 ? 2 : step - 1)];
        }

        car->TopSpeed = car->DefaultTopSpeed + mod;
        if (car->DefaultTopSpeed < Players[0].car.DefaultTopSpeed && mod > 0.0f)
            car->TopSpeed = Players[0].car.DefaultTopSpeed + mod;
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

    // (the old "direction sense" auto-flip is gone: it existed to cope with
    // the corrupt node graph the broken .fan count produced. With the loader
    // fixed, the Next[] links are the authored racing direction, as retail
    // assumes — flipping on wrong-way readings only destabilised the cars.)
    (void)sWrongWayTime;
    (void)fwdSpeed;

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

    // -------------------------------------------------- control quantization
    // (retail CAI_QuantizeControls: round the sticks to steps of 8 so the AI's
    // inputs look like a human thumb on a pad, not a servo)

    {
        long q = Control->dx;
        q = (q >= 0) ? ((q + 4) & ~7) : -((-q + 4) & ~7);
        if (q >  CTRL_RANGE_MAX) q =  CTRL_RANGE_MAX;
        if (q < -CTRL_RANGE_MAX) q = -CTRL_RANGE_MAX;
        Control->dx = (signed char)q;

        q = Control->dy;
        q = (q >= 0) ? ((q + 4) & ~7) : -((-q + 4) & ~7);
        if (q >  CTRL_RANGE_MAX) q =  CTRL_RANGE_MAX;
        if (q < -CTRL_RANGE_MAX) q = -CTRL_RANGE_MAX;
        Control->dy = (signed char)q;
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
