#include "production/core/seam.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void check(const cv::Mat& image, const cas::SeamParams& p, bool reject) {
    const auto r = cas::measureSeam(image, p);
    require(r.found && r.reject == reject, "synthetic seam verdict");
    require(r.rowDev.size() == 420 && r.rowPos.size() == r.rowState.size(), "one trace sample per source pixel");
    require(r.rowRef.size() == r.rowDev.size() && r.rowTol.size() == r.rowDev.size(), "complete trace arrays");
    double ratio = 0; int valid = 0, evaluated = 0;
    for (size_t i = 0; i < r.rowDev.size(); ++i) {
        const double t = double(i) / 420;
        require(std::abs(r.rowTol[i] - (std::min(t, 1-t) < p.edgeFrac ? p.tolEdge : p.tolMid)) < 1e-6, "trace uses actual per-position tolerance");
        if (t < p.endIgnore || t > 1-p.endIgnore) require(r.rowState[i] == 2, "ends are explicitly ignored");
        else ++evaluated;
        if (r.rowState[i] != 0) { require(std::isnan(r.rowDev[i]), "unchecked sample has no fabricated deviation"); continue; }
        ++valid;
        ratio = std::max(ratio, double(std::abs(r.rowDev[i]) / r.rowTol[i]));
        require(std::abs(cv::norm(r.rowEdge[i]-r.rowRef[i])-std::abs(r.rowDev[i])) < 1e-3, "overlay geometry agrees with plotted deviation");
    }
    require(std::abs(ratio-r.ratio) < 1e-5, "graph reproduces verdict's maximum ratio");
    require(std::abs(double(valid)/evaluated-r.coverage) < 1e-9, "graph reproduces measured coverage");
}
}
int main() {
    try {
        cv::Mat image(480, 360, CV_8UC3, cv::Scalar(30,30,30));
        cv::line(image, {180,20}, {180,460}, {240,240,240}, 3);
        cas::SeamParams p; p.enabled=true; p.p0={180,30}; p.p1={180,450};
        p.bandHalf=20; p.tolMid=.8; p.tolEdge=4;
        check(image,p,false);
        auto kink=image.clone(); cv::rectangle(kink,{165,220,30,24},{30,30,30},-1);
        cv::line(kink,{186,220},{186,243},{240,240,240},3);
        check(kink,p,true);
        auto gap=image.clone(); cv::rectangle(gap,{165,210,30,35},{30,30,30},-1);
        const auto missing=cas::measureSeam(gap,p);
        require(std::count(missing.rowState.begin(),missing.rowState.end(),3)>15,"missing samples remain explicit gaps");
        cv::Mat mask(image.size(),CV_8U,cv::Scalar(0)); cv::rectangle(mask,{160,210,40,35},255,-1);
        const auto excluded=cas::measureSeam(kink,p,mask);
        require(!excluded.reject && std::count(excluded.rowState.begin(),excluded.rowState.end(),1)>=35,"excluded defect is marked, not plotted as normal");
        require(cas::measureSeam(cv::Mat(image.size(),image.type(),cv::Scalar(30,30,30)),p).rowDev.empty(),"no invented trace when fit fails");
        p.enabled=false;require(cas::measureSeam(image,p).rowDev.empty(),"disabled measurement has no trace");
        std::cout << "Seam diagnostic trace: straight, kink, coverage, tolerances, gaps and exclusions passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
