//***************************************************************************
//
//	PQueue.cpp -- Implementation of priority queues
//
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//

#include "pqueue.h"

#include <assert.h>

static const uint32_t SENTINEL_ID = 0xFFFFFFFF;

//***************************************************************************
// Class PriorityQueue
//***************************************************************************

int PriorityQueue::init (int max, int keyMinValue) {

	assert(max > 0);
	// The original implementation accepted max + 1 items (and overflowed its
	// buffer on the next insert); keep the same usable capacity, safely.
	maxItems = max + 1;
	numItems = 0;
	keyMin = keyMinValue;

	// Index 0 is the sentinel, items occupy 1..maxItems.
	pqList.assign(maxItems + 1, PQNode());
	pqList[0].key = keyMin;
	pqList[0].id = SENTINEL_ID;

	uint32_t capacity = 16;
	while (capacity < (uint32_t)maxItems * 2)
		capacity <<= 1;
	slotIds.assign(capacity, 0);
	slotIndex.assign(capacity, 0);
	slotMask = capacity - 1;
	return(0);
}

//---------------------------------------------------------------------------
// id -> heap index table

uint32_t PriorityQueue::slotFor (uint32_t id) const {

	// Fibonacci hashing spreads the (often sequential) map cell ids.
	uint32_t slot = (id * 2654435769u) & slotMask;
	while (slotIndex[slot] != 0 && slotIds[slot] != id)
		slot = (slot + 1) & slotMask;
	return(slot);
}

void PriorityQueue::indexSet (uint32_t id, int index) {

	const uint32_t slot = slotFor(id);
	slotIds[slot] = id;
	slotIndex[slot] = index;
}

void PriorityQueue::indexErase (uint32_t id) {

	uint32_t slot = slotFor(id);
	if (slotIndex[slot] == 0)
		return;

	// Backward-shift deletion keeps every remaining entry reachable from its
	// home slot without tombstones.
	uint32_t next = (slot + 1) & slotMask;
	while (slotIndex[next] != 0) {
		const uint32_t home = (slotIds[next] * 2654435769u) & slotMask;
		// Move `next` into the hole unless its home lies cyclically in (slot, next].
		const bool homeBetween = (slot <= next) ? (home > slot && home <= next)
												: (home > slot || home <= next);
		if (!homeBetween) {
			slotIds[slot] = slotIds[next];
			slotIndex[slot] = slotIndex[next];
			slot = next;
		}
		next = (next + 1) & slotMask;
	}
	slotIndex[slot] = 0;
}

void PriorityQueue::indexClear (void) {

	slotIndex.assign(slotIndex.size(), 0);
}

//---------------------------------------------------------------------------

inline void PriorityQueue::place (int index, const PQNode& node) {

	pqList[index] = node;
	indexSet(node.id, index);
}

//---------------------------------------------------------------------------

void PriorityQueue::upHeap (int curIndex) {

	const PQNode startNode = pqList[curIndex];
	const int32_t stopKey = startNode.key;

	//--------------------
	// sort up the heap... (the sentinel at index 0 stops the loop)
	while (pqList[curIndex/2].key >= stopKey) {
		place(curIndex, pqList[curIndex/2]);
		curIndex /= 2;
	}
	place(curIndex, startNode);
}

//---------------------------------------------------------------------------

int PriorityQueue::insert (PQNode& item) {

	if (numItems >= maxItems)
		return(1);

	assert(item.id != SENTINEL_ID);
	assert(item.key > keyMin);
	assert(find(item.id) == 0 && "ids must be unique while queued");

	pqList[++numItems] = item;
	upHeap(numItems);
	return(0);
}

//---------------------------------------------------------------------------

void PriorityQueue::downHeap (int curIndex) {

	//----------------------------------
	// Start at the top from curIndex...
	const PQNode startNode = pqList[curIndex];
	const int32_t stopKey = startNode.key;

	//----------------------
	// Sort down the heap...
	while (curIndex <= numItems/2) {
		int nextIndex = curIndex << 1;
		if ((nextIndex < numItems) && (pqList[nextIndex].key > pqList[nextIndex + 1].key))
			nextIndex++;
		if (stopKey <= pqList[nextIndex].key)
			break;
		place(curIndex, pqList[nextIndex]);
		curIndex = nextIndex;
	}
	place(curIndex, startNode);
}

//---------------------------------------------------------------------------

void PriorityQueue::remove (PQNode& item) {

	assert(numItems > 0);
	item = pqList[1];
	indexErase(item.id);

	const PQNode last = pqList[numItems--];
	if (numItems > 0) {
		pqList[1] = last;
		downHeap(1);
	}
}

//---------------------------------------------------------------------------

void PriorityQueue::change (int itemIndex, int newValue) {

	assert(itemIndex >= 1 && itemIndex <= numItems);
	if (newValue > pqList[itemIndex].key) {
		pqList[itemIndex].key = newValue;
		downHeap(itemIndex);
	}
	else if (newValue < pqList[itemIndex].key) {
		pqList[itemIndex].key = newValue;
		upHeap(itemIndex);
	}
}

//---------------------------------------------------------------------------

int PriorityQueue::find (unsigned int id) {

	if (numItems == 0)
		return(0);
	return(slotIndex[slotFor(id)]);
}

//---------------------------------------------------------------------------

int PriorityQueue::findByKey (int32_t key, uint32_t id, int /*startIndex*/) {

	const int index = find(id);
	return (index != 0 && pqList[index].key == key) ? index : 0;
}

//---------------------------------------------------------------------------

void PriorityQueue::clear (void) {

	// Called before every path search: erasing the queued ids is cheaper
	// than wiping the whole table when only a few items are left.
	if ((uint32_t)numItems * 4 < slotIndex.size()) {
		for (int i = 1; i <= numItems; i++)
			indexErase(pqList[i].id);
	}
	else {
		indexClear();
	}
	numItems = 0;
}

//---------------------------------------------------------------------------

void PriorityQueue::destroy (void) {

	std::vector<PQNode>().swap(pqList);
	std::vector<uint32_t>().swap(slotIds);
	std::vector<int32_t>().swap(slotIndex);
	maxItems = 0;
	numItems = 0;
	slotMask = 0;
}

//***************************************************************************
