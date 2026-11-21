//***************************************************************************
//
//	PQueue.h -- Prototype for priority queues
//
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//

#ifndef PQUEUE_H
#define PQUEUE_H

#include <stdint.h>
#include <vector>

//***************************************************************************
// Binary min-heap used as the A* open list by the path finders.
//
// Items live in pqList[1..numItems]; pqList[0] is a sentinel whose key is
// keyMin, which lets upHeap() stop without a bounds check. Besides the heap,
// the queue keeps an id -> heap position index so find() is O(1) instead of
// a linear scan (the path finders call it for every neighbour they relax).
// This requires ids to be unique while queued, which the callers guarantee
// via their OPEN flags.

typedef struct _PQNode {
	int32_t         key;			// sort value
	uint32_t        id;				// hash value for this map position
	int32_t         row;			// HB-specific
	int32_t         col;			// HB-specific
} PQNode;

class PriorityQueue {

	protected:

		std::vector<PQNode>		pqList;
		int32_t					maxItems;		// usable capacity (sentinel excluded)
		int32_t					numItems;
		int32_t					keyMin;

		// Open-addressing hash table: id -> index into pqList. Capacity is a
		// power of two, at least twice maxItems, so probes stay short.
		std::vector<uint32_t>	slotIds;
		std::vector<int32_t>	slotIndex;		// 0 = empty slot
		uint32_t				slotMask;

		void downHeap (int curIndex);
		void upHeap (int curIndex);

		void place (int index, const PQNode& node);
		uint32_t slotFor (uint32_t id) const;
		void indexSet (uint32_t id, int index);
		void indexErase (uint32_t id);
		void indexClear (void);

	public:

		void init (void) {
			maxItems = numItems = 0;
			keyMin = 0;
			slotMask = 0;
		}

		PriorityQueue (void) {
			init();
		}

		// Allocates room for `maxItems` entries. keyMinValue must be lower
		// than every key that will be inserted.
		int init (int maxItems, int keyMinValue = -2000000);

		// Returns 0 on success, 1 if the queue is full.
		int insert (PQNode& item);

		// Pops the item with the smallest key. The queue must not be empty.
		void remove (PQNode& item);

		// Changes the key of the item at itemIndex (as returned by find()).
		void change (int itemIndex, int newValue);

		// Returns the heap index of the item with this id, or 0 if absent.
		int find (unsigned int id);

		// Kept for compatibility; equivalent to find() for items present.
		int findByKey (int32_t key, uint32_t id, int startIndex = 1);

		void clear (void);

		int getNumItems (void) {
			return(numItems);
		}

		bool isEmpty (void) {
			return(numItems == 0);
		}

		PQNode* getItem (int itemIndex) {
			return(&pqList[itemIndex]);
		}

		void destroy (void);

		~PriorityQueue (void) {
			destroy();
		}
};

typedef PriorityQueue* PriorityQueuePtr;

//***************************************************************************

#endif
