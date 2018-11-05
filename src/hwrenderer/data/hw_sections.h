
#ifndef __GL_SECTIONS_H
#define __GL_SECTIONS_H

#include <algorithm>
#include "tarray.h"
#include "r_defs.h"

// Undefine Windows garbage.
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

//==========================================================================
//
//
//
//==========================================================================

struct BoundingRect
{
	double left, top, right, bottom;

	bool contains(const BoundingRect & other)
	{
		return left <= other.left && top <= other.top && right >= other.right && bottom >= other.bottom;
	}

	bool intersects(const BoundingRect & other)
	{
		return !(other.left > right ||
			other.right < left ||
			other.top > bottom ||
			other.bottom < top);
	}

	void Union(const BoundingRect & other)
	{
		if (other.left < left) left = other.left;
		if (other.right > right) right = other.right;
		if (other.top < top) top = other.top;
		if (other.bottom > bottom) bottom = other.bottom;
	}

	double distanceTo(const BoundingRect &other)
	{
		if (intersects(other)) return 0;
		return std::max(std::min(fabs(left - other.right), fabs(right - other.left)),
			std::min(fabs(top - other.bottom), fabs(bottom - other.top)));
	}

	void addVertex(double x, double y)
	{
		if (x < left) left = x;
		if (x > right) right = x;
		if (y < top) top = y;
		if (y > bottom) bottom = y;
	}

	bool operator == (const BoundingRect &other)
	{
		return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
	}
};


struct FSectionLine
{
	vertex_t *start;
	vertex_t *end;
	FSectionLine *partner;
	side_t *sidedef;
};

struct FSectionVertex
{
	vertex_t *vertex;       // index into vertex array
	int qualifier;          // some index to prevent vertices in different groups from being merged together.
};

struct FSection
{
	// tbd: Do we need a list of subsectors here? Ideally the subsectors should not be used anywhere anymore except for finding out where a location is.
	TArrayView<FSectionLine> segments;
	TArrayView<side_t *>	 sides;				// contains all sidedefs, including the internal ones that do not make up the outer shape.
	TArrayView<subsector_t *>	 subsectors;	// contains all subsectors making up this section
	sector_t				*sector;
	FLightNode				*lighthead;			// Light nodes (blended and additive)
	BoundingRect			 bounds;
	int						 vertexindex;		// This is relative to the start of the entire sector's vertex plane data because it needs to be used with different sources.
	int						 vertexcount;
	int						 validcount;
	short					 mapsection;
	char					 hacked;			// 1: is part of a render hack
};

class FSectionContainer
{
public:
	TArray<FSectionLine> allLines;
	TArray<FSection> allSections;
	TArray<side_t *> allSides;
	TArray<subsector_t *> allSubsectors;
	TArray<int> allIndices;

	int *sectionForSubsectorPtr;			// stored inside allIndices
	int *sectionForSidedefPtr;				// also stored inside allIndices;
	int *firstSectionForSectorPtr;			// ditto.
	int *numberOfSectionForSectorPtr;		// ditto.

	FSection *SectionForSubsector(subsector_t *sub)
	{
		return SectionForSubsector(sub->Index());
	}
	FSection *SectionForSubsector(int ssindex)
	{
		return ssindex < 0 ? nullptr : &allSections[sectionForSubsectorPtr[ssindex]];
	}
	int SectionNumForSubsector(subsector_t *sub)
	{
		return SectionNumForSubsector(sub->Index());
	}
	int SectionNumForSubsector(int ssindex)
	{
		return ssindex < 0 ? -1 : sectionForSubsectorPtr[ssindex];
	}
	FSection *SectionForSidedef(side_t *side)
	{
		return SectionForSidedef(side->Index());
	}
	FSection *SectionForSidedef(int sindex)
	{
		return sindex < 0 ? nullptr : &allSections[sectionForSidedefPtr[sindex]];
	}
	int SectionNumForSidedef(side_t *side)
	{
		return SectionNumForSidedef(side->Index());
	}
	int SectionNumForSidedef(int sindex)
	{
		return sindex < 0 ? -1 : sectionForSidedefPtr[sindex];
	}
	TArrayView<FSection> SectionsForSector(sector_t *sec)
	{
		return SectionsForSector(sec->Index());
	}
	TArrayView<FSection> SectionsForSector(int sindex)
	{
		return sindex < 0 ? TArrayView<FSection>(0) : TArrayView<FSection>(&allSections[firstSectionForSectorPtr[sindex]], numberOfSectionForSectorPtr[sindex]);
	}
	int SectionIndex(const FSection *sect)
	{
		return int(sect - allSections.Data());
	}
	void Clear()
	{
		allLines.Clear();
		allSections.Clear();
		allIndices.Clear();
		allSides.Clear();
		allSubsectors.Clear();
	}
	void Reset()
	{
		Clear();
		allLines.ShrinkToFit();
		allSections.ShrinkToFit();
		allIndices.ShrinkToFit();
		allSides.ShrinkToFit();
		allSubsectors.ShrinkToFit();
	}
};


void CreateSections(FSectionContainer &container);

#endif
