// Tests for the GameOS memory heaps (GameOS/gameos/gameos.cpp): blocks made
// while a heap is current are tracked, released by
// gos_DestroyMemoryHeap(heap, false) and left valid by (heap, true).

#include "mc2test.h"

#include "gameos.hpp"

#include <string.h>

// kept in globals so the compiler cannot elide the allocations
static char* volatile g_b;
static int* volatile g_c;

TEST(heap_tracks_gos_malloc_and_new)
{
    HGOSHEAP heap = gos_CreateMemoryHeap("test");
    size_t bytes = 1;
    DWORD count = 1;
    gos_GetHeapUsage(heap, &bytes, &count);
    CHECK_EQ(bytes, (size_t)0);
    CHECK_EQ(count, (DWORD)0);

    void* a = gos_Malloc(100, heap); // explicit heap
    gos_PushCurrentHeap(heap);
    g_b = new char[50];              // current heap
    g_c = new int(7);
    char* b = g_b;
    int* c = g_c;
    gos_PopCurrentHeap();
    char* d = new char[10];          // no heap
    gos_GetHeapUsage(heap, &bytes, &count);
    CHECK_EQ(count, (DWORD)3);
    CHECK_EQ(bytes, (size_t)(100 + 50 + sizeof(int)));
    gos_WalkMemoryHeap(heap);

    gos_Free(a);
    delete[] b;
    gos_GetHeapUsage(heap, &bytes, &count);
    CHECK_EQ(count, (DWORD)1);
    CHECK_EQ(bytes, sizeof(int));
    delete c;
    delete[] d;
    gos_GetHeapUsage(heap, &bytes, &count);
    CHECK_EQ(count, (DWORD)0);
    gos_DestroyMemoryHeap(heap);
}

TEST(heap_destroy_releases_or_keeps_blocks)
{
    // shouldBeEmpty = false: the blocks go with the heap (no leak, no crash)
    HGOSHEAP owner = gos_CreateMemoryHeap("owner");
    for (int i = 0; i < 100; ++i)
        gos_Malloc(64, owner);
    gos_DestroyMemoryHeap(owner, false);

    // shouldBeEmpty = true: leaked blocks stay usable and can be freed later
    HGOSHEAP leaky = gos_CreateMemoryHeap("leaky");
    char* p = (char*)gos_Malloc(32, leaky);
    strcpy(p, "still here");
    gos_DestroyMemoryHeap(leaky, true);
    CHECK(strcmp(p, "still here") == 0);
    gos_Free(p);
}

TEST(heap_placement_new_with_heap_and_alignment)
{
    HGOSHEAP heap = gos_CreateMemoryHeap("array");
    double* v = new (heap) double[4]; // operator new[](size, HGOSHEAP)
    CHECK(((uintptr_t)v % 16) == 0);
    DWORD count = 0;
    gos_GetHeapUsage(heap, NULL, &count);
    CHECK_EQ(count, (DWORD)1);
    delete[] v;
    gos_GetHeapUsage(heap, NULL, &count);
    CHECK_EQ(count, (DWORD)0);
    gos_DestroyMemoryHeap(heap);
}
