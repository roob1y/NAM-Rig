#pragma once
// Saturation — shared anti-aliased soft-clip kernels for the rig's nonlinear
// stages. Header-only, no JUCE dependency (the measurement suites compile it
// with juce_audio_basics only, like the other blocks).
//
// The cubic soft-clip + 2nd-order ADAA (Parker/Bilbao) lifted verbatim from
// DriveBlock so the DelayBlock tape saturation reuses ONE tested copy. The cubic
// is the cheap choice for 2nd-order ADAA: closed-form F1 and F2 (no dilogarithm),
// odd-symmetric, unit slope at 0, saturates to +/-2/3. 2nd-order matters MORE in
// the delay than in a drive because the saturator sits INSIDE the feedback loop,
// so any aliasing would compound on every repeat.
//
// All math is in DOUBLE: in float the antiderivative subtraction loses precision
// at small signal and crackles. Callers keep the two-sample history (x[n-1],
// x[n-2]) and a peak guard is built in (the x[n]==x[n-2]!=x[n-1] alternation at
// signal peaks would divide by zero -> Inf/NaN that poisons downstream state).

#include <cmath>

namespace nam_rig
{
namespace sat
{

// ---- cubic soft-clip: f, antiderivative F1 (1st-order ADAA), 2nd antiderivative F2 ----
//   f(x)  = x - x^3/3        (|x| <= 1),    sign(x)*2/3                 (|x| > 1)
//   F1(x) = x^2/2 - x^4/12   (|x| <= 1),    (2/3)|x| - 1/4              (|x| > 1)  [even]
//   F2(x) = x^3/6 - x^5/60   (|x| <= 1),    s*((1/3)x^2 - |x|/4 + 1/15) (|x| > 1)  [odd]
inline double cubF(double x)
{
    if (x > 1.0) return 2.0 / 3.0;
    if (x < -1.0) return -2.0 / 3.0;
    return x - x * x * x / 3.0;
}
inline double cubF1(double x)
{
    const double a = std::abs(x);
    if (a <= 1.0) return 0.5 * x * x - x * x * x * x / 12.0;
    return (2.0 / 3.0) * a - 0.25;
}
inline double cubF2(double x)
{
    const double a = std::abs(x);
    if (a <= 1.0) return x * x * x / 6.0 - x * x * x * x * x / 60.0;
    const double s = x < 0.0 ? -1.0 : 1.0;
    return s * ((1.0 / 3.0) * a * a - 0.25 * a + 1.0 / 15.0);
}
// (F2(a)-F2(b))/(a-b) with the L'Hopital limit F1((a+b)/2) for a~=b.
inline double cubD(double a, double b)
{
    const double d = a - b;
    if (std::abs(d) < 1.0e-5) return cubF1(0.5 * (a + b));
    return (cubF2(a) - cubF2(b)) / d;
}
// 2nd-order ADAA of the cubic. x newest, x1 = x[n-1], x2 = x[n-2]. Same guards as
// the DriveBlock copy: degenerate x~=x[n-1] expands via F1/f; the x~=x[n-2] peak
// alternation falls back to well-conditioned 1st-order ADAA over the step (F1,
// not F2 -- cubD would return the wrong scale).
inline double cubicADAA2(double x, double x1, double x2)
{
    const double TOL = 1.0e-5;
    if (std::abs(x - x1) < TOL)
    {
        const double xBar = 0.5 * (x + x2);
        const double delta = xBar - x1;
        if (std::abs(delta) < TOL)
            return cubF(0.5 * (xBar + x1));
        return (2.0 / delta) * (cubF1(xBar) + (cubF2(x1) - cubF2(xBar)) / delta);
    }
    if (std::abs(x - x2) < TOL)
        return (cubF1(x) - cubF1(x1)) / (x - x1);
    return (2.0 / (x - x2)) * (cubD(x, x1) - cubD(x1, x2));
}

} // namespace sat
} // namespace nam_rig
