#include "seam.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace cd {
namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

double polyval(const std::vector<double>& c, double t) {
    double v = 0, tp = 1;
    for (double k : c) { v += k * tp; tp *= t; }
    return v;
}

// Least-squares polynomial (ascending coefficients) with iterative outlier rejection.
// NaN entries of `o` are ignored. Empty result = not enough valid points.
std::vector<double> robustFit(const std::vector<double>& t, const std::vector<double>& o, int deg, double tol) {
    const size_t n = t.size();
    std::vector<char> valid(n), w(n);
    size_t nv = 0;
    for (size_t i = 0; i < n; ++i) { valid[i] = !std::isnan(o[i]); nv += valid[i]; }
    if (nv < n * 0.3) return {};
    w = valid;
    std::vector<double> c;
    for (int it = 0; it < 5; ++it) {
        size_t cnt = std::count(w.begin(), w.end(), 1);
        if (cnt < size_t(deg + 3)) return c;
        cv::Mat A(int(cnt), deg + 1, CV_64F), b(int(cnt), 1, CV_64F);
        int r = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!w[i]) continue;
            double tp = 1;
            for (int k = 0; k <= deg; ++k) { A.at<double>(r, k) = tp; tp *= t[i]; }
            b.at<double>(r, 0) = o[i];
            ++r;
        }
        cv::Mat x;
        cv::solve(A, b, x, cv::DECOMP_SVD);
        c.assign(deg + 1, 0.0);
        for (int k = 0; k <= deg; ++k) c[k] = x.at<double>(k, 0);
        for (size_t i = 0; i < n; ++i) w[i] = valid[i] && std::fabs(o[i] - polyval(c, t[i])) < tol;
    }
    return c;
}

// Robust local line fit of `e` (NaN = missing) in a sliding window of `win` samples (rows). The window is kept
// inside [0, N), so rows near the ends are fitted from their one-sided neighbourhood. Points further than `tol`
// from the line are dropped and the line is refitted, so a short bump (a defect) does not drag the reference with
// it, while a slowly varying offset (pose, curvature, imprecise control points) is followed. NaN = not enough data.
std::vector<double> localFit(const std::vector<double>& e, int win, double tol) {
    const int N = int(e.size());
    win = std::min(win, N);
    std::vector<double> f(N, kNaN);
    std::vector<char> in(win);
    for (int i = 0; i < N; ++i) {
        const int a = std::clamp(i - win / 2, 0, N - win);
        std::fill(in.begin(), in.end(), 1);
        double A = 0, B = 0;
        bool ok = false;
        for (int it = 0; it < 4; ++it) {
            double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (int j = a; j < a + win; ++j) {
                if (!in[j - a] || std::isnan(e[j])) continue;
                const double x = j - i;
                sw += 1; sx += x; sy += e[j]; sxx += x * x; sxy += x * e[j];
            }
            const double det = sw * sxx - sx * sx;
            if (sw < win * 0.3 || det < 1e-9) { ok = false; break; }
            B = (sw * sxy - sx * sy) / det;
            A = (sy - B * sx) / sw;
            ok = true;
            for (int j = a; j < a + win; ++j) in[j - a] = !std::isnan(e[j]) && std::fabs(e[j] - (A + B * (j - i))) < tol;
        }
        if (ok) f[i] = A;
    }
    return f;
}

// Search axis resampled at 1 px steps: position, unit tangent and unit normal for every sample.
struct Curve {
    std::vector<cv::Point2d> P, U, Nrm;
    std::vector<double> ctrlArc;     // arc length (= sample index) at every control point
    double length = 0;
    int size() const { return int(P.size()); }
};

// Catmull-Rom spline through the control points (a straight line for two points), resampled by arc length.
Curve buildCurve(const std::vector<cv::Point2d>& c) {
    const int n = int(c.size());
    std::vector<cv::Point2d> dense;
    std::vector<size_t> ctrlIdx;
    for (int s = 0; s + 1 < n; ++s) {
        ctrlIdx.push_back(dense.size());
        const cv::Point2d p0 = c[s > 0 ? s - 1 : s], p1 = c[s], p2 = c[s + 1], p3 = c[s + 2 < n ? s + 2 : s + 1];
        const int steps = std::max(8, int(std::hypot(p2.x - p1.x, p2.y - p1.y) / 2));
        for (int k = 0; k < steps; ++k) {
            const double t = double(k) / steps, t2 = t * t, t3 = t2 * t;
            dense.push_back(0.5 * (2.0 * p1 + (p2 - p0) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                                   (3.0 * p1 - p0 - 3.0 * p2 + p3) * t3));
        }
    }
    ctrlIdx.push_back(dense.size());
    dense.push_back(c.back());

    std::vector<double> cum(dense.size(), 0.0);
    for (size_t i = 1; i < dense.size(); ++i)
        cum[i] = cum[i - 1] + std::hypot(dense[i].x - dense[i - 1].x, dense[i].y - dense[i - 1].y);

    Curve cv_;
    cv_.length = cum.back();
    for (size_t k : ctrlIdx) cv_.ctrlArc.push_back(cum[k]);
    const int N = int(cv_.length);
    cv_.P.resize(N);
    size_t j = 0;
    for (int i = 0; i < N; ++i) {
        while (j + 2 < dense.size() && cum[j + 1] < i) ++j;
        const double seg = cum[j + 1] - cum[j];
        const double f = seg > 1e-9 ? std::clamp((i - cum[j]) / seg, 0.0, 1.0) : 0.0;
        cv_.P[i] = dense[j] + (dense[j + 1] - dense[j]) * f;
    }
    cv_.U.resize(N);
    cv_.Nrm.resize(N);
    const int k = 3;
    for (int i = 0; i < N; ++i) {
        cv::Point2d d = cv_.P[std::min(i + k, N - 1)] - cv_.P[std::max(i - k, 0)];
        const double l = std::max(std::hypot(d.x, d.y), 1e-9);
        cv_.U[i] = d * (1.0 / l);
        cv_.Nrm[i] = cv::Point2d(-cv_.U[i].y, cv_.U[i].x);
    }
    return cv_;
}

// Samples `src` (crop whose top-left is `org` in original px) along the curve: row i = cross-section at
// sample i, columns = offsets -hw..hw from the (optionally shifted) line centre.
cv::Mat sampleBand(const cv::Mat& src, cv::Point2d org, const Curve& cu, const std::vector<double>* center, int hw) {
    const int N = cu.size(), W = 2 * hw + 1;
    cv::Mat mx(N, W, CV_32F), my(N, W, CV_32F);
    for (int i = 0; i < N; ++i) {
        const double c = center ? (*center)[i] : 0.0;
        float* px = mx.ptr<float>(i);
        float* py = my.ptr<float>(i);
        for (int j = 0; j < W; ++j) {
            const double off = (j - hw) + c;
            px[j] = float(cu.P[i].x + cu.Nrm[i].x * off - org.x);
            py[j] = float(cu.P[i].y + cu.Nrm[i].y * off - org.y);
        }
    }
    cv::Mat out;
    cv::remap(src, out, mx, my, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

// Per-row position (offset from the sampling centre) of the brightest ridge, sub-pixel by a 5-sample centroid.
std::vector<double> peakOffsets(const cv::Mat& v, int hw, double minVal) {
    std::vector<double> o(v.rows, kNaN);
    for (int i = 0; i < v.rows; ++i) {
        const float* row = v.ptr<float>(i);
        int k = int(std::max_element(row, row + v.cols) - row);
        if (row[k] < minVal || k < 2 || k >= v.cols - 2) continue;
        double sw = 0, s = 0;
        for (int j = k - 2; j <= k + 2; ++j) {
            double wj = std::max(0.0f, row[j]);
            sw += wj;
            s += (j - hw) * wj;
        }
        o[i] = s / std::max(sw, 1e-6);
    }
    return o;
}

// Per-row position of the strongest step (max signed derivative across the row), sub-pixel by a parabola.
// polarity: +1 = brighter at larger offsets, -1 = darker, 0 = either. A row without a step of at least
// `minContrast` gray levels between the 3 px on each side gives NaN.
std::vector<double> stepOffsets(const cv::Mat& v, int hw, double minContrast, int polarity) {
    std::vector<double> o(v.rows, kNaN);
    const int W = v.cols;
    std::vector<double> d(W, 0.0);
    for (int i = 0; i < v.rows; ++i) {
        const float* row = v.ptr<float>(i);
        for (int j = 1; j < W - 1; ++j) {
            const double x = row[j + 1] - row[j - 1];
            d[j] = polarity ? polarity * x : std::fabs(x);
        }
        int k = 5;
        for (int j = 5; j <= W - 6; ++j)
            if (d[j] > d[k]) k = j;
        const double lo = (row[k - 4] + row[k - 3] + row[k - 2]) / 3.0, hi = (row[k + 2] + row[k + 3] + row[k + 4]) / 3.0;
        const double contrast = polarity ? polarity * (hi - lo) : std::fabs(hi - lo);
        if (d[k] <= 0 || contrast < minContrast) continue;
        const double a = d[k - 1], b = d[k], c = d[k + 1], den = a - 2 * b + c;
        const double sub = std::fabs(den) > 1e-9 ? std::clamp(0.5 * (a - c) / den, -1.0, 1.0) : 0.0;
        o[i] = (k - hw) + sub;
    }
    return o;
}

} // namespace

SeamResult measureSeam(const cv::Mat& bgr, const SeamParams& p) {
    auto t0 = std::chrono::steady_clock::now();
    SeamResult res;
    res.enabled = p.enabled;
    auto done = [&]() {
        res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return res;
    };
    if (!p.enabled || bgr.empty() || p.pts.size() < 2) return done();

    const Curve cu = buildCurve(p.pts);
    const int N = cu.size();
    if (cu.length < 50) return done();
    const double L = cu.length;
    res.rowPos.resize(N);
    res.rowDev.assign(N, std::numeric_limits<float>::quiet_NaN());
    res.rowState.assign(N, 3);
    res.rowTol.assign(N, 0.f);
    res.rowEdge.assign(N, cv::Point2f());
    res.rowRef.assign(N, cv::Point2f());
    for (int i = 0; i < N; ++i) res.rowPos[i] = cv::Point2f(cu.P[i]);
    for (int i = 0; i < N; i += 4) {
        res.path.emplace_back(cu.P[i]);
        res.pathNormal.emplace_back(cu.Nrm[i]);
    }

    const bool step = p.mode == SeamParams::Step;
    const int hwEdge = 12;
    const int hw1 = std::max(8, int(p.bandHalf));
    const int pad = hw1 + hwEdge + 8;

    cv::Point2d lo = cu.P[0], hi = cu.P[0];
    for (const auto& q : cu.P) {
        lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
        hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
    }
    cv::Rect box(int(std::floor(lo.x)) - pad, int(std::floor(lo.y)) - pad, int(std::ceil(hi.x - lo.x)) + 2 * pad,
                 int(std::ceil(hi.y - lo.y)) + 2 * pad);
    box &= cv::Rect(0, 0, bgr.cols, bgr.rows);
    if (box.width < 16 || box.height < 16) return done();
    const cv::Point2d org(box.x, box.y);

    cv::Mat g8, g32, g1, bg, ridge, gE;
    cv::cvtColor(bgr(box), g8, cv::COLOR_BGR2GRAY);
    g8.convertTo(g32, CV_32F);
    cv::GaussianBlur(g32, g1, cv::Size(), 1.2);
    if (!step) {
        cv::GaussianBlur(g1, bg, cv::Size(), 6.0);
        ridge = g1 - bg;                               // thin bright line response
    }
    cv::GaussianBlur(g32, gE, cv::Size(), 0.8);        // profile for the half-maximum edges / step position

    std::vector<double> t(N);
    for (int i = 0; i < N; ++i) t[i] = i / L;

    // pass 1: coarse position inside the wide band. A straight seam is a line; any other axis is a free curve.
    const int coarseDeg = p.pts.size() == 2 && !step ? 1 : 3;
    std::vector<double> o1 = step ? stepOffsets(sampleBand(g1, org, cu, nullptr, hw1), hw1, p.minContrast, p.polarity)
                                  : peakOffsets(sampleBand(ridge, org, cu, nullptr, hw1), hw1, 6.0);
    std::vector<double> c1 = robustFit(t, o1, coarseDeg, 3.0);
    if (c1.empty()) return done();
    std::vector<double> cen(N);
    for (int i = 0; i < N; ++i) cen[i] = polyval(c1, t[i]);

    // pass 2: precise edge position(s) around the coarse line
    cv::Mat g = sampleBand(gE, org, cu, &cen, hwEdge);
    const int W = g.cols;
    std::vector<std::vector<double>> edges;            // Ridge: left+right edge at half height; Step: the step
    if (step) {
        std::vector<double> e = stepOffsets(g, hwEdge, p.minContrast, p.polarity);
        for (int i = 0; i < N; ++i)
            if (!std::isnan(e[i])) e[i] += cen[i];
        edges.push_back(std::move(e));
    } else {
        std::vector<double> eL(N, kNaN), eR(N, kNaN);
        for (int i = 0; i < N; ++i) {
            const float* row = g.ptr<float>(i);
            float border[6] = {row[0], row[1], row[2], row[W - 3], row[W - 2], row[W - 1]};
            std::sort(border, border + 6);
            const double base = 0.5 * (border[2] + border[3]);
            int pos = int(std::max_element(row, row + W) - row);
            const double pk = row[pos];
            if (pk - base < p.minContrast || pos < 3 || pos > W - 4) continue;
            const double h = base + 0.5 * (pk - base);
            int a = pos, b = pos;
            while (a > 0 && row[a] > h) --a;
            while (b < W - 1 && row[b] > h) ++b;
            if (row[a] > h || row[b] > h) continue;    // line wider than the window
            eL[i] = (a - hwEdge) + (h - row[a]) / (row[a + 1] - row[a] + 1e-6) + cen[i];
            eR[i] = (b - 1 - hwEdge) + (row[b - 1] - h) / (row[b - 1] - row[b] + 1e-6) + cen[i];
        }
        edges.push_back(std::move(eL));
        edges.push_back(std::move(eR));
    }

    // skipped segments: their rows (plus a margin for the pose variation) are neither measured nor used in the fits
    std::vector<char> skipRow(N, 0);
    for (int sg : p.skip) {
        if (sg < 0 || sg + 1 >= int(cu.ctrlArc.size())) continue;
        const int a = std::max(0, int(std::floor(cu.ctrlArc[sg] - p.skipMargin)));
        const int b = std::min(N - 1, int(std::ceil(cu.ctrlArc[sg + 1] + p.skipMargin)));
        for (int i = a; i <= b; ++i) skipRow[i] = 1;
    }
    for (auto& e : edges)
        for (int i = 0; i < N; ++i)
            if (skipRow[i]) e[i] = kNaN;

    // deviation of each edge from its own smooth curve (the label is on a cylinder: curvature is allowed)
    const int deg = std::clamp(p.fitDeg > 0 ? p.fitDeg : (step ? 4 : 2), 1, 6);
    const double fitTol = step ? std::max(0.6, p.tolMid) : 1.5;
    // reference curve per edge: Ridge = one polynomial over the whole line, Step = local robust line (sliding window)
    std::vector<std::vector<double>> ref;
    for (const auto& e : edges) {
        std::vector<double> r(N, kNaN);
        if (step) {
            r = localFit(e, std::max(15, int(p.smoothWin)), fitTol);
        } else {
            const std::vector<double> c = robustFit(t, e, deg, fitTol);
            if (c.empty()) return done();
            for (int i = 0; i < N; ++i) r[i] = polyval(c, t[i]);
        }
        ref.push_back(std::move(r));
    }

    int evalRows = 0, validRows = 0, gapRun = 0, gapMax = 0;
    for (int i = 0; i < N; ++i) {
        {
            const double e0 = std::min(t[i], 1.0 - t[i]);
            res.rowTol[i] = float(std::max(0.05, e0 < p.edgeFrac ? p.tolEdge : p.tolMid));
        }
        if (skipRow[i]) { res.rowState[i] = 1; continue; }
        if (t[i] < p.endIgnore || t[i] > 1.0 - p.endIgnore) { res.rowState[i] = 2; continue; }
        ++evalRows;
        bool ok = true;
        for (size_t k = 0; k < edges.size(); ++k) ok = ok && !std::isnan(edges[k][i]) && !std::isnan(ref[k][i]);
        if (!ok) { gapMax = std::max(gapMax, ++gapRun); continue; }
        gapRun = 0;
        ++validRows;
        res.rowState[i] = 0;
        const double e = std::min(t[i], 1.0 - t[i]);
        const double tol = std::max(0.05, e < p.edgeFrac ? p.tolEdge : p.tolMid);
        double centre = 0, refc = 0;
        for (size_t k = 0; k < edges.size(); ++k) {
            refc += ref[k][i] / edges.size();
            const double dev = edges[k][i] - ref[k][i];
            if (std::isnan(res.rowDev[i]) || std::fabs(dev) > std::fabs(res.rowDev[i])) res.rowDev[i] = float(dev);
            const double ratio = std::fabs(dev) / tol;
            const cv::Point2f pt(float(cu.P[i].x + cu.Nrm[i].x * edges[k][i]), float(cu.P[i].y + cu.Nrm[i].y * edges[k][i]));
            if (ratio > 1.0) res.badPts.push_back(pt);
            if (ratio > res.ratio) {
                res.ratio = ratio;
                res.maxDev = std::fabs(dev);
                res.worstT = t[i];
            }
            centre += edges[k][i] / edges.size();
        }
        res.rowEdge[i] = cv::Point2f(float(cu.P[i].x + cu.Nrm[i].x * centre), float(cu.P[i].y + cu.Nrm[i].y * centre));
        res.rowRef[i] = cv::Point2f(float(cu.P[i].x + cu.Nrm[i].x * refc), float(cu.P[i].y + cu.Nrm[i].y * refc));
        if (i % 4 == 0)
            res.centerPts.emplace_back(float(cu.P[i].x + cu.Nrm[i].x * centre), float(cu.P[i].y + cu.Nrm[i].y * centre));
    }
    {
        std::vector<float> a;
        for (int i = 0; i < N; ++i)
            if (res.rowState[i] == 0) a.push_back(std::fabs(res.rowDev[i]));
        if (a.size() > 20) {
            std::nth_element(a.begin(), a.begin() + a.size() * 95 / 100, a.end());
            res.noise = a[a.size() * 95 / 100];
        }
    }
    res.coverage = evalRows ? double(validRows) / evalRows : 0.0;
    res.longestGap = evalRows ? double(gapMax) / evalRows : 0.0;
    res.found = res.coverage >= 0.5;
    if (!res.found) {                                   // no line in this frame: nothing to judge here
        res.ratio = res.maxDev = res.worstT = 0;
        res.centerPts.clear();
        res.badPts.clear();
    } else {
        res.gapReject = res.longestGap > p.maxGap;
        res.reject = res.ratio > 1.0 || res.gapReject;
    }
    return done();
}

} // namespace cd
