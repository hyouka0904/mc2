// SPDX-License-Identifier: GPL-3.0-only
// Tests for the A* open list (mclib/pqueue).

#include "mc2test.h"

#include "pqueue.h"

#include <algorithm>
#include <map>
#include <random>
#include <vector>

static PQNode node(uint32_t id, int32_t key)
{
    PQNode n;
    n.id = id;
    n.key = key;
    n.row = (int32_t)id / 100;
    n.col = (int32_t)id % 100;
    return n;
}

// Every parent key <= child keys, and find() maps each id to its slot.
static bool heapIsConsistent(PriorityQueue& q)
{
    const int n = q.getNumItems();
    for (int i = 1; i <= n; ++i) {
        if (i > 1 && q.getItem(i / 2)->key > q.getItem(i)->key)
            return false;
        if (q.find(q.getItem(i)->id) != i)
            return false;
    }
    return true;
}

TEST(pqueue_pops_in_key_order)
{
    PriorityQueue q;
    q.init(100);
    const int keys[] = {50, 10, 40, 10, 99, 0, 7, 33};
    for (uint32_t i = 0; i < 8; ++i) {
        PQNode n = node(i, keys[i]);
        CHECK_EQ(q.insert(n), 0);
    }
    CHECK(heapIsConsistent(q));

    int last = -1;
    while (!q.isEmpty()) {
        PQNode out;
        q.remove(out);
        CHECK(out.key >= last);
        last = out.key;
        CHECK_EQ(q.find(out.id), 0); // removed items are no longer found
        CHECK(heapIsConsistent(q));
    }
}

TEST(pqueue_find_returns_zero_for_unknown_ids)
{
    PriorityQueue q;
    q.init(10);
    CHECK_EQ(q.find(5), 0);
    PQNode n = node(5, 1);
    q.insert(n);
    CHECK_EQ(q.find(5), 1);
    CHECK_EQ(q.find(6), 0);
    CHECK_EQ(q.find(0xFFFFFFFF), 0); // the sentinel is never reported
}

TEST(pqueue_change_moves_item_both_ways)
{
    PriorityQueue q;
    q.init(10);
    for (uint32_t i = 1; i <= 5; ++i) {
        PQNode n = node(i, (int32_t)i * 10);
        q.insert(n);
    }
    q.change(q.find(5), 1);    // decrease: becomes the minimum
    CHECK(heapIsConsistent(q));
    CHECK_EQ(q.getItem(1)->id, (uint32_t)5);

    q.change(q.find(5), 1000); // increase: sinks to the bottom
    CHECK(heapIsConsistent(q));
    CHECK_EQ(q.getItem(1)->id, (uint32_t)1);
    CHECK_EQ(q.findByKey(1000, 5), q.find(5));
    CHECK_EQ(q.findByKey(999, 5), 0);
}

TEST(pqueue_reports_full_instead_of_overflowing)
{
    PriorityQueue q;
    q.init(3); // usable capacity is max + 1, matching the original queue
    for (uint32_t i = 0; i < 4; ++i) {
        PQNode n = node(i, (int32_t)i);
        CHECK_EQ(q.insert(n), 0);
    }
    PQNode extra = node(99, 1);
    CHECK_EQ(q.insert(extra), 1);
    CHECK_EQ(q.getNumItems(), 4);
    CHECK(heapIsConsistent(q));
}

TEST(pqueue_clear_resets_index)
{
    PriorityQueue q;
    q.init(1000);
    for (uint32_t i = 0; i < 900; ++i) {
        PQNode n = node(i * 7, (int32_t)i);
        q.insert(n);
    }
    q.clear(); // large clear path
    CHECK(q.isEmpty());
    CHECK_EQ(q.find(7), 0);

    for (uint32_t i = 0; i < 5; ++i) {
        PQNode n = node(i, (int32_t)i);
        q.insert(n);
    }
    q.clear(); // small clear path
    CHECK_EQ(q.find(3), 0);
    PQNode again = node(3, 1);
    CHECK_EQ(q.insert(again), 0);
    CHECK_EQ(q.find(3), 1);
}

// Randomized differential test against a std::map reference model, using
// operations in the same mix as the A* search (insert / relax / pop).
TEST(pqueue_matches_reference_model)
{
    std::mt19937 rng(12345);
    PriorityQueue q;
    q.init(5000);
    std::map<uint32_t, int32_t> model; // id -> key

    for (int step = 0; step < 60000; ++step) {
        const int op = rng() % 10;
        if (op < 5 && (int)model.size() < 5000) {
            // ids clustered like map cells, many collisions in low bits
            const uint32_t id = (rng() % 720) * 1024 + (rng() % 720);
            if (model.count(id))
                continue;
            const int32_t key = (int32_t)(rng() % 100000);
            PQNode n = node(id, key);
            REQUIRE(q.insert(n) == 0);
            model[id] = key;
        } else if (op < 8 && !model.empty()) {
            std::map<uint32_t, int32_t>::iterator it = model.begin();
            std::advance(it, rng() % model.size());
            const int index = q.find(it->first);
            REQUIRE(index != 0);
            REQUIRE(q.getItem(index)->key == it->second);
            const int32_t key = (int32_t)(rng() % 100000);
            q.change(index, key);
            it->second = key;
        } else if (!model.empty()) {
            PQNode out;
            q.remove(out);
            int32_t minKey = 0x7fffffff;
            for (std::map<uint32_t, int32_t>::iterator it = model.begin(); it != model.end(); ++it)
                minKey = std::min(minKey, it->second);
            REQUIRE(out.key == minKey);
            REQUIRE(model.count(out.id) && model[out.id] == out.key);
            model.erase(out.id);
        }
        if (step % 10000 == 0)
            REQUIRE(heapIsConsistent(q));
    }
    CHECK_EQ(q.getNumItems(), (int)model.size());
    CHECK(heapIsConsistent(q));
}
