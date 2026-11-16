#include "adaptive_roi.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

namespace cd {
namespace {
using Model = std::array<double,7>;
cv::Point2d normalise(cv::Point2f p, cv::Size size) {
    return {p.x / size.width - .5, p.y / size.height - .5};
}
cv::Point2d project(const Model& m, cv::Point2d p) {
    return {m[0]+m[1]*p.x+m[2]*p.y+m[3]*p.x*p.x, m[4]+m[5]*p.x+m[6]*p.y};
}
bool fit(const std::vector<cv::Point2d>& a, const std::vector<cv::Point2d>& b,
         const std::vector<int>& indices, Model& model) {
    cv::Mat x(int(indices.size()),4,CV_64F), y(int(indices.size()),3,CV_64F);
    cv::Mat u(int(indices.size()),1,CV_64F), v(int(indices.size()),1,CV_64F);
    for(int row=0;row<int(indices.size());++row) {
        const int i=indices[row]; const auto p=a[i];
        x.at<double>(row,0)=y.at<double>(row,0)=1;
        x.at<double>(row,1)=y.at<double>(row,1)=p.x;
        x.at<double>(row,2)=y.at<double>(row,2)=p.y;
        x.at<double>(row,3)=p.x*p.x;
        u.at<double>(row)=b[i].x; v.at<double>(row)=b[i].y;
    }
    cv::Mat sx,sy;
    if(!cv::solve(x,u,sx,cv::DECOMP_SVD)||!cv::solve(y,v,sy,cv::DECOMP_SVD))return false;
    model={sx.at<double>(0),sx.at<double>(1),sx.at<double>(2),sx.at<double>(3),
           sy.at<double>(0),sy.at<double>(1),sy.at<double>(2)};
    return std::all_of(model.begin(),model.end(),[](double q){return std::isfinite(q);});
}
bool plausible(const Model& m, cv::Size size, double maxMotion) {
    if(m[6]<.8||m[6]>1.2||std::abs(m[2])>.25||std::abs(m[5])>.25||std::abs(m[3])>1.1)return false;
    for(double x : {-.5,0.,.5}) for(double y : {-.5,0.,.5}) {
        const double dx=m[1]+2*m[3]*x;
        if(dx<.55||dx>1.65||dx*m[6]-m[2]*m[5]<.45)return false;
        const auto q=project(m,{x,y});
        if(std::hypot((q.x-x)*size.width,(q.y-y)*size.height)>maxMotion)return false;
    }
    return true;
}
cv::Mat gray(const cv::Mat& bgr, cv::Size size) {
    cv::Mat g;
    if(bgr.channels()==1)g=bgr;else cv::cvtColor(bgr,g,cv::COLOR_BGR2GRAY);
    if(g.size()!=size)cv::resize(g,g,size,0,0,cv::INTER_AREA);
    return g;
}
}

cv::Point2f AdaptiveRoiResult::toCurrent(cv::Point2f p) const {
    const auto q=project(model,normalise(p,size));
    return {float((q.x+.5)*size.width),float((q.y+.5)*size.height)};
}
cv::Mat AdaptiveRoiResult::toCurrentImage(const cv::Mat& input, int interpolation) const {
    if(input.empty()||inverseX.empty())return {};
    cv::Mat full=input,result;
    if(full.size()!=size)cv::resize(input,full,size,0,0,interpolation);
    cv::remap(full,result,inverseX,inverseY,interpolation,cv::BORDER_CONSTANT);
    return result;
}

AdaptiveRoiReference::AdaptiveRoiReference(const cv::Mat& bgr,const cv::Mat& inspectionMask)
    :size_(bgr.size()),mask_(inspectionMask.clone()) {
    const double scale=std::min(1.,768./std::max(size_.width,size_.height));
    trackingSize_={std::max(1,cvRound(size_.width*scale)),std::max(1,cvRound(size_.height*scale))};
    gray_=gray(bgr,trackingSize_);
    const double coarseScale=std::min(1.,256./std::max(gray_.cols,gray_.rows));
    cv::Mat small;cv::resize(gray_,small,{std::max(16,cvRound(gray_.cols*coarseScale)),std::max(16,cvRound(gray_.rows*coarseScale))},0,0,cv::INTER_AREA);
    small.convertTo(coarse_,CV_32F);cv::createHanningWindow(window_,small.size(),CV_32F);
    cv::Mat mask;cv::resize(mask_,mask,trackingSize_,0,0,cv::INTER_NEAREST);
    cv::erode(mask,mask,cv::getStructuringElement(cv::MORPH_RECT,{15,15}));
    cv::Mat coarseMask;cv::resize(mask,coarseMask,window_.size(),0,0,cv::INTER_AREA);
    window_.setTo(0,coarseMask<254);
    // Limit clustering: each of 48 cells contributes at most six landmarks.
    for(int row=0;row<6;++row)for(int col=0;col<8;++col) {
        const cv::Rect cell(col*gray_.cols/8,row*gray_.rows/6,
            (col+1)*gray_.cols/8-col*gray_.cols/8,(row+1)*gray_.rows/6-row*gray_.rows/6);
        if(cell.empty())continue;
        std::vector<cv::Point2f> found;
        cv::goodFeaturesToTrack(gray_(cell),found,6,.015,7,mask(cell),5);
        for(auto p:found)points_.push_back(p+cv::Point2f(float(cell.x),float(cell.y)));
    }
    if(!points_.empty())cv::buildOpticalFlowPyramid(gray_,pyramid_,{21,21},3);
}

AdaptiveRoiResult AdaptiveRoiReference::locate(const cv::Mat& current,const AdaptiveRoiOptions& options) const {
    AdaptiveRoiResult r;r.size=size_;r.features=int(points_.size());
    const auto start=std::chrono::steady_clock::now();
    auto done=[&](const char* reason){r.reason=reason;r.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return r;};
    if(points_.size()<16)return done("adaptive_registration_failed");
    auto g=gray(current,trackingSize_);
    std::vector<cv::Mat> pyramid;cv::buildOpticalFlowPyramid(g,pyramid,{21,21},3);
    std::vector<cv::Point2f> forward=points_,backward=points_;
    cv::Mat small,smallF;cv::resize(g,small,coarse_.size(),0,0,cv::INTER_AREA);small.convertTo(smallF,CV_32F);
    auto referenceCoarse=coarse_.clone();double response=0;
    const auto shift=cv::phaseCorrelate(referenceCoarse,smallF,window_,&response);
    const cv::Point2f initial(float(shift.x*g.cols/coarse_.cols),float(shift.y*g.rows/coarse_.rows));
    if(response>.05 && cv::norm(initial)*size_.width/trackingSize_.width<=options.maxMotion)
        for(auto& p:forward)p+=initial;
    std::vector<uchar> status,backStatus;std::vector<float> error,backError;
    cv::calcOpticalFlowPyrLK(pyramid_,pyramid,points_,forward,status,error,{21,21},3,
        {cv::TermCriteria::COUNT|cv::TermCriteria::EPS,25,.01},cv::OPTFLOW_USE_INITIAL_FLOW);
    cv::calcOpticalFlowPyrLK(pyramid,pyramid_,forward,backward,backStatus,backError,{21,21},3,
        {cv::TermCriteria::COUNT|cv::TermCriteria::EPS,25,.01},cv::OPTFLOW_USE_INITIAL_FLOW);
    std::vector<cv::Point2d> a,b;std::vector<int> source;
    for(size_t i=0;i<points_.size();++i) {
        if(!status[i]||!backStatus[i]||error[i]>28||cv::norm(points_[i]-backward[i])>1.0||
           forward[i].x<1||forward[i].y<1||forward[i].x>=g.cols-1||forward[i].y>=g.rows-1)continue;
        a.push_back(normalise(points_[i],trackingSize_));b.push_back(normalise(forward[i],trackingSize_));source.push_back(int(i));
    }
    r.tracked=int(a.size());if(a.size()<16)return done("adaptive_registration_failed");
    auto inliers=[&](const Model& m){
        std::vector<int> ids;
        for(int i=0;i<int(a.size());++i){const auto d=project(m,a[i])-b[i];
            if(std::hypot(d.x*g.cols,d.y*g.rows)<=1.6)ids.push_back(i);}
        return ids;
    };
    cv::RNG random(0x6141);std::vector<int> best;
    for(int iteration=0;iteration<240;++iteration) {
        std::vector<int> sample;
        while(sample.size()<4){const int i=random.uniform(0,int(a.size()));if(std::find(sample.begin(),sample.end(),i)==sample.end())sample.push_back(i);}
        Model candidate;
        if(!fit(a,b,sample,candidate)||!plausible(candidate,size_,options.maxMotion))continue;
        auto ids=inliers(candidate);if(ids.size()>best.size()){best=std::move(ids);r.model=candidate;}
    }
    if(best.size()<16||best.size()<a.size()*.55)return done("adaptive_registration_failed");
    for(int iteration=0;iteration<3;++iteration){if(!fit(a,b,best,r.model))return done("adaptive_registration_failed");best=inliers(r.model);if(best.size()<16)return done("adaptive_registration_failed");}
    if(best.size()<a.size()*.55||!plausible(r.model,size_,options.maxMotion))return done("adaptive_registration_failed");
    r.inliers=int(best.size());
    cv::Rect2f spread;bool first=true;int cells=0;std::array<bool,12> occupied{};
    double squared=0;
    for(int i:best) {
        const cv::Point2f p(float((a[i].x+.5)*size_.width),float((a[i].y+.5)*size_.height));
        const cv::Point2f q(float((b[i].x+.5)*size_.width),float((b[i].y+.5)*size_.height));
        r.referencePoints.push_back(p);r.currentPoints.push_back(q);
        if(first){spread={p.x,p.y,.001f,.001f};first=false;}else spread|=cv::Rect2f(p.x,p.y,.001f,.001f);
        const auto d=project(r.model,a[i])-b[i];squared+=std::pow(d.x*size_.width,2)+std::pow(d.y*size_.height,2);
        const int cell=std::clamp(int((a[i].y+.5)*3),0,2)*4+std::clamp(int((a[i].x+.5)*4),0,3);
        if(!occupied[cell]){occupied[cell]=true;++cells;}
    }
    const auto bounds=cv::boundingRect(mask_);
    if(spread.width<bounds.width*.45||spread.height<bounds.height*.35||cells<4)return done("adaptive_registration_failed");
    r.residual=std::sqrt(squared/best.size());
    r.mapX.create(size_,CV_32F);r.mapY.create(size_,CV_32F);r.validReference=cv::Mat::zeros(size_,CV_8U);
    r.inverseX.create(size_,CV_32F);r.inverseY.create(size_,CV_32F);
    for(int y=0;y<size_.height;++y)for(int x=0;x<size_.width;++x) {
        const auto q=r.toCurrent({float(x),float(y)});r.mapX.at<float>(y,x)=q.x;r.mapY.at<float>(y,x)=q.y;
        // Geometric footprint only: missing feature matches NEVER punch holes in the inspection.
        if(q.x>=3&&q.y>=3&&q.x<size_.width-4&&q.y<size_.height-4)r.validReference.at<uchar>(y,x)=255;
        const double tx=double(x)/size_.width-.5,ty=double(y)/size_.height-.5;
        double rx=tx;
        for(int k=0;k<6;++k) {
            const double ry=(ty-r.model[4]-r.model[5]*rx)/r.model[6];
            const double value=r.model[0]+r.model[1]*rx+r.model[2]*ry+r.model[3]*rx*rx-tx;
            const double derivative=r.model[1]+2*r.model[3]*rx-r.model[2]*r.model[5]/r.model[6];
            rx-=value/std::max(.3,derivative);
        }
        const double ry=(ty-r.model[4]-r.model[5]*rx)/r.model[6];
        r.inverseX.at<float>(y,x)=float((rx+.5)*size_.width);r.inverseY.at<float>(y,x)=float((ry+.5)*size_.height);
    }
    cv::Mat checked;cv::bitwise_and(mask_,r.validReference,checked);
    const int area=cv::countNonZero(mask_);r.coverage=area?double(cv::countNonZero(checked))/area:0;
    r.checkedCurrent=r.toCurrentImage(checked,cv::INTER_NEAREST);
    if(r.coverage<options.minCoverage)return done("adaptive_low_coverage");
    cv::remap(current,r.aligned,r.mapX,r.mapY,cv::INTER_LINEAR,cv::BORDER_CONSTANT);
    r.valid=true;return done("");
}
}
