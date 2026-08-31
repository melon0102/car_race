
extern "C" void DbgPrintf(const char *fmt, ...);   // ANDROID_PORT

#include "revolt.h"
#include "trigger.h"
#include "edittrig.h"
#include "main.h"
#include "geom.h"
#include "car.h"
#include "ctrlread.h"
#include "object.h"
#include "obj_init.h"   // ANDROID_PORT: object thrower
#include "control.h"
#include "player.h"
#include "piano.h"
#include "panel.h"
#ifdef _PC
#include "camera.h"
#include "ai_car.h"
#endif
#ifdef _N64
#include "ffs_code.h"
#include "ffs_list.h"
#include "utils.h"
#endif

// globals

long TriggerNum;
TRIGGER *Triggers;

// trigger info - handler, local only

void TriggerObjectThrower(PLAYER *player, long flag, long n, VEC *vec);   // ANDROID_PORT

static TRIGGER_INFO TriggerInfo[] = {
	TriggerPiano, FALSE,
	TriggerSplit, TRUE,
	TriggerTrackDir, TRUE,
#ifdef _PC
	TriggerCamera, FALSE,
	CAI_TriggerAiHome, TRUE,
	// ANDROID_PORT: retail trigger types used by the level data. NULL entries
	// are accepted and ignored rather than read past the end of the table.
	NULL, FALSE,                  // CAMSHORTEN  (camera tweak, not ported)
	TriggerObjectThrower, FALSE,  // OBJECTTHROWER — launches the basketballs
	NULL, FALSE,                  // GAPCAM      (camera tweak, not ported)
	NULL, FALSE,                  // REPOSITION_CAR
#endif
};

#ifdef _PC
static_assert(sizeof(TriggerInfo) / sizeof(TriggerInfo[0]) == TRIGGER_NUM,
              "TriggerInfo must have exactly one entry per TRIGGER_*");   // ANDROID_PORT
#endif

///////////////////
// load triggers //
///////////////////
#ifdef _PC
void LoadTriggers(char *file)
{
	long i;
	REAL time;
	FILE *fp;
	FILE_TRIGGER ftri;
	VEC vec;

// zero misc

	TriggerNum = 0;
	Triggers = NULL;

// open trigger file

	fp = fopen(file, "rb");
	if (!fp)
	{
		return;
	}

// read trigger num

	fread(&TriggerNum, sizeof(TriggerNum), 1, fp);
	if (!TriggerNum)
	{
		fclose(fp);
		return;
	}

// alloc ram for triggers

	Triggers = (TRIGGER*)malloc(sizeof(TRIGGER) * TriggerNum);
	if (!Triggers)
	{
		fclose(fp);
		Box(NULL, "Can't alloc memory for triggers!", MB_OK);
		QuitGame = TRUE;
		return;
	}

// load and convert each trigger

	for (i = 0 ; i < TriggerNum ; i++)
	{

// read file zone

		fread(&ftri, sizeof(ftri), 1, fp);

// set misc

		Triggers[i].ID = ftri.ID;
		Triggers[i].Flag = ftri.Flag;
		Triggers[i].GlobalFirst = TRUE;
		// ANDROID_PORT: bounds check — the retail data carries trigger IDs
		// this build may not know, and the original read straight past the
		// end of TriggerInfo[]
		if (ftri.ID >= 0 && ftri.ID < TRIGGER_NUM)
		{
			Triggers[i].Function = TriggerInfo[ftri.ID].Func;
			Triggers[i].LocalPlayerOnly = TriggerInfo[ftri.ID].LocalPlayerOnly;
		}
		else
		{
			Triggers[i].Function = NULL;
			Triggers[i].LocalPlayerOnly = FALSE;
		}

// set XYZ size

		Triggers[i].Size[0] = ftri.Size[0];
		Triggers[i].Size[1] = ftri.Size[1];
		Triggers[i].Size[2] = ftri.Size[2];

// build 3 planes

		Triggers[i].Plane[0].v[A] = ftri.Matrix.m[RX];
		Triggers[i].Plane[0].v[B] = ftri.Matrix.m[RY];
		Triggers[i].Plane[0].v[C] = ftri.Matrix.m[RZ];
		Triggers[i].Plane[0].v[D] = -DotProduct(&ftri.Matrix.mv[R], &ftri.Pos);

		Triggers[i].Plane[1].v[A] = ftri.Matrix.m[UX];
		Triggers[i].Plane[1].v[B] = ftri.Matrix.m[UY];
		Triggers[i].Plane[1].v[C] = ftri.Matrix.m[UZ];
		Triggers[i].Plane[1].v[D] = -DotProduct(&ftri.Matrix.mv[U], &ftri.Pos);

		Triggers[i].Plane[2].v[A] = ftri.Matrix.m[LX];
		Triggers[i].Plane[2].v[B] = ftri.Matrix.m[LY];
		Triggers[i].Plane[2].v[C] = ftri.Matrix.m[LZ];
		Triggers[i].Plane[2].v[D] = -DotProduct(&ftri.Matrix.mv[L], &ftri.Pos);

// set vec?

		vec.v[Y] = 0;

		if (Triggers[i].ID == TRIGGER_AIHOME)
		{
			if (Triggers[i].Flag < 8)
			{
				time = (float)Triggers[i].Flag / 8.0f;
				vec.v[X] = -Triggers[i].Size[X];
				vec.v[Z] = -Triggers[i].Size[Z] + Triggers[i].Size[Z] * time * 2;
			}
			else if (Triggers[i].Flag < 16)
			{
				time = (float)(Triggers[i].Flag - 8) / 8.0f;
				vec.v[X] = -Triggers[i].Size[X] + Triggers[i].Size[X] * time * 2;
				vec.v[Z] = Triggers[i].Size[Z];
			}
			else if (Triggers[i].Flag < 24)
			{
				time = (float)(Triggers[i].Flag - 16) / 8.0f;
				vec.v[X] = Triggers[i].Size[X];
				vec.v[Z] = Triggers[i].Size[Z] - Triggers[i].Size[Z] * time * 2;
			}
			else if (Triggers[i].Flag < 32)
			{
				time = (float)(Triggers[i].Flag - 24) / 8.0f;
				vec.v[X] = Triggers[i].Size[X] - Triggers[i].Size[X] * time * 2;
				vec.v[Z] = -Triggers[i].Size[Z];
			}

			RotTransVector(&ftri.Matrix, &ftri.Pos, &vec, &Triggers[i].Vector);
		}
	}

// close file

	fclose(fp);
}
#endif


//
// N64 version
//

#ifdef _N64
void LoadTriggers()
{
	long			i, j;
	REAL			time;
	FIL				*fp;
	FILE_TRIGGER 	ftri;
	VEC				vec;

// zero misc
	TriggerNum = 0;
	Triggers = NULL;

	printf("Loading trigger data...\n");
// open trigger file
	fp = FFS_Open(FFS_TYPE_TRACK | TRK_TRIGGERS);
	if (!fp)
	{
		printf("...unable to open trigger file.\n");
		return;
	}

// read trigger num

	FFS_Read(&TriggerNum, sizeof(TriggerNum), fp);
	if (!TriggerNum)
	{
		printf("...unable to read trigger file.\n");
		FFS_Close(fp);
		return;
	}
	TriggerNum = EndConvLong(TriggerNum);

// alloc ram for triggers
	Triggers = (TRIGGER *)malloc(sizeof(TRIGGER) * TriggerNum);
	if (!Triggers)
	{
		ERROR("TRG", "LoadTriggers", "Unable to alloc memory for triggers", 1);
	}

// load and convert each trigger
	for (i = 0 ; i < TriggerNum ; i++)
	{
// read file zone
		FFS_Read(&ftri, sizeof(ftri), fp);
		ftri.ID = EndConvLong(ftri.ID);
		ftri.Flag = EndConvLong(ftri.Flag);
		ftri.Pos.v[0] = EndConvReal(ftri.Pos.v[0]);
		ftri.Pos.v[1] = EndConvReal(ftri.Pos.v[1]);
		ftri.Pos.v[2] = EndConvReal(ftri.Pos.v[2]);
		for (j = 0; j < 9; j++)
		{
			ftri.Matrix.m[j] = EndConvReal(ftri.Matrix.m[j]);
		}
		ftri.Size[0] = EndConvReal(ftri.Size[0]);
		ftri.Size[1] = EndConvReal(ftri.Size[1]);
		ftri.Size[2] = EndConvReal(ftri.Size[2]);

// set misc
		Triggers[i].ID = ftri.ID;
		Triggers[i].Flag = ftri.Flag;
		Triggers[i].GlobalFirst = TRUE;
		Triggers[i].Function = TriggerInfo[ftri.ID].Func;
		Triggers[i].LocalPlayerOnly = TriggerInfo[ftri.ID].LocalPlayerOnly;

// set XYZ size
		Triggers[i].Size[0] = ftri.Size[0];
		Triggers[i].Size[1] = ftri.Size[1];
		Triggers[i].Size[2] = ftri.Size[2];

// build 3 planes
		Triggers[i].Plane[0].v[A] = ftri.Matrix.m[RX];
		Triggers[i].Plane[0].v[B] = ftri.Matrix.m[RY];
		Triggers[i].Plane[0].v[C] = ftri.Matrix.m[RZ];
		Triggers[i].Plane[0].v[D] = -DotProduct(&ftri.Matrix.mv[R], &ftri.Pos);

		Triggers[i].Plane[1].v[A] = ftri.Matrix.m[UX];
		Triggers[i].Plane[1].v[B] = ftri.Matrix.m[UY];
		Triggers[i].Plane[1].v[C] = ftri.Matrix.m[UZ];
		Triggers[i].Plane[1].v[D] = -DotProduct(&ftri.Matrix.mv[U], &ftri.Pos);

		Triggers[i].Plane[2].v[A] = ftri.Matrix.m[LX];
		Triggers[i].Plane[2].v[B] = ftri.Matrix.m[LY];
		Triggers[i].Plane[2].v[C] = ftri.Matrix.m[LZ];
		Triggers[i].Plane[2].v[D] = -DotProduct(&ftri.Matrix.mv[L], &ftri.Pos);

// set vec?
		vec.v[Y] = 0;

		if (Triggers[i].ID == TRIGGER_AIHOME)
		{
			if (Triggers[i].Flag < 8)
			{
				time = (float)Triggers[i].Flag / 8.0f;
				vec.v[X] = -Triggers[i].Size[X];
				vec.v[Z] = -Triggers[i].Size[Z] + Triggers[i].Size[Z] * time * 2;
			}
			else if (Triggers[i].Flag < 16)
			{
				time = (float)(Triggers[i].Flag - 8) / 8.0f;
				vec.v[X] = -Triggers[i].Size[X] + Triggers[i].Size[X] * time * 2;
				vec.v[Z] = Triggers[i].Size[Z];
			}
			else if (Triggers[i].Flag < 24)
			{
				time = (float)(Triggers[i].Flag - 16) / 8.0f;
				vec.v[X] = Triggers[i].Size[X];
				vec.v[Z] = Triggers[i].Size[Z] - Triggers[i].Size[Z] * time * 2;
			}
			else if (Triggers[i].Flag < 32)
			{
				time = (float)(Triggers[i].Flag - 24) / 8.0f;
				vec.v[X] = Triggers[i].Size[X] - Triggers[i].Size[X] * time * 2;
				vec.v[Z] = -Triggers[i].Size[Z];
			}
			RotTransVector(&ftri.Matrix, &ftri.Pos, &vec, &Triggers[i].Vector);
		}
	}

// close file
	FFS_Close(fp);
}
#endif


///////////////////
// free triggers //
///////////////////

void FreeTriggers(void)
{
	free(Triggers);
}

////////////////////
// check triggers //
////////////////////

void CheckTriggers(void)
{
	long i, k, skip, flag;
	float dist;
	TRIGGER *trigger;
	CAR *car;
	VEC *pos;
	PLAYER *player;

// loop thru all triggers

	trigger = Triggers;
	// ANDROID_PORT: also skip known-but-unimplemented types (NULL handler)
	for (i = 0 ; i < TriggerNum ; i++, trigger++) if (trigger->ID < TRIGGER_NUM && trigger->Function)
	{

// loop thru players

		for (player = PLR_PlayerHead ; player != NULL ; player = player->next)
		{
			car = &player->car;

			if (trigger->LocalPlayerOnly && car != &PLR_LocalPlayer->car)
				continue;

// inside trigger?

			pos = &car->Body->Centre.Pos;

			skip = FALSE;
			for (k = 0 ; k < 3 ; k++)
			{
				dist = PlaneDist(&trigger->Plane[k], pos);
				if (dist < -trigger->Size[k] || dist > trigger->Size[k])
				{
					skip = TRUE;
					break;
				}
			}

// yep

			if (!skip)
			{
				flag = 0;
				if (trigger->GlobalFirst) flag |= TRIGGER_GLOBAL_FIRST;
				if (trigger->FrameStamp != FrameCount) flag |= TRIGGER_FRAME_FIRST;

				if (trigger->Function) trigger->Function(player, flag, trigger->Flag, &trigger->Vector);

				trigger->FrameStamp = FrameCount;
				trigger->GlobalFirst = FALSE;
			}
		}
	}
}

/////////////////////////////////////////
// reset trigger flags of a given type //
/////////////////////////////////////////

void ResetTriggerFlags(long ID)
{
	long i;

	for (i = 0 ; i < TriggerNum ; i++)
	{
		if (Triggers[i].ID == ID)
		{
			Triggers[i].FrameStamp--;
			Triggers[i].GlobalFirst = TRUE;
		}
	}
}

////////////////////////////////////////////////////////////////
//
// ANDROID_PORT: TriggerObjectThrower — ported from the retail Xbox tree
// (rvsource/Xbox/Src/trigger.cpp). Finds the thrower object carrying this
// trigger ID, spawns its configured object at the thrower position and
// launches it along the thrower Look vector.
//
////////////////////////////////////////////////////////////////

void TriggerObjectThrower(PLAYER *player, long flag, long n, VEC *vec)
{
    OBJECT *obj, *newObj;
    OBJECT_THROWER_OBJ *objThrower;

    // only once per level
    if (!(flag & TRIGGER_GLOBAL_FIRST)) return;

    for (obj = OBJ_ObjectHead; obj != NULL; obj = obj->next)
    {
        if (obj->Type != OBJECT_TYPE_OBJECT_THROWER) continue;

        objThrower = (OBJECT_THROWER_OBJ*)obj->Data;
        if (!objThrower || objThrower->ID != n) continue;

        newObj = NULL;
        if (objThrower->ObjectType >= 0 && objThrower->ObjectType < OBJECT_TYPE_MAX)
            newObj = CreateObject(&obj->body.Centre.Pos, &obj->body.Centre.WMatrix,
                                  objThrower->ObjectType, NULL);

        if (newObj != NULL && objThrower->Speed > 0)
        {
            VecEqScalarVec(&newObj->body.Centre.Vel, objThrower->Speed * 50,
                           &obj->body.Centre.WMatrix.mv[L]);
            DbgPrintf("THROW OBJ TYPE %d", (int)objThrower->ObjectType);
        }

        return;
    }
}
