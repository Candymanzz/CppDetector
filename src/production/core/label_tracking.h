#pragma once
#include "reference.h"
#include <opencv2/imgproc.hpp>

namespace cas {
// Verify the predicted boundary; never fit away a missing piece of the label.
inline double labelBoundarySupport(const cv::Mat& reference, const cv::Mat& current,
                                   const NormPolygon& contour, cv::Point2d shift) {
    int usable=0, matched=0;
    auto sample=[](const cv::Mat& image, cv::Point2d q, cv::Vec3d& value) {
        const int x=cvRound(q.x), y=cvRound(q.y);
        if(x<0||y<0||x>=image.cols||y>=image.rows) return false;
        value=image.at<cv::Vec3b>(y,x); return true;
    };
    for(size_t i=0;i<contour.size();++i) {
        cv::Point2d a(contour[i].x*(reference.cols-1),contour[i].y*(reference.rows-1));
        const auto q=contour[(i+1)%contour.size()];
        cv::Point2d b(q.x*(reference.cols-1),q.y*(reference.rows-1));
        const double length=cv::norm(b-a); if(length<1) continue;
        const cv::Point2d normal(-(b.y-a.y)/length,(b.x-a.x)/length);
        for(double t=4;t<length;t+=6) {
            const auto p=a+(b-a)*(t/length);
            cv::Vec3d inside,outside;
            if(!sample(reference,p+normal*3,inside)||!sample(reference,p-normal*3,outside)) continue;
            const auto gradient=inside-outside; const double strength=cv::norm(gradient);
            if(strength<20) continue;
            bool visible=false,found=false;
            for(int offset=-2;offset<=2;++offset) {
                cv::Vec3d c,d;
                if(!sample(current,p+shift+normal*(offset+3),c)||!sample(current,p+shift+normal*(offset-3),d)) continue;
                visible=true; const auto actual=c-d;
                if(actual.dot(gradient)>strength*strength*.25) found=true;
            }
            if(visible) { ++usable; if(found) ++matched; }
        }
    }
    return usable>=16 ? double(matched)/usable : 0.;
}
}
