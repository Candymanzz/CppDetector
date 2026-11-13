#include "learning_matcher.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace cas {
namespace {
int flagged(const cv::Mat& diff, int threshold) {
    cv::Mat energy;
    diff.convertTo(energy, CV_8U, 128.0 / std::max(1, threshold));
    return cv::countNonZero(energy > 63);
}
cv::Mat envelopeDifference(const cv::Mat& expected, const cv::Mat& current, cv::Size kernelSize) {
    cv::Mat lower, upper, lo, hi, result;
    const auto kernel=cv::getStructuringElement(cv::MORPH_RECT,kernelSize);
    cv::erode(expected,lower,kernel); cv::dilate(expected,upper,kernel);
    cv::subtract(lower,current,lo);cv::subtract(current,upper,hi);cv::max(lo,hi,result);
    return result;
}
struct Placement { bool valid=false; cv::Rect box; double correlation=0; };
Placement locate(const cv::Mat& image,const cv::Mat& sample,cv::Rect original,int radius) {
    Placement p;
    // Search only complete placements: no replicated pixels or wrapped correlation at the boundary.
    const int left=std::max(0,original.x-radius),top=std::max(0,original.y-radius);
    const int right=std::min(image.cols,original.br().x+radius),bottom=std::min(image.rows,original.br().y+radius);
    const cv::Rect search(left,top,right-left,bottom-top);
    if(search.width<sample.cols || search.height<sample.rows) return p;
    cv::Scalar mean,sd;cv::meanStdDev(sample,mean,sd);
    if(sd[0]<1.0) return p; // correlation of a featureless crop is not evidence
    cv::Mat scores;cv::matchTemplate(image(search),sample,scores,cv::TM_CCOEFF_NORMED);
    cv::Point peak;cv::minMaxLoc(scores,nullptr,&p.correlation,nullptr,&peak);
    p.box={peak.x+search.x,peak.y+search.y,sample.cols,sample.rows};
    p.valid=std::isfinite(p.correlation) && p.correlation>=0.8;
    return p;
}
int apply(cv::Mat& diff,cv::Rect box,const cv::Mat& alternate,const cv::Mat& mask,int threshold) {
    auto target=diff(box);const int before=flagged(target,threshold);
    cv::Mat candidate;cv::min(target,alternate,candidate);candidate.copyTo(target,mask);
    return before-flagged(target,threshold);
}
}

void compareLearned(const cas::Reference& reference,const cas::Prepared& prepared,const cv::Mat& current,cv::Mat& diff,
                    cv::Rect region,double scale,const cas::Settings& s,cas::InspectOutcome& out,
                    const LearningOptions& options,nlohmann::json& diagnostics) {
    diagnostics=nlohmann::json::array();
    const double sensitivity = std::clamp(s.sensitivity, 0.0, 1.0);
    const bool hasCorners = prepared.upperCornerArea > 0;
    const int threshold = cvRound(80 - 75 * (hasCorners
        ? std::min(1.0, sensitivity * kUpperCornerSensitivityMultiplier) : sensitivity));
    const int radius=std::max(0,cvRound(options.searchRadius*scale));
    const cv::Size kernel(2*std::max(0,cvRound(s.shiftToleranceX*scale))+1,
                          2*std::max(0,cvRound(s.shiftToleranceY*scale))+1);
    const cv::Mat primary=prepared.refGray(region);
    const double sigma=std::max(1.0,6.0*scale);

    for(const auto& learned:reference.learned()) {
        nlohmann::json d={{"id",learned.id},{"status","outside_region"},{"removed_pixels",0},
            {"search_radius",options.searchRadius},{"pattern_correlation",nullptr},
            {"shadow_correlation",nullptr},{"shift",nullptr},{"method","none"},{"work_scale",scale}};
        cv::Rect box(cvRound(learned.bbox.x*scale)-region.x,cvRound(learned.bbox.y*scale)-region.y,
            std::max(1,cvRound(learned.bbox.width*scale)),std::max(1,cvRound(learned.bbox.height*scale)));
        const cv::Rect bounds(0,0,diff.cols,diff.rows);
        if((box & bounds)!=box) {diagnostics.push_back(d);continue;}
        cv::Rect search=box;
        search.x-=radius;search.y-=radius;search.width+=2*radius;search.height+=2*radius;search &= bounds;
        d["candidate_pixels"]=flagged(diff(search),threshold);
        if(!d["candidate_pixels"].get<int>()) {d["status"]="no_anomaly";diagnostics.push_back(d);continue;}
        if(box.width<8 || box.height<8) {d["status"]="crop_too_small";diagnostics.push_back(d);continue;}
        cv::Mat sample;
        cv::cvtColor(learned.bgr,sample,cv::COLOR_BGR2GRAY);
        cv::resize(sample,sample,box.size(),0,0,cv::INTER_AREA);
        cv::GaussianBlur(sample,sample,{5,5},0);
        cv::Mat sampleF,residual;
        sample.convertTo(sampleF,CV_32F);cv::subtract(sampleF,primary(box),residual,cv::noArray(),CV_32F);
        // Application mask is the changed part of the saved crop, not its whole rectangular background.
        cv::Mat support=cv::abs(residual)>6.0;
        const int sourceMaskPixels=cv::countNonZero(support);
        auto placement=locate(current,sample,box,radius);
        d["pattern_correlation"]=placement.correlation;
        d["pattern_aligned"]=placement.valid;
        // Always allow the original position even on a plain patch: pixel comparison itself decides.
        const auto patternBox=placement.valid?placement.box:box;
        // Current is globally aligned, while the saved crop is from the raw frame.
        // Compare the sample to the primary at its matched position: translating
        // a mask made at the source position can miss the learned ROI boundary.
        cv::Mat placedResidual;
        cv::absdiff(sample,primary(patternBox),placedResidual);
        cv::Mat placedSupport=placedResidual>6.0;
        const int placedMaskPixels=cv::countNonZero(placedSupport);
        if(!sourceMaskPixels && !placedMaskPixels) {d["status"]="no_learned_difference";diagnostics.push_back(d);continue;}
        d["mask_pixels"]=sourceMaskPixels;
        int removed=0;
        cv::Mat alternate=envelopeDifference(sample,current(patternBox),kernel);
        bool shadowFit=false;

        if(options.shadows && sourceMaskPixels) {
            cv::Mat smooth,high;
            cv::GaussianBlur(residual,smooth,{},sigma);
            cv::absdiff(residual,smooth,high);
            const double amplitude=cv::mean(cv::abs(residual),support)[0];
            const double roughness=cv::mean(high,support)[0];
            double highPeak=0;cv::minMaxLoc(high,nullptr,&highPeak,nullptr,nullptr,support);
            d["shadow_roughness"]=roughness;
            d["shadow_high_peak"]=highPeak;
            // A smooth signed brightness change can move independently from the printed label.
            // Abrupt printed alternatives continue through the pattern path only.
            if(roughness<=std::max(2.0,0.18*amplitude) && highPeak<=18.0) {
                // Filter only the search context plus a Gaussian halo, not the entire camera image.
                const int halo=cvCeil(4*sigma);
                cv::Rect context(search.x-halo,search.y-halo,search.width+2*halo,search.height+2*halo);context &= bounds;
                cv::Mat delta,deltaSmooth;
                cv::subtract(current(context),primary(context),delta,cv::noArray(),CV_32F);
                cv::GaussianBlur(delta,deltaSmooth,{},sigma);
                auto shadow=locate(deltaSmooth,smooth,box-context.tl(),radius);
                d["shadow_correlation"]=shadow.correlation;
                if(shadow.valid) {
                    const cv::Mat targetDelta=deltaSmooth(shadow.box);
                    shadow.box += context.tl();
                    cv::Scalar m,sd;cv::meanStdDev(smooth,m,sd);
                    const double variance=sd[0]*sd[0];
                    const double targetMean=cv::mean(targetDelta)[0];
                    const double covariance=cv::mean(smooth.mul(targetDelta))[0]-m[0]*targetMean;
                    const double gain=variance>1.0?covariance/variance:1.0;
                    const double offset=targetMean-gain*m[0];
                    d["lighting_gain"]=gain;d["lighting_offset"]=offset;
                    if(gain>=0.7 && gain<=1.3 && std::abs(offset)<=12) {
                        shadowFit=true;
                        cv::Mat corrected,correctedU8;
                        // Subtract only the learned, translated smooth illumination component.
                        // Fine details come from the current image and must still match the primary reference.
                        current(shadow.box).convertTo(corrected,CV_32F);
                        corrected=corrected-smooth*gain-offset;
                        corrected.convertTo(correctedU8,CV_8U);
                        auto shadowDiff=envelopeDifference(primary(shadow.box),correctedU8,kernel);
                        cv::Mat shadowSupport=cv::abs(smooth)>6.0;
                        const int shadowRemoved=apply(diff,shadow.box,shadowDiff,shadowSupport,threshold);
                        removed+=shadowRemoved;
                        if(shadowRemoved) {
                            d["method"]="shadow";
                            d["shift"]={(shadow.box.x-box.x)/scale,(shadow.box.y-box.y)/scale};
                        }
                    } else d["shadow_skip_reason"]="lighting_change_too_large";
                } else d["shadow_skip_reason"]="unreliable_match";
            } else d["shadow_skip_reason"]="not_smooth";
        }
        if(!shadowFit) {
            d["mask_pixels"]=placedMaskPixels;
            removed+=apply(diff,patternBox,alternate,placedSupport,threshold);
            if(removed) {d["method"]="pattern";d["shift"]={(patternBox.x-box.x)/scale,(patternBox.y-box.y)/scale};}
        }
        d["remaining_pixels"]=flagged(diff(search),threshold);
        d["removed_pixels"]=removed;
        d["status"]=removed?"matched":"no_effect";
        if(removed) out.matchedCaseIds.push_back(learned.id);
        diagnostics.push_back(std::move(d));
    }
}
}
