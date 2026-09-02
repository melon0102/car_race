// ANDROID_PORT: retail position-node system (rvsource/Xbox/Src/posnode.h).
// A dedicated node chain (levels/*.pan — shipped with the retail level data)
// purpose-built for race position, wrong-way detection and lap crossing.
// The 1999 tree derived all of this from the AI racing-line nodes, whose
// orientations are unreliable with retail data: false wrong-way signs and
// broken position ranking were the visible symptoms.

#ifndef POSNODE_H
#define POSNODE_H

// (no player.h include — the 1999 headers are order-sensitive; the update
// prototype uses the struct tag like trigger.h does)
struct PlayerStruct;

#define POSNODE_MAX_LINKS 4

typedef struct {
	VEC Pos;
	REAL Dist;
	long Prev[POSNODE_MAX_LINKS];
	long Next[POSNODE_MAX_LINKS];
} FILE_POSNODE;

typedef struct _POSNODE {
	VEC Pos;
	REAL Dist;
	struct _POSNODE *Prev[POSNODE_MAX_LINKS];
	struct _POSNODE *Next[POSNODE_MAX_LINKS];
} POSNODE;

// prototypes

extern void LoadPosNodes(char *file);
extern void FreePosNodes(void);
extern long UpdateCarPosNodeDist(struct PlayerStruct *player, unsigned long *timedelta);

// globals

extern long PosNodeNum, PosStartNode;
extern POSNODE *PosNode;
extern REAL PosTotalDist;

#endif // POSNODE_H
