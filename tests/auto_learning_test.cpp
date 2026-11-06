#include "inspect/learning_region.h"
#include <iostream>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        cv::Mat heat = cv::Mat::zeros(80,100,CV_8U);
        heat.at<uchar>(20,30)=1; heat.at<uchar>(50,70)=255;
        require(cd::learnedRegionBounds(heat,{1000,800})==cv::Rect(290,190,430,330),"main frontend disjoint anomalies and 10 source pixel padding");
        cv::Mat edges = (cv::Mat_<uchar>(2,2)<<1,0,0,1);
        require(cd::learnedRegionBounds(edges,{100,80})==cv::Rect(0,0,100,80),"clip padding at frame boundaries");
        require(cd::learnedRegionBounds(cv::Mat::zeros(2,2,CV_8U),{100,80}).empty(),"empty heatmap must not select the whole frame");
        require(cd::learnedRegionBounds(cv::Mat(),{100,80}).empty(),"unavailable heatmap");
        heat=cv::Mat::zeros(3,7,CV_8U); heat.at<uchar>(1,2)=1;
        require(cd::learnedRegionBounds(heat,{100,80},0)==cv::Rect(28,26,15,28),"fractional scale rounds outwards");
        bool invalid=false;try{cd::learnedRegionBounds(heat,{});}catch(const std::invalid_argument&){invalid=true;}
        require(invalid,"invalid frame rejected");
        std::cout<<"automatic bounds match main frontend fixtures\n";return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n";return 1; }
}
