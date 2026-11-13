#include "app/app.h"
#include "production/core/seam.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
const std::vector<cv::Point> hole{{90,90},{190,90},{190,190},{90,190}};
cv::Mat reference() {
    cv::Mat grey(320,384,CV_8U); cv::RNG rng(42); rng.fill(grey,cv::RNG::UNIFORM,40,210);
    cv::Mat image; cv::cvtColor(grey,image,cv::COLOR_GRAY2BGR); return image;
}
void masksAndDefects() {
    const auto ref = reference(); cv::Mat inside = ref.clone();
    cv::rectangle(inside,{115,115,50,50},{0,0,0},cv::FILLED);
    cd::InspectParams p; p.rejectThreshold=10; p.minBlobArea=3;
    p.maxAlignmentShiftX=p.maxAlignmentShiftY=12;
    const std::vector<cv::Point> roi{{30,30},{350,30},{350,290},{30,290}};
    cd::Inspector inspector;
    for (double scale : {1.0,.73,.5}) {
        p.downscale=scale; p.exclusions.clear(); inspector.setReference(ref,roi,p);
        require(inspector.inspect(inside,p).reject,"negative control: defect rejects before exclusion");
        p.exclusions={hole,hole}; inspector.setReference(ref,roi,p);
        const auto ignored=inspector.inspect(inside,p);
        require(!ignored.reject && ignored.score==0,"ignored defect must not affect score/verdict");
        require(cv::countNonZero(ignored.native.kept)==0,"ignored defect absent from scored components");
        cv::Mat outside=inside.clone(); cv::rectangle(outside,{240,220,45,45},{0,0,0},cv::FILLED);
        require(inspector.inspect(outside,p).reject,"defect outside exclusion still rejects");
        const auto clean=inspector.inspect(ref,p);
        require(!clean.reject,"clean frame remains accepted");
    }
    cas::Reference native(ref); native.setRoi({{.1,.1},{.9,.1},{.9,.9},{.1,.9}});
    const auto before=native.prepared(1);
    const auto after=native.prepared(1, {{{.3,.3},{.5,.3},{.5,.5},{.3,.5}}});
    require(before!=after && after->roiArea<before->roiArea,"exclusions invalidate prepared cache and remove denominator area");
    require(cv::norm(before->upperCornerMask,after->upperCornerMask,cv::NORM_INF)==0,"interior hole does not move upper corners");
    require(native.prepared(1)->roiArea==before->roiArea,"removal restores original mask");
    p.exclusions={{{0,0},{383,0},{383,319},{0,319}}}; p.sensitivity=1;
    inspector.setReference(ref,{},p); const auto none=inspector.inspect(inside,p);
    require(!none.reject && none.score==0 && none.reason=="inspection_excluded","fully excluded inspection is skipped even at sensitivity one");
}
void seamExclusion() {
    cv::Mat image(320,384,CV_8UC3,cv::Scalar(40,40,40));
    for(int y=20;y<300;++y) cv::line(image,{180+(y>=135&&y<155?5:0),y},{183+(y>=135&&y<155?5:0),y},{240,240,240},1);
    cas::SeamParams p; p.enabled=true;p.p0={181.5,20};p.p1={181.5,300};p.bandHalf=15;p.tolMid=p.tolEdge=.8;
    require(cas::measureSeam(image,p).reject,"negative control: seam kink rejects");
    cv::Mat excluded=cv::Mat::zeros(image.size(),CV_8U); cv::rectangle(excluded,{140,110,90,70},255,cv::FILLED);
    const auto clean=cas::measureSeam(image,p,excluded);
    require(clean.found&&!clean.reject,"excluded seam kink neither affects fit nor rejects");
    for(int y=215;y<235;++y) { image.row(y).setTo(cv::Scalar(40,40,40));cv::line(image,{186,y},{189,y},{240,240,240},1); }
    require(cas::measureSeam(image,p,excluded).reject,"outside seam kink remains checked");
}
void persistence(const std::filesystem::path& dir) {
    const auto image=reference();std::string err;
    const cd::json polygons={{{90,90},{190,90},{190,190},{90,190}}};
    {
        cd::App app(dir/"frames",dir/"config"/"test.json",dir/"logs");
        require(app.setReferenceFromImage(0,image,err),"upload reference");
        require(app.setCamera(0,{{"exclusions",polygons}},err),"save exclusions");
        require(app.state()["cameras_info"][0]["exclusions"]==polygons,"state exposes saved zone");
        require(app.state()["cameras_info"][1]["exclusions"].empty(),"camera isolation");
        const auto before=app.state()["cameras_info"][0];
        for(const auto& bad : {cd::json("bad"),cd::json{{{0,0},{1,1}}},cd::json{{{-1,0},{50,0},{50,50}}},cd::json{{{0,0},{384,0},{10,10}}},cd::json{{{0,0},{1,1},{2,2}}}}) {
            err.clear(); require(!app.setCamera(0,{{"sensitivity",.8},{"exclusions",bad}},err),"malformed exclusions rejected");
            require(app.state()["cameras_info"][0]==before,"failed update is atomic");
        }
        err.clear(); require(app.setReferenceFromImage(0,image.clone(),err),"same sized replacement");
        require(app.state()["cameras_info"][0]["exclusions"]==polygons,"same sized replacement preserves zones like ROI");
    }
    {
        cd::App app(dir/"frames",dir/"config"/"test.json",dir/"logs");
        require(app.state()["cameras_info"][0]["exclusions"]==polygons,"zones survive restart");
        require(app.setCamera(0,{{"exclusions",cd::json::array()}},err),"clear zones");
        require(app.state()["cameras_info"][0]["exclusions"].empty(),"clear reflected in state");
        require(app.setCamera(0,{{"exclusions",polygons}},err),"restore zone");
        require(app.setReferenceFromImage(0,cv::Mat(100,100,CV_8UC3,cv::Scalar(80,80,80)),err),"different sized reference");
        require(app.state()["cameras_info"][0]["exclusions"].empty(),"new image dimensions reset zones like ROI");
    }
}
}
int main() {
    cv::setNumThreads(1);
    const auto dir=std::filesystem::temp_directory_path()/("cpp-exclusion-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        masksAndDefects();seamExclusion();persistence(dir);
        require(std::filesystem::weakly_canonical(dir).parent_path()==std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()),"safe temporary path");
        std::filesystem::remove_all(dir);
        std::cout<<"Exclusions: masks, scales, outside defects, cache, corners, seam, validation and persistence passed\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<" (data: "<<dir.string()<<")\n";return 1; }
}
