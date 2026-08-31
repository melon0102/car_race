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
#include "weapon.h"

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

    // per-driver personality (retail keeps a per-car CAI_SKILLS table — here
    // a deterministic profile per grid slot): pace, braking caution, steering
    // aggression and how far ahead they read the track
    static const struct {
        REAL speedScale, brakeMargin, steerGain, lookScale;
    } kDrivers[8] = {
        { 1.00f, 1.15f, 1.00f, 1.00f },   // balanced
        { 0.97f, 1.22f, 0.92f, 1.10f },   // careful
        { 1.03f, 1.10f, 1.08f, 0.90f },   // aggressive
        { 0.94f, 1.30f, 0.85f, 1.20f },   // cautious tourer
        { 1.01f, 1.12f, 1.05f, 0.95f },   // quick
        { 0.96f, 1.25f, 0.90f, 1.05f },   // steady
        { 1.05f, 1.08f, 1.12f, 0.85f },   // hothead
        { 0.92f, 1.35f, 0.80f, 1.15f },   // backmarker
    };
    const REAL drvSpeedScale = kDrivers[slot & 7].speedScale;
    const REAL drvBrakeMargin = kDrivers[slot & 7].brakeMargin;
    const REAL drvSteerGain = kDrivers[slot & 7].steerGain;
    const REAL drvLookScale = kDrivers[slot & 7].lookScale;

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

    // backing out of a wall / stuck spot — steer alternately per car so a
    // clump of stuck cars disperses instead of shuffling in place
    if (sReverseTime[slot] > 0.0f) {
        sReverseTime[slot] -= TimeStep;
        Control->dy = CTRL_RANGE_MAX;   // reverse
        Control->dx = (signed char)((slot & 1) ? 90 : -90);
        return;
    }

    // look-ahead point on the racing line (short enough to actually follow
    // the line through corners, like the retail AI)
    REAL lookahead = (256.0f + absSpeed * 0.25f) * drvLookScale;
    if (lookahead > 1200.0f) lookahead = 1200.0f;
    // corner scan window: how far ahead we look for authored slow zones —
    // grows with speed so braking starts early enough (retail
    // CAI_CalcBrakingParameters: brake point moves up for slower corners)
    REAL brakeWindow = 400.0f + absSpeed * 0.8f;

    // AIN_GetForwardNode is dead in this drop (AIN_NearestNode is commented
    // out and always returns NULL). Instead start from CarAI.FinishDistNode —
    // the nearest-node tracker UpdateCarFinishDist maintains every frame —
    // and walk race-forward, collecting the slowest authored racing-line
    // speed inside the braking window and the steering node at look-ahead.
    long idx = player->CarAI.FinishDistNode;
    if (idx < 0 || idx >= AiNodeNum)
        return;
    AINODE *node = &AiNode[idx];
    AINODE *steerNode = NULL;
    REAL walked = 0.0f;
    REAL minSpeedMph = 1000.0f;   // authored node speeds are MPH (0 = unauthored)

    for (long iter = 0; iter < 64; iter++) {
        if (node->RacingLineSpeed > 0 && walked <= brakeWindow) {
            REAL s = (REAL)node->RacingLineSpeed;
            if (s < minSpeedMph) minSpeedMph = s;
        }

        VEC toNode;
        SubVector(&node->Centre, &car->Body->Centre.Pos, &toNode);
        REAL nd2 = toNode.v[X] * toNode.v[X] + toNode.v[Y] * toNode.v[Y]
                 + toNode.v[Z] * toNode.v[Z];
        if (!steerNode && nd2 >= lookahead * lookahead)
            steerNode = node;
        if (steerNode && walked > brakeWindow)
            break;

        // race-forward at this node, from the same construction as the game's
        // wrong-way check (UpdateCarFinishDist): norm = (-RVec.z, RVec.y,
        // RVec.x) points AGAINST the race direction, so forward is -norm
        VEC fwdDir;
        fwdDir.v[X] = node->RVec.v[Z] * sDirSign[slot];
        fwdDir.v[Y] = -node->RVec.v[Y] * sDirSign[slot];
        fwdDir.v[Z] = -node->RVec.v[X] * sDirSign[slot];

        // neighbor that advances furthest along the race direction; at forks
        // (both Next links valid) each driver prefers a different branch so
        // the field doesn't all take the same route
        AINODE *cand[4] = { node->Next[0], node->Next[1], node->Prev[0], node->Prev[1] };
        AINODE *fwd = NULL;
        REAL best = 0.0f;
        for (long c = 0; c < 4; c++) {
            if (!cand[c]) continue;
            VEC step;
            SubVector(&cand[c]->Centre, &node->Centre, &step);
            REAL along = VecDotVec(&step, &fwdDir);
            if (c < 2 && (long)(c) == (slot & 1)) along *= 1.2f;   // route preference
            if (along > best) { best = along; fwd = cand[c]; }
        }
        if (!fwd)
            break;
        walked += best;   // best = forward projection of the step ~ its length
        node = fwd;
    }
    if (!steerNode) steerNode = node;
    node = steerNode;
    player->CarAI.CurNode = node;

    // target = the node's racing-line point (lerp across the track edges),
    // plus a per-car lateral offset so the field spreads over the road
    // instead of queueing single-file on the exact line
    VEC target, d;
    REAL t = node->RacingLine;
    REAL lineOff = (((REAL)((slot * 53) % 100)) / 100.0f - 0.5f) * 90.0f;
    target.v[X] = node->Node[0].Pos.v[X] + (node->Node[1].Pos.v[X] - node->Node[0].Pos.v[X]) * t
                + node->RVec.v[X] * lineOff;
    target.v[Y] = node->Node[0].Pos.v[Y] + (node->Node[1].Pos.v[Y] - node->Node[0].Pos.v[Y]) * t
                + node->RVec.v[Y] * lineOff;
    target.v[Z] = node->Node[0].Pos.v[Z] + (node->Node[1].Pos.v[Z] - node->Node[0].Pos.v[Z]) * t
                + node->RVec.v[Z] * lineOff;

    // target bearing in car space
    SubVector(&target, &car->Body->Centre.Pos, &d);
    REAL right = VecDotVec(&d, &car->Body->Centre.WMatrix.mv[R]);
    REAL fwd   = VecDotVec(&d, &car->Body->Centre.WMatrix.mv[L]);
    REAL angle = (REAL)atan2f(right, fwd);   // 0 = dead ahead, + = to the right
    REAL absAngle = (angle < 0) ? -angle : angle;

    // car avoidance: steer around cars ahead of us and lift when right on
    // someone's bumper (without this the whole field wedges into one pile)
    {
        PLAYER *other;
        REAL avoid = 0.0f;
        int blocked = 0;

        for (other = PLR_PlayerHead; other; other = other->next) {
            if (other == player || !other->car.Body) continue;
            VEC od;
            SubVector(&other->car.Body->Centre.Pos, &car->Body->Centre.Pos, &od);
            REAL ofwd = VecDotVec(&od, &car->Body->Centre.WMatrix.mv[L]);
            if (ofwd < 0.0f || ofwd > 320.0f) continue;
            REAL oside = VecDotVec(&od, &car->Body->Centre.WMatrix.mv[R]);
            if (oside > 100.0f || oside < -100.0f) continue;
            // push away from the blocker, harder the closer it is
            avoid += ((oside >= 0.0f) ? -1.0f : 1.0f) * (1.0f - ofwd / 320.0f);
            if (ofwd < 150.0f) blocked = 1;
        }

        if (avoid > 1.0f) avoid = 1.0f;
        if (avoid < -1.0f) avoid = -1.0f;
        angle += avoid * 0.55f;
        absAngle = (angle < 0) ? -angle : angle;

        // bumper-to-bumper at speed difference -> lift so we slot in behind
        if (blocked && fwdSpeed > 600.0f)
            fwdSpeed += 400.0f;   // pretend we're faster: throttle logic eases off
    }

    // target essentially behind us: three-point turn — reverse with mirrored
    // steering so the nose swings toward the target (skip stuck logic; this
    // IS the recovery)
    if (absAngle > 2.2f) {
        Control->dx = (signed char)((angle > 0) ? -CTRL_RANGE_MAX : CTRL_RANGE_MAX);
        Control->dy = CTRL_RANGE_MAX;   // reverse
        return;
    }

    // steering: retail-style sqrt response — strong correction for small
    // errors, saturating at full lock around ~35 degrees off the line
    REAL steerMag = (REAL)sqrtf(absAngle / 0.6f) * 127.0f * drvSteerGain;
    if (steerMag > 127.0f) steerMag = 127.0f;
    Control->dx = (signed char)(long)((angle > 0) ? steerMag : -steerMag);

    // throttle from the AUTHORED corner speeds (retail behavior): the slowest
    // racing-line speed inside the braking window sets the target velocity —
    // flat out on straights, braking in time for slow corners.
    // Node speeds are in MPH (convert!), not fractions — treating them as
    // fractions had the whole field lurching around walking pace.
    REAL topSpeed = car->TopSpeed;
    if (topSpeed < 500.0f) topSpeed = 3000.0f;   // guard against odd car data
    REAL targetVel = (minSpeedMph >= 999.0f)
                   ? topSpeed
                   : minSpeedMph * MPH2OGU_SPEED;

    // per-driver pace, plus retail-style catch-up: cars behind the human
    // push a little harder, cars far ahead ease off — keeps the field racing
    targetVel *= drvSpeedScale;
    if (PLR_LocalPlayer && PLR_LocalPlayer != player && AiNodeTotalDist > 1.0f) {
        REAL gap = player->CarAI.FinishDist - PLR_LocalPlayer->CarAI.FinishDist;
        if (gap > AiNodeTotalDist * 0.5f) gap -= AiNodeTotalDist;
        else if (gap < -AiNodeTotalDist * 0.5f) gap += AiNodeTotalDist;
        // gap > 0 = behind the player (finish dist runs down toward the line)
        REAL boost = gap * 0.00003f;
        if (boost > 0.08f) boost = 0.08f;
        if (boost < -0.08f) boost = -0.08f;
        targetVel *= (1.0f + boost);
    }

    if (targetVel > topSpeed * 1.02f) targetVel = topSpeed * 1.02f;
    if (targetVel < 15.0f * MPH2OGU_SPEED) targetVel = 15.0f * MPH2OGU_SPEED;  // floor ~15mph

    if (absAngle > 1.0f)
        Control->dy = -(CTRL_RANGE_MAX / 4);         // way off line: creep and turn
    else if (fwdSpeed > targetVel * drvBrakeMargin && fwdSpeed > 800.0f)
        Control->dy = CTRL_RANGE_MAX;                // brake for the corner (never at a crawl)
    else if (fwdSpeed > targetVel)
        Control->dy = 0;                             // coast down to corner speed
    else
        Control->dy = -CTRL_RANGE_MAX;               // flat out

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

    // use pickups: rockets wait for a lock, everything else fires when held.
    // FIRE is pulsed (16 frames on / 16 off) so the edge-triggered input
    // registers repeatedly for multi-shot packs.
    static long sFirePulse = 0;
    sFirePulse++;
    if (player->PickupNum > 0) {
        int needsLock = (player->PickupType == PICKUP_FIREWORK ||
                         player->PickupType == PICKUP_FIREWORKPACK);
        if ((!needsLock || player->PickupTarget) && ((sFirePulse >> 4) & 1))
            Control->digital |= CTRL_FIRE;
    }

    // heartbeat to logcat (~every 2s at 60fps) so remote debugging can see us
    static long sBeat = 0;
    if ((sBeat++ % 120) == 0) {
        __android_log_print(4 /*INFO*/, "revolt-ai",
                            "cpu slot %ld: node %ld dx %d dy %d speed %.0f/%.0f mph %.0f angle %.2f",
                            slot, (long)(node - AiNode), (int)Control->dx,
                            (int)Control->dy, (double)fwdSpeed, (double)targetVel,
                            (double)minSpeedMph, (double)angle);
    }

    // stuck: throttle on but barely moving -> back out (trigger fast, and
    // stagger the reverse duration per car so a pile-up untangles instead
    // of the whole clump reversing and advancing in lockstep)
    if (absSpeed < 48.0f && Control->dy < 0) {
        sStuckTime[slot] += TimeStep;
        if (sStuckTime[slot] > 1.2f) {
            sStuckTime[slot] = 0.0f;
            sReverseTime[slot] = 0.7f + (REAL)((slot * 29) % 70) / 100.0f;
        }
    } else {
        sStuckTime[slot] = 0.0f;
    }
}
