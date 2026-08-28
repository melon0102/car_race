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

    REAL nodeDist;
    AINODE *node = AIN_GetForwardNode(player, lookahead, &nodeDist);
    if (!node)
        node = AIN_GetForwardNode(player, 128.0f, &nodeDist);
    if (!node)
        return;
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

    // steering: full lock when ~35 degrees off the line
    REAL steer = angle * (127.0f / 0.6f);
    if (steer > 127.0f) steer = 127.0f;
    if (steer < -127.0f) steer = -127.0f;
    Control->dx = (signed char)(long)steer;

    // throttle: flat out on the straights, ease through corners,
    // brake when the target is way off the nose
    if (absAngle > 1.4f)
        Control->dy = CTRL_RANGE_MAX / 2;             // brake
    else if (absAngle > 0.7f)
        Control->dy = -(CTRL_RANGE_MAX / 3);
    else if (absAngle > 0.35f)
        Control->dy = -(CTRL_RANGE_MAX * 2 / 3);
    else
        Control->dy = -CTRL_RANGE_MAX;

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
