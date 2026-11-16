#include "inspect/inspector.h"
#include "app/app.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
cv::Mat reference(int seed=42) {
    cv::Mat image(480,640,CV_8UC3,cv::Scalar(180,170,160));cv::RNG random(seed);
    for(int i=0;i<300;++i) {
        cv::Point p(random.uniform(12,628),random.uniform(12,468));
        cv::Scalar colour(random.uniform(20,240),random.uniform(20,240),random.uniform(20,240));
        cv::rectangle(image,{p.x,p.y,random.uniform(3,15),random.uniform(3,14)},colour,-1);
    }
    for(int y=55;y<440;y+=65)cv::putText(image,"BUCKET 1234 LABEL",{30,y},cv::FONT_HERSHEY_SIMPLEX,.72,{25,25,25},2);
    return image;
}
cv::Mat transformed(const cv::Mat& image,const std::array<double,7>& m) {
    cv::Mat mx(image.size(),CV_32F),my(image.size(),CV_32F);
    for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {
        const double tx=double(x)/image.cols-.5,ty=double(y)/image.rows-.5;
        double rx=tx;
        for(int k=0;k<10;++k){const double ry=(ty-m[4]-m[5]*rx)/m[6];
            rx-=(m[0]+m[1]*rx+m[2]*ry+m[3]*rx*rx-tx)/(m[1]+2*m[3]*rx-m[2]*m[5]/m[6]);}
        const double ry=(ty-m[4]-m[5]*rx)/m[6];
        mx.at<float>(y,x)=float((rx+.5)*image.cols);my.at<float>(y,x)=float((ry+.5)*image.rows);
    }
    cv::Mat result;cv::remap(image,result,mx,my,cv::INTER_LINEAR,cv::BORDER_CONSTANT,{240,240,240});return result;
}
const std::vector<cv::Point> roi{{12,12},{627,12},{627,467},{12,467}};
cd::InspectParams parameters() {
    cd::InspectParams p;p.adaptiveRoi.enabled=true;p.adaptiveRoi.maxMotion=160;
    p.shiftToleranceX=p.shiftToleranceY=3;p.minBlobArea=6;p.rejectThreshold=60;return p;
}
void geometryAndDefects() {
    const auto ref=reference();const auto original=ref.clone();auto p=parameters();cd::Inspector inspector;
    inspector.setReference(ref,roi,p);
    for(const auto& model:std::vector<std::array<double,7>>{{0,1,0,0,0,0,1},{.055,1,0,0,0,0,1},
        {.025,.96,.018,.13,-.012,-.012,1.005},{-.035,1.02,-.018,-.12,.015,.015,.99}}) {
        const auto current=transformed(ref,model);
        for(double scale:{1.,.73,.5}) {
            p.downscale=scale;
            auto clean=inspector.inspect(current,p);
            std::cout<<"clean scale="<<scale<<" valid="<<clean.adaptive.valid<<" score="<<clean.score<<" inliers="<<clean.adaptive.inliers<<" residual="<<clean.adaptive.residual<<" reason="<<clean.reason<<"\n";
            require(clean.adaptive.valid&&clean.controlComplete,"known smooth transform must register");
            require(!clean.reject,"clean transformed label must pass");
            const auto center=clean.adaptive.toCurrent({320,240});
            require(cv::norm(center-cv::Point2f(float((model[0]+.5)*640),float((model[4]+.5)*480)))<2,"known center accuracy");
            for(cv::Point2f anchor:std::vector<cv::Point2f>{{320,240},{65,340},{560,120}}) {
                auto defect=current.clone();const auto q=clean.adaptive.toCurrent(anchor);
                cv::rectangle(defect,{cvRound(q.x)-12,cvRound(q.y)-12,25,25},{0,0,0},-1);
                const auto bad=inspector.inspect(defect,p);
                require(bad.reject,"interior and near-edge defects cannot become unknown surface");
                require(bad.controlComplete&&bad.adaptive.valid,"small defect must not destroy registration");
                require(!bad.zones.empty(),"defect has a displayed current-image zone");
                const auto heatBox=cv::boundingRect(bad.heatmap>0);
                const auto heatPoint=q*float(bad.heatmap.cols)/640.f;
                require(heatBox.contains(cv::Point(heatPoint)),"displayed heatmap follows defect in current image");
            }
        }
    }
    require(cv::norm(ref,original,cv::NORM_INF)==0,"reference remains immutable");
    p.downscale=1;
    require(inspector.inspect(ref,p).adaptive.coverage>.995,"a previous transform must not shrink the cached reference ROI");
    const auto shifted=transformed(ref,{.11,1,0,0,0,0,1});
    auto partial=inspector.inspect(shifted,p);
    std::cout<<"partial valid="<<partial.adaptive.valid<<" coverage="<<partial.adaptive.coverage<<" inliers="<<partial.adaptive.inliers<<" tracked="<<partial.adaptive.tracked<<" reason="<<partial.reason<<"\n";
    require(partial.adaptive.valid&&partial.adaptive.coverage<.96&&partial.adaptive.coverage>.8,"geometric clipping reduces reference coverage");
    p.adaptiveRoi.minCoverage=.99;
    const auto incomplete=inspector.inspect(shifted,p);
    require(incomplete.reject&&!incomplete.controlComplete&&incomplete.reason=="adaptive_low_coverage","insufficient coverage never becomes OK");
    p=parameters();cv::Mat missing(ref.size(),ref.type(),cv::Scalar(220,220,220));
    const auto absent=inspector.inspect(missing,p);
    require(absent.reject&&!absent.controlComplete,"missing label never becomes ignored surface");
    const auto unrelated=inspector.inspect(reference(91),p);
    require(unrelated.reject,"different print must not pass");
    cd::Inspector flat;flat.setReference(missing,roi,p);
    require(!flat.inspect(missing,p).controlComplete,"textureless reference is explicitly unavailable");

    // Exclusions travel with the reference, rather than remaining in screen coordinates.
    p.exclusions={{{280,190},{365,190},{365,290},{280,290}}};inspector.setReference(ref,roi,p);
    const auto clean=inspector.inspect(shifted,p);auto ignored=shifted.clone();const auto q=clean.adaptive.toCurrent({320,240});
    cv::rectangle(ignored,{cvRound(q.x)-14,cvRound(q.y)-14,29,29},{0,0,0},-1);
    require(!inspector.inspect(ignored,p).reject,"moved manual exclusion remains excluded");
    p.exclusions.clear();p.adaptiveRoi.enabled=false;inspector.setReference(ref,roi,p);
    const auto before=inspector.inspect(ref,p);
    require(!before.reject&&before.score==0,"disabled adaptive mode preserves native clean comparison");
    require(before.adaptive.features==0,"disabled mode does not run registration");
}
void persistence(const std::filesystem::path& dir) {
    std::string error;
    {
        cd::App app(dir/"frames",dir/"config"/"profile.json",dir/"logs");
        require(app.setReferenceFromImage(0,reference(),error),"upload test reference");
        require(app.setCamera(0,{{"roi",{{12,12},{627,12},{627,467},{12,467}}},{"adaptive_roi_enabled",true},
            {"adaptive_roi_min_coverage",.85},{"adaptive_roi_max_motion",125}},error),"save adaptive parameters");
        const auto before=app.state()["cameras_info"][0];
        for(const auto& bad:std::vector<cd::json>{{{"adaptive_roi_enabled",1}},{{"adaptive_roi_min_coverage",0}},
            {{"adaptive_roi_max_motion",999}},{{"adaptive_roi_min_coverage","bad"}}}) {
            require(!app.setCamera(0,bad,error),"reject invalid adaptive parameters");
            require(app.state()["cameras_info"][0]==before,"invalid update is atomic");
        }
        const auto result=app.inspectUpload(0,reference(),"self",false,error);
        require(result["control_complete"]==true&&result["adaptive_roi"]["valid"]==true,"result exposes diagnostics");
        const auto failed=app.inspectUpload(0,cv::Mat(480,640,CV_8UC3,cv::Scalar(220,220,220)),"missing",false,error);
        require(failed["verdict"]=="NOT_INSPECTED"&&failed["reject"]==true,"unavailable registration is an explicit non-OK verdict");
        require(app.state()["cameras_info"][1]["adaptive_roi_enabled"]==false,"per-camera isolation");
    }
    {
        cd::App app(dir/"frames",dir/"config"/"profile.json",dir/"logs");
        const auto c=app.state()["cameras_info"][0];
        require(c["adaptive_roi_enabled"]==true&&c["adaptive_roi_min_coverage"]==.85&&c["adaptive_roi_max_motion"]==125,"adaptive parameters survive restart");
    }
}
void cylinderAndSeam() {
    const auto ref=reference();auto p=parameters();cd::Inspector inspector;
    const std::vector<cv::Point> inner{{70,30},{570,30},{570,449},{70,449}};
    inspector.setReference(ref,inner,p);
    // True orthographic rotation of a cylindrical texture, rather than our fitted polynomial.
    for(double angle:{-.05,.05}) {
        cv::Mat mx(ref.size(),CV_32F),my(ref.size(),CV_32F);
        for(int y=0;y<ref.rows;++y)for(int x=0;x<ref.cols;++x){
            const double theta=std::asin(std::clamp((x-320.)/360.,-1.,1.))-angle;
            mx.at<float>(y,x)=float(320+360*std::sin(theta));my.at<float>(y,x)=float(y);
        }
        cv::Mat current;cv::remap(ref,current,mx,my,cv::INTER_LINEAR,cv::BORDER_CONSTANT,{240,240,240});
        const auto r=inspector.inspect(current,p);
        std::cout<<"cylinder angle="<<angle<<" valid="<<r.adaptive.valid<<" score="<<r.score<<" residual="<<r.adaptive.residual<<"\n";
        require(r.adaptive.valid&&r.controlComplete&&!r.reject,"small actual cylindrical rotation passes");
        const auto q=r.adaptive.toCurrent({320,240});cv::rectangle(current,{cvRound(q.x)-12,cvRound(q.y)-12,25,25},{0,0,0},-1);
        require(inspector.inspect(current,p).reject,"defect on a rotated cylindrical texture still rejects");
    }
    auto seamRef=ref.clone();cv::rectangle(seamRef,{330,20,30,440},{30,30,30},-1);
    cv::line(seamRef,{342,25},{342,455},{240,240,240},3);
    cd::SeamParams seam;seam.enabled=true;seam.pts={{342,35},{342,445}};seam.bandHalf=14;seam.tolMid=seam.tolEdge=.8;
    inspector.setReference(seamRef,roi,p);
    auto good=inspector.inspect(seamRef,p,&seam);
    require(good.seam.found&&!good.seam.reject,"adaptive reference seam remains measurable");
    auto kink=seamRef.clone();cv::rectangle(kink,{330,210,30,24},{30,30,30},-1);cv::line(kink,{348,210},{348,233},{240,240,240},3);
    const auto bad=inspector.inspect(kink,p,&seam);
    require(bad.adaptive.valid&&bad.seam.reject&&bad.reject,"global deformation cannot straighten a local seam kink");
    const auto shifted=transformed(ref,{.04,1,0,.1,.01,0,1});inspector.setReference(ref,roi,p);
    const auto expected=inspector.inspect(shifted,p);
    std::vector<std::future<cd::InspectResult>> work;
    for(int i=0;i<8;++i)work.push_back(std::async(std::launch::async,[&]{return inspector.inspect(shifted,p);}));
    for(auto& item:work){const auto r=item.get();require(r.adaptive.valid&&r.score==expected.score&&r.adaptive.model==expected.adaptive.model,"parallel geometry does not mutate cached reference features");}
}
void combinedLabelAndExclusions() {
    const auto ref=reference();auto p=parameters();
    p.labelOutline={{40,40},{600,40},{600,440},{40,440}};
    p.exclusions={{{280,190},{365,190},{365,290},{280,290}}};
    // Adaptive registration owns the movement limit, even with a confirmed label.
    p.horizontalShiftMm=p.verticalShiftMm=.01;
    cd::Inspector inspector;inspector.setReference(ref,roi,p);
    const auto current=transformed(ref,{.04,1,0,.08,.01,0,1});
    for(double scale:{1.,.5}) {
        p.downscale=scale;
        const auto clean=inspector.inspect(current,p);
        require(clean.adaptive.valid&&clean.controlComplete&&!clean.reject,
            "adaptive geometry and confirmed contour accept a clean moving label");
        require(clean.native.labelEnabled,"confirmed label remains active in adaptive mode");
        require(!clean.native.shiftRefined,"adaptive registration does not claim an ECC refinement");
        require(!clean.native.inspectionMask.empty()&&!clean.native.comparisonMask.empty(),
            "both modes expose the effective comparison mask");
        require(cv::norm(clean.native.inspectionMask,
            clean.native.comparisonMask(clean.native.region),cv::NORM_INF)==0,
            "coverage and visual masks include the final label inset");
        auto defect=current.clone();const auto q=clean.adaptive.toCurrent({150,240});
        cv::rectangle(defect,{cvRound(q.x)-14,cvRound(q.y)-14,29,29},{0,0,0},-1);
        require(inspector.inspect(defect,p).reject,"combined modes still reject a new interior defect");
        auto ignored=current.clone();const auto hole=clean.adaptive.toCurrent({320,240});
        cv::rectangle(ignored,{cvRound(hole.x)-12,cvRound(hole.y)-12,25,25},{0,0,0},-1);
        require(!inspector.inspect(ignored,p).reject,"combined modes preserve moving manual exclusions");
    }
    p.exclusions={roi};inspector.setReference(ref,roi,p);
    const auto excluded=inspector.inspect(ref,p);
    require(!excluded.adaptive.valid&&excluded.adaptive.features==0&&
        excluded.reason=="label_coverage_insufficient",
        "fully excluded surface does not attempt unsupported registration");
}
void immutableMasks() {
    const auto ref=reference();cas::Reference native(ref);
    native.setRoi({{.02,.02},{.98,.02},{.98,.98},{.02,.98}});
    const auto prep=native.prepared(1);const auto mask=prep->roiMask.clone(),corners=prep->upperCornerMask.clone();
    cas::Settings settings;settings.shiftToleranceX=settings.shiftToleranceY=3;
    cas::InspectionGeometry geometry;geometry.prealigned=true;geometry.allowLearned=false;
    for(int i=0;i<4;++i) {
        geometry.validReference=cv::Mat(ref.size(),CV_8U,cv::Scalar(255));
        cv::rectangle(geometry.validReference,{i%2?480:0,0,160,ref.rows},0,-1);
        const auto result=cas::inspectFrame(native,ref,settings,true,&geometry);
        require(result.roiArea<prep->roiArea,"request mask reduces only that inspection's area");
        require(cv::norm(prep->roiMask,mask,cv::NORM_INF)==0&&cv::norm(prep->upperCornerMask,corners,cv::NORM_INF)==0,"adaptive masks never mutate the immutable cache");
    }
    const auto restored=cas::inspectFrame(native,ref,settings,true);
    require(restored.roiArea==prep->roiArea,"native comparison regains full ROI after adaptive requests");
}
}
int main() {
    cv::setNumThreads(1);
    const auto dir=std::filesystem::temp_directory_path()/("adaptive-roi-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {geometryAndDefects();cylinderAndSeam();combinedLabelAndExclusions();immutableMasks();persistence(dir);
        require(std::filesystem::weakly_canonical(dir).parent_path()==std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()),"temporary cleanup stays in temp");
        std::filesystem::remove_all(dir);
        std::cout<<"Adaptive ROI: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"; temp="<<dir.string()<<"\n";return 1;}
}
