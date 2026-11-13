#include "inspect/inspector.h"
#include "core/inspector.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void check(const cv::Mat& image, int& checks) {
    const std::vector<cv::Point> roi{{image.cols/6,image.rows/6},{image.cols*5/6,image.rows/6},
        {image.cols*5/6,image.rows*5/6},{image.cols/6,image.rows*5/6}};
    cas::NormPolygon normalized;
    for (const auto& p : roi) normalized.emplace_back(double(p.x)/(image.cols-1),double(p.y)/(image.rows-1));
    cas::Reference reference(image.clone()); reference.setRoi(normalized);
    cd::InspectParams p; p.cameraId=4; p.rejectThreshold=60;
    cd::Inspector adapter; adapter.setReference(image,roi,p);
    for (double scale : {1., .5, .37}) for (double sensitivity : {0., .5, .8, 1.})
    for (cv::Point shift : {cv::Point(0,0),cv::Point(3,-4),cv::Point(-5,3),cv::Point(30,0)}) {
        p.downscale=scale; p.sensitivity=sensitivity;
        cv::Mat current;
        cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,shift.x,0,1,shift.y);
        cv::warpAffine(image,current,transform,image.size(),cv::INTER_LINEAR,cv::BORDER_REPLICATE);
        for (bool defect : {false,true}) {
            cv::Mat frame=current.clone();
            if (defect) cv::rectangle(frame,{image.cols/2,image.rows/2,18,16},{0,0,0},cv::FILLED);
            auto s=cas::Settings{}.withMetricDefaults(p.cameraId);
            s.sensitivity=sensitivity; s.threshold=.6; s.inspectScale=scale;
            s.horizontalShiftMm=p.horizontalShiftMm; s.verticalShiftMm=p.verticalShiftMm;
            s.shiftToleranceX=s.shiftToleranceY=p.shiftTolerance; s.minBlobArea=p.minBlobArea;
            const auto expected=cas::inspectFrame(reference,frame,s,true);
            const auto actual=adapter.inspect(frame,p);
            require(std::abs(actual.score-100*expected.anomalyScore)<1e-10,"score differs from production");
            require(actual.reason==expected.reason && actual.reject==expected.reject,"verdict differs from production");
            require(cv::norm(actual.shift-expected.rawShift)<1e-8,"position differs from production");
            require(actual.zones==expected.zones,"zones differ from production");
            require(actual.heatmap.size()==expected.workSize,"heatmap coordinates mismatch");
            if (!defect && shift==cv::Point(0,0) && sensitivity!=1.)
                require(actual.score==0 && cv::norm(actual.shift)<1e-4,"self comparison is not zero");
            ++checks;
        }
    }
}
int main(int argc, char** argv) {
    try {
        cv::setNumThreads(1);
        cv::Mat image(513,613,CV_8UC3); cv::RNG random(42);
        random.fill(image,cv::RNG::UNIFORM,20,220);
        cv::putText(image,"LABEL",{140,210},cv::FONT_HERSHEY_SIMPLEX,2,{255,255,255},4);
        int checks=0; check(image,checks);
        for (int i=1;i<argc;++i) { auto real=cv::imread(argv[i]); require(!real.empty(),"cannot load frame"); check(real,checks); }
        cd::Inspector empty; require(empty.inspect(image,{}).reason=="no_reference","missing reference");
        empty.setReference(image,{},{});
        require(empty.inspect(cv::Mat(20,20,CV_8UC3),{}).reason=="size_mismatch","wrong size");
        std::cout<<checks<<" parity cases passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
