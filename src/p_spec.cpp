// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//		Implements special effects:
//		Texture animation, height or lighting changes
//		 according to adjacent sectors, respective
//		 utility functions, etc.
//		Line Tag handling. Line and Sector triggers.
//		Implements donut linedef triggers
//		Initializes and implements BOOM linedef triggers for
//			Scrollers/Conveyors
//			Friction
//			Wind/Current
//
//-----------------------------------------------------------------------------


#include <stdlib.h>

#include "templates.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "gstrings.h"

#include "i_system.h"
#include "m_argv.h"
#include "m_random.h"
#include "m_bbox.h"
#include "w_wad.h"

#include "p_local.h"
#include "p_spec.h"
#include "p_blockmap.h"
#include "p_lnspec.h"
#include "p_terrain.h"
#include "p_acs.h"
#include "p_3dmidtex.h"

#include "g_game.h"

#include "s_sound.h"
#include "sc_man.h"
#include "gi.h"
#include "statnums.h"
#include "g_level.h"
#include "v_font.h"
#include "a_sharedglobal.h"
#include "farchive.h"
#include "a_keys.h"
#include "c_dispatch.h"
#include "r_sky.h"
#include "d_player.h"
#include "p_maputl.h"
#include "p_blockmap.h"
#ifndef NO_EDATA
#include "edata.h"
#endif

// State.
#include "r_state.h"

#include "c_console.h"

#include "r_data/r_interpolate.h"

static FRandom pr_playerinspecialsector ("PlayerInSpecialSector");

EXTERN_CVAR(Bool, cl_predict_specials)

IMPLEMENT_POINTY_CLASS (DPusher)
 DECLARE_POINTER (m_Source)
END_POINTERS

DPusher::DPusher ()
{
}

inline FArchive &operator<< (FArchive &arc, DPusher::EPusher &type)
{
	BYTE val = (BYTE)type;
	arc << val;
	type = (DPusher::EPusher)val;
	return arc;
}

void DPusher::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	arc << m_Type
		<< m_Source
		<< m_PushVec
		<< m_Magnitude
		<< m_Radius
		<< m_Affectee;
}

// killough 3/7/98: Initialize generalized scrolling
void P_SpawnScrollers();
static void P_SpawnFriction ();		// phares 3/16/98
static void P_SpawnPushers ();		// phares 3/20/98


// [RH] Check dmflags for noexit and respond accordingly
bool CheckIfExitIsGood (AActor *self, level_info_t *info)
{
	cluster_info_t *cluster;

	// The world can always exit itself.
	if (self == NULL)
		return true;

	// We must kill all monsters to exit the level.
	if ((dmflags2 & DF2_KILL_MONSTERS) && level.killed_monsters != level.total_monsters)
		return false;

	// Is this a deathmatch game and we're not allowed to exit?
	if ((deathmatch || alwaysapplydmflags) && (dmflags & DF_NO_EXIT))
	{
		P_DamageMobj (self, self, self, TELEFRAG_DAMAGE, NAME_Exit);
		return false;
	}
	// Is this a singleplayer game and the next map is part of the same hub and we're dead?
	if (self->health <= 0 &&
		!multiplayer &&
		info != NULL &&
		info->cluster == level.cluster &&
		(cluster = FindClusterInfo(level.cluster)) != NULL &&
		cluster->flags & CLUSTER_HUB)
	{
		return false;
	}
	if (deathmatch && gameaction != ga_completed)
	{
		Printf ("%s exited the level.\n", self->player->userinfo.GetName());
	}
	return true;
}


//
// UTILITIES
//

//============================================================================
//
// P_ActivateLine
//
//============================================================================

bool P_ActivateLine (line_t *line, AActor *mo, int side, int activationType, DVector3 *optpos)
{
	int lineActivation;
	INTBOOL repeat;
	INTBOOL buttonSuccess;
	BYTE special;

	if (!P_TestActivateLine (line, mo, side, activationType, optpos))
	{
		return false;
	}
	bool remote = (line->special != 7 && line->special != 8 && (line->special < 11 || line->special > 14));
	if (line->locknumber > 0 && !P_CheckKeys (mo, line->locknumber, remote)) return false;
	lineActivation = line->activation;
	repeat = line->flags & ML_REPEAT_SPECIAL;
	buttonSuccess = false;
	buttonSuccess = P_ExecuteSpecial(line->special,
					line, mo, side == 1, line->args[0],
					line->args[1], line->args[2],
					line->args[3], line->args[4]);

	special = line->special;
	if (!repeat && buttonSuccess)
	{ // clear the special on non-retriggerable lines
		line->special = 0;
	}

	if (buttonSuccess)
	{
		if (activationType == SPAC_Use || activationType == SPAC_Impact || activationType == SPAC_Push)
		{
			P_ChangeSwitchTexture (line->sidedef[0], repeat, special);
		}
	}
	// some old WADs use this method to create walls that change the texture when shot.
	else if (activationType == SPAC_Impact &&					// only for shootable triggers
		(level.flags2 & LEVEL2_DUMMYSWITCHES) &&				// this is only a compatibility setting for an old hack!
		!repeat &&												// only non-repeatable triggers
		(special<Generic_Floor || special>Generic_Crusher) &&	// not for Boom's generalized linedefs
		special &&												// not for lines without a special
		tagManager.LineHasID(line, line->args[0]) &&							// Safety check: exclude edited UDMF linedefs or ones that don't map the tag to args[0]
		line->args[0] &&										// only if there's a tag (which is stored in the first arg)
		P_FindFirstSectorFromTag (line->args[0]) == -1)			// only if no sector is tagged to this linedef
	{
		P_ChangeSwitchTexture (line->sidedef[0], repeat, special);
		line->special = 0;
	}
// end of changed code
	if (developer && buttonSuccess)
	{
		Printf ("Line special %d activated on line %i\n", special, int(line - lines));
	}
	return true;
}

//============================================================================
//
// P_TestActivateLine
//
//============================================================================

bool P_TestActivateLine (line_t *line, AActor *mo, int side, int activationType, DVector3 *optpos)
{
 	int lineActivation = line->activation;

	if (line->flags & ML_FIRSTSIDEONLY && side == 1)
	{
		return false;
	}

	if (lineActivation & SPAC_UseThrough)
	{
		lineActivation |= SPAC_Use;
	}
	else if (line->special == Teleport &&
		(lineActivation & SPAC_Cross) &&
		activationType == SPAC_PCross &&
		mo != NULL &&
		mo->flags & MF_MISSILE)
	{ // Let missiles use regular player teleports
		lineActivation |= SPAC_PCross;
	}
	// BOOM's generalized line types that allow monster use can actually be
	// activated by anything except projectiles.
	if (lineActivation & SPAC_AnyCross)
	{
		lineActivation |= SPAC_Cross|SPAC_MCross;
	}
	if (activationType == SPAC_Use || activationType == SPAC_UseBack)
	{
		if (!P_CheckSwitchRange(mo, line, side, optpos))
		{
			return false;
		}
	}

	if (activationType == SPAC_Use && (lineActivation & SPAC_MUse) && !mo->player && mo->flags4 & MF4_CANUSEWALLS)
	{
		return true;
	}
	if (activationType == SPAC_Push && (lineActivation & SPAC_MPush) && !mo->player && mo->flags2 & MF2_PUSHWALL)
	{
		return true;
	}
	if ((lineActivation & activationType) == 0)
	{
		if (activationType != SPAC_MCross || lineActivation != SPAC_Cross)
		{ 
			return false;
		}
	}
	if (activationType == SPAC_AnyCross && (lineActivation & activationType))
	{
		return true;
	}
	if (mo && !mo->player &&
		!(mo->flags & MF_MISSILE) &&
		!(line->flags & ML_MONSTERSCANACTIVATE) &&
		(activationType != SPAC_MCross || (!(lineActivation & SPAC_MCross))))
	{ 
		// [RH] monsters' ability to activate this line depends on its type
		// In Hexen, only MCROSS lines could be activated by monsters. With
		// lax activation checks, monsters can also activate certain lines
		// even without them being marked as monster activate-able. This is
		// the default for non-Hexen maps in Hexen format.
		if (!(level.flags2 & LEVEL2_LAXMONSTERACTIVATION))
		{
			return false;
		}
		if ((activationType == SPAC_Use || activationType == SPAC_Push)
			&& (line->flags & ML_SECRET))
			return false;		// never open secret doors

		bool noway = true;

		switch (activationType)
		{
		case SPAC_Use:
		case SPAC_Push:
			switch (line->special)
			{
			case Door_Raise:
				if (line->args[0] == 0 && line->args[1] < 64)
					noway = false;
				break;
			case Teleport:
			case Teleport_NoFog:
				noway = false;
			}
			break;

		case SPAC_MCross:
			if (!(lineActivation & SPAC_MCross))
			{
				switch (line->special)
				{
				case Door_Raise:
					if (line->args[1] >= 64)
					{
						break;
					}
				case Teleport:
				case Teleport_NoFog:
				case Teleport_Line:
				case Plat_DownWaitUpStayLip:
				case Plat_DownWaitUpStay:
					noway = false;
				}
			}
			else noway = false;
			break;

		default:
			noway = false;
		}
		return !noway;
	}
	if (activationType == SPAC_MCross && !(lineActivation & SPAC_MCross) &&
		!(line->flags & ML_MONSTERSCANACTIVATE))
	{
		return false;
	}
	return true;
}

//============================================================================
//
// P_PredictLine
//
//============================================================================

bool P_PredictLine(line_t *line, AActor *mo, int side, int activationType)
{
	int lineActivation;
	INTBOOL buttonSuccess;
	BYTE special;

	// Only predict a very specifc section of specials
	if (line->special != Teleport_Line &&
		line->special != Teleport)
	{
		return false;
	}

	if (!P_TestActivateLine(line, mo, side, activationType) || !cl_predict_specials)
	{
		return false;
	}

	if (line->locknumber > 0) return false;
	lineActivation = line->activation;
	buttonSuccess = false;
	buttonSuccess = P_ExecuteSpecial(line->special,
		line, mo, side == 1, line->args[0],
		line->args[1], line->args[2],
		line->args[3], line->args[4]);

	special = line->special;

	// end of changed code
	if (developer && buttonSuccess)
	{
		Printf("Line special %d predicted on line %i\n", special, int(line - lines));
	}
	return true;
}

//
// P_PlayerInSpecialSector
// Called every tic frame
//	that the player origin is in a special sector
//
void P_PlayerInSpecialSector (player_t *player, sector_t * sector)
{
	if (sector == NULL)
	{
		// Falling, not all the way down yet?
		sector = player->mo->Sector;
		if (player->mo->Z() != sector->LowestFloorAt(player->mo)
			&& !player->mo->waterlevel)
		{
			return;
		}
	}

	// Has hit ground.
	AInventory *ironfeet;

	// [RH] Apply any customizable damage
	if (sector->damageamount > 0)
	{
		// Allow subclasses. Better would be to implement it as armor and let that reduce
		// the damage as part of the normal damage procedure. Unfortunately, I don't have
		// different damage types yet, so that's not happening for now.
		for (ironfeet = player->mo->Inventory; ironfeet != NULL; ironfeet = ironfeet->Inventory)
		{
			if (ironfeet->IsKindOf (RUNTIME_CLASS(APowerIronFeet)))
				break;
		}

		if (sector->Flags & SECF_ENDGODMODE) player->cheats &= ~CF_GODMODE;
		if ((ironfeet == NULL || pr_playerinspecialsector() < sector->leakydamage))
		{
			if (sector->Flags & SECF_HAZARD)
			{
				player->hazardcount += sector->damageamount;
				player->hazardtype = sector->damagetype;
				player->hazardinterval = sector->damageinterval;
			}
			else if (level.time % sector->damageinterval == 0)
			{
				if (!(player->cheats & (CF_GODMODE|CF_GODMODE2))) P_DamageMobj(player->mo, NULL, NULL, sector->damageamount, sector->damagetype);
				if ((sector->Flags & SECF_ENDLEVEL) && player->health <= 10 && (!deathmatch || !(dmflags & DF_NO_EXIT)))
				{
					G_ExitLevel(0, false);
				}
				if (sector->Flags & SECF_DMGTERRAINFX)
				{
					P_HitWater(player->mo, player->mo->Sector, player->mo->Pos(), false, true, true);
				}
			}
		}
	}
	else if (sector->damageamount < 0)
	{
		if (level.time % sector->damageinterval == 0)
		{
			P_GiveBody(player->mo, -sector->damageamount, 100);
		}
	}

	if (sector->isSecret())
	{
		sector->ClearSecret();
		P_GiveSecret(player->mo, true, true, int(sector - sectors));
	}
}

//============================================================================
//
// P_SectorDamage
//
//============================================================================

static void DoSectorDamage(AActor *actor, sector_t *sec, int amount, FName type, PClassActor *protectClass, int flags)
{
	if (!(actor->flags & MF_SHOOTABLE))
		return;

	if (!(flags & DAMAGE_NONPLAYERS) && actor->player == NULL)
		return;

	if (!(flags & DAMAGE_PLAYERS) && actor->player != NULL)
		return;

	if (!(flags & DAMAGE_IN_AIR) && actor->_f_Z() != sec->floorplane.ZatPoint(actor) && !actor->waterlevel)
		return;

	if (protectClass != NULL)
	{
		if (actor->FindInventory(protectClass, !!(flags & DAMAGE_SUBCLASSES_PROTECT)))
			return;
	}

	P_DamageMobj (actor, NULL, NULL, amount, type);
}

void P_SectorDamage(int tag, int amount, FName type, PClassActor *protectClass, int flags)
{
	FSectorTagIterator itr(tag);
	int secnum;
	while ((secnum = itr.Next()) >= 0)
	{
		AActor *actor, *next;
		sector_t *sec = &sectors[secnum];

		// Do for actors in this sector.
		for (actor = sec->thinglist; actor != NULL; actor = next)
		{
			next = actor->snext;
			DoSectorDamage(actor, sec, amount, type, protectClass, flags);
		}
		// If this is a 3D floor control sector, also do for anything in/on
		// those 3D floors.
		for (unsigned i = 0; i < sec->e->XFloor.attached.Size(); ++i)
		{
			sector_t *sec2 = sec->e->XFloor.attached[i];

			for (actor = sec2->thinglist; actor != NULL; actor = next)
			{
				next = actor->snext;
				// Only affect actors touching the 3D floor
				fixed_t z1 = sec->floorplane.ZatPoint(actor);
				fixed_t z2 = sec->ceilingplane.ZatPoint(actor);
				if (z2 < z1)
				{
					// Account for Vavoom-style 3D floors
					fixed_t zz = z1;
					z1 = z2;
					z2 = zz;
				}
				if (actor->_f_Z() + actor->_f_height() > z1)
				{
					// If DAMAGE_IN_AIR is used, anything not beneath the 3D floor will be
					// damaged (so, anything touching it or above it). Other 3D floors between
					// the actor and this one will not stop this effect.
					if ((flags & DAMAGE_IN_AIR) || actor->_f_Z() <= z2)
					{
						// Here we pass the DAMAGE_IN_AIR flag to disable the floor check, since it
						// only works with the real sector's floor. We did the appropriate height checks
						// for 3D floors already.
						DoSectorDamage(actor, NULL, amount, type, protectClass, flags | DAMAGE_IN_AIR);
					}
				}
			}
		}
	}
}

//============================================================================
//
// P_GiveSecret
//
//============================================================================

CVAR(Bool, showsecretsector, false, 0)
CVAR(Bool, cl_showsecretmessage, true, CVAR_ARCHIVE)

void P_GiveSecret(AActor *actor, bool printmessage, bool playsound, int sectornum)
{
	if (actor != NULL)
	{
		if (actor->player != NULL)
		{
			actor->player->secretcount++;
		}
		if (cl_showsecretmessage && actor->CheckLocalView(consoleplayer))
		{
			if (printmessage)
			{
				if (!showsecretsector || sectornum < 0) C_MidPrint(SmallFont, GStrings["SECRETMESSAGE"]);
				else
				{
					FString s = GStrings["SECRETMESSAGE"];
					s.AppendFormat(" (Sector %d)", sectornum);
					C_MidPrint(SmallFont, s);
				}
			}
			if (playsound) S_Sound (CHAN_AUTO | CHAN_UI, "misc/secret", 1, ATTN_NORM);
		}
	}
	level.found_secrets++;
}

//============================================================================
//
// P_PlayerOnSpecialFlat
//
//============================================================================

void P_PlayerOnSpecialFlat (player_t *player, int floorType)
{
	if (Terrains[floorType].DamageAmount &&
		!(level.time & Terrains[floorType].DamageTimeMask))
	{
		AInventory *ironfeet = NULL;

		if (Terrains[floorType].AllowProtection)
		{
			for (ironfeet = player->mo->Inventory; ironfeet != NULL; ironfeet = ironfeet->Inventory)
			{
				if (ironfeet->IsKindOf (RUNTIME_CLASS(APowerIronFeet)))
					break;
			}
		}

		int damage = 0;
		if (ironfeet == NULL)
		{
			damage = P_DamageMobj (player->mo, NULL, NULL, Terrains[floorType].DamageAmount,
				Terrains[floorType].DamageMOD);
		}
		if (damage > 0 && Terrains[floorType].Splash != -1)
		{
			S_Sound (player->mo, CHAN_AUTO,
				Splashes[Terrains[floorType].Splash].NormalSplashSound, 1,
				ATTN_IDLE);
		}
	}
}



//
// P_UpdateSpecials
// Animate planes, scroll walls, etc.
//
EXTERN_CVAR (Float, timelimit)

void P_UpdateSpecials ()
{
	// LEVEL TIMER
	if (deathmatch && timelimit)
	{
		if (level.maptime >= (int)(timelimit * TICRATE * 60))
		{
			Printf ("%s\n", GStrings("TXT_TIMELIMIT"));
			G_ExitLevel(0, false);
		}
	}
}



//
// SPECIAL SPAWNING
//

CUSTOM_CVAR (Bool, forcewater, false, CVAR_ARCHIVE|CVAR_SERVERINFO)
{
	if (gamestate == GS_LEVEL)
	{
		int i;

		for (i = 0; i < numsectors; i++)
		{
			sector_t *hsec = sectors[i].GetHeightSec();
			if (hsec &&
				!(sectors[i].heightsec->MoreFlags & SECF_UNDERWATER))
			{
				if (self)
				{
					hsec->MoreFlags |= SECF_FORCEDUNDERWATER;
				}
				else
				{
					hsec->MoreFlags &= ~SECF_FORCEDUNDERWATER;
				}
			}
		}
	}
}

class DLightTransfer : public DThinker
{
	DECLARE_CLASS (DLightTransfer, DThinker)

	DLightTransfer() {}
public:
	DLightTransfer (sector_t *srcSec, int target, bool copyFloor);
	void Serialize (FArchive &arc);
	void Tick ();

protected:
	static void DoTransfer (int level, int target, bool floor);

	sector_t *Source;
	int TargetTag;
	bool CopyFloor;
	short LastLight;
};

IMPLEMENT_CLASS (DLightTransfer)

void DLightTransfer::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	if (SaveVersion < 3223)
	{
		BYTE bytelight;
		arc << bytelight;
		LastLight = bytelight;
	}
	else
	{
		arc << LastLight;
	}
	arc << Source << TargetTag << CopyFloor;
}

DLightTransfer::DLightTransfer (sector_t *srcSec, int target, bool copyFloor)
{
	int secnum;

	Source = srcSec;
	TargetTag = target;
	CopyFloor = copyFloor;
	DoTransfer (LastLight = srcSec->lightlevel, target, copyFloor);

	if (copyFloor)
	{
		FSectorTagIterator itr(target);
		while ((secnum = itr.Next()) >= 0)
			sectors[secnum].ChangeFlags(sector_t::floor, 0, PLANEF_ABSLIGHTING);
	}
	else
	{
		FSectorTagIterator itr(target);
		while ((secnum = itr.Next()) >= 0)
			sectors[secnum].ChangeFlags(sector_t::ceiling, 0, PLANEF_ABSLIGHTING);
	}
	ChangeStatNum (STAT_LIGHTTRANSFER);
}

void DLightTransfer::Tick ()
{
	int light = Source->lightlevel;

	if (light != LastLight)
	{
		LastLight = light;
		DoTransfer (light, TargetTag, CopyFloor);
	}
}

void DLightTransfer::DoTransfer (int level, int target, bool floor)
{
	int secnum;

	if (floor)
	{
		FSectorTagIterator itr(target);
		while ((secnum = itr.Next()) >= 0)
			sectors[secnum].SetPlaneLight(sector_t::floor, level);
	}
	else
	{
		FSectorTagIterator itr(target);
		while ((secnum = itr.Next()) >= 0)
			sectors[secnum].SetPlaneLight(sector_t::ceiling, level);
	}
}


class DWallLightTransfer : public DThinker
{
	enum
	{
		WLF_SIDE1=1,
		WLF_SIDE2=2,
		WLF_NOFAKECONTRAST=4
	};

	DECLARE_CLASS (DWallLightTransfer, DThinker)
	DWallLightTransfer() {}
public:
	DWallLightTransfer (sector_t *srcSec, int target, BYTE flags);
	void Serialize (FArchive &arc);
	void Tick ();

protected:
	static void DoTransfer (short level, int target, BYTE flags);

	sector_t *Source;
	int TargetID;
	short LastLight;
	BYTE Flags;
};

IMPLEMENT_CLASS (DWallLightTransfer)

void DWallLightTransfer::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	if (SaveVersion < 3223)
	{
		BYTE bytelight;
		arc << bytelight;
		LastLight = bytelight;
	}
	else
	{
		arc << LastLight;
	}
	arc << Source << TargetID << Flags;
}

DWallLightTransfer::DWallLightTransfer (sector_t *srcSec, int target, BYTE flags)
{
	int linenum;
	int wallflags;

	Source = srcSec;
	TargetID = target;
	Flags = flags;
	DoTransfer (LastLight = srcSec->GetLightLevel(), target, Flags);

	if (!(flags & WLF_NOFAKECONTRAST))
	{
		wallflags = WALLF_ABSLIGHTING;
	}
	else
	{
		wallflags = WALLF_ABSLIGHTING | WALLF_NOFAKECONTRAST;
	}

	FLineIdIterator itr(target);
	while ((linenum = itr.Next()) >= 0)
	{
		if (flags & WLF_SIDE1 && lines[linenum].sidedef[0] != NULL)
		{
			lines[linenum].sidedef[0]->Flags |= wallflags;
		}

		if (flags & WLF_SIDE2 && lines[linenum].sidedef[1] != NULL)
		{
			lines[linenum].sidedef[1]->Flags |= wallflags;
		}
	}
	ChangeStatNum(STAT_LIGHTTRANSFER);
}

void DWallLightTransfer::Tick ()
{
	short light = sector_t::ClampLight(Source->lightlevel);

	if (light != LastLight)
	{
		LastLight = light;
		DoTransfer (light, TargetID, Flags);
	}
}

void DWallLightTransfer::DoTransfer (short lightlevel, int target, BYTE flags)
{
	int linenum;

	FLineIdIterator itr(target);
	while ((linenum = itr.Next()) >= 0)
	{
		line_t *line = &lines[linenum];

		if (flags & WLF_SIDE1 && line->sidedef[0] != NULL)
		{
			line->sidedef[0]->SetLight(lightlevel);
		}

		if (flags & WLF_SIDE2 && line->sidedef[1] != NULL)
		{
			line->sidedef[1]->SetLight(lightlevel);
		}
	}
}

//-----------------------------------------------------------------------------
//
// Portals
//
//-----------------------------------------------------------------------------

//---------------------------------------------------------------------------
// Upper stacks go in the top sector. Lower stacks go in the bottom sector.

static void SetupFloorPortal (AStackPoint *point)
{
	NActorIterator it (NAME_LowerStackLookOnly, point->tid);
	sector_t *Sector = point->Sector;
	ASkyViewpoint *skyv = static_cast<ASkyViewpoint*>(it.Next());
	Sector->SkyBoxes[sector_t::floor] = skyv;
	if (skyv != NULL && skyv->bAlways)
	{
		skyv->Mate = point;
		if (Sector->GetAlpha(sector_t::floor) == OPAQUE)
			Sector->SetAlpha(sector_t::floor, Scale (point->args[0], OPAQUE, 255));
	}
}

static void SetupCeilingPortal (AStackPoint *point)
{
	NActorIterator it (NAME_UpperStackLookOnly, point->tid);
	sector_t *Sector = point->Sector;
	ASkyViewpoint *skyv = static_cast<ASkyViewpoint*>(it.Next());
	Sector->SkyBoxes[sector_t::ceiling] = skyv;
	if (skyv != NULL && skyv->bAlways)
	{
		skyv->Mate = point;
		if (Sector->GetAlpha(sector_t::ceiling) == OPAQUE)
			Sector->SetAlpha(sector_t::ceiling, Scale(point->args[0], OPAQUE, 255));
	}
}

void P_SetupPortals()
{
	TThinkerIterator<AStackPoint> it;
	AStackPoint *pt;
	TArray<AStackPoint *> points;

	while ((pt = it.Next()))
	{
		FName nm = pt->GetClass()->TypeName;
		if (nm == NAME_UpperStackLookOnly)
		{
			SetupFloorPortal(pt);
		}
		else if (nm == NAME_LowerStackLookOnly)
		{
			SetupCeilingPortal(pt);
		}
		pt->special1 = 0;
		points.Push(pt);
	}
}

static void SetPortal(sector_t *sector, int plane, ASkyViewpoint *portal, fixed_t alpha)
{
	// plane: 0=floor, 1=ceiling, 2=both
	if (plane > 0)
	{
		if (sector->SkyBoxes[sector_t::ceiling] == NULL || !barrier_cast<ASkyViewpoint*>(sector->SkyBoxes[sector_t::ceiling])->bAlways)
		{
			sector->SkyBoxes[sector_t::ceiling] = portal;
			if (sector->GetAlpha(sector_t::ceiling) == OPAQUE)
				sector->SetAlpha(sector_t::ceiling, alpha);

			if (!portal->bAlways) sector->SetTexture(sector_t::ceiling, skyflatnum);
		}
	}
	if (plane == 2 || plane == 0)
	{
		if (sector->SkyBoxes[sector_t::floor] == NULL || !barrier_cast<ASkyViewpoint*>(sector->SkyBoxes[sector_t::floor])->bAlways)
		{
			sector->SkyBoxes[sector_t::floor] = portal;
		}
		if (sector->GetAlpha(sector_t::floor) == OPAQUE)
			sector->SetAlpha(sector_t::floor, alpha);

		if (!portal->bAlways) sector->SetTexture(sector_t::floor, skyflatnum);
	}
}

static void CopyPortal(int sectortag, int plane, ASkyViewpoint *origin, fixed_t alpha, bool tolines)
{
	int s;
	FSectorTagIterator itr(sectortag);
	while ((s = itr.Next()) >= 0)
	{
		SetPortal(&sectors[s], plane, origin, alpha);
	}

	for (int j=0;j<numlines;j++)
	{
		// Check if this portal needs to be copied to other sectors
		// This must be done here to ensure that it gets done only after the portal is set up
		if (lines[j].special == Sector_SetPortal &&
			lines[j].args[1] == 1 &&
			(lines[j].args[2] == plane || lines[j].args[2] == 3) &&
			lines[j].args[3] == sectortag)
		{
			if (lines[j].args[0] == 0)
			{
				SetPortal(lines[j].frontsector, plane, origin, alpha);
			}
			else
			{
				FSectorTagIterator itr(lines[j].args[0]);
				while ((s = itr.Next()) >= 0)
				{
					SetPortal(&sectors[s], plane, origin, alpha);
				}
			}
		}
	}
}

void P_SpawnPortal(line_t *line, int sectortag, int plane, int alpha, int linked)
{
	if (plane < 0 || plane > 2 || (linked && plane == 2)) return;
	for (int i=0;i<numlines;i++)
	{
		// We must look for the reference line with a linear search unless we want to waste the line ID for it
		// which is not a good idea.
		if (lines[i].special == Sector_SetPortal &&
			lines[i].args[0] == sectortag &&
			lines[i].args[1] == linked &&
			lines[i].args[2] == plane &&
			lines[i].args[3] == 1)
		{
			// beware of overflows.
			DVector3 pos1((line->v1->fX() + line->v2->fX()) / 2, (line->v1->fY() + line->v2->fY()) / 2, 0);
			DVector3 pos2((lines[i].v1->fX() + lines[i].v2->fX()) / 2, (lines[i].v1->fY() + lines[i].v2->fY()) / 2, 0);
			double z = linked ? line->frontsector->GetPlaneTexZF(plane) : 0;	// the map's sector height defines the portal plane for linked portals

			fixed_t alpha = Scale (lines[i].args[4], OPAQUE, 255);

			AStackPoint *anchor = Spawn<AStackPoint>(pos1, NO_REPLACE);
			AStackPoint *reference = Spawn<AStackPoint>(pos2, NO_REPLACE);
			reference->special1 = linked ? SKYBOX_LINKEDPORTAL : SKYBOX_PORTAL;
			anchor->special1 = SKYBOX_ANCHOR;
			// store the portal displacement in the unused scaleX/Y members of the portal reference actor.
			anchor->Scale = -(reference->Scale = pos2 - pos1);
			anchor->specialf1 = reference->specialf1 = z;

			reference->Mate = anchor;
			anchor->Mate = reference;

			// This is so that the renderer can distinguish these portals from
			// the ones spawned with the '*StackLookOnly' things.
			reference->flags |= MF_JUSTATTACKED;
			anchor->flags |= MF_JUSTATTACKED;

			CopyPortal(sectortag, plane, reference, alpha, false);
			return;
		}
	}
}

// This searches the viewpoint's sector
// for a skybox line special, gets its tag and transfers the skybox to all tagged sectors.
void P_SpawnSkybox(ASkyViewpoint *origin)
{
	sector_t *Sector = origin->Sector;
	if (Sector == NULL)
	{
		Printf("Sector not initialized for SkyCamCompat\n");
		origin->Sector = Sector = P_PointInSector(origin->_f_X(), origin->_f_Y());
	}
	if (Sector)
	{
		line_t * refline = NULL;
		for (short i = 0; i < Sector->linecount; i++)
		{
			refline = Sector->lines[i];
			if (refline->special == Sector_SetPortal && refline->args[1] == 2)
			{
				// We found the setup linedef for this skybox, so let's use it for our init.
				CopyPortal(refline->args[0], refline->args[2], origin, 0, true);
				return;
			}
		}
	}
}



//
// P_SetSectorDamage
//
// Sets damage properties for one sector. Allows combination of original specials with explicit use of the damage properties
//

static void P_SetupSectorDamage(sector_t *sector, int damage, int interval, int leakchance, FName type, int flags)
{
	// Only set if damage is not yet initialized. This ensures that UDMF takes precedence over sector specials.
	if (sector->damageamount == 0)
	{
		sector->damageamount = damage;
		sector->damageinterval = MAX(1, interval);
		sector->leakydamage = leakchance;
		sector->damagetype = type;
		sector->Flags = (sector->Flags & ~SECF_DAMAGEFLAGS) | (flags & SECF_DAMAGEFLAGS);
	}
}

//
// P_InitSectorSpecial
//
// Sets up everything derived from 'sector->special' for one sector
// ('fromload' is necessary to allow conversion upon savegame load.)
//

void P_InitSectorSpecial(sector_t *sector, int special, bool nothinkers)
{
	// [RH] All secret sectors are marked with a BOOM-ish bitfield
	if (sector->special & SECRET_MASK)
	{
		sector->Flags |= SECF_SECRET | SECF_WASSECRET;
		level.total_secrets++;
	}
	if (sector->special & FRICTION_MASK)
	{
		sector->Flags |= SECF_FRICTION;
	}
	if (sector->special & PUSH_MASK)
	{
		sector->Flags |= SECF_PUSH;
	}
	if ((sector->special & DAMAGE_MASK) == 0x100)
	{
		P_SetupSectorDamage(sector, 5, 32, 0, NAME_Fire, 0);
	}
	else if ((sector->special & DAMAGE_MASK) == 0x200)
	{
		P_SetupSectorDamage(sector, 10, 32, 0, NAME_Slime, 0);
	}
	else if ((sector->special & DAMAGE_MASK) == 0x300)
	{
		P_SetupSectorDamage(sector, 20, 32, 5, NAME_Slime, 0);
	}
	sector->special &= 0xff;

	// [RH] Normal DOOM special or BOOM specialized?
	bool keepspecial = false;
	switch (sector->special)
	{
	case Light_Phased:
		if (!nothinkers) new DPhased (sector, 48, 63 - (sector->lightlevel & 63));
		break;

		// [RH] Hexen-like phased lighting
	case LightSequenceStart:
		if (!nothinkers) new DPhased (sector);
		break;

	case dLight_Flicker:
		if (!nothinkers) new DLightFlash (sector);
		break;

	case dLight_StrobeFast:
		if (!nothinkers) new DStrobe (sector, STROBEBRIGHT, FASTDARK, false);
		break;
			
	case dLight_StrobeSlow:
		if (!nothinkers) new DStrobe (sector, STROBEBRIGHT, SLOWDARK, false);
		break;

	case dLight_Strobe_Hurt:
		if (!nothinkers) new DStrobe (sector, STROBEBRIGHT, FASTDARK, false);
		P_SetupSectorDamage(sector, 20, 32, 5, NAME_Slime, 0);
		break;

	case dDamage_Hellslime:
		P_SetupSectorDamage(sector, 10, 32, 0, NAME_Slime, 0);
		break;

	case dDamage_Nukage:
		P_SetupSectorDamage(sector, 5, 32, 0, NAME_Slime, 0);
		break;

	case dLight_Glow:
		if (!nothinkers) new DGlow (sector);
		break;
			
	case dSector_DoorCloseIn30:
		new DDoor(sector, DDoor::doorWaitClose, FRACUNIT * 2, 0, 0, 30 * TICRATE);
		break;
			
	case dDamage_End:
		P_SetupSectorDamage(sector, 20, 32, 256, NAME_None, SECF_ENDGODMODE|SECF_ENDLEVEL);
		break;

	case dLight_StrobeSlowSync:
		if (!nothinkers) new DStrobe (sector, STROBEBRIGHT, SLOWDARK, true);
		break;

	case dLight_StrobeFastSync:
		if (!nothinkers) new DStrobe (sector, STROBEBRIGHT, FASTDARK, true);
		break;

	case dSector_DoorRaiseIn5Mins:
		new DDoor (sector, DDoor::doorWaitRaise, 2*FRACUNIT, TICRATE*30/7, 0, 5*60*TICRATE);
		break;

	case dFriction_Low:
		sector->friction = FRICTION_LOW;
		sector->movefactor = 0x269/65536.;
		sector->Flags |= SECF_FRICTION;
		break;

	case dDamage_SuperHellslime:
		P_SetupSectorDamage(sector, 20, 32, 5, NAME_Slime, 0);
		break;

	case dLight_FireFlicker:
		if (!nothinkers) new DFireFlicker (sector);
		break;

	case dDamage_LavaWimpy:
		P_SetupSectorDamage(sector, 5, 32, 256, NAME_Fire, SECF_DMGTERRAINFX);
		break;

	case dDamage_LavaHefty:
		P_SetupSectorDamage(sector, 8, 32, 256, NAME_Fire, SECF_DMGTERRAINFX);
		break;

	case dScroll_EastLavaDamage:
		P_SetupSectorDamage(sector, 5, 32, 256, NAME_Fire, SECF_DMGTERRAINFX);
		if (!nothinkers)
		{
			new DStrobe(sector, STROBEBRIGHT, FASTDARK, false);
			P_CreateScroller(EScroll::sc_floor, -((FRACUNIT / 2) << 3),
				0, -1, int(sector - sectors), 0);
		}
		keepspecial = true;
		break;

	case hDamage_Sludge:
		P_SetupSectorDamage(sector, 4, 32, 0, NAME_Slime, 0);
		break;

	case sLight_Strobe_Hurt:
		P_SetupSectorDamage(sector, 5, 32, 0, NAME_Slime, 0);
		if (!nothinkers) new DStrobe (sector, STROBEBRIGHT, FASTDARK, false);
		break;

	case sDamage_Hellslime:
		P_SetupSectorDamage(sector, 2, 32, 0, NAME_Slime, SECF_HAZARD);
		break;

	case Damage_InstantDeath:
		// Strife's instant death sector
		P_SetupSectorDamage(sector, TELEFRAG_DAMAGE, 1, 256, NAME_InstantDeath, 0);
		break;

	case sDamage_SuperHellslime:
		P_SetupSectorDamage(sector, 4, 32, 0, NAME_Slime, SECF_HAZARD);
		break;

	case Sector_Hidden:
		sector->MoreFlags |= SECF_HIDDEN;
		break;

	case Sector_Heal:
		// CoD's healing sector
		P_SetupSectorDamage(sector, -1, 32, 0, NAME_None, 0);
		break;

	case Sky2:
		sector->sky = PL_SKYFLAT;
		break;

	default:
		if (sector->special >= Scroll_North_Slow &&
			sector->special <= Scroll_SouthWest_Fast)
		{ // Hexen scroll special
			static const char hexenScrollies[24][2] =
			{
				{  0,  1 }, {  0,  2 }, {  0,  4 },
				{ -1,  0 }, { -2,  0 }, { -4,  0 },
				{  0, -1 }, {  0, -2 }, {  0, -4 },
				{  1,  0 }, {  2,  0 }, {  4,  0 },
				{  1,  1 }, {  2,  2 }, {  4,  4 },
				{ -1,  1 }, { -2,  2 }, { -4,  4 },
				{ -1, -1 }, { -2, -2 }, { -4, -4 },
				{  1, -1 }, {  2, -2 }, {  4, -4 }
			};

			
			int i = sector->special - Scroll_North_Slow;
			fixed_t dx = hexenScrollies[i][0] * (FRACUNIT/2);
			fixed_t dy = hexenScrollies[i][1] * (FRACUNIT/2);
			if (!nothinkers) P_CreateScroller(EScroll::sc_floor, dx, dy, -1, int(sector-sectors), 0);
		}
		else if (sector->special >= Carry_East5 &&
					sector->special <= Carry_East35)
		{ // Heretic scroll special
			// Only east scrollers also scroll the texture
			if (!nothinkers) P_CreateScroller(EScroll::sc_floor,
				(-FRACUNIT/2)<<(sector->special - Carry_East5),
				0, -1, int(sector-sectors), 0);
		}
		keepspecial = true;
		break;
	}
	if (!keepspecial) sector->special = 0;
}

//
// P_SpawnSpecials
//
// After the map has been loaded, scan for specials that spawn thinkers
//

void P_SpawnSpecials (void)
{
	sector_t *sector;
	int i;

	P_SetupPortals();

	//	Init special SECTORs.
	sector = sectors;
	for (i = 0; i < numsectors; i++, sector++)
	{
		if (sector->special == 0)
			continue;

		P_InitSectorSpecial(sector, sector->special, false);
	}

#ifndef NO_EDATA
	ProcessEDSectors();
#endif

	
	// Init other misc stuff

	P_SpawnScrollers(); // killough 3/7/98: Add generalized scrollers
	P_SpawnFriction();	// phares 3/12/98: New friction model using linedefs
	P_SpawnPushers();	// phares 3/20/98: New pusher model using linedefs

	TThinkerIterator<ASkyCamCompat> it2;
	ASkyCamCompat *pt2;
	while ((pt2 = it2.Next()))
	{
		P_SpawnSkybox(pt2);
	}

	for (i = 0; i < numlines; i++)
	{
		switch (lines[i].special)
		{
			int s;
			sector_t *sec;

		// killough 3/7/98:
		// support for drawn heights coming from different sector
		case Transfer_Heights:
			{
				sec = lines[i].frontsector;
				if (lines[i].args[1] & 2)
				{
					sec->MoreFlags |= SECF_FAKEFLOORONLY;
				}
				if (lines[i].args[1] & 4)
				{
					sec->MoreFlags |= SECF_CLIPFAKEPLANES;
				}
				if (lines[i].args[1] & 8)
				{
					sec->MoreFlags |= SECF_UNDERWATER;
				}
				else if (forcewater)
				{
					sec->MoreFlags |= SECF_FORCEDUNDERWATER;
				}
				if (lines[i].args[1] & 16)
				{
					sec->MoreFlags |= SECF_IGNOREHEIGHTSEC;
				}
				if (lines[i].args[1] & 32)
				{
					sec->MoreFlags |= SECF_NOFAKELIGHT;
				}
				FSectorTagIterator itr(lines[i].args[0]);
				while ((s = itr.Next()) >= 0)
				{
					sectors[s].heightsec = sec;
					sec->e->FakeFloor.Sectors.Push(&sectors[s]);
					sectors[s].AdjustFloorClip();
				}
				break;
			}

		// killough 3/16/98: Add support for setting
		// floor lighting independently (e.g. lava)
		case Transfer_FloorLight:
			new DLightTransfer (lines[i].frontsector, lines[i].args[0], true);
			break;

		// killough 4/11/98: Add support for setting
		// ceiling lighting independently
		case Transfer_CeilingLight:
			new DLightTransfer (lines[i].frontsector, lines[i].args[0], false);
			break;

		// [Graf Zahl] Add support for setting lighting
		// per wall independently
		case Transfer_WallLight:
			new DWallLightTransfer (lines[i].frontsector, lines[i].args[0], lines[i].args[1]);
			break;

		case Sector_Attach3dMidtex:
			P_Attach3dMidtexLinesToSector(lines[i].frontsector, lines[i].args[0], lines[i].args[1], !!lines[i].args[2]);
			break;

		case Sector_SetLink:
			if (lines[i].args[0] == 0)
			{
				P_AddSectorLinks(lines[i].frontsector, lines[i].args[1], lines[i].args[2], lines[i].args[3]);
			}
			break;

		case Sector_SetPortal:
			// arg 0 = sector tag
			// arg 1 = type
			//	- 0: normal (handled here)
			//	- 1: copy (handled by the portal they copy)
			//	- 2: EE-style skybox (handled by the camera object)
			//  - 3: EE-style flat portal (GZDoom HW renderer only for now)
			//  - 4: EE-style horizon portal (GZDoom HW renderer only for now)
			//  - 5: copy portal to line (GZDoom HW renderer only for now)
			//  - 6: linked portal
			//	other values reserved for later use
			// arg 2 = 0:floor, 1:ceiling, 2:both
			// arg 3 = 0: anchor, 1: reference line
			// arg 4 = for the anchor only: alpha
			if ((lines[i].args[1] == 0 || lines[i].args[1] == 6) && lines[i].args[3] == 0)
			{
				P_SpawnPortal(&lines[i], lines[i].args[0], lines[i].args[2], lines[i].args[4], lines[i].args[1]);
			}
			break;

		case Line_SetPortal:
			P_SpawnLinePortal(&lines[i]);
			break;

		// [RH] ZDoom Static_Init settings
		case Static_Init:
			switch (lines[i].args[1])
			{
			case Init_Gravity:
				{
					double grav = lines[i].Delta().Length() / 100.;
					FSectorTagIterator itr(lines[i].args[0]);
					while ((s = itr.Next()) >= 0)
						sectors[s].gravity = grav;
				}
				break;

			//case Init_Color:
			// handled in P_LoadSideDefs2()

			case Init_Damage:
				{
					int damage = P_AproxDistance (lines[i].dx, lines[i].dy) >> FRACBITS;
					FSectorTagIterator itr(lines[i].args[0]);
					while ((s = itr.Next()) >= 0)
					{
						sector_t *sec = &sectors[s];
						sec->damageamount = damage;
						sec->damagetype = NAME_None;
						if (sec->damageamount < 20)
						{
							sec->leakydamage = 0;
							sec->damageinterval = 32;
						}
						else if (sec->damageamount < 50)
						{
							sec->leakydamage = 5;
							sec->damageinterval = 32;
						}
						else
						{
							sec->leakydamage = 256;
							sec->damageinterval = 1;
						}
					}
				}
				break;

			case Init_SectorLink:
				if (lines[i].args[3] == 0)
					P_AddSectorLinksByID(lines[i].frontsector, lines[i].args[0], lines[i].args[2]);
				break;

			// killough 10/98:
			//
			// Support for sky textures being transferred from sidedefs.
			// Allows scrolling and other effects (but if scrolling is
			// used, then the same sector tag needs to be used for the
			// sky sector, the sky-transfer linedef, and the scroll-effect
			// linedef). Still requires user to use F_SKY1 for the floor
			// or ceiling texture, to distinguish floor and ceiling sky.

			case Init_TransferSky:
				{
					FSectorTagIterator itr(lines[i].args[0]);
					while ((s = itr.Next()) >= 0)
						 sectors[s].sky = (i + 1) | PL_SKYFLAT; 
					break;
				}
			}
			break;
		}
	}
	// [RH] Start running any open scripts on this map
	FBehavior::StaticStartTypedScripts (SCRIPT_Open, NULL, false);
}

////////////////////////////////////////////////////////////////////////////
//
// FRICTION EFFECTS
//
// phares 3/12/98: Start of friction effects

// As the player moves, friction is applied by decreasing the x and y
// velocity values on each tic. By varying the percentage of decrease,
// we can simulate muddy or icy conditions. In mud, the player slows
// down faster. In ice, the player slows down more slowly.
//
// The amount of friction change is controlled by the length of a linedef
// with type 223. A length < 100 gives you mud. A length > 100 gives you ice.
//
// Also, each sector where these effects are to take place is given a
// new special type _______. Changing the type value at runtime allows
// these effects to be turned on or off.
//
// Sector boundaries present problems. The player should experience these
// friction changes only when his feet are touching the sector floor. At
// sector boundaries where floor height changes, the player can find
// himself still 'in' one sector, but with his feet at the floor level
// of the next sector (steps up or down). To handle this, Thinkers are used
// in icy/muddy sectors. These thinkers examine each object that is touching
// their sectors, looking for players whose feet are at the same level as
// their floors. Players satisfying this condition are given new friction
// values that are applied by the player movement code later.

//
// killough 8/28/98:
//
// Completely redid code, which did not need thinkers, and which put a heavy
// drag on CPU. Friction is now a property of sectors, NOT objects inside
// them. All objects, not just players, are affected by it, if they touch
// the sector's floor. Code simpler and faster, only calling on friction
// calculations when an object needs friction considered, instead of doing
// friction calculations on every sector during every tic.
//
// Although this -might- ruin Boom demo sync involving friction, it's the only
// way, short of code explosion, to fix the original design bug. Fixing the
// design bug in Boom's original friction code, while maintaining demo sync
// under every conceivable circumstance, would double or triple code size, and
// would require maintenance of buggy legacy code which is only useful for old
// demos. Doom demos, which are more important IMO, are not affected by this
// change.
//
// [RH] On the other hand, since I've given up on trying to maintain demo
//		sync between versions, these considerations aren't a big deal to me.
//
/////////////////////////////
//
// Initialize the sectors where friction is increased or decreased

static void P_SpawnFriction(void)
{
	int i;
	line_t *l = lines;

	for (i = 0 ; i < numlines ; i++,l++)
	{
		if (l->special == Sector_SetFriction)
		{
			int length;

			if (l->args[1])
			{	// [RH] Allow setting friction amount from parameter
				length = l->args[1] <= 200 ? l->args[1] : 200;
			}
			else
			{
				length = int(l->Delta().Length());
			}

			P_SetSectorFriction (l->args[0], length, false);
			l->special = 0;
		}
	}
}

void P_SetSectorFriction (int tag, int amount, bool alterFlag)
{
	int s;
	double friction, movefactor;

	// An amount of 100 should result in a friction of
	// ORIG_FRICTION (0xE800)
	friction = ((0x1EB8 * amount) / 0x80 + 0xD001) / 65536.;

	// killough 8/28/98: prevent odd situations
	friction = clamp(friction, 0., 1.);

	// The following check might seem odd. At the time of movement,
	// the move distance is multiplied by 'friction/0x10000', so a
	// higher friction value actually means 'less friction'.
	movefactor = FrictionToMoveFactor(friction);

	FSectorTagIterator itr(tag);
	while ((s = itr.Next()) >= 0)
	{
		// killough 8/28/98:
		//
		// Instead of spawning thinkers, which are slow and expensive,
		// modify the sector's own friction values. Friction should be
		// a property of sectors, not objects which reside inside them.
		// Original code scanned every object in every friction sector
		// on every tic, adjusting its friction, putting unnecessary
		// drag on CPU. New code adjusts friction of sector only once
		// at level startup, and then uses this friction value.

		sectors[s].friction = friction;
		sectors[s].movefactor = movefactor;
		if (alterFlag)
		{
			// When used inside a script, the sectors' friction flags
			// can be enabled and disabled at will.
			if (friction == ORIG_FRICTION)
			{
				sectors[s].Flags &= ~SECF_FRICTION;
			}
			else
			{
				sectors[s].Flags |= SECF_FRICTION;
			}
		}
	}
}

double FrictionToMoveFactor(double friction)
{
	double movefactor;

	// [RH] Twiddled these values so that velocity on ice (with
	//		friction 0xf900) is the same as in Heretic/Hexen.
	if (friction >= ORIG_FRICTION)	// ice
									//movefactor = ((0x10092 - friction)*(0x70))/0x158;
		movefactor = (((0x10092 - friction * 65536) * 1024) / 4352 + 568) / 65536.;
	else
		movefactor = (((friction*65536. - 0xDB34)*(0xA)) / 0x80) / 65536.;

	// killough 8/28/98: prevent odd situations
	if (movefactor < 1 / 2048.)
		movefactor = 1 / 2048.;

	return movefactor;
}


//
// phares 3/12/98: End of friction effects
//
////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
//
// PUSH/PULL EFFECT
//
// phares 3/20/98: Start of push/pull effects
//
// This is where push/pull effects are applied to objects in the sectors.
//
// There are four kinds of push effects
//
// 1) Pushing Away
//
//    Pushes you away from a point source defined by the location of an
//    MT_PUSH Thing. The force decreases linearly with distance from the
//    source. This force crosses sector boundaries and is felt w/in a circle
//    whose center is at the MT_PUSH. The force is felt only if the point
//    MT_PUSH can see the target object.
//
// 2) Pulling toward
//
//    Same as Pushing Away except you're pulled toward an MT_PULL point
//    source. This force crosses sector boundaries and is felt w/in a circle
//    whose center is at the MT_PULL. The force is felt only if the point
//    MT_PULL can see the target object.
//
// 3) Wind
//
//    Pushes you in a constant direction. Full force above ground, half
//    force on the ground, nothing if you're below it (water).
//
// 4) Current
//
//    Pushes you in a constant direction. No force above ground, full
//    force if on the ground or below it (water).
//
// The magnitude of the force is controlled by the length of a controlling
// linedef. The force vector for types 3 & 4 is determined by the angle
// of the linedef, and is constant.
//
// For each sector where these effects occur, the sector special type has
// to have the PUSH_MASK bit set. If this bit is turned off by a switch
// at run-time, the effect will not occur. The controlling sector for
// types 1 & 2 is the sector containing the MT_PUSH/MT_PULL Thing.


#define PUSH_FACTOR 128

/////////////////////////////
//
// Add a push thinker to the thinker list

DPusher::DPusher (DPusher::EPusher type, line_t *l, int magnitude, int angle,
				  AActor *source, int affectee)
{
	m_Source = source;
	m_Type = type;
	if (l)
	{
		m_PushVec = l->Delta();
		m_Magnitude = m_PushVec.Length();
	}
	else
	{ // [RH] Allow setting magnitude and angle with parameters
		ChangeValues (magnitude, angle);
	}
	if (source) // point source exist?
	{
		m_Radius = m_Magnitude * 2; // where force goes to zero
	}
	m_Affectee = affectee;
}

int DPusher::CheckForSectorMatch (EPusher type, int tag)
{
	if (m_Type == type && tagManager.SectorHasTag(m_Affectee, tag))
		return m_Affectee;
	else
		return -1;
}


/////////////////////////////
//
// T_Pusher looks for all objects that are inside the _f_radius() of
// the effect.
//
void DPusher::Tick ()
{
	sector_t *sec;
	AActor *thing;
	msecnode_t *node;
	double ht;

	if (!var_pushers)
		return;

	sec = sectors + m_Affectee;

	// Be sure the special sector type is still turned on. If so, proceed.
	// Else, bail out; the sector type has been changed on us.

	if (!(sec->Flags & SECF_PUSH))
		return;

	// For constant pushers (wind/current) there are 3 situations:
	//
	// 1) Affected Thing is above the floor.
	//
	//    Apply the full force if wind, no force if current.
	//
	// 2) Affected Thing is on the ground.
	//
	//    Apply half force if wind, full force if current.
	//
	// 3) Affected Thing is below the ground (underwater effect).
	//
	//    Apply no force if wind, full force if current.
	//
	// Apply the effect to clipped players only for now.
	//
	// In Phase II, you can apply these effects to Things other than players.
	// [RH] No Phase II, but it works with anything having MF2_WINDTHRUST now.

	if (m_Type == p_push)
	{
		// Seek out all pushable things within the force _f_radius() of this
		// point pusher. Crosses sectors, so use blockmap.

		FPortalGroupArray check(FPortalGroupArray::PGA_NoSectorPortals);	// no sector portals because this thing is utterly z-unaware.
		FMultiBlockThingsIterator it(check, m_Source, FLOAT2FIXED(m_Radius));
		FMultiBlockThingsIterator::CheckResult cres;


		while (it.Next(&cres))
		{
			AActor *thing = cres.thing;
			// Normal ZDoom is based only on the WINDTHRUST flag, with the noclip cheat as an exemption.
			bool pusharound = ((thing->flags2 & MF2_WINDTHRUST) && !(thing->flags & MF_NOCLIP));
					
			// MBF allows any sentient or shootable thing to be affected, but players with a fly cheat aren't.
			if (compatflags & COMPATF_MBFMONSTERMOVE)
			{
				pusharound = ((pusharound || (thing->IsSentient()) || (thing->flags & MF_SHOOTABLE)) // Add categories here
					&& (!(thing->player && (thing->flags & (MF_NOGRAVITY))))); // Exclude flying players here
			}

			if ((pusharound) )
			{
				DVector2 pos = m_Source->Vec2To(thing);
				double dist = pos.Length();
				double speed = (m_Magnitude - (dist/2)) / (PUSH_FACTOR * 2);

				// If speed <= 0, you're outside the effective _f_radius(). You also have
				// to be able to see the push/pull source point.

				if ((speed > 0) && (P_CheckSight (thing, m_Source, SF_IGNOREVISIBILITY)))
				{
					DAngle pushangle = pos.Angle();
					if (m_Source->GetClass()->TypeName == NAME_PointPuller) pushangle += 180;  
					thing->Thrust(pushangle, speed);
				}
			}
		}
		return;
	}

	// constant pushers p_wind and p_current

	node = sec->touching_thinglist; // things touching this sector
	for ( ; node ; node = node->m_snext)
	{
		thing = node->m_thing;
		if (!(thing->flags2 & MF2_WINDTHRUST) || (thing->flags & MF_NOCLIP))
			continue;

		sector_t *hsec = sec->GetHeightSec();
		fixedvec3 pos = thing->_f_PosRelative(sec);
		DVector2 pushvel;
		if (m_Type == p_wind)
		{
			if (hsec == NULL)
			{ // NOT special water sector
				if (thing->Z() > thing->floorz) // above ground
				{
					pushvel = m_PushVec; // full force
				}
				else // on ground
				{
					pushvel = m_PushVec / 2; // half force
				}
			}
			else // special water sector
			{
				ht = hsec->floorplane.ZatPointF(pos);
				if (thing->Z() > ht) // above ground
				{
					pushvel = m_PushVec; // full force
				}
				else if (thing->player->viewz < ht) // underwater
				{
					pushvel.Zero(); // no force
				}
				else // wading in water
				{
					pushvel = m_PushVec / 2; // full force
				}
			}
		}
		else // p_current
		{
			const secplane_t *floor;

			if (hsec == NULL)
			{ // NOT special water sector
				floor = &sec->floorplane;
			}
			else
			{ // special water sector
				floor = &hsec->floorplane;
			}
			if (thing->Z() > floor->ZatPointF(pos))
			{ // above ground
				pushvel.Zero(); // no force
			}
			else
			{ // on ground/underwater
				pushvel = m_PushVec; // full force
			}
		}
		thing->Vel += pushvel / PUSH_FACTOR;
	}
}

/////////////////////////////
//
// P_GetPushThing() returns a pointer to an MT_PUSH or MT_PULL thing,
// NULL otherwise.

AActor *P_GetPushThing (int s)
{
	AActor* thing;
	sector_t* sec;

	sec = sectors + s;
	thing = sec->thinglist;

	while (thing &&
		thing->GetClass()->TypeName != NAME_PointPusher &&
		thing->GetClass()->TypeName != NAME_PointPuller)
	{
		thing = thing->snext;
	}
	return thing;
}

/////////////////////////////
//
// Initialize the sectors where pushers are present
//

static void P_SpawnPushers ()
{
	int i;
	line_t *l = lines;
	int s;

	for (i = 0; i < numlines; i++, l++)
	{
		switch (l->special)
		{
		case Sector_SetWind: // wind
		{
			FSectorTagIterator itr(l->args[0]);
			while ((s = itr.Next()) >= 0)
				new DPusher(DPusher::p_wind, l->args[3] ? l : NULL, l->args[1], l->args[2], NULL, s);
			l->special = 0;
			break;
		}

		case Sector_SetCurrent: // current
		{
			FSectorTagIterator itr(l->args[0]);
			while ((s = itr.Next()) >= 0)
				new DPusher(DPusher::p_current, l->args[3] ? l : NULL, l->args[1], l->args[2], NULL, s);
			l->special = 0;
			break;
		}

		case PointPush_SetForce: // push/pull
			if (l->args[0]) {	// [RH] Find thing by sector
				FSectorTagIterator itr(l->args[0]);
				while ((s = itr.Next()) >= 0)
				{
					AActor *thing = P_GetPushThing (s);
					if (thing) {	// No MT_P* means no effect
						// [RH] Allow narrowing it down by tid
						if (!l->args[1] || l->args[1] == thing->tid)
							new DPusher (DPusher::p_push, l->args[3] ? l : NULL, l->args[2],
										 0, thing, s);
					}
				}
			} else {	// [RH] Find thing by tid
				AActor *thing;
				FActorIterator iterator (l->args[1]);

				while ( (thing = iterator.Next ()) )
				{
					if (thing->GetClass()->TypeName == NAME_PointPusher ||
						thing->GetClass()->TypeName == NAME_PointPuller)
					{
						new DPusher (DPusher::p_push, l->args[3] ? l : NULL, l->args[2],
									 0, thing, int(thing->Sector - sectors));
					}
				}
			}
			l->special = 0;
			break;
		}
	}
}

//
// phares 3/20/98: End of Pusher effects
//
////////////////////////////////////////////////////////////////////////////

void sector_t::AdjustFloorClip () const
{
	msecnode_t *node;

	for (node = touching_thinglist; node; node = node->m_snext)
	{
		if (node->m_thing->flags2 & MF2_FLOORCLIP)
		{
			node->m_thing->AdjustFloorClip();
		}
	}
}
