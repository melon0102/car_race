// ANDROID_PORT: retail position-node system, ported from
// rvsource/Xbox/Src/posnode.cpp (PC paths only; the local-player compass
// angle section is left out — this port's panel doesn't draw that arrow).
//
// UpdateCarPosNodeDist replaces the 1999 AI-node-based finish-distance code
// for racing cars: it walks the dedicated .pan node chain, computes the
// continuous distance to the finish line, drives the wrong-way flag from the
// averaged forward link direction (the 1999 AI-node normals point sideways
// on parts of the retail tracks — false wrong-way signs), maintains the
// panel distance with wrap detection, and returns TRUE exactly when the car
// completes a counted lap: the grid sits behind the line, so every car
// starts BackTracking and the first crossing only clears that flag
// (retail's PreLap/BackTracking scheme).

#include "revolt.h"
#include "geom.h"
#include "particle.h"
#include "model.h"
#include "aerial.h"
#include "NewColl.h"
#include "Body.h"
#include "car.h"
#include "ctrlread.h"
#include "object.h"
#include "Control.h"
#include "player.h"
#include "ai_car.h"
#include "aizone.h"
#include "timing.h"
#include "posnode.h"

#include <float.h>

// globals

long PosNodeNum, PosStartNode;
POSNODE *PosNode;
REAL PosTotalDist = 1.0f;

////////////////////
// load pos nodes //
////////////////////

void LoadPosNodes(char *file)
{
	long i, j;
	FILE *fp;
	FILE_POSNODE pan;

// open node file

	PosNode = NULL;
	PosNodeNum = 0;
	PosTotalDist = 1.0f;

	fp = fopen(file, "rb");
	if (!fp) return;

// read num, start node, total dist

	fread(&PosNodeNum, sizeof(PosNodeNum), 1, fp);
	if (!PosNodeNum)
	{
		fclose(fp);
		return;
	}

	fread(&PosStartNode, sizeof(PosStartNode), 1, fp);
	fread(&PosTotalDist, sizeof(PosTotalDist), 1, fp);
	if (PosTotalDist < 1.0f) PosTotalDist = 1.0f;

// alloc mem

	PosNode = (POSNODE*)malloc(sizeof(POSNODE) * PosNodeNum);
	if (!PosNode)
	{
		PosNodeNum = 0;
		fclose(fp);
		return;
	}

// loop thru all nodes

	for (i = 0 ; i < PosNodeNum ; i++)
	{
		fread(&pan, sizeof(pan), 1, fp);

		PosNode[i].Pos = pan.Pos;
		PosNode[i].Dist = pan.Dist;

		for (j = 0 ; j < POSNODE_MAX_LINKS ; j++)
		{
			if (pan.Prev[j] != -1 && pan.Prev[j] < PosNodeNum)
				PosNode[i].Prev[j] = PosNode + pan.Prev[j];
			else
				PosNode[i].Prev[j] = NULL;

			if (pan.Next[j] != -1 && pan.Next[j] < PosNodeNum)
				PosNode[i].Next[j] = PosNode + pan.Next[j];
			else
				PosNode[i].Next[j] = NULL;
		}
	}

	fclose(fp);
}

////////////////////
// kill pos nodes //
////////////////////

void FreePosNodes(void)
{
	free(PosNode);
	PosNode = NULL;
	PosNodeNum = 0;
}

////////////////////////////////
// update car finish distance //
////////////////////////////////

long UpdateCarPosNodeDist(struct PlayerStruct *player, unsigned long *timedelta)
{
	POSNODE *currentnode, *nearestnode;
	REAL cdist, ndist, dist;
	VEC vec, norm;
	AIZONE *zone;
	long i, j, flag, outzone;
	REAL lastdist;

// zero time delta

	if (timedelta)
	{
		*timedelta = 0;
	}

// not if no nodes or zones

	if (!PosNodeNum)
		return FALSE;

	if (!AiZones)
		return FALSE;

// less to do if I'm outside my current ai zone

	outzone = FALSE;

	zone = AiZoneHeaders[player->CarAI.ZoneID].Zones;
	flag = FALSE;

	for (i = 0 ; i < AiZoneHeaders[player->CarAI.ZoneID].Count ; i++, zone++)
	{
		for (j = 0 ; j < 3 ; j++)
		{
			dist = PlaneDist(&zone->Plane[j], &player->car.Body->Centre.Pos);
			if (dist < -zone->Size[j] || dist > zone->Size[j])
			{
				break;
			}
		}

		if (j == 3)
			flag = TRUE;
	}

	if (!flag)
	{
		player->CarAI.WrongWay = TRUE;
		outzone = TRUE;
	}

// get dist to current node

	if (player->CarAI.FinishDistNode < 0 || player->CarAI.FinishDistNode >= PosNodeNum)
		player->CarAI.FinishDistNode = PosStartNode;

	currentnode = &PosNode[player->CarAI.FinishDistNode];

	if (!outzone)
	{
		SubVector(&currentnode->Pos, &player->car.Body->Centre.Pos, &vec);
		cdist = vec.v[X] * vec.v[X] + vec.v[Y] * vec.v[Y] + vec.v[Z] * vec.v[Z];

		ndist = FLT_MAX;
		nearestnode = currentnode;

// look for nearer node

		for (i = 0 ; i < POSNODE_MAX_LINKS ; i++)
		{
			if (currentnode->Prev[i])
			{
				SubVector(&currentnode->Prev[i]->Pos, &player->car.Body->Centre.Pos, &vec);
				dist = vec.v[X] * vec.v[X] + vec.v[Y] * vec.v[Y] + vec.v[Z] * vec.v[Z];

				if (dist < ndist)
				{
					ndist = dist;
					nearestnode = currentnode->Prev[i];
				}
			}

			if (currentnode->Next[i])
			{
				SubVector(&currentnode->Next[i]->Pos, &player->car.Body->Centre.Pos, &vec);
				dist = vec.v[X] * vec.v[X] + vec.v[Y] * vec.v[Y] + vec.v[Z] * vec.v[Z];

				if (dist < ndist)
				{
					ndist = dist;
					nearestnode = currentnode->Next[i];
				}
			}
		}

// update current node?

		if (ndist < cdist)
		{
			player->CarAI.FinishDistNode = (long)(nearestnode - PosNode);
			currentnode = nearestnode;
		}

// calc average normal from forward links

		SetVector(&norm, 0, 0, 0);

		for (i = 0 ; i < POSNODE_MAX_LINKS ; i++)
		{
			if (currentnode->Next[i])
			{
				SubVector(&currentnode->Pos, &currentnode->Next[i]->Pos, &vec);
				NormalizeVector(&vec);
				AddVector(&norm, &vec, &norm);
			}
		}

		NormalizeVector(&norm);

// update finish dist

		SubVector(&player->car.Body->Centre.Pos, &currentnode->Pos, &vec);
		player->CarAI.FinishDist = currentnode->Dist + DotProduct(&norm, &vec);

// update 'wrong way' flag

		player->CarAI.WrongWay = (DotProduct(&norm, &player->car.Body->Centre.WMatrix.mv[L]) > 0.6f);

// update panel finish dist (retail: direct with wrap, no rate clamp)

		lastdist = player->CarAI.FinishDistPanel;

		dist = player->CarAI.FinishDist / PosTotalDist;
		{
			REAL add = dist - player->CarAI.FinishDistPanel;

			if (add > 0.5f) add -= 1.0f;
			else if (add < -0.5f) add += 1.0f;

			player->CarAI.FinishDistPanel += add;
			if (player->CarAI.FinishDistPanel >= 1.0f) player->CarAI.FinishDistPanel -= 1.0f;
			else if (player->CarAI.FinishDistPanel < 0.0f) player->CarAI.FinishDistPanel += 1.0f;
		}

// crossed line?

		if (lastdist < 0.25f && player->CarAI.FinishDistPanel > 0.75f)
		{

// calc time delta

			if (timedelta && (!player->CarAI.BackTracking || player->CarAI.PreLap))
			{
				dist = 1.0f - player->CarAI.FinishDistPanel;
				*timedelta = (unsigned long)(dist / (dist + lastdist) * TimeStep * 1000);
			}

// zero prelap

			player->CarAI.PreLap = FALSE;

// was backtracking?

			if (player->CarAI.BackTracking)
			{
				player->CarAI.BackTracking = FALSE;
			}

// nope, new lap

			else
			{
				return TRUE;
			}
		}

// start backtracking?

		else if (lastdist > 0.75f && player->CarAI.FinishDistPanel < 0.25f)
		{
			player->CarAI.BackTracking = TRUE;
		}
	}

// return false

	return FALSE;
}
