#include "core/learning_matcher.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}

void placedMask(cv::Rect region,bool shadows) {
    // A static label boundary remains in the same raw-frame position while the
    // global alignment moves it by five pixels. Its saved crop can equal the
    // primary image at the source position yet differ at the matched position.
    cv::Mat image(140,180,CV_8UC3,cv::Scalar::all(50));
    image(cv::Rect(45,0,135,140)).setTo(cv::Scalar::all(200));
    image(cv::Rect(0,65,180,10)).setTo(cv::Scalar::all(120));
    cas::Reference reference(image.clone());
    const cv::Rect box(20,20,70,90);
    reference.setLearned({{"boundary",box,image(box).clone()}});
    cv::Mat moved(140,180,CV_8UC3,cv::Scalar::all(50));
    moved(cv::Rect(50,0,130,140)).setTo(cv::Scalar::all(200));
    moved(cv::Rect(0,65,180,10)).setTo(cv::Scalar::all(120));
    cv::Mat current;cv::cvtColor(moved,current,cv::COLOR_BGR2GRAY);cv::GaussianBlur(current,current,{5,5},0);
    current=current(region).clone();
    const auto primary=reference.prepared(1)->refGray(region);
    cv::Mat diff;cv::absdiff(primary,current,diff);
    cas::Settings settings;settings.shiftToleranceX=settings.shiftToleranceY=0;
    const auto before=diff.clone();
    cas::InspectOutcome result;nlohmann::json diagnostics;
    cas::compareLearned(reference,*reference.prepared(1),current,diff,region,1,settings,result,{true,shadows,12},diagnostics);
    require(diagnostics[0]["pattern_aligned"]==true,"fixture must match the translated sample");
    require(diagnostics[0]["shift"][0]==5 && diagnostics[0]["shift"][1]==0,"known five pixel placement");
    const cv::Rect placed(box.x+5-region.x,box.y-region.y,box.width,box.height);
    require(cv::countNonZero(before(placed)>21)>0,"fixture starts with a boundary anomaly");
    require(cv::countNonZero(diff(placed)>21)==0,"matched sample removes the boundary anomaly at its actual position");
    require(!result.matchedCaseIds.empty(),"a sample identical at the source position still applies after alignment");

    auto defect=current.clone();
    const cv::Rect mark(47-region.x,40-region.y,3,14);
    defect(mark).setTo(0);
    cv::absdiff(primary,defect,diff);
    cas::compareLearned(reference,*reference.prepared(1),defect,diff,region,1,settings,result,{true,shadows,12},diagnostics);
    require(cv::countNonZero(diff(mark)>21)>0,"new defect inside the learned support remains visible");
}
}
int main(){try{cv::setNumThreads(1);
    for(const auto region:{cv::Rect(0,0,180,140),cv::Rect(10,10,120,120)})
        for(bool shadows:{false,true})placedMask(region,shadows);
    std::cout<<"learned mask placement and new-defect checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
