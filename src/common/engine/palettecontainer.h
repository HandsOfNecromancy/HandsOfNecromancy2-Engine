#pragma once

#include <stdint.h>
#include "memarena.h"
#include "palentry.h"
#include "files.h"

enum
{
	TRANSLATION_Internal = 0
};


struct FRemapTable
{
	FRemapTable(int count = 256) { NumEntries = count; }

	FRemapTable(const FRemapTable& o) = default;
	FRemapTable& operator=(const FRemapTable& o) = default;

	bool operator==(const FRemapTable& o) const;
	void MakeIdentity();
	bool IsIdentity() const;
	bool AddIndexRange(int start, int end, int pal1, int pal2);
	bool AddColorRange(int start, int end, int r1, int g1, int b1, int r2, int g2, int b2);
	bool AddDesaturation(int start, int end, double r1, double g1, double b1, double r2, double g2, double b2);
	bool AddColourisation(int start, int end, int r, int g, int b);
	bool AddTint(int start, int end, int r, int g, int b, int amount);
	bool AddToTranslation(const char* range);
	bool AddColors(int start, int count, const uint8_t*, int trans_color = 0);

	uint8_t Remap[256];				// For the software renderer
	PalEntry Palette[256];			// The ideal palette this maps to
	int crc32;
	int Index;
	int NumEntries;				// # of elements in this table (usually 256)
	bool Inactive = false;				// This table is inactive and should be treated as if it was passed as NULL
	bool TwodOnly = false;				// Only used for 2D rendering 
	bool ForFont = false;				// Mark font translations because they may require different handling than the ones for sprites-
	bool NoTransparency = false;		// This palette has no transparent index and must be excluded from all treatment for that.

private:
};



// outside facing translation ID
struct FTranslationID
{
public:
	FTranslationID() = default;

private:
	constexpr FTranslationID(int id) : ID(id)
	{
	}
public:
	static constexpr FTranslationID fromInt(int i)
	{
		return FTranslationID(i);
	}
	FTranslationID(const FTranslationID& other) = default;
	FTranslationID& operator=(const FTranslationID& other) = default;
	bool operator !=(FTranslationID other) const
	{
		return ID != other.ID;
	}
	bool operator ==(FTranslationID other) const
	{
		return ID == other.ID;
	}
	constexpr int index() const
	{
		return ID;
	}
	constexpr bool isvalid() const
	{
		return ID >= 0;
	}
private:

	int ID;
};

constexpr FTranslationID NO_TRANSLATION = FTranslationID::fromInt(0);
constexpr FTranslationID INVALID_TRANSLATION = FTranslationID::fromInt(-1);

// A class that initializes unusued pointers to NULL. This is used so that when
// the TAutoGrowArray below is expanded, the new elements will be NULLed.
class FRemapTablePtr
{
public:
	FRemapTablePtr() throw() : Ptr(0) {}
	FRemapTablePtr(FRemapTable* p) throw() : Ptr(p) {}
	FRemapTablePtr(const FRemapTablePtr& p) throw() : Ptr(p.Ptr) {}
	operator FRemapTable* () const throw() { return Ptr; }
	FRemapTablePtr& operator= (FRemapTable* p) throw() { Ptr = p; return *this; }
	FRemapTablePtr& operator= (FRemapTablePtr& p) throw() { Ptr = p.Ptr; return *this; }
	FRemapTable& operator*() const throw() { return *Ptr; }
	FRemapTable* operator->() const throw() { return Ptr; }
private:
	FRemapTable* Ptr = nullptr;
};


enum
{
	TRANSLATION_SHIFT = 16,
	TRANSLATION_MASK = ((1 << TRANSLATION_SHIFT) - 1),
	TRANSLATIONTYPE_MASK = (255 << TRANSLATION_SHIFT)
};

inline constexpr FTranslationID TRANSLATION(uint8_t a, uint32_t b)
{
	return FTranslationID::fromInt((a << TRANSLATION_SHIFT) | b);
}
inline constexpr FTranslationID MakeLuminosityTranslation(int range, uint8_t min, uint8_t max)
{
	// ensure that the value remains positive.
	return FTranslationID::fromInt( (1 << 30) | ((range&0x3fff) << 16) | (min << 8) | max );
}

inline constexpr bool IsLuminosityTranslation(FTranslationID trans)
{
	return trans.index() > 0 && (trans.index() & (1 << 30));
}

inline constexpr bool IsLuminosityTranslation(int trans)
{
	return trans > 0 && (trans & (1 << 30));
}

inline constexpr int GetTranslationType(FTranslationID trans)
{
	assert(!IsLuminosityTranslation(trans));
	return (trans.index() & TRANSLATIONTYPE_MASK) >> TRANSLATION_SHIFT;
}

inline constexpr int GetTranslationIndex(FTranslationID trans)
{
	assert(!IsLuminosityTranslation(trans));
	return (trans.index() & TRANSLATION_MASK);
}

inline constexpr int GetTranslationType(int trans)
{
	assert(!IsLuminosityTranslation(trans));
	return (trans & TRANSLATIONTYPE_MASK) >> TRANSLATION_SHIFT;
}

inline constexpr int GetTranslationIndex(int trans)
{
	assert(!IsLuminosityTranslation(trans));
	return (trans & TRANSLATION_MASK);
}

class PaletteContainer
{
public:
	PalEntry	BaseColors[256];	// non-gamma corrected palette
	PalEntry	RawColors[256];		// colors as read from the game data without the transparancy remap applied
	uint8_t		Remap[256];			// remap original palette indices to in-game indices

	uint8_t		WhiteIndex;			// white in original palette index
	uint8_t		BlackIndex;			// black in original palette index

	bool HasGlobalBrightmap;
	FRemapTable GlobalBrightmap;
	FRemapTable GrayRamp;
	FRemapTable GrayscaleMap;
	FRemapTable IceMap;				// This is used by the texture compositor so it must be globally accessible.
	uint8_t GrayMap[256];

	TArray<FRemapTable*> uniqueRemaps;

private:
	FMemArena remapArena;
	TArray<TAutoGrowArray<FRemapTablePtr, FRemapTable*>> TranslationTables;
public:
	void Init(int numslots, const uint8_t *indexmap);	// This cannot be a constructor!!!
	void SetPalette(const uint8_t* colors, int transparent_index = -1);
	void Clear();
	int DetermineTranslucency(FileReader& file);
	FRemapTable* AddRemap(FRemapTable* remap);
	void UpdateTranslation(FTranslationID trans, FRemapTable* remap);
	FTranslationID AddTranslation(int slot, FRemapTable* remap, int count = 1);
	void CopyTranslation(FTranslationID dest, FTranslationID src);
	FTranslationID StoreTranslation(int slot, FRemapTable* remap);
	FRemapTable* TranslationToTable(int translation) const;
	FRemapTable* TranslationToTable(FTranslationID translation) const
	{
		return TranslationToTable(translation.index());
	}

	void GenerateGlobalBrightmapFromColormap(const uint8_t* cmapdata, int numlevels);

	void PushIdentityTable(int slot)
	{
		AddTranslation(slot, nullptr);
	}

	FRemapTable* GetTranslation(int slot, int index) const
	{
		if (TranslationTables.Size() <= (unsigned)slot) return nullptr;
		return TranslationTables[slot].GetVal(index);
	}

	void ClearTranslationSlot(int slot)
	{
		if (TranslationTables.Size() <= (unsigned)slot) return;
		TranslationTables[slot].Clear();
	}

	unsigned NumTranslations(int slot) const
	{
		if (TranslationTables.Size() <= (unsigned)slot) return 0;
		return TranslationTables[slot].Size();
	}


};

struct LuminosityTranslationDesc
{
	int colorrange;
	int lum_min;
	int lum_max;

	static LuminosityTranslationDesc fromInt(int translation)
	{
		LuminosityTranslationDesc t;
		t.colorrange = (translation >> 16) & 0x3fff;
		t.lum_min = (translation >> 8) & 0xff;
		t.lum_max = translation & 0xff;
		return t;
	}

	static LuminosityTranslationDesc fromID(FTranslationID translation)
	{
		return fromInt(translation.index());
	}
};

extern PaletteContainer GPalette;

