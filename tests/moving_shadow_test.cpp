#include "inspect/inspector.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <future>
#include <memory>
#include <algorithm>

void require(bool value,const char* msg) {if(!value) throw std::runtime_error(msg);}
cv::Mat shadow(const cv::Mat& base,int dx,int dy,double strength=65,double width=1) {
    auto result=base.clone();
    for(int y=0;y<base.rows;++y) for(int x=0;x<base.cols;++x) {
        const double px=(x-140-dx)/(12*width),py=(y-135-dy)/(10*width);
        const double shade=strength*std::exp(-0.5*(px*px+py*py));
        for(int c=0;c<3;++c)result.at<cv::Vec3b>(y,x)[c]=cv::saturate_cast<uchar>(base.at<cv::Vec3b>(y,x)[c]-shade);
    }
    return result;
}
int main(int argc,char**) {
    try {
        cv::setNumThreads(1);
        cv::Mat base(320,384,CV_8UC3);cv::RNG rng(42);rng.fill(base,cv::RNG::UNIFORM,130,230);
        cv::GaussianBlur(base,base,{3,3},0);
        cv::putText(base,"LABEL",{55,140},cv::FONT_HERSHEY_SIMPLEX,.7,{100,100,100},2);
        const auto normal=shadow(base,0,0);
        const cv::Rect box(102,97,76,76);
        cd::InspectParams p;p.rejectThreshold=10;p.minBlobArea=3;p.shiftToleranceX=p.shiftToleranceY=1;
        cd::Inspector inspector;inspector.setReference(base,{},p);
        require(inspector.inspect(normal,p).reject,"unlearned shadow must reject");
        inspector.setLearned({{"shade",box,normal(box).clone()}});
        auto result=inspector.inspect(normal,p);
        std::cout<<"same "<<result.score<<" "<<result.learningDiagnostics.dump()<<"\n";
        require(!result.reject,"original learned shadow must pass");
        for(double scale:{1.0,0.7,0.5}) for(cv::Point delta:{cv::Point(20,8),cv::Point(-18,-7),cv::Point(0,18)}) {
            p.downscale=scale;
            auto moved=shadow(base,delta.x,delta.y);
            result=inspector.inspect(moved,p);
            std::cout<<"moved "<<scale<<" "<<delta<<" score="<<result.score<<" "<<result.learningDiagnostics.dump()<<"\n";
            require(!result.reject,"moved shadow must pass inside independent search radius");
            require(!result.native.matchedCaseIds.empty(),"moved shadow must report case id");
            require(result.learningDiagnostics[0]["method"]=="shadow" || result.learningDiagnostics[0]["method"]=="pattern_and_shadow","shadow path used");
            auto scratch=moved.clone();cv::line(scratch,{140+delta.x,120+delta.y},{140+delta.x,150+delta.y},{0,0,0},7);
            require(inspector.inspect(scratch,p).reject,"new scratch on moving shadow must reject");
            auto outside=moved.clone();cv::rectangle(outside,{270,220,15,20},{0,0,0},-1);
            require(inspector.inspect(outside,p).reject,"defect outside moved support must reject");
        }
        p.downscale=1;
        require(!inspector.inspect(shadow(base,15,-8,58,1.05),p).reject,"small shadow depth/width change");
        auto disabled=p;disabled.learning.robust=false;
        require(inspector.inspect(shadow(base,20,8),disabled).reject,"native matcher reproduces old false reject");
        disabled=p;disabled.learning.shadows=false;
        require(inspector.inspect(shadow(base,20,8),disabled).reject,"shadow path can be disabled independently");
        disabled=p;disabled.learning.searchRadius=4;
        require(inspector.inspect(shadow(base,20,8),disabled).reject,"search radius is enforced");
        require(inspector.inspect(shadow(base,65,0),p).reject,"shadow outside radius must reject");
        require(inspector.inspect(shadow(base,15,8,130),p).reject,"large unlearned brightness change must reject");
        auto missing=shadow(base,20,8);cv::rectangle(missing,{150,130,20,12},{230,230,230},-1);
        require(inspector.inspect(missing,p).reject,"missing printed detail must reject");
        auto spot=base.clone();cv::rectangle(spot,{135,125,20,20},{0,0,0},-1);
        require(inspector.inspect(spot,p).reject,"hard black spot must not be explained as shadow");
        disabled=p;disabled.sensitivity=1;
        require(inspector.inspect(normal,disabled).reject,"sensitivity endpoint remains strict");
        disabled=p;disabled.sensitivity=0;
        require(!inspector.inspect(normal,disabled).reject,"sensitivity zero endpoint");
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<20;++i) require(!inspector.inspect(shadow(base,20,8),p).reject,"stable repeated match");
        std::cout<<"20 repeated inspections ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"\n";
        if(argc>1) {
            cv::Mat full;cv::resize(base,full,{1224,1024});
            const auto learned=shadow(full,0,0),input=shadow(full,20,8);
            std::vector<std::unique_ptr<cd::Inspector>> cameras;
            for(int i=0;i<10;++i) {
                auto cam=std::make_unique<cd::Inspector>();cam->setReference(full,{},p);
                cam->setLearned({{"shade",box,learned(box).clone()}});cameras.push_back(std::move(cam));
            }
            for(bool robust:{false,true}) {
                auto params=p;params.learning.robust=robust;std::vector<double> times;
                int rejects=0;double learningMs=0;
                for(int batch=0;batch<22;++batch) {
                    const auto begin=std::chrono::steady_clock::now();
                    std::vector<std::future<cd::InspectResult>> jobs;
                    for(auto& cam:cameras) {auto* camera=cam.get();jobs.push_back(std::async(std::launch::async,[&,camera]{return camera->inspect(input,params);}));}
                    for(auto& job:jobs) {auto result=job.get();if(batch>=2){rejects+=result.reject;learningMs+=result.learningMs;}}
                    if(batch>=2) times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
                }
                std::sort(times.begin(),times.end());double total=0;for(double t:times)total+=t;
                std::cout<<"BENCH "<<(robust?"robust":"native")<<" 10x1224x1024 20 batches: mean="<<total/times.size()<<" p95="<<times[18]<<" max="<<times.back()<<" ms, rejects="<<rejects<<"/200, learning_mean="<<learningMs/200<<" ms\n";
            }
        }
        std::cout<<"moving shadow checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
