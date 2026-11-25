#include "inspect/seam_mask.h"
#include "production/core/seam.h"
#include "app/app.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
using nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void synthetic(){
    cv::Mat straight(480,360,CV_8UC3,cv::Scalar(70,70,70));
    cv::line(straight,{180,20},{180,460},{100,100,100},4);
    // Distracting print is brighter than the seam but does not form a continuous path.
    for(int y=55;y<440;y+=38)cv::rectangle(straight,{193,y,6,9},{210,210,210},-1);
    cd::SeamParams p;p.enabled=true;p.maskSearch=true;p.pts={{180,30},{180,450}};p.bandHalf=20;p.tolMid=.8;p.tolEdge=4;
    auto a=cd::measureMaskedSeam(straight,p);require(a.controlComplete&&a.found&&!a.reject,"weak straight seam found through print distractors");
    auto kink=straight.clone();cv::rectangle(kink,{170,220,20,24},{70,70,70},-1);cv::line(kink,{187,220},{187,243},{100,100,100},4);
    auto b=cd::measureMaskedSeam(kink,p);require(b.controlComplete&&b.reject&&b.maxDev>3,"mask path must not straighten away a real kink");
    auto gap=straight.clone();cv::rectangle(gap,{150,200,60,130},{70,70,70},-1);
    auto g=cd::measureMaskedSeam(gap,p);require(!g.controlComplete&&g.longestGap>.2,"long missing segment never becomes an accepted interpolated seam");
    for(size_t i=0;i<g.rowState.size();++i)if(g.rowState[i]!=0)require(std::isnan(g.rowDev[i]),"missing evidence stays unmeasured");
    auto blank=cd::measureMaskedSeam(cv::Mat(straight.size(),straight.type(),cv::Scalar(80,80,80)),p);require(!blank.found&&!blank.controlComplete,"blank frame cannot invent a path");
    auto border=p;border.pts={{1,30},{1,450}};require(!cd::measureMaskedSeam(cv::Mat(straight.size(),straight.type(),cv::Scalar(80,80,80)),border).found,"image border outside the search band is not an invented seam");
    cv::Mat excluded(straight.size(),CV_8U,cv::Scalar(0));cv::rectangle(excluded,{150,210,70,50},255,-1);
    auto e=cd::measureMaskedSeam(kink,p,excluded);require(e.controlComplete&&!e.reject,"explicitly excluded kink is excluded from fit and verdict");
    p.pts[0].y=650;require(cd::measureMaskedSeam(straight,p).maskReason=="seam_mask_outside_frame","stale coordinates fail explicitly");
    std::cout<<"Mask seam synthetic checks passed\n";
}
void paired(){
    cv::Mat ref(480,360,CV_8UC3,cv::Scalar(70,70,70));
    cv::rectangle(ref,{175,15,11,450},{125,125,125},-1);
    cd::SeamParams p;p.enabled=true;p.maskSearch=true;p.pts={{180,30},{180,450}};p.bandHalf=18;p.tolMid=p.tolEdge=8;
    const auto base=cd::inspectMaskedSeam(ref,ref,p);
    require(base.pairedMask&&base.controlComplete&&!base.reject&&base.maxDev<.001,"paired reference self comparison measures both sides with zero error");
    for(int side:{-1,1}){
        auto half=ref.clone();cv::rectangle(half,side<0?cv::Rect(165,210,16,35):cv::Rect(180,210,16,35),{70,70,70},-1);
        const auto old=cd::measureMaskedSeam(half,p);require(old.controlComplete&&!old.reject,"regression fixture reproduces the old surviving-side false acceptance");
        const auto r=cd::inspectMaskedSeam(half,ref,p);
        require(r.reject||!r.controlComplete,"erasing either half must not pass through its surviving opposite side");
        require(r.maskReason=="seam_mask_width"||r.maskReason=="seam_mask_side_missing"||!r.controlComplete,"one-sided loss has an explicit diagnostic");
        const auto intact=side<0?1:0;require(r.sides[intact].coverage>.9,"the opposite unchanged side remains independently measured");
        for(size_t i=0;i<r.rowState.size();++i)if(r.rowState[i]!=0)require(std::isnan(r.rowDev[i]),"a partial pair is not a fabricated complete measurement");
        cv::Mat exclusions(ref.size(),CV_8U,cv::Scalar(0));cv::rectangle(exclusions,{150,195,65,65},255,-1);
        const auto skipped=cd::inspectMaskedSeam(half,ref,p,{},exclusions);require(skipped.controlComplete&&!skipped.reject,"explicit exclusion resets both side runs");
    }
    auto missing=ref.clone();cv::rectangle(missing,{155,210,55,35},{70,70,70},-1);
    const auto absent=cd::inspectMaskedSeam(missing,ref,p);require(absent.reject&&absent.maskReason=="seam_mask_side_missing","short complete disappearance below the old global gap limit is rejected");
    auto longerRun=p;longerRun.maskDefectRun=100;const auto allowed=cd::inspectMaskedSeam(missing,ref,longerRun);
    require(allowed.controlComplete&&!allowed.reject,"the explicit local defect-length control changes the missing-run decision");
    auto square=missing.clone();cv::rectangle(square,{191,210,8,35},{15,15,15},-1);
    const auto distractor=cd::inspectMaskedSeam(square,ref,p);require(distractor.reject||!distractor.controlComplete,"nearby print cannot substitute for an erased pair");
    cv::Mat bright;ref.convertTo(bright,-1,1,15);const auto lighting=cd::inspectMaskedSeam(bright,ref,p);require(lighting.controlComplete&&!lighting.reject,"uniform illumination does not erase either side");
    cv::Mat shifted;cv::warpAffine(ref,shifted,cv::Matx23d(1,0,5,0,1,3),ref.size(),cv::INTER_LINEAR,cv::BORDER_REPLICATE);
    const auto pose=cd::inspectMaskedSeam(shifted,ref,p,{5,3});require(pose.controlComplete&&!pose.reject&&pose.maxDev<.001,"whole-image pose moves both reference rays together");
    require(!cd::inspectMaskedSeam(ref,ref,p,{190,0}).controlComplete,"expected rays outside the current frame cannot be replaced by another edge");
    auto noLine=p;noLine.pts.clear();require(!cd::inspectMaskedSeam(ref,ref,noLine).controlComplete,"no endpoints cannot produce complete paired control");
    cv::Mat step(ref.size(),CV_8UC3,cv::Scalar(70,70,70));cv::rectangle(step,{180,0,180,480},{125,125,125},-1);
    const auto unsupported=cd::inspectMaskedSeam(step,step,p);require(!unsupported.controlComplete&&unsupported.maskReason=="seam_mask_reference_pair","ridge cannot silently degrade to a single boundary");
    p.mode=cd::SeamParams::Step;require(!cd::inspectMaskedSeam(step,step,p).pairedMask,"step mode remains a single boundary");
    std::cout<<"Paired mask side/width checks passed\n";
}
void persistence(){
    namespace fs=std::filesystem;const auto base=fs::weakly_canonical(fs::temp_directory_path());
    const auto temp=base/("seam-mask-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp/"frames/camera_0/ref");cv::imwrite((temp/"frames/camera_0/ref/frame.jpg").string(),cv::Mat(480,360,CV_8UC3,cv::Scalar(80,80,80)));
    const auto config=temp/"config/settings.json";
    {cd::App app(temp/"frames",config,temp/"logs");std::string error;
        require(app.setCamera(0,{{"seam",{{"search_method","mask"},{"mask_low",5},{"mask_high",20},{"mask_coverage",.8},{"mask_max_gap",.15},{"mask_defect_run",7},{"mask_width_tolerance",.4},{"pts",{{180,30},{180,450}}}}}},error),"mask API settings accepted");
        const auto before=app.state()["cameras_info"][0]["seam"];
        require(!app.setCamera(0,{{"seam",{{"mask_low",30},{"mask_high",10}}}},error),"inverted thresholds rejected");
        require(app.state()["cameras_info"][0]["seam"]==before,"invalid update is atomic");
        require(!app.setCamera(0,{{"seam",{{"mask_defect_run",0}}}},error),"invalid missing-run limit rejected");
        require(!app.setCamera(0,{{"seam",{{"mask_width_tolerance",-1}}}},error),"invalid width limit rejected");
        require(app.state()["cameras_info"][0]["seam"]==before,"new invalid settings are atomic");
    }
    {cd::App app(temp/"frames",config,temp/"logs");const auto s=app.state()["cameras_info"][0]["seam"];
        require(s["search_method"]=="mask"&&s["mask_low"]==5&&s["mask_high"]==20&&s["mask_coverage"]==.8&&s["mask_max_gap"]==.15,"mask method and settings survive restart");
        require(s["mask_defect_run"]==7&&s["mask_width_tolerance"]==.4,"paired settings survive restart");
    }
    const auto resolved=fs::weakly_canonical(temp);require(resolved.parent_path()==base&&resolved.filename().string().rfind("seam-mask-test-",0)==0,"safe temporary cleanup");fs::remove_all(resolved);
}
void photos(const std::filesystem::path& snapshot,const std::filesystem::path& output){
    std::ifstream in(snapshot);json state;in>>state;json report=json::array();std::filesystem::create_directories(output);
    const auto c=state["cameras_info"][6],s=c["seam"];cd::SeamParams p;p.enabled=true;p.maskSearch=true;
    for(const auto& q:s["pts"])p.pts.emplace_back(q[0].get<double>(),q[1].get<double>());
    p.bandHalf=s["band"];p.tolMid=s["tol_mid"];p.tolEdge=s["tol_edge"];p.edgeFrac=s["edge_frac"];
    const auto reference=cv::imread("D:/Pj/CppDetector/frames36/camera_6/"+c["reference"].get<std::string>()+"/frame.jpg");
    const auto self=cd::inspectMaskedSeam(reference,reference,p);
    require(self.controlComplete&&!self.reject&&self.maxDev<.001,"real reference measures itself with zero deviation on the runtime grid");
    const auto direction=p.pts.back()-p.pts.front();const auto u=direction/cv::norm(direction);const cv::Point2d normal(-u.y,u.x);
    int start=140,best=-1;
    for(int at=60;at+42<int(self.rowState.size())-30;++at){int count=0;for(int i=at;i<at+42;++i)if(self.rowState[i]==0)++count;if(count>best){best=count;start=at;}}
    json injected=json::array();
    for(int side=0;side<2;++side){auto changed=reference.clone();
        for(int i=start;i<start+42;++i){
            const auto left=cv::Point2d(self.sides[0].reference[i]),right=cv::Point2d(self.sides[1].reference[i]);
            const auto middle=(left+right)*.5,outer=(side==0?left-normal*5:right+normal*5);
            const int x=std::clamp(cvRound(outer.x),0,reference.cols-1),y=std::clamp(cvRound(outer.y),0,reference.rows-1);
            const auto color=reference.at<cv::Vec3b>(y,x);cv::line(changed,outer,middle,cv::Scalar(color[0],color[1],color[2]),2);
        }
        const auto result=cd::inspectMaskedSeam(changed,reference,p);
        require(result.reject||!result.controlComplete,"one-sided paint on the actual camera reference cannot produce an accepted seam");
        const auto name=std::string("paint-side-")+std::to_string(side+1);
        cv::imwrite((output/(name+".png")).string(),changed);cv::imwrite((output/(name+"-mask.png")).string(),result.maskPreview);
        injected.push_back({{"file",name+".png"},{"side",side},{"start",start},{"length",42},{"reject",result.reject},{"complete",result.controlComplete},{"reason",result.maskReason}});
    }
    std::ofstream(output/"injected.json")<<injected.dump(2);
    for(const auto& entry:c["frames"]){std::string name=entry;auto image=cv::imread("D:/Pj/CppDetector/frames36/camera_6/"+name+"/frame.jpg");if(image.empty())continue;
        cas::SeamParams old;old.enabled=true;old.p0=p.pts.front();old.p1=p.pts.back();old.bandHalf=p.bandHalf;old.tolMid=p.tolMid;old.tolEdge=p.tolEdge;old.edgeFrac=p.edgeFrac;
        const auto before=cas::measureSeam(image,old);const auto after=cd::measureMaskedSeam(image,p);
        const auto pair=cd::inspectMaskedSeam(image,reference,p);
        if(name=="f_0000079"||name=="f_0000080") {
            json raw={{"signs",after.candidateSigns},{"offsets",after.candidateOffsets}};
            std::ofstream(output/(name+"-candidates.json"))<<raw.dump();
            if(!after.maskPreview.empty())cv::imwrite((output/(name+"-mask.png")).string(),after.maskPreview);
        }
        report.push_back({{"frame",name},{"old_coverage",before.coverage},{"mask_coverage",after.coverage},{"gap",after.longestGap},{"found",after.found},{"complete",after.controlComplete},{"reject",after.reject},{"max_dev",after.maxDev},{"sides",after.maskSides},{"ms",after.ms},
            {"pair_coverage",pair.coverage},{"pair_complete",pair.controlComplete},{"pair_reject",pair.reject},{"pair_reason",pair.maskReason},{"pair_width_run",pair.longestWidthRun},{"pair_ms",pair.ms}});
        if(name=="f_0000080")cv::imwrite((output/"pair-camera6-f80.png").string(),pair.maskPreview);
        if(name=="f_0000080"&&!after.maskPreview.empty())cv::imwrite((output/"mask-camera6-f80.png").string(),after.maskPreview);
    }
    std::ofstream(output/"photos.json")<<report.dump(2);std::cout<<report.dump()<<'\n';
}
}
int main(int argc,char**argv){try{cv::setNumThreads(1);synthetic();paired();persistence();if(argc>2)photos(argv[1],argv[2]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
