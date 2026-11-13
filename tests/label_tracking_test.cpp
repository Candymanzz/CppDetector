#include "inspect/inspector.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
int main() {
 try {
    cv::setNumThreads(1);
    cv::Mat reference(384,512,CV_8UC3,cv::Scalar(220,220,220)), texture(reference.size(),reference.type());
    cv::RNG rng(52);rng.fill(texture,cv::RNG::UNIFORM,15,130);
    const std::vector<cv::Point> outline{{60,100},{130,65},{260,50},{390,65},{450,100},{430,310},{260,340},{80,310}};
    cv::Mat mask=cv::Mat::zeros(reference.size(),CV_8U);cv::fillPoly(mask,std::vector<std::vector<cv::Point>>{outline},255);
    texture.copyTo(reference,mask);
    cd::InspectParams settings;settings.labelOutline=outline;settings.horizontalShiftMm=30;settings.verticalShiftMm=30;
    settings.minBlobArea=4;settings.rejectThreshold=60;
    cd::Inspector inspector;inspector.setReference(reference,{},settings);
    cv::Mat inset;cv::erode(mask,inset,cv::getStructuringElement(cv::MORPH_RECT,{3,3}));
    for (double scale : {1.,.5}) for (int tolerance : {0,3,5}) {
        auto p=settings;p.downscale=scale;p.shiftToleranceX=tolerance;p.shiftToleranceY=tolerance;
        const auto self=inspector.inspect(reference,p);
        require(!self.reject,"self comparison with fixed inset must pass");
        const auto& scored=self.native.inspectionMask;
        if(scale==1.) require(scored.at<unsigned char>(50,260)==0,
            "native label perimeter must have a small inset");
        if(scale==.5) require(self.native.labelCoverage==1.,
            "half analysis pixel guard must not grow to a two-native-pixel inset");
        require(scored.at<unsigned char>(cvRound(56*scale),cvRound(260*scale))==255,
            "fixed inset must not grow with comparison tolerance");
        if(scale==1.) require(cv::norm(scored,inset,cv::NORM_INF)==0,
            "native scale must inset exactly one pixel per axis");
    }
    int checks=0;
    for(double scale:{1.,.5}) for(auto shift:{cv::Point(0,0),cv::Point(0,8),cv::Point(0,-8),cv::Point(8,0),cv::Point(-8,0),cv::Point(8,-8)}) {
        settings.downscale=scale;cv::Mat frame;
        cv::Mat warp=(cv::Mat_<double>(2,3)<<1,0,shift.x,0,1,shift.y);
        cv::warpAffine(reference,frame,warp,reference.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT,cv::Scalar(220,220,220));
        auto result=inspector.inspect(frame,settings);
        if(result.reject) std::cerr<<"scale "<<scale<<" shift "<<shift<<" reason "<<result.reason<<" score "<<result.score<<" confidence "<<result.native.shiftResponse<<" boundary "<<result.native.labelBoundarySupport<<'\n';
        require(!result.reject && result.score==0,"shifted label / exposed bucket must pass");
        cv::rectangle(frame,{240+shift.x,170+shift.y,30,25},{0,0,0},cv::FILLED);
        require(inspector.inspect(frame,settings).reject,"interior defect must still reject"); ++checks;
    }
    auto missing=reference.clone();missing.setTo(cv::Scalar(220,220,220));
    const auto stable=inspector.inspect(reference,settings);
    for(int i=0;i<30;++i) {
        const auto repeat=inspector.inspect(reference,settings);
        require(!repeat.reject && repeat.native.labelCoverage==stable.native.labelCoverage,"per-frame masks must not shrink the cache");
    }
    auto edgeDefect=reference.clone();cv::rectangle(edgeDefect,{250,50,30,24},{220,220,220},cv::FILLED);
    require(inspector.inspect(edgeDefect,settings).reject,"missing label material near boundary must reject");
    require(inspector.inspect(missing,settings).reject,"missing label cannot pass");
    settings.sensitivity=0;require(!inspector.inspect(missing,settings).reject,"zero sensitivity disables rejection");
    settings.sensitivity=.5;settings.horizontalShiftMm=settings.verticalShiftMm=.1;
    cv::Mat moved;cv::Mat warp=(cv::Mat_<double>(2,3)<<1,0,0,0,1,8);
    cv::warpAffine(reference,moved,warp,reference.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT,cv::Scalar(220,220,220));
    require(inspector.inspect(moved,settings).reason=="shift_exceeded","excess displacement rejects independently");
    settings.labelTrackingEnabled=false;
    inspector.setReference(reference,{},settings);
    const auto disabled=inspector.inspect(moved,settings);
    require(!disabled.native.labelEnabled && !settings.labelOutline.empty(),"disabled mode keeps outline without applying it");
    auto baseline=settings; baseline.labelOutline.clear();
    cd::Inspector original; original.setReference(reference,{},baseline);
    const auto expected=original.inspect(moved,baseline);
    require(disabled.score==expected.score && disabled.reject==expected.reject && disabled.reason==expected.reason &&
        cv::norm(disabled.native.rawShift-expected.native.rawShift)<1e-8,"disabled mode matches original inspection");
    settings.labelTrackingEnabled=true;
    inspector.setReference(reference,{},settings);
    require(inspector.inspect(reference,settings).native.labelEnabled,"re-enabling restores contour inspection");
    // A chromatic edge change with identical inspection luminance must not bypass
    // the normal comparison and force rejection via a separate color gate.
    auto colorReference=reference.clone();
    cv::Mat color(1,1,CV_8UC3,cv::Scalar(0,255,235)), grayColor, inner;
    cv::cvtColor(color,grayColor,cv::COLOR_BGR2GRAY);
    const auto level=grayColor.at<unsigned char>(0,0);
    cv::erode(mask,inner,cv::getStructuringElement(cv::MORPH_RECT,{25,25}));
    cv::Mat ring=mask & ~inner;
    colorReference.setTo(cv::Scalar(level,level,level),~mask);
    colorReference.setTo(cv::Scalar(0,255,235),ring);
    auto colorCurrent=colorReference.clone();colorCurrent.setTo(cv::Scalar(level,level,level),ring);
    auto colorSettings=settings;colorSettings.downscale=1;
    colorSettings.horizontalShiftMm=30;colorSettings.verticalShiftMm=30;
    cd::Inspector colorInspector;colorInspector.setReference(colorReference,{},colorSettings);
    const auto colorResult=colorInspector.inspect(colorCurrent,colorSettings);
    require(colorResult.native.labelBoundarySupport<.7,"fixture must fail the old color gate");
    require(!colorResult.reject && colorResult.score==0 && !colorResult.native.diff.empty(),
        "boundary color diagnostics must not prevent normal comparison");
    std::cout<<checks<<" shift/defect pairs and failure guards passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
