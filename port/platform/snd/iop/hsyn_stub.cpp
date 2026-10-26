/* hsyn_stub: the unit-test hooks of the native modhsyn (NI6b; every modhsyn function is native since H5R): the call
 * log the M4 setters report to (M4Log) and the LfoShapeEval mock (hsyn::GetStubView). */
#include "hsyn.h"
#include "hsyn_state.h"

namespace hsyn {

static StubView s_stub;
static int32_t s_shape_ret;
static StubCall s_calls[64];
static int s_ncalls;

void StubReset()
{
    s_stub = StubView{};
    s_shape_ret = 0;
    g_shapeMock = false;
    s_ncalls = 0;
}
void GetStubView(StubView *v) { *v = s_stub; }
int GetStubCalls(StubCall *out, int max)
{
    for (int i = 0; i < s_ncalls && i < max && i < 64; i++) out[i] = s_calls[i];
    return s_ncalls;
}
void StubClearCalls() { s_ncalls = 0; }
static void call(uint32_t fn, uint32_t v, int32_t a, int32_t b, int32_t c)
{
    s_stub.m4++;
    if (s_ncalls < 64) s_calls[s_ncalls] = StubCall{fn, a, b, c, v};
    s_ncalls++;
}

/* the unit tests' LfoShapeEval mock (StubSetShape): StepObjStep's handlers tested with a chosen shape value */
bool g_shapeMock;
int32_t ShapeMockEval(uint32_t, uint32_t carry)
{
    s_stub.shape++;
    s_stub.shape_carry = carry;
    return s_shape_ret;
}
void StubSetShape(int32_t ret)
{
    s_shape_ret = ret;
    g_shapeMock = true;
}
void StubShapeOff() { g_shapeMock = false; }

/* the M4 setter log (test view): VelDepth / SetTremoloDepth / SetVibratoDepth / CrossFade / PanBase / PanLaw /
 * UpdateVolume / SetBend / VelShape report their entry while the sd mock is installed (hsyn::UnitTest only) */
void M4Log(uint32_t fn, uint32_t v, int32_t a, int32_t b, int32_t c) { call(fn, v, a, b, c); }

}  // namespace hsyn
