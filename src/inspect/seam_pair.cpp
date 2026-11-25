#include "seam_mask.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <array>

namespace cd {
namespace {
const float missing=std::numeric_limits<float>::quiet_NaN();
struct Strip { cv::Mat raw,smooth,dx,mask,valid; };
double median(std::vector<double> v) {
    if(v.empty())return 0;
    auto at=v.begin()+v.size()/2;std::nth_element(v.begin(),at,v.end());return *at;
}
Strip sample(const cv::Mat& image,const SeamParams& p,cv::Point2d pose,int rows,int band,cv::Point2d u,cv::Point2d n) {
    const int cols=2*band+1;Strip s;
    cv::Mat mx(rows,cols,CV_32F),my(rows,cols,CV_32F);s.valid=cv::Mat(rows,cols,CV_8U,cv::Scalar(0));
    for(int i=0;i<rows;++i)for(int j=0;j<cols;++j){
        const auto q=p.pts.front()+pose+u*i+n*(j-band);
        mx.at<float>(i,j)=float(q.x);my.at<float>(i,j)=float(q.y);
        s.valid.at<uchar>(i,j)=q.x>=2&&q.y>=2&&q.x<image.cols-2&&q.y<image.rows-2?255:0;
    }
    cv::remap(image,s.raw,mx,my,cv::INTER_LINEAR,cv::BORDER_REPLICATE);
    cv::Mat gray,dy;cv::cvtColor(s.raw,gray,cv::COLOR_BGR2GRAY);cv::GaussianBlur(gray,s.smooth,{3,7},.8,1.5);
    cv::Canny(s.smooth,s.mask,p.maskLow,p.maskHigh,3,true);
    cv::Sobel(s.smooth,s.dx,CV_32F,1,0);cv::Sobel(s.smooth,dy,CV_32F,0,1);
    for(int i=0;i<rows;++i)for(int j=0;j<cols;++j)
        if(!s.valid.at<uchar>(i,j)||j<4||j>=cols-4||std::abs(s.dx.at<float>(i,j))<1.25f*std::abs(dy.at<float>(i,j)))s.mask.at<uchar>(i,j)=0;
    return s;
}
double subpixel(const cv::Mat& dx,int i,int j) {
    const double a=std::abs(dx.at<float>(i,j-1)),b=std::abs(dx.at<float>(i,j)),c=std::abs(dx.at<float>(i,j+1));
    const double d=a-2*b+c;return std::abs(d)>1e-6?std::clamp(.5*(a-c)/d,-.5,.5):0;
}
std::vector<double> interpolate(const std::vector<double>& edge) {
    auto axis=edge;int previous=-1;
    for(int i=0;i<int(edge.size());++i)if(std::isfinite(edge[i])){
        if(previous<0)std::fill(axis.begin(),axis.begin()+i,edge[i]);
        else for(int k=previous+1;k<i;++k)axis[k]=edge[previous]+(edge[i]-edge[previous])*double(k-previous)/(i-previous);
        previous=i;
    }
    if(previous>=0)std::fill(axis.begin()+previous+1,axis.end(),edge[previous]);
    return axis;
}
double profileLoss(const Strip& a,const Strip& b,int i,int j,int expected,double bias) {
    double am=0,bm=0;int count=0;
    for(int di=-2;di<=2;++di)for(int dj=-3;dj<=3;++dj){const int row=std::clamp(i+di,0,a.raw.rows-1);am+=a.smooth.at<uchar>(row,j+dj);bm+=b.smooth.at<uchar>(row,expected+dj);++count;}
    am/=count;bm/=count;double err=0;
    for(int di=-2;di<=2;++di)for(int dj=-3;dj<=3;++dj){const int row=std::clamp(i+di,0,a.raw.rows-1);err+=std::abs((a.smooth.at<uchar>(row,j+dj)-am)-(b.smooth.at<uchar>(row,expected+dj)-bm));}
    const double strength=std::max(8.,std::abs(double(b.dx.at<float>(i,expected)))),contrast=std::abs(a.dx.at<float>(i,j));
    return err/count/std::max(3.,strength/4.)+.5*std::abs(std::log((contrast+4)/(strength+4)))+.04*std::abs(am-bias-bm);
}
std::vector<double> follow(const Strip& current,const Strip& ref,const std::vector<double>& expected,
                           const std::vector<double>& axis,int sign,int band,double reach,
                           const std::vector<char>& state,double bias) {
    const int rows=current.raw.rows,cols=current.raw.cols;
    cv::Mat observed(rows,cols,CV_8U,cv::Scalar(0)),cost(rows,cols,CV_32F,cv::Scalar(1e5)),back(rows,cols,CV_16S);
    for(int i=0;i<rows;++i){const int base=std::clamp(cvRound(axis[i])+band,4,cols-5);
        for(int j=4;j<cols-4;++j){
            const double d=j-band-axis[i];float c=float(.10*std::abs(d));
            if(!state[i]&&std::isfinite(expected[i])&&current.valid.at<uchar>(i,base)&&current.mask.at<uchar>(i,j)&&current.dx.at<float>(i,j)*sign>0&&std::abs(d)<=reach){
                const double loss=profileLoss(current,ref,i,j,base,bias);
                // Appearance ranks real Canny edges; changed print/brightness alone must not erase one.
                observed.at<uchar>(i,j)=255;c+=float(-2.+.1*std::min(10.,loss));
            }else if(!state[i])c+=2;
            cost.at<float>(i,j)=c;
        }
    }
    std::vector<float> prev(cols),next(cols);
    for(int i=0;i<rows;++i){const double change=i?axis[i]-axis[i-1]:0;
        for(int j=0;j<cols;++j){float best=1e9;int from=j;
            for(int k=std::max(0,j-3);k<=std::min(cols-1,j+3);++k){const float d=float(j-k-change),v=prev[k]+.12f*std::abs(d)+.2f*d*d;if(v<best){best=v;from=k;}}
            next[j]=best+cost.at<float>(i,j);back.at<short>(i,j)=short(from);
        }prev.swap(next);
    }
    int j=int(std::min_element(prev.begin(),prev.end())-prev.begin());std::vector<double> offsets(rows,missing);
    for(int i=rows-1;i>=0;--i){if(observed.at<uchar>(i,j))offsets[i]=j-band+subpixel(current.dx,i,j);j=back.at<short>(i,j);}
    return offsets;
}
}

SeamResult inspectMaskedSeam(const cv::Mat& image,const cv::Mat& reference,const SeamParams& p,cv::Point2d pose,const cv::Mat& excluded) {
    if(p.mode==SeamParams::Step)return measureMaskedSeam(image,p,excluded);
    const auto begin=std::chrono::steady_clock::now();SeamResult r;r.enabled=p.enabled;r.maskSearch=true;r.pairedMask=true;r.controlComplete=false;
    auto done=[&](){r.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return r;};
    if(!p.enabled){r.controlComplete=true;return done();}
    const auto extracted=measureMaskedSeam(reference,p);
    if(extracted.candidateOffsets.size()!=2){r.maskReason=extracted.maskReason.empty()?"seam_mask_reference_pair":extracted.maskReason;return done();}
    if(image.empty()||image.size()!=reference.size()){r.maskReason="seam_mask_outside_frame";return done();}
    const auto d=p.pts.back()-p.pts.front();const double length=cv::norm(d);const int rows=int(length),band=std::min(250,int(std::ceil(p.bandHalf))+20),cols=2*band+1;
    const auto u=d/length;const cv::Point2d n(-u.y,u.x),origin=p.pts.front()+pose;
    const auto ref=sample(reference,p,{},rows,band,u,n),current=sample(image,p,pose,rows,band,u,n);
    std::array<std::vector<double>,2> edges={extracted.candidateOffsets[0],extracted.candidateOffsets[1]};
    std::array<int,2> signs={extracted.candidateSigns[0],extracted.candidateSigns[1]};
    std::vector<double> distance;
    for(int i=0;i<rows;++i)if(std::isfinite(edges[0][i])&&std::isfinite(edges[1][i]))distance.push_back(edges[1][i]-edges[0][i]);
    if(distance.empty()){r.maskReason="seam_mask_reference_pair";return done();}
    if(median(distance)<0){std::swap(edges[0],edges[1]);std::swap(signs[0],signs[1]);}
    // Refine on the exact runtime grid. Interpolation guides search only, never creates reference evidence.
    for(int k=0;k<2;++k)for(int i=0;i<rows;++i)if(std::isfinite(edges[k][i])){
        const int base=cvRound(edges[k][i])+band;int best=-1;
        for(int j=base-2;j<=base+2;++j)if(ref.mask.at<uchar>(i,j)&&ref.dx.at<float>(i,j)*signs[k]>0)
            if(best<0||std::abs(j-base)<std::abs(best-base)||(std::abs(j-base)==std::abs(best-base)&&std::abs(ref.dx.at<float>(i,j))>std::abs(ref.dx.at<float>(i,best))))best=j;
        if(best<0)edges[k][i]=missing;else edges[k][i]=best-band+subpixel(ref.dx,i,best);
    }
    std::array<std::vector<double>,2> axes={interpolate(edges[0]),interpolate(edges[1])};
    r.rowPos.resize(rows);r.rowEdge.resize(rows);r.rowRef.resize(rows);r.rowDev.assign(rows,missing);r.rowTol.resize(rows);r.rowState.resize(rows);
    r.rowWidth.assign(rows,missing);r.rowReferenceWidth.assign(rows,missing);r.rowWidthBad.assign(rows,0);r.rowWidthReject.assign(rows,0);r.sides.resize(2);r.maskSides=2;
    r.path={cv::Point2f(origin),cv::Point2f(origin+u*(rows-1))};r.pathNormal={cv::Point2f(n),cv::Point2f(n)};
    int eligible=0,referenceCount=0;
    for(int i=0;i<rows;++i){const double t=double(i)/length;r.rowPos[i]=cv::Point2f(origin+u*i);
        r.rowState[i]=(t<p.endIgnore||t>1-p.endIgnore)?2:0;
        r.rowTol[i]=float(std::max(.05,std::min(t,1-t)<p.edgeFrac?p.tolEdge:p.tolMid));
        if(!excluded.empty())for(int j=band-int(p.bandHalf);j<=band+int(p.bandHalf);++j){const auto q=origin+u*i+n*(j-band);const int x=cvRound(q.x),y=cvRound(q.y);if(x>=0&&y>=0&&x<image.cols&&y<image.rows&&excluded.at<uchar>(y,x))r.rowState[i]=1;}
        if(r.rowState[i])continue;++eligible;
        if(std::isfinite(edges[0][i])&&std::isfinite(edges[1][i])&&edges[1][i]-edges[0][i]>=1){++referenceCount;r.rowReferenceWidth[i]=float(edges[1][i]-edges[0][i]);}
    }
    r.pairReferenceCoverage=eligible?double(referenceCount)/eligible:0;
    if(r.pairReferenceCoverage<p.maskCoverage){for(auto& state:r.rowState)if(!state)state=4;r.maskReason="seam_mask_reference_pair";return done();}
    std::vector<double> brightness;
    for(int i=0;i<rows;i+=4)for(int j:{4,cols-5})if(current.valid.at<uchar>(i,j)&&ref.valid.at<uchar>(i,j))brightness.push_back(double(current.smooth.at<uchar>(i,j))-ref.smooth.at<uchar>(i,j));
    const double bias=median(brightness);std::array<std::vector<double>,2> measured;
    for(int k=0;k<2;++k){
        // The identical path solver/grid establishes the observed reference offsets too.
        // Its interpolated guide stays fixed, so half-pixel ties cannot introduce self-deviation.
        auto baseline=follow(ref,ref,edges[k],axes[k],signs[k],band,std::max(8.,p.bandHalf),r.rowState,0);
        measured[k]=follow(current,ref,edges[k],axes[k],signs[k],band,std::max(8.,p.bandHalf),r.rowState,bias);
        edges[k]=std::move(baseline);
    }
    referenceCount=0;
    for(int i=0;i<rows;++i){r.rowReferenceWidth[i]=missing;if(!r.rowState[i]&&std::isfinite(edges[0][i])&&std::isfinite(edges[1][i])&&edges[1][i]-edges[0][i]>=1){++referenceCount;r.rowReferenceWidth[i]=float(edges[1][i]-edges[0][i]);}}
    r.pairReferenceCoverage=eligible?double(referenceCount)/eligible:0;
    if(r.pairReferenceCoverage<p.maskCoverage){for(auto& state:r.rowState)if(!state)state=4;r.maskReason="seam_mask_reference_pair";return done();}
    int found=0,gap=0,longest=0,widthRun=0;std::array<int,2> sideFound{},sideEligible{},sideRun{};
    for(int k=0;k<2;++k){auto& side=r.sides[k];side.sign=signs[k];side.state=r.rowState;side.deviation.assign(rows,missing);side.edge.resize(rows);side.reference.resize(rows);}
    for(int i=0;i<rows;++i){
        if(std::isfinite(measured[0][i])&&std::isfinite(measured[1][i])&&measured[0][i]>=measured[1][i])measured[0][i]=measured[1][i]=missing;
        bool both=true;
        for(int k=0;k<2;++k){auto& side=r.sides[k];side.reference[i]=cv::Point2f(origin+u*i+n*(std::isfinite(edges[k][i])?edges[k][i]:axes[k][i]));
            if(r.rowState[i]){sideRun[k]=0;continue;}
            if(!std::isfinite(edges[k][i])){side.state[i]=4;sideRun[k]=0;both=false;continue;}
            ++sideEligible[k];
            if(!std::isfinite(measured[k][i])){side.state[i]=3;side.longestMissingPx=std::max(side.longestMissingPx,double(++sideRun[k]));both=false;continue;}
            sideRun[k]=0;++sideFound[k];side.edge[i]=cv::Point2f(origin+u*i+n*measured[k][i]);side.deviation[i]=float(measured[k][i]-edges[k][i]);
            if(std::abs(side.deviation[i])>r.rowTol[i])r.badPts.push_back(side.edge[i]);
            const double ratio=std::abs(side.deviation[i])/r.rowTol[i];if(ratio>r.ratio){r.ratio=ratio;r.maxDev=std::abs(side.deviation[i]);r.worstT=double(i)/length;}
        }
        if(r.rowState[i]){gap=widthRun=0;continue;}
        if(!both||!std::isfinite(r.rowReferenceWidth[i])||measured[1][i]<=measured[0][i]){
            r.rowState[i]=std::isfinite(r.rowReferenceWidth[i])?3:4;longest=std::max(longest,++gap);widthRun=0;continue;
        }
        ++found;gap=0;const int worst=std::abs(r.sides[0].deviation[i])>std::abs(r.sides[1].deviation[i])?0:1;
        r.rowDev[i]=r.sides[worst].deviation[i];r.rowEdge[i]=r.sides[worst].edge[i];r.rowRef[i]=r.sides[worst].reference[i];
        if(i%4==0)r.centerPts.push_back(cv::Point2f(origin+u*i+n*((measured[0][i]+measured[1][i])*.5)));
        r.rowWidth[i]=float(measured[1][i]-measured[0][i]);
        const double widthLimit=std::max(1.,p.maskWidthTolerance*r.rowReferenceWidth[i]);
        r.rowWidthBad[i]=std::abs(r.rowWidth[i]-r.rowReferenceWidth[i])>widthLimit;
        if(r.rowWidthBad[i]){r.longestWidthRun=std::max(r.longestWidthRun,double(++widthRun));}else widthRun=0;
    }
    for(int k=0;k<2;++k)r.sides[k].coverage=sideEligible[k]?double(sideFound[k])/sideEligible[k]:0;
    const bool missingSide=r.sides[0].longestMissingPx>=p.maskDefectRun||r.sides[1].longestMissingPx>=p.maskDefectRun;
    const bool badWidth=r.longestWidthRun>=p.maskDefectRun;
    for(int start=0;start<rows;){
        if(!r.rowWidthBad[start]){++start;continue;}
        int end=start+1;while(end<rows&&r.rowWidthBad[end])++end;
        if(end-start>=p.maskDefectRun)std::fill(r.rowWidthReject.begin()+start,r.rowWidthReject.begin()+end,1);
        start=end;
    }
    r.coverage=eligible?double(found)/eligible:0;r.longestGap=eligible?double(longest)/eligible:1;
    r.found=r.coverage>=p.maskCoverage;r.controlComplete=r.found&&r.longestGap<=p.maskMaxGap;
    r.maskReason=!r.found?"seam_mask_not_found":r.longestGap>p.maskMaxGap?"seam_mask_gap":missingSide?"seam_mask_side_missing":badWidth?"seam_mask_width":"";
    r.gapReject=missingSide;r.reject=r.controlComplete&&(r.ratio>1||missingSide||badWidth);
    // Mark persistent absence at its expected position, including the side that still exists.
    if(missingSide||badWidth)for(int i=0;i<rows;++i)if(r.rowState[i]==3||r.rowWidthBad[i])for(int k=0;k<2;++k)
        if(r.sides[k].state[i]==3||r.rowWidthBad[i])r.badPts.push_back(r.sides[k].reference[i]);
    cv::Mat raw,mask;cv::transpose(current.raw,raw);cv::transpose(current.mask,mask);cv::cvtColor(mask,mask,cv::COLOR_GRAY2BGR);
    r.maskPreview=cv::Mat(2*cols+4,rows,CV_8UC3,cv::Scalar(24,24,24));raw.copyTo(r.maskPreview({0,0,rows,cols}));mask.copyTo(r.maskPreview({0,cols+4,rows,cols}));
    for(int i=0;i<rows;++i)for(int k=0;k<2;++k){const auto& side=r.sides[k];if(side.state[i]==1||side.state[i]==2||side.state[i]==4)continue;
        const int y=cvRound(axes[k][i])+band;const bool absent=side.state[i]==3;
        if(i%20==0||absent)for(int panel:{0,cols+4})cv::line(r.maskPreview,{i,std::clamp(cvRound(axes[0][i])+band,0,cols-1)+panel},{i,std::clamp(cvRound(axes[1][i])+band,0,cols-1)+panel},cv::Scalar(180,140,0),1);
        for(int panel:{0,cols+4}){
            for(int sign:{-1,1}){const int limit=cvRound(y+sign*r.rowTol[i]);if(limit>=0&&limit<cols)r.maskPreview.at<cv::Vec3b>(limit+panel,i)={255,180,0};}
            if(absent)cv::circle(r.maskPreview,{i,y+panel},1,cv::Scalar(255,0,255),-1);
            else {const bool bad=std::abs(side.deviation[i])>r.rowTol[i]||r.rowWidthReject[i];const int at=cvRound(measured[k][i])+band;r.maskPreview.at<cv::Vec3b>(at+panel,i)=bad?cv::Vec3b(0,0,255):r.rowWidthBad[i]?cv::Vec3b(0,180,255):cv::Vec3b(0,230,0);}
        }
    }
    return done();
}
}
