#include "seam_reference.h"
#include "seam_mask.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>

namespace cd {
namespace {
constexpr int radius=96; // fixed capture area: moving the tolerance never changes the measured path
constexpr int width=2*radius+1;
const double missing=std::numeric_limits<double>::quiet_NaN();
struct Strip { cv::Mat raw,smooth,dx,mask,valid; };

Strip sample(const cv::Mat& image,const SeamReference& ref,cv::Point2d pose) {
    Strip out;cv::Mat mx(ref.rows,width,CV_32F),my(ref.rows,width,CV_32F);out.valid=cv::Mat(ref.rows,width,CV_8U,cv::Scalar(0));
    for(int i=0;i<ref.rows;++i)for(int j=0;j<width;++j){
        const auto q=ref.origin+pose+ref.u*i+ref.normal*(j-radius);
        mx.at<float>(i,j)=float(q.x);my.at<float>(i,j)=float(q.y);
        out.valid.at<uchar>(i,j)=q.x>=2&&q.y>=2&&q.x<image.cols-2&&q.y<image.rows-2?255:0;
    }
    cv::remap(image,out.raw,mx,my,cv::INTER_LINEAR,cv::BORDER_REPLICATE);
    cv::Mat grey,dy;cv::cvtColor(out.raw,grey,cv::COLOR_BGR2GRAY);cv::GaussianBlur(grey,out.smooth,{3,7},.8,1.5);
    cv::Sobel(out.smooth,out.dx,CV_32F,1,0);cv::Sobel(out.smooth,dy,CV_32F,0,1);
    cv::Canny(out.smooth,out.mask,8,24,3,true);
    for(int i=0;i<ref.rows;++i)for(int j=0;j<width;++j)if(!out.valid.at<uchar>(i,j)||std::abs(out.dx.at<float>(i,j))<1.25f*std::abs(dy.at<float>(i,j)))out.mask.at<uchar>(i,j)=0;
    return out;
}
double quantile(std::vector<double> v,double p){if(v.empty())return 1e6;size_t at=size_t((v.size()-1)*p);std::nth_element(v.begin(),v.begin()+at,v.end());return v[at];}
double smoothness(const std::vector<double>& v){
    std::vector<double> changes,center;size_t count=0;
    for(size_t i=0;i<v.size();++i)if(std::isfinite(v[i])){
        ++count;center.push_back(std::abs(v[i]));
        if(i>0&&i+1<v.size()&&std::isfinite(v[i-1])&&std::isfinite(v[i+1]))changes.push_back(std::abs(v[i-1]-2*v[i]+v[i+1]));
    }
    // Choose a persistent reference boundary once. Brighter print is not a reason to switch edges live.
    return 20.*(1.-double(count)/v.size())+quantile(changes,.95)+.01*quantile(center,.5);
}
double subpixel(const cv::Mat& dx,int row,int column){
    const double a=std::abs(dx.at<float>(row,column-1)),b=std::abs(dx.at<float>(row,column)),c=std::abs(dx.at<float>(row,column+1));
    const double denominator=a-2*b+c;return std::abs(denominator)>1e-6?std::clamp(.5*(a-c)/denominator,-.5,.5):0;
}
double matchLoss(const Strip& current,const SeamReference& ref,int row,int column,double brightnessBias){
    const int expected=std::clamp(cvRound(ref.axis[row])+radius,7,width-8);
    double aMean=0,bMean=0;int count=0;
    for(int di=-2;di<=2;++di){const int i=std::clamp(row+di,0,ref.rows-1);for(int dj=-5;dj<=5;++dj){aMean+=current.smooth.at<uchar>(i,column+dj);bMean+=ref.smooth.at<uchar>(i,expected+dj);++count;}}
    aMean/=count;bMean/=count;double error=0;
    for(int di=-2;di<=2;++di){const int i=std::clamp(row+di,0,ref.rows-1);for(int dj=-5;dj<=5;++dj)error+=std::abs((current.smooth.at<uchar>(i,column+dj)-aMean)-(ref.smooth.at<uchar>(i,expected+dj)-bMean));}
    const double strength=std::max(8.,std::abs(double(ref.dx.at<float>(row,expected))));
    const double contrast=std::abs(current.dx.at<float>(row,column));
    return error/count/std::max(3.,strength/4.)+.60*std::abs(std::log((contrast+4.)/(strength+4.)))+.05*std::abs((aMean-brightnessBias)-bMean);
}
}

std::string seamReferenceSignature(const std::string& pixels,const SeamParams& p){
    std::ostringstream out;out<<"seam-ref-v1:"<<pixels<<std::setprecision(17);
    out<<":band="<<std::max(12.,std::min(30.,p.bandHalf));
    for(const auto& q:p.pts)out<<':'<<q.x<<','<<q.y;
    return out.str();
}

std::shared_ptr<const SeamReference> prepareSeamReference(const cv::Mat& image,const SeamParams& params,const std::string& pixels){
    auto ref=std::make_shared<SeamReference>();ref->signature=seamReferenceSignature(pixels,params);ref->imageSize=image.size();
    auto p=params;p.enabled=true;p.referenceSearch=false;p.maskSearch=true;p.bandHalf=std::max(12.,std::min(30.,p.bandHalf));
    p.endIgnore=.03;
    p.maskLow=8;p.maskHigh=24;p.maskCoverage=.7;p.maskMaxGap=.2;
    const auto extracted=measureMaskedSeam(image,p);
    if(extracted.candidateOffsets.empty()){ref->reason=extracted.maskReason.empty()?"seam_reference_not_found":extracted.maskReason;return ref;}
    size_t best=0;for(size_t k=1;k<extracted.candidateOffsets.size();++k)if(smoothness(extracted.candidateOffsets[k])<smoothness(extracted.candidateOffsets[best]))best=k;
    ref->edge=extracted.candidateOffsets[best];ref->sign=extracted.candidateSigns[best];ref->rows=int(ref->edge.size());
    ref->origin=p.pts.front();const auto d=p.pts.back()-p.pts.front();ref->length=cv::norm(d);ref->u=d/ref->length;ref->normal={-ref->u.y,ref->u.x};
    // Freeze subpixel positions from exactly the same sampling grid used at runtime.
    // Otherwise a different strip border can give a nonzero self-comparison.
    const auto strip=sample(image,*ref,{});
    for(int i=0;i<ref->rows;++i)if(std::isfinite(ref->edge[i])){
        const int column=std::clamp(cvRound(ref->edge[i])+radius,1,width-2);
        ref->edge[i]=column-radius+subpixel(strip.dx,i,column);
    }
    int eligible=0,observed=0,gap=0,longest=0;
    for(int i=0;i<ref->rows;++i){const double t=double(i)/ref->length;if(t<.03||t>.97)continue;++eligible;if(std::isfinite(ref->edge[i])){++observed;gap=0;}else longest=std::max(longest,++gap);}
    ref->coverage=eligible?double(observed)/eligible:0;
    if(ref->coverage<.95||longest>eligible*.05){ref->reason="seam_reference_not_found";return ref;}
    ref->axis=ref->edge;
    for(int i=0;i<ref->rows;++i)if(!std::isfinite(ref->axis[i])){
        int left=i-1,right=i+1;while(left>=0&&!std::isfinite(ref->edge[left]))--left;while(right<ref->rows&&!std::isfinite(ref->edge[right]))++right;
        ref->axis[i]=left>=0&&right<ref->rows?ref->edge[left]+(ref->edge[right]-ref->edge[left])*double(i-left)/(right-left):left>=0?ref->edge[left]:right<ref->rows?ref->edge[right]:0;
    }
    ref->smooth=strip.smooth;ref->dx=strip.dx;ref->ready=true;return ref;
}

SeamResult inspectReferenceSeam(const cv::Mat& image,const SeamReference* ref,const SeamParams& p,cv::Point2d pose,const cv::Mat& excluded){
    const auto begin=std::chrono::steady_clock::now();SeamResult r;r.enabled=p.enabled;r.maskSearch=true;r.referenceSearch=true;r.controlComplete=false;
    auto done=[&](){r.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return r;};
    if(!ref||!ref->ready||p.referenceSignature!=ref->signature){r.maskReason="seam_reference_required";return done();}
    if(image.size()!=ref->imageSize||p.pts.size()!=2||cv::norm(p.pts.front()-ref->origin)>1e-6||cv::norm(p.pts.back()-(ref->origin+ref->u*ref->length))>1e-6){r.maskReason="seam_reference_required";return done();}
    const auto strip=sample(image,*ref,pose);const int rows=ref->rows;
    std::vector<double> brightness;
    for(int i=0;i<rows;i+=4)for(int side:{-16,16}){const int j=std::clamp(cvRound(ref->axis[i])+radius+side,0,width-1);if(strip.valid.at<uchar>(i,j))brightness.push_back(double(strip.smooth.at<uchar>(i,j))-ref->smooth.at<uchar>(i,j));}
    const double brightnessBias=brightness.empty()?0:quantile(brightness,.5);
    cv::Mat cost(rows,width,CV_32F,cv::Scalar(1e6)),observed(rows,width,CV_8U,cv::Scalar(0)),back(rows,width,CV_16S);
    r.rowPos.resize(rows);r.rowRef.resize(rows);r.rowEdge.resize(rows);r.rowDev.assign(rows,float(missing));r.rowTol.assign(rows,float(p.referenceTolerance));r.rowState.resize(rows);
    r.path={cv::Point2f(ref->origin+pose),cv::Point2f(ref->origin+pose+ref->u*(rows-1))};r.pathNormal={cv::Point2f(ref->normal),cv::Point2f(ref->normal)};
    for(int i=0;i<rows;++i){
        const double t=double(i)/ref->length;const auto point=ref->origin+pose+ref->u*i;r.rowPos[i]=cv::Point2f(point);r.rowRef[i]=cv::Point2f(point+ref->normal*ref->axis[i]);
        if(t<.03||t>.97)r.rowState[i]=2;
        const auto expected=point+ref->normal*ref->axis[i];const int x=cvRound(expected.x),y=cvRound(expected.y);
        const bool expectedVisible=x>=2&&y>=2&&x<image.cols-2&&y<image.rows-2;
        if(!excluded.empty()&&x>=0&&y>=0&&x<excluded.cols&&y<excluded.rows&&excluded.at<uchar>(y,x))r.rowState[i]=1;
        for(int j=7;j<width-7;++j){
            const double displacement=j-radius-ref->axis[i];
            float c=.06f*float(std::abs(displacement));
            if(!r.rowState[i]&&expectedVisible&&std::isfinite(ref->edge[i])&&strip.mask.at<uchar>(i,j)&&strip.dx.at<float>(i,j)*ref->sign>0){
                const double loss=matchLoss(strip,*ref,i,j,brightnessBias);
                if(loss<3.){observed.at<uchar>(i,j)=255;c+=float(-2.+loss);}
                else c+=2.f;
            }else if(!r.rowState[i])c+=2.f;
            cost.at<float>(i,j)=c;
        }
    }
    std::vector<float> prev(width,0),next(width);
    for(int i=0;i<rows;++i){
        const double change=i?ref->axis[i]-ref->axis[i-1]:0;
        for(int j=0;j<width;++j){float best=1e9;int from=j;
            for(int k=std::max(0,j-3);k<=std::min(width-1,j+3);++k){const float d=float(j-k-change),candidate=prev[k]+.12f*std::abs(d)+.20f*d*d;if(candidate<best){best=candidate;from=k;}}
            next[j]=best+cost.at<float>(i,j);back.at<short>(i,j)=short(from);
        }prev.swap(next);
    }
    int column=int(std::min_element(prev.begin(),prev.end())-prev.begin());std::vector<int> path(rows);
    for(int i=rows-1;i>=0;--i){path[i]=column;column=back.at<short>(i,column);}
    int eligible=0,found=0,gap=0,longest=0;
    for(int i=0;i<rows;++i){
        if(r.rowState[i]){gap=0;continue;}++eligible;const int j=path[i];
        if(!observed.at<uchar>(i,j)||!std::isfinite(ref->edge[i])){r.rowState[i]=3;longest=std::max(longest,++gap);continue;}
        ++found;gap=0;const double offset=j-radius+subpixel(strip.dx,i,j),dev=offset-ref->edge[i];
        r.rowEdge[i]=cv::Point2f(ref->origin+pose+ref->u*i+ref->normal*offset);r.rowDev[i]=float(dev);
        if(i%4==0)r.centerPts.push_back(r.rowEdge[i]);
        if(std::abs(dev)>p.referenceTolerance)r.badPts.push_back(r.rowEdge[i]);
        const double ratio=std::abs(dev)/p.referenceTolerance;if(ratio>r.ratio){r.ratio=ratio;r.maxDev=std::abs(dev);r.worstT=double(i)/ref->length;}
    }
    r.coverage=eligible?double(found)/eligible:0;r.longestGap=eligible?double(longest)/eligible:1;r.found=r.coverage>=.7;r.controlComplete=r.found&&r.longestGap<=.2;r.maskSides=1;
    r.maskReason=!r.found?"seam_mask_not_found":r.longestGap>.2?"seam_mask_gap":"";r.reject=r.controlComplete&&r.ratio>1;
    cv::Mat raw,mask;cv::transpose(strip.raw,raw);cv::transpose(strip.mask,mask);cv::cvtColor(mask,mask,cv::COLOR_GRAY2BGR);
    r.maskPreview=cv::Mat(2*width+4,rows,CV_8UC3,cv::Scalar(24,24,24));raw.copyTo(r.maskPreview({0,0,rows,width}));mask.copyTo(r.maskPreview({0,width+4,rows,width}));
    for(int i=0;i<rows;++i){const int base=cvRound(ref->axis[i])+radius;
        for(int sign:{-1,1}){int y=cvRound(base+sign*p.referenceTolerance);if(y>=0&&y<width){r.maskPreview.at<cv::Vec3b>(y,i)={255,210,0};r.maskPreview.at<cv::Vec3b>(y+width+4,i)={255,210,0};}}
        if(r.rowState[i]==0){const int y=path[i];const cv::Vec3b color=std::abs(r.rowDev[i])>p.referenceTolerance?cv::Vec3b(0,0,255):cv::Vec3b(0,230,0);r.maskPreview.at<cv::Vec3b>(y,i)=color;r.maskPreview.at<cv::Vec3b>(y+width+4,i)=color;}
    }
    return done();
}
}
