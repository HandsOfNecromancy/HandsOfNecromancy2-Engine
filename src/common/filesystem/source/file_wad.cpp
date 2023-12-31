/*
** file_wad.cpp
**
**---------------------------------------------------------------------------
** Copyright 1998-2009 Randy Heit
** Copyright 2005-2009 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include <ctype.h>
#include "resourcefile.h"
#include "fs_filesystem.h"
#include "fs_swap.h"

namespace FileSys {
	using namespace byteswap;

struct wadinfo_t
{
	// Should be "IWAD" or "PWAD".
	uint32_t		Magic;
	uint32_t		NumLumps;
	uint32_t		InfoTableOfs;
};

struct wadlump_t
{
	uint32_t		FilePos;
	uint32_t		Size;
	char		Name[8];
};

//==========================================================================
//
// Wad Lump (with console doom LZSS support)
//
//==========================================================================

class FWadFileLump : public FResourceLump
{
public:
	bool Compressed;
	int	Position;
	int Namespace;

	int GetNamespace() const override { return Namespace; }

	int GetFileOffset() override { return Position; }
	FileReader *GetReader() override
	{
		if(!Compressed)
		{
			Owner->GetContainerReader()->Seek(Position, FileReader::SeekSet);
			return Owner->GetContainerReader();
		}
		return NULL;
	}
	int FillCache() override
	{
		if(!Compressed)
		{
			const char * buffer = Owner->GetContainerReader()->GetBuffer();

			if (buffer != NULL)
			{
				// This is an in-memory file so the cache can point directly to the file's data.
				Cache = const_cast<char*>(buffer) + Position;
				RefCount = -1;
				return -1;
			}
		}

		Owner->GetContainerReader()->Seek(Position, FileReader::SeekSet);
		Cache = new char[LumpSize];

		if(Compressed)
		{
			FileReader lzss;
			if (lzss.OpenDecompressor(*Owner->GetContainerReader(), LumpSize, METHOD_LZSS, false, true))
			{
				lzss.Read(Cache, LumpSize);
			}
		}
		else
		{
			auto read = Owner->GetContainerReader()->Read(Cache, LumpSize);
			if (read != LumpSize)
			{
				throw FileSystemException("only read %d of %d bytes", (int)read, (int)LumpSize);
			}
		}

		RefCount = 1;
		return 1;
	}
};

//==========================================================================
//
// Wad file
//
//==========================================================================

class FWadFile : public FResourceFile
{
	TArray<FWadFileLump> Lumps;

	bool IsMarker(int lump, const char *marker);
	void SetNamespace(const char *startmarker, const char *endmarker, namespace_t space, FileSystemMessageFunc Printf, bool flathack=false);
	void SkinHack (FileSystemMessageFunc Printf);

public:
	FWadFile(const char * filename, FileReader &file, StringPool* sp);
	FResourceLump *GetLump(int lump) { return &Lumps[lump]; }
	bool Open(LumpFilterInfo* filter, FileSystemMessageFunc Printf);
};


//==========================================================================
//
// FWadFile::FWadFile
//
// Initializes a WAD file
//
//==========================================================================

FWadFile::FWadFile(const char *filename, FileReader &file, StringPool* sp)
	: FResourceFile(filename, file, sp)
{
}

//==========================================================================
//
// Open it
//
//==========================================================================

bool FWadFile::Open(LumpFilterInfo*, FileSystemMessageFunc Printf)
{
	wadinfo_t header;
	uint32_t InfoTableOfs;
	bool isBigEndian = false; // Little endian is assumed until proven otherwise
	auto wadSize = Reader.GetLength();

	Reader.Read(&header, sizeof(header));
	NumLumps = LittleLong(header.NumLumps);
	InfoTableOfs = LittleLong(header.InfoTableOfs);

	// Check to see if the little endian interpretation is valid
	// This should be sufficient to detect big endian wads.
	if (InfoTableOfs + NumLumps*sizeof(wadlump_t) > (unsigned)wadSize)
	{
		NumLumps = BigLong(header.NumLumps);
		InfoTableOfs = BigLong(header.InfoTableOfs);
		isBigEndian = true;

		// Check again to detect broken wads
		if (InfoTableOfs + NumLumps*sizeof(wadlump_t) > (unsigned)wadSize)
		{
			Printf(FSMessageLevel::Error, "%s: Bad directory offset.\n", FileName);
			return false;
		}
	}

	TArray<wadlump_t> fileinfo(NumLumps, true);
	Reader.Seek (InfoTableOfs, FileReader::SeekSet);
	Reader.Read (fileinfo.Data(), NumLumps * sizeof(wadlump_t));

	Lumps.Resize(NumLumps);

	for(uint32_t i = 0; i < NumLumps; i++)
	{
		char n[9];
		for(int j = 0; j < 8; j++) n[j] = toupper(fileinfo[i].Name[j]);
		n[8] = 0;
		// This needs to be done differently. We cannot simply assume that all lumps where the first character's high bit is set are compressed without verification.
		// This requires explicit toggling for precisely the files that need it.
#if 0
		Lumps[i].Compressed = !(gameinfo.flags & GI_SHAREWARE) && (n[0] & 0x80) == 0x80;
		n[0] &= ~0x80;
#else
		Lumps[i].Compressed = false;
#endif
		Lumps[i].LumpNameSetup(n, stringpool);

		Lumps[i].Owner = this;
		Lumps[i].Position = isBigEndian ? BigLong(fileinfo[i].FilePos) : LittleLong(fileinfo[i].FilePos);
		Lumps[i].LumpSize = isBigEndian ? BigLong(fileinfo[i].Size) : LittleLong(fileinfo[i].Size);
		Lumps[i].Namespace = ns_global;
		Lumps[i].Flags = Lumps[i].Compressed ? LUMPF_COMPRESSED | LUMPF_SHORTNAME : LUMPF_SHORTNAME;

		// Check if the lump is within the WAD file and print a warning if not.
		if (Lumps[i].Position + Lumps[i].LumpSize > wadSize || Lumps[i].Position < 0 || Lumps[i].LumpSize < 0)
		{
			if (Lumps[i].LumpSize != 0)
			{
				Printf(FSMessageLevel::Warning, "%s: Lump %s contains invalid positioning info and will be ignored\n", FileName, Lumps[i].getName());
				Lumps[i].clearName();
			}
			Lumps[i].LumpSize = Lumps[i].Position = 0;
		}
	}

	GenerateHash(); // Do this before the lump processing below.

	SetNamespace("S_START", "S_END", ns_sprites, Printf);
	SetNamespace("F_START", "F_END", ns_flats, Printf, true);
	SetNamespace("C_START", "C_END", ns_colormaps, Printf);
	SetNamespace("A_START", "A_END", ns_acslibrary, Printf);
	SetNamespace("TX_START", "TX_END", ns_newtextures, Printf);
	SetNamespace("V_START", "V_END", ns_strifevoices, Printf);
	SetNamespace("HI_START", "HI_END", ns_hires, Printf);
	SetNamespace("VX_START", "VX_END", ns_voxels, Printf);
	SkinHack(Printf);
	return true;
}

//==========================================================================
//
// IsMarker
//
// (from BOOM)
//
//==========================================================================

inline bool FWadFile::IsMarker(int lump, const char *marker)
{
	if (Lumps[lump].getName()[0] == marker[0])
	{
		return (!strcmp(Lumps[lump].getName(), marker) ||
			(marker[1] == '_' && !strcmp(Lumps[lump].getName() +1, marker)));
	}
	else return false;
}

//==========================================================================
//
// SetNameSpace
//
// Sets namespace information for the lumps. It always looks for the first
// x_START and the last x_END lump, except when loading flats. In this case
// F_START may be absent and if that is the case all lumps with a size of
// 4096 will be flagged appropriately.
//
//==========================================================================

// This class was supposed to be local in the function but GCC
// does not like that.
struct Marker
{
	int markertype;
	unsigned int index;
};

void FWadFile::SetNamespace(const char *startmarker, const char *endmarker, namespace_t space, FileSystemMessageFunc Printf, bool flathack)
{
	bool warned = false;
	int numstartmarkers = 0, numendmarkers = 0;
	unsigned int i;
	TArray<Marker> markers;

	for(i = 0; i < NumLumps; i++)
	{
		if (IsMarker(i, startmarker))
		{
			Marker m = { 0, i };
			markers.Push(m);
			numstartmarkers++;
		}
		else if (IsMarker(i, endmarker))
		{
			Marker m = { 1, i };
			markers.Push(m);
			numendmarkers++;
		}
	}

	if (numstartmarkers == 0)
	{
		if (numendmarkers == 0) return;	// no markers found

		Printf(FSMessageLevel::Warning, "%s: %s marker without corresponding %s found.\n", FileName, endmarker, startmarker);


		if (flathack)
		{
			// We have found no F_START but one or more F_END markers.
			// mark all lumps before the last F_END marker as potential flats.
			unsigned int end = markers[markers.Size()-1].index;
			for(unsigned int ii = 0; ii < end; ii++)
			{
				if (Lumps[ii].LumpSize == 4096)
				{
					// We can't add this to the flats namespace but 
					// it needs to be flagged for the texture manager.
					Printf(FSMessageLevel::DebugNotify, "%s: Marking %s as potential flat\n", FileName, Lumps[ii].getName());
					Lumps[ii].Flags |= LUMPF_MAYBEFLAT;
				}
			}
		}
		return;
	}

	i = 0;
	while (i < markers.Size())
	{
		int start, end;
		if (markers[i].markertype != 0)
		{
			Printf(FSMessageLevel::Warning, "%s: %s marker without corresponding %s found.\n", FileName, endmarker, startmarker);
			i++;
			continue;
		}
		start = i++;

		// skip over subsequent x_START markers
		while (i < markers.Size() && markers[i].markertype == 0)
		{
			Printf(FSMessageLevel::Warning, "%s: duplicate %s marker found.\n", FileName, startmarker);
			i++;
			continue;
		}
		// same for x_END markers
		while (i < markers.Size()-1 && (markers[i].markertype == 1 && markers[i+1].markertype == 1))
		{
			Printf(FSMessageLevel::Warning, "%s: duplicate %s marker found.\n", FileName, endmarker);
			i++;
			continue;
		}
		// We found a starting marker but no end marker. Ignore this block.
		if (i >= markers.Size())
		{
			Printf(FSMessageLevel::Warning, "%s: %s marker without corresponding %s found.\n", FileName, startmarker, endmarker);
			end = NumLumps;
		}
		else
		{
			end = markers[i++].index;
		}

		// we found a marked block
		Printf(FSMessageLevel::DebugNotify, "%s: Found %s block at (%d-%d)\n", FileName, startmarker, markers[start].index, end);
		for(int j = markers[start].index + 1; j < end; j++)
		{
			if (Lumps[j].Namespace != ns_global)
			{
				if (!warned)
				{
					Printf(FSMessageLevel::Warning, "%s: Overlapping namespaces found (lump %d)\n", FileName, j);
				}
				warned = true;
			}
			else if (space == ns_sprites && Lumps[j].LumpSize < 8)
			{
				// sf 26/10/99:
				// ignore sprite lumps smaller than 8 bytes (the smallest possible)
				// in size -- this was used by some dmadds wads
				// as an 'empty' graphics resource
				Printf(FSMessageLevel::DebugWarn, "%s: Skipped empty sprite %s (lump %d)\n", FileName, Lumps[j].getName(), j);
			}
			else
			{
				Lumps[j].Namespace = space;
			}
		}
	}
}


//==========================================================================
//
// W_SkinHack
//
// Tests a wad file to see if it contains an S_SKIN marker. If it does,
// every lump in the wad is moved into a new namespace. Because skins are
// only supposed to replace player sprites, sounds, or faces, this should
// not be a problem. Yes, there are skins that replace more than that, but
// they are such a pain, and breaking them like this was done on purpose.
// This also renames any S_SKINxx lumps to just S_SKIN.
//
//==========================================================================

void FWadFile::SkinHack (FileSystemMessageFunc Printf)
{
	// this being static is not a problem. The only relevant thing is that each skin gets a different number.
	static int namespc = ns_firstskin;
	bool skinned = false;
	bool hasmap = false;
	uint32_t i;

	for (i = 0; i < NumLumps; i++)
	{
		FResourceLump *lump = &Lumps[i];

		if (!strnicmp(lump->getName(), "S_SKIN", 6))
		{ // Wad has at least one skin.
			lump->LumpNameSetup("S_SKIN", nullptr);
			if (!skinned)
			{
				skinned = true;
				uint32_t j;

				for (j = 0; j < NumLumps; j++)
				{
					Lumps[j].Namespace = namespc;
				}
				namespc++;
			}
		}
		// needless to say, this check is entirely useless these days as map names can be more diverse..
		if ((lump->getName()[0] == 'M' &&
			 lump->getName()[1] == 'A' &&
			 lump->getName()[2] == 'P' &&
			 lump->getName()[3] >= '0' && lump->getName()[3] <= '9' &&
			 lump->getName()[4] >= '0' && lump->getName()[4] <= '9' &&
			 lump->getName()[5] == '\0')
			||
			(lump->getName()[0] == 'E' &&
			 lump->getName()[1] >= '0' && lump->getName()[1] <= '9' &&
			 lump->getName()[2] == 'M' &&
			 lump->getName()[3] >= '0' && lump->getName()[3] <= '9' &&
			 lump->getName()[4] == '\0'))
		{
			hasmap = true;
		}
	}
	if (skinned && hasmap)
	{
		Printf(FSMessageLevel::Attention, "%s: The maps will not be loaded because it has a skin.\n", FileName);
		Printf(FSMessageLevel::Attention, "You should remove the skin from the wad to play these maps.\n");
	}
}


//==========================================================================
//
// File open
//
//==========================================================================

FResourceFile *CheckWad(const char *filename, FileReader &file, LumpFilterInfo* filter, FileSystemMessageFunc Printf, StringPool* sp)
{
	char head[4];

	if (file.GetLength() >= 12)
	{
		file.Seek(0, FileReader::SeekSet);
		file.Read(&head, 4);
		file.Seek(0, FileReader::SeekSet);
		if (!memcmp(head, "IWAD", 4) || !memcmp(head, "PWAD", 4))
		{
			auto rf = new FWadFile(filename, file, sp);
			if (rf->Open(filter, Printf)) return rf;

			file = rf->Destroy();
		}
	}
	return NULL;
}

}
