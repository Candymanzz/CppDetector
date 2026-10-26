#include "seam.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace cas {
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

// Samples `src` (crop whose top-left is `org` in original px) along the seam: row i = cross-section at
// distance i from p0, columns = offsets -hw..hw from the (optionally shifted) line centre.
cv::Mat sampleBand(const cv::Mat& src, cv::Point2d org, cv::Point2d p0, cv::Point2d u, cv::Point2d nrm,
                   int N, const std::vector<double>* center, int hw) {
    const int W = 2 * hw + 1;
    cv::Mat mx(N, W, CV_32F), my(N, W, CV_32F);
    for (int i = 0; i < N; ++i) {
        const double c = center ? (*center)[i] : 0.0;
        float* px = mx.ptr<float>(i);
        float* py = my.ptr<float>(i);
        for (int j = 0; j < W; ++j) {
            const double off = (j - hw) + c;
            px[j] = float(p0.x + u.x * i + nrm.x * off - org.x);
            py[j] = float(p0.y + u.y * i + nrm.y * off - org.y);
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

} // namespace

SeamResult measureSeam(const cv::Mat& bgr, const SeamParams& p) {
    auto t0 = std::chrono::steady_clock::now();
    SeamResult res;
    res.enabled = p.enabled;
    auto done = [&]() {
        res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return res;
    };
    if (!p.enabled || bgr.empty()) return done();

    const cv::Point2d d = p.p1 - p.p0;
    const double L = std::hypot(d.x, d.y);
    if (L < 50) return done();
    const cv::Point2d u = d * (1.0 / L), nrm(-u.y, u.x);
    const int N = int(L);
    const int hwEdge = 12;
    const int hw1 = std::max(4, int(p.bandHalf));
    const int pad = hw1 + hwEdge + 8;

    cv::Rect box(int(std::floor(std::min(p.p0.x, p.p1.x))) - pad, int(std::floor(std::min(p.p0.y, p.p1.y))) - pad,
                 int(std::ceil(std::abs(d.x))) + 2 * pad, int(std::ceil(std::abs(d.y))) + 2 * pad);
    box &= cv::Rect(0, 0, bgr.cols, bgr.rows);
    if (box.width < 16 || box.height < 16) return done();
    const cv::Point2d org(box.x, box.y);

    cv::Mat g8, g32, g1, bg, ridge, gE;
    cv::cvtColor(bgr(box), g8, cv::COLOR_BGR2GRAY);
    g8.convertTo(g32, CV_32F);
    cv::GaussianBlur(g32, g1, cv::Size(), 1.2);
    cv::GaussianBlur(g1, bg, cv::Size(), 6.0);
    ridge = g1 - bg;                                   // thin bright line response
    cv::GaussianBlur(g32, gE, cv::Size(), 0.8);        // profile for the half-maximum edges

    std::vector<double> t(N);
    for (int i = 0; i < N; ++i) t[i] = i / L;

    // pass 1: coarse straight line inside the wide band
    cv::Mat v = sampleBand(ridge, org, p.p0, u, nrm, N, nullptr, hw1);
    std::vector<double> o1 = peakOffsets(v, hw1, 6.0);
    std::vector<double> c1 = robustFit(t, o1, 1, 3.0);
    if (c1.empty()) return done();
    std::vector<double> cen(N);
    for (int i = 0; i < N; ++i) cen[i] = polyval(c1, t[i]);

    // pass 2: left/right edge of the line at half of its height, around the coarse line
    cv::Mat g = sampleBand(gE, org, p.p0, u, nrm, N, &cen, hwEdge);
    const int W = g.cols;
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
        if (row[a] > h || row[b] > h) continue;        // line wider than the window
        eL[i] = (a - hwEdge) + (h - row[a]) / (row[a + 1] - row[a] + 1e-6) + cen[i];
        eR[i] = (b - 1 - hwEdge) + (row[b - 1] - h) / (row[b - 1] - row[b] + 1e-6) + cen[i];
    }

    // deviation of each edge from its own smooth curve (line + curvature: the label is on a cylinder)
    std::vector<double> fL = robustFit(t, eL, 2, 1.5), fR = robustFit(t, eR, 2, 1.5);
    if (fL.empty() || fR.empty()) return done();

    int evalRows = 0, validRows = 0;
    for (int i = 0; i < N; ++i) {
        if (t[i] < p.endIgnore || t[i] > 1.0 - p.endIgnore) continue;
        ++evalRows;
        if (std::isnan(eL[i]) || std::isnan(eR[i])) continue;
        ++validRows;
        const double e = std::min(t[i], 1.0 - t[i]);
        const double tol = std::max(0.05, e < p.edgeFrac ? p.tolEdge : p.tolMid);
        const double dev[2] = {eL[i] - polyval(fL, t[i]), eR[i] - polyval(fR, t[i])};
        const double edges[2] = {eL[i], eR[i]};
        for (int k = 0; k < 2; ++k) {
            const double ratio = std::fabs(dev[k]) / tol;
            const cv::Point2f pt(float(p.p0.x + u.x * i + nrm.x * edges[k]), float(p.p0.y + u.y * i + nrm.y * edges[k]));
            if (ratio > 1.0) res.badPts.push_back(pt);
            if (ratio > res.ratio) {
                res.ratio = ratio;
                res.maxDev = std::fabs(dev[k]);
                res.worstT = t[i];
            }
        }
        if (i % 4 == 0) {
            const double c = 0.5 * (eL[i] + eR[i]);
            res.centerPts.emplace_back(float(p.p0.x + u.x * i + nrm.x * c), float(p.p0.y + u.y * i + nrm.y * c));
        }
    }
    res.coverage = evalRows ? double(validRows) / evalRows : 0.0;
    res.found = res.coverage >= 0.5;
    if (!res.found) {                                   // no seam in this frame: nothing to judge here
        res.ratio = res.maxDev = res.worstT = 0;
        res.centerPts.clear();
        res.badPts.clear();
    } else {
        res.reject = res.ratio > 1.0;
    }
    return done();
}

SeamResult measureSeam(const cv::Mat& bgr, const SeamSettings& s) {
    SeamParams p;
    p.enabled = s.enabled && s.hasLine() && !bgr.empty();
    p.p0 = {s.p0x * bgr.cols, s.p0y * bgr.rows};
    p.p1 = {s.p1x * bgr.cols, s.p1y * bgr.rows};
    p.bandHalf = s.band;
    p.tolMid = s.tolMid;
    p.tolEdge = s.tolEdge;
    p.edgeFrac = s.edgeFrac;
    return measureSeam(bgr, p);
}

} // namespace cas
