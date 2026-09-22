/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// SimpleObjectIterator
// Implementation of a simple object iterator
// Author: Steven Johnson, September 2001
#include "PreRTS.h"
import engine.debug;	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/ObjectIter.h"

#include "Common/ThingTemplate.h"
#include "GameLogic/Object.h"


/// @todo Doxygenize this file

SimpleObjectIterator::ClumpCompareProc SimpleObjectIterator::theClumpCompareProcs[] =
{
	nullptr,						// "fastest" gets no proc
	SimpleObjectIterator::sortNearToFar,
	SimpleObjectIterator::sortFarToNear,
	SimpleObjectIterator::sortCheapToExpensive,
	SimpleObjectIterator::sortExpensiveToCheap
};

//=============================================================================
SimpleObjectIterator::Clump::Clump()
{
	m_nextClump = nullptr;
}

//=============================================================================
SimpleObjectIterator::Clump::~Clump()
{
}

//=============================================================================
SimpleObjectIterator::SimpleObjectIterator()
{
	m_firstClump = nullptr;
	m_curClump = nullptr;
	m_clumpCount = 0;
}

//=============================================================================
SimpleObjectIterator::~SimpleObjectIterator()
{
	makeEmpty();
}

//=============================================================================
void SimpleObjectIterator::insert(Object *obj, Real numeric)
{
	engine::debug::invariant((obj), "obj", __FILE__, __LINE__, "sorry, no nulls allowed here");

	Clump *clump = newInstance(Clump)();

	clump->m_nextClump = m_firstClump;
	m_firstClump = clump;

	clump->m_obj = obj;
	clump->m_numeric = numeric;

	++m_clumpCount;
}

//=============================================================================
Object *SimpleObjectIterator::nextWithNumeric(Real *num)
{
	Object *obj = nullptr;
	if (num)
		*num = 0.0f;

	if (m_curClump)
	{
		obj = m_curClump->m_obj;
		if (num)
			*num = m_curClump->m_numeric;
		m_curClump = m_curClump->m_nextClump;
	}

	return obj;
}

//=============================================================================
void SimpleObjectIterator::reset()
{
	m_curClump = m_firstClump;
}

//=============================================================================
void SimpleObjectIterator::makeEmpty()
{
	while (m_firstClump)
	{
		Clump *next = m_firstClump->m_nextClump;
		deleteInstance(m_firstClump);
		m_firstClump = next;
		--m_clumpCount;
	}
	engine::debug::invariant((m_clumpCount == 0), "m_clumpCount == 0", __FILE__, __LINE__, "hmm");

	m_firstClump = nullptr;
	m_curClump = nullptr;
	m_clumpCount = 0;
}

//=============================================================================
void SimpleObjectIterator::sort(IterOrderType order)
{
	if (m_clumpCount == 0)
		return;

#ifdef INTENSE_DEBUG
{
	engine::debug::log_info("\n\n---------- BEFORE sort for %d -----------",order);
	for (Clump *p = m_firstClump; p; p = p->m_nextClump)
	{
		engine::debug::log_info("    obj %08lx numeric %f",p->m_obj,p->m_numeric);
	}
}
#endif

	ClumpCompareProc cmpProc = theClumpCompareProcs[order];

	if (!cmpProc)
		return;	// my, that was easy

	// do a basic mergesort, which works nicely for linked lists,
	// and is reasonably efficient (N log N).

  for ( Int n = 1 ; ; n *= 2 )
	{
		Clump *to_do = m_firstClump;
		Clump *tail = nullptr;
		m_firstClump = nullptr;

		Int mergeCount = 0;

		while (to_do)
		{
			++mergeCount;

			Int to_do_count = 0;

			// make two lists of length 'n' (to_do is one, sub is the other)
			Clump *sub = to_do;
			for (Int i = 0; i < n; i++)
			{
				++to_do_count;
				sub = sub->m_nextClump;
				if (!sub)
					break;
			}
			Int subCount = sub ? n : 0;

			// merge the two lists.
			engine::debug::invariant((to_do_count + subCount >= 0), "to_do_count + subCount >= 0", __FILE__, __LINE__, "uhoh");
			while (to_do_count + subCount > 0) {

				engine::debug::invariant((to_do_count + subCount >= 0), "to_do_count + subCount >= 0", __FILE__, __LINE__, "uhoh");

				Clump *tmp;

				// bleah, coalesce into more elegant test case
				if (subCount == 0)
				{
					engine::debug::invariant((to_do_count > 0), "to_do_count > 0", __FILE__, __LINE__, "hmm, expected nonzero to_do_count");
					tmp = to_do;
					to_do = to_do->m_nextClump;
					--to_do_count;
				}
				else if (to_do_count == 0)
				{
					engine::debug::invariant((subCount > 0), "subCount > 0", __FILE__, __LINE__, "hmm, expected nonzero subCount");
					tmp = sub;
					sub = sub->m_nextClump;
					--subCount;
				}
				else if ((*cmpProc)(to_do, sub) <= 0.0f)
				{
					engine::debug::invariant((to_do_count > 0), "to_do_count > 0", __FILE__, __LINE__, "hmm, expected nonzero to_do_count");
					tmp = to_do;
					to_do = to_do->m_nextClump;
					--to_do_count;
				}
				else
				{
					engine::debug::invariant((subCount > 0), "subCount > 0", __FILE__, __LINE__, "hmm, expected nonzero subCount");
					tmp = sub;
					sub = sub->m_nextClump;
					--subCount;
				}
				if (!sub) subCount = 0;
				if (!to_do) to_do_count = 0;

				if (tail)
					tail->m_nextClump = tmp;
				else
					m_firstClump = tmp;
				tail = tmp;
			}

			to_do = sub;
		}
		if (tail)
			tail->m_nextClump = nullptr;

		if (mergeCount <= 1)	// when we have done just one (or none) swap, we're done
			break;
	}

#ifdef INTENSE_DEBUG
{
	engine::debug::log_info("\n\n---------- sort for %d -----------",order);
	for (Clump *p = m_firstClump; p; p = p->m_nextClump)
	{
		engine::debug::log_info("    obj %08lx numeric %f",p->m_obj,p->m_numeric);
	}
}
#endif

	// always reset after sorting, to prevent weirdness
	reset();
}

//-----------------------------------------------------------------------------
Real SimpleObjectIterator::sortNearToFar(Clump *a, Clump *b)
{
	return a->m_numeric - b->m_numeric;
}

//-----------------------------------------------------------------------------
Real SimpleObjectIterator::sortFarToNear(Clump *a, Clump *b)
{
	return b->m_numeric - a->m_numeric;
}

//-----------------------------------------------------------------------------
Real SimpleObjectIterator::sortCheapToExpensive(Clump *a, Clump *b)
{
	return a->m_obj->getTemplate()->friend_getBuildCost() -
				 b->m_obj->getTemplate()->friend_getBuildCost();
}

//-----------------------------------------------------------------------------
Real SimpleObjectIterator::sortExpensiveToCheap(Clump *a, Clump *b)
{
	return b->m_obj->getTemplate()->friend_getBuildCost() -
				 a->m_obj->getTemplate()->friend_getBuildCost();
}

