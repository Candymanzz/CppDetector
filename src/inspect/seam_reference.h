#pragma once
#include "seam.h"
#include <memory>

namespace cd {
struct SeamReference {
    bool ready=false;
    std::string signature,reason;
    cv::Size imageSize;
    cv::Point2d origin,u,normal;
    int rows=0,sign=0;
    double length=0,coverage=0;
    std::vector<double> edge;       // confirmed observed edge, NaN for unsupported reference rows
    std::vector<double> axis;       // reference search hypothesis only, interpolates short reference gaps
    cv::Mat smooth,dx;              // immutable reference strip in a fixed +/-96 px search area
};
std::string seamReferenceSignature(const std::string& pixels,const SeamParams& params);
std::shared_ptr<const SeamReference> prepareSeamReference(const cv::Mat& reference,const SeamParams& params,const std::string& pixels);
SeamResult inspectReferenceSeam(const cv::Mat& current,const SeamReference* reference,const SeamParams& params,
                               cv::Point2d pose={},const cv::Mat& excluded={});
}
