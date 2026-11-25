#include "seam_mask.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace cd {
namespace {
constexpr double nan = std::numeric_limits<double>::quiet_NaN();
struct Track { std::vector<double> offset, fit; double cost = 0; int count = 0, sign = 0; };
double value(const std::vector<double>& c, double t) { return c[0]+c[1]*t+c[2]*t*t; }
std::vector<double> fit(const std::vector<double>& offsets) {
    std::vector<char> use(offsets.size());
    for(size_t i=0;i<use.size();++i)use[i]=std::isfinite(offsets[i]);
    std::vector<double> c;
    for(int pass=0;pass<6;++pass) {
        const int count=int(std::count(use.begin(),use.end(),1));if(count<8)return c;
        cv::Mat a(count,3,CV_64F),b(count,1,CV_64F);int row=0;
        for(size_t i=0;i<use.size();++i)if(use[i]){double t=double(i)/offsets.size();a.at<double>(row,0)=1;a.at<double>(row,1)=t;a.at<double>(row,2)=t*t;b.at<double>(row++,0)=offsets[i];}
        cv::Mat x;cv::solve(a,b,x,cv::DECOMP_SVD);c={x.at<double>(0),x.at<double>(1),x.at<double>(2)};
        for(size_t i=0;i<use.size();++i)use[i]=std::isfinite(offsets[i])&&std::abs(offsets[i]-value(c,double(i)/offsets.size()))<1.5;
    }
    return c;
}
Track trace(const cv::Mat& mask,const cv::Mat& dx,int sign,const std::vector<char>& state,int band) {
    const int rows=mask.rows,cols=mask.cols;
    std::vector<float> prev(cols),next(cols);cv::Mat back(rows,cols,CV_16S);
    const auto observed=[&](int i,int j){return mask.at<uchar>(i,j)&&dx.at<float>(i,j)*sign>0;};
    for(int i=0;i<rows;++i) {
        for(int j=0;j<cols;++j) {
            float best=std::numeric_limits<float>::infinity();int from=j;
            for(int k=std::max(0,j-2);k<=std::min(cols-1,j+2);++k){float d=float(j-k),v=prev[k]+.20f*std::abs(d)+.35f*d*d;if(v<best){best=v;from=k;}}
            const float evidence=state[i]?0.f:observed(i,j)?-1.f:1.f;
            next[j]=best+evidence+.002f*std::abs(j-band);back.at<short>(i,j)=short(from);
        }
        prev.swap(next);
    }
    int j=int(std::min_element(prev.begin(),prev.end())-prev.begin());Track out;out.sign=sign;out.cost=prev[j];out.offset.assign(rows,nan);
    for(int i=rows-1;i>=0;--i){
        // The path may bridge a missing row for continuity, but that row is never measured.
        if(!state[i]&&j>0&&j<cols-1&&observed(i,j)) {
            const double a=std::abs(dx.at<float>(i,j-1)),b=std::abs(dx.at<float>(i,j)),c=std::abs(dx.at<float>(i,j+1));
            const double den=a-2*b+c,sub=std::abs(den)>1e-6?std::clamp(.5*(a-c)/den,-.5,.5):0;
            out.offset[i]=j-band+sub;++out.count;
        }
        j=back.at<short>(i,j);
    }
    out.fit=fit(out.offset);return out;
}
}

SeamResult measureMaskedSeam(const cv::Mat& bgr,const SeamParams& p,const cv::Mat& excluded) {
    const auto begin=std::chrono::steady_clock::now();SeamResult r;r.enabled=p.enabled;r.maskSearch=true;r.controlComplete=false;
    auto done=[&](){r.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return r;};
    if(!p.enabled){r.controlComplete=true;return done();}
    if(bgr.empty()||p.pts.size()!=2){r.maskReason="seam_mask_line_invalid";return done();}
    for(auto q:p.pts)if(!std::isfinite(q.x)||!std::isfinite(q.y)||q.x<0||q.y<0||q.x>=bgr.cols||q.y>=bgr.rows){r.maskReason="seam_mask_outside_frame";return done();}
    const auto d=p.pts.back()-p.pts.front();const double length=cv::norm(d);
    if(length<50){r.maskReason="seam_mask_line_invalid";return done();}
    const int rows=int(length),band=int(p.bandHalf),cols=2*band+1;const auto u=d/length;const cv::Point2d n(-u.y,u.x);
    cv::Mat mx(rows,cols,CV_32F),my(rows,cols,CV_32F),valid(rows,cols,CV_8U,cv::Scalar(0));
    std::vector<char> state(rows);r.rowPos.resize(rows);r.rowEdge.resize(rows);r.rowRef.resize(rows);r.rowDev.assign(rows,float(nan));r.rowTol.resize(rows);
    r.path={cv::Point2f(p.pts.front()),cv::Point2f(p.pts.back())};r.pathNormal={cv::Point2f(n),cv::Point2f(n)};
    for(int i=0;i<rows;++i){
        const double t=double(i)/length;r.rowPos[i]=cv::Point2f(p.pts.front()+u*i);state[i]=(t<p.endIgnore||t>1-p.endIgnore)?2:0;
        r.rowTol[i]=float(std::max(.05,std::min(t,1-t)<p.edgeFrac?p.tolEdge:p.tolMid));
        for(int j=0;j<cols;++j){const auto q=p.pts.front()+u*i+n*(j-band);mx.at<float>(i,j)=float(q.x);my.at<float>(i,j)=float(q.y);int x=cvRound(q.x),y=cvRound(q.y);if(x>=0&&y>=0&&x<bgr.cols&&y<bgr.rows){valid.at<uchar>(i,j)=255;if(!excluded.empty()&&excluded.at<uchar>(y,x))state[i]=1;}}
    }
    cv::Mat strip,grey,smooth,mask,dx,dy;cv::remap(bgr,strip,mx,my,cv::INTER_LINEAR,cv::BORDER_CONSTANT);
    cv::cvtColor(strip,grey,cv::COLOR_BGR2GRAY);cv::GaussianBlur(grey,smooth,{3,7},.8,1.5);
    cv::Canny(smooth,mask,p.maskLow,p.maskHigh,3,true);cv::Sobel(smooth,dx,CV_32F,1,0);cv::Sobel(smooth,dy,CV_32F,0,1);
    for(int i=0;i<rows;++i)for(int j=0;j<cols;++j){
        bool inside=j>=2&&j<cols-2;
        for(int di=-1;di<=1&&inside;++di)for(int dj=-2;dj<=2;++dj)if(!valid.at<uchar>(std::clamp(i+di,0,rows-1),std::clamp(j+dj,0,cols-1)))inside=false;
        if(state[i]||!inside||std::abs(dx.at<float>(i,j))<1.25f*std::abs(dy.at<float>(i,j)))mask.at<uchar>(i,j)=0;
    }
    Track a=trace(mask,dx,1,state,band),b=trace(mask,dx,-1,state,band);if(a.cost>b.cost)std::swap(a,b);
    r.candidateOffsets={a.offset,b.offset};r.candidateSigns={a.sign,b.sign};
    std::vector<Track*> tracks;if(!a.fit.empty())tracks.push_back(&a);
    if(!b.fit.empty()&&!a.fit.empty()){
        std::vector<double> widths;for(int i=0;i<rows;++i)if(std::isfinite(a.offset[i])&&std::isfinite(b.offset[i]))widths.push_back(std::abs(a.offset[i]-b.offset[i]));
        if(widths.size()>size_t(rows*.4)){auto mid=widths.begin()+widths.size()/2;std::nth_element(widths.begin(),mid,widths.end());if(*mid>=1&&*mid<=16)tracks.push_back(&b);}
    }
    r.maskSides=int(tracks.size());r.rowState=state;int eligible=0,measured=0,gap=0,maxGap=0;
    for(int i=0;i<rows;++i){
        if(state[i]){gap=0;continue;}++eligible;double worst=0;bool observed=false;cv::Point2f centre;
        for(const auto* track:tracks)if(std::isfinite(track->offset[i])){
            const double offset=track->offset[i],reference=value(track->fit,double(i)/rows),dev=offset-reference;
            const auto edge=cv::Point2f(p.pts.front()+u*i+n*offset);centre=edge;
            if(!observed||std::abs(dev)>std::abs(worst)){worst=dev;r.rowEdge[i]=edge;r.rowRef[i]=cv::Point2f(p.pts.front()+u*i+n*reference);}
            observed=true;if(std::abs(dev)>r.rowTol[i])r.badPts.push_back(edge);
        }
        if(!observed){r.rowState[i]=3;maxGap=std::max(maxGap,++gap);continue;}
        ++measured;gap=0;r.rowDev[i]=float(worst);if(i%4==0)r.centerPts.push_back(centre);
        const double ratio=std::abs(worst)/r.rowTol[i];if(ratio>r.ratio){r.ratio=ratio;r.maxDev=std::abs(worst);r.worstT=double(i)/length;}
    }
    r.coverage=eligible?double(measured)/eligible:0;r.longestGap=eligible?double(maxGap)/eligible:1;
    r.found=r.coverage>=p.maskCoverage;r.controlComplete=r.found&&r.longestGap<=p.maskMaxGap;
    r.maskReason=!r.found?"seam_mask_not_found":r.longestGap>p.maskMaxGap?"seam_mask_gap":"";
    r.reject=r.controlComplete&&r.ratio>1;
    // Diagnostic image: the actual unwrapped source above the actual binary mask.
    // There is no interpolated measured path across zero-evidence rows.
    cv::Mat raw,edges;cv::transpose(strip,raw);cv::transpose(mask,edges);cv::cvtColor(edges,edges,cv::COLOR_GRAY2BGR);
    r.maskPreview=cv::Mat(2*cols+4,rows,CV_8UC3,cv::Scalar(32,32,32));raw.copyTo(r.maskPreview({0,0,rows,cols}));edges.copyTo(r.maskPreview({0,cols+4,rows,cols}));
    for(int i=0;i<rows;++i)for(const auto* track:tracks)if(std::isfinite(track->offset[i])){const int y=cvRound(track->offset[i])+band;const auto color=std::abs(track->offset[i]-value(track->fit,double(i)/rows))>r.rowTol[i]?cv::Scalar(0,0,255):cv::Scalar(0,230,0);cv::circle(r.maskPreview,{i,y},0,color,-1);cv::circle(r.maskPreview,{i,y+cols+4},0,color,-1);}
    return done();
}
}
