// Runs the self-tests that ship with the original Stuff library
// (mclib/stuff/*_test.cpp). They were compiled into the game but never
// executed; each TestClass() returns false on the first failed assumption.
//
// Only tests with real assertions are listed; the ones the original authors
// stubbed out (Matrix4D, AffineMatrix4D, Ray3D, ...) would pass vacuously,
// and UnitQuaternion::TestClass() is compiled out and always returns false,
// so it is replaced by quaternion_matrix_roundtrip below.

#include "mc2test.h"

#include "stuff/stuffheaders.hpp"

#include <algorithm>
#include <cmath>

using namespace Stuff;

namespace {
// Stuff's container classes allocate through registered class data, so the
// library has to be initialized once before any self-test runs.
struct StuffLibrary {
    StuffLibrary() { Stuff::InitializeClasses(); }
};
void ensureInitialized() { static StuffLibrary lib; }
}

#define STUFF_SELFTEST(Class)                \
    TEST(stuff_##Class)                      \
    {                                        \
        ensureInitialized();                 \
        CHECK(Class::TestClass());           \
    }

// math
STUFF_SELFTEST(Radian)
STUFF_SELFTEST(Degree)
STUFF_SELFTEST(SinCosPair)
STUFF_SELFTEST(Point3D)
STUFF_SELFTEST(Vector3D)
STUFF_SELFTEST(Vector4D)
STUFF_SELFTEST(UnitVector3D)
STUFF_SELFTEST(EulerAngles)
STUFF_SELFTEST(Origin3D)
STUFF_SELFTEST(Random)

// containers and streams
STUFF_SELFTEST(Hash)
STUFF_SELFTEST(MemoryStream)

// Replacement for the compiled-out UnitQuaternion self-test: converting a
// rotation quaternion to a matrix and back must give the same rotation
// (q and -q describe the same rotation).
TEST(stuff_quaternion_matrix_roundtrip)
{
    ensureInitialized();
    const float axes[][3] = {{1,0,0}, {0,1,0}, {0,0,1}, {0.267261f,0.534522f,0.801784f}};
    const float angles[] = {0.3f, 1.2f, -2.5f, 3.0f};
    for (size_t a = 0; a < sizeof(axes)/sizeof(axes[0]); ++a) {
        for (size_t k = 0; k < sizeof(angles)/sizeof(angles[0]); ++k) {
            SinCosPair sc;
            sc = Radian(angles[k] * 0.5f);
            const UnitQuaternion q(axes[a][0]*sc.sine, axes[a][1]*sc.sine, axes[a][2]*sc.sine, sc.cosine);

            LinearMatrix4D m(true);
            m = q;
            UnitQuaternion back;
            back = m;

            float same = 0, flipped = 0;
            for (int i = 0; i < 4; ++i) {
                same = std::max(same, std::fabs(back[i] - q[i]));
                flipped = std::max(flipped, std::fabs(back[i] + q[i]));
            }
            CHECK(std::min(same, flipped) < 1e-4f);
        }
    }
}
