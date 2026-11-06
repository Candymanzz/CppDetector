#include "core/inspector.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a - b) < 1e-10; }
double normalizer(double sensitivity) {
    return sensitivity <= .5 ? 100 + (.2 - 100) * (2 * sensitivity)
                            : .2 + (.001 - .2) * (2 * sensitivity - 1);
}
cv::Mat fixture() {
    cv::Mat image(420, 700, CV_8UC3, cv::Scalar::all(100));
    cv::RNG rng(20261102);
    cv::Mat texture = image(cv::Rect(190, 140, 320, 190));
    rng.fill(texture, cv::RNG::UNIFORM, 30, 220);
    cv::putText(image, "CORNER INSPECTION", {190, 250}, cv::FONT_HERSHEY_SIMPLEX, .6, {240,240,240}, 2);
    return image;
}
const cas::NormPolygon polygon{{40./699, 20./419}, {659./699,20./419},
                               {659./699,399./419}, {40./699,399./419}};
cv::Mat placed(const cv::Mat& regionMask, const cv::Rect& region, cv::Size size) {
    cv::Mat full = cv::Mat::zeros(size, CV_8U); regionMask.copyTo(full(region)); return full;
}

void geometry() {
    auto image = fixture(); cas::Reference reference(image.clone());
    require(reference.prepared(1)->upperCornerArea == 0, "No implicit corners without an explicit ROI");
    reference.setRoi(polygon);
    const auto prepared = reference.prepared(1);
    auto mask = placed(prepared->upperCornerMask, prepared->region, image.size());
    require(mask.at<uchar>(45,70) && mask.at<uchar>(45,630), "Both upper ROI corners selected");
    require(!mask.at<uchar>(45,350) && !mask.at<uchar>(100,70), "Top middle and lower sides not selected");
    require(!mask.at<uchar>(10,70), "Corner masks stay inside ROI");
    require(cv::countNonZero(mask) == 2 * 93 * 55, "Rectangular geometry is 15 percent width and one-seventh height");
    const auto old = prepared;
    reference.setRoi({{.1,.2},{.9,.05},{.85,.9},{.12,.95}});
    const auto tilted = reference.prepared(1);
    require(old.get() != tilted.get(), "Changing ROI invalidates cached geometry");
    auto selected = placed(tilted->upperCornerMask, tilted->region, image.size());
    cv::Mat outside; cv::bitwise_and(selected, tilted->roiMask == 0, outside);
    require(cv::countNonZero(outside) == 0, "Tilted corner mask stays within polygon");
    for (int x : {95,590}) {
        int top = 0; while (top < image.rows && !tilted->roiMask.at<uchar>(top,x)) ++top;
        require(selected.at<uchar>(top+2,x), "Upper corners follow slanted top instead of bounding box");
    }
    require(cv::countNonZero(selected.col(350))==0, "Slanted top does not accidentally select the middle of ROI");
    for (double scale : {.2,.5,.73,1.}) {
        auto p = reference.prepared(scale);
        require(p->upperCornerArea > 0 && p->upperCornerMask.size() == p->region.size(), "Corner geometry follows analysis scale");
    }
}

void sensitivityAndMask() {
    const auto image = fixture(); cas::Reference reference(image.clone()); reference.setRoi(polygon);
    cas::Settings settings;
    auto changed = image.clone();
    cv::rectangle(changed, {65,40,8,10}, cv::Scalar::all(120), cv::FILLED);
    cv::rectangle(changed, {620,40,8,10}, cv::Scalar::all(120), cv::FILLED);
    for (double sensitivity : {.25,.5,.8,.9,.95}) {
        settings.sensitivity = sensitivity;
        const auto out = cas::inspectFrame(reference, changed, settings, true);
        require(near(out.upperCornerSensitivity, std::min(1., 1.1*sensitivity)), "Boost is relative, not percentage points");
        require(out.anomalyScore >= out.mainRoiScore && out.anomalyScore <= 1, "Boost does not reduce base score");
        const auto prep = reference.prepared(settings.inspectScale);
        cv::Mat corners; cv::bitwise_and(out.kept, prep->upperCornerMask, corners);
        const int inside = cv::countNonZero(corners), all = cv::countNonZero(out.kept);
        const double expected = std::min(1., 100. * all / out.roiArea / normalizer(sensitivity));
        require(near(out.anomalyScore, expected), "Score uses the common ROI denominator and global sensitivity normalizer");
        require(near(out.areaPercent, 100.*all/out.roiArea), "Combined area counts each pixel once");
        require(near(out.upperCornerExtraScore, out.anomalyScore-out.mainRoiScore), "Diagnostic score explains boost contribution");
        double area = 0; for (double n : out.keptBlobAreas) area += n;
        require(area == all && out.zones.size() == out.keptBlobAreas.size(), "Components agree with union mask");
        const auto heat = cas::buildHeatmap(out, polygon, 0);
        require((cv::countNonZero(heat)>0) == (all>0), "Heatmap and detection agree");
        const auto withoutVisuals = cas::inspectFrame(reference, changed, settings, false);
        require(near(out.anomalyScore,withoutVisuals.anomalyScore) && out.reject==withoutVisuals.reject,
                "Visual output flag does not change detection");
        const auto self = cas::inspectFrame(reference, image, settings, true);
        require(!self.reject && self.anomalyScore==0, "Clamped local 100% must not reject an identical image");
    }
    settings.sensitivity = .5;
    const auto corner = cas::inspectFrame(reference, changed, settings, true);
    require(corner.upperCornerExtraScore > 0 && corner.anomalyScore > corner.mainRoiScore, "Small corner defect gains sensitivity");
    auto weakImage = image.clone();
    cv::rectangle(weakImage,{65,40,10,14},cv::Scalar::all(120),cv::FILLED);
    const auto weak = cas::inspectFrame(reference,weakImage,settings,true);
    cv::Mat baseEnergy; weak.diff.convertTo(baseEnergy,CV_8U,128./weak.pixelThreshold);
    cv::Mat newlyVisible; cv::bitwise_and(weak.kept,baseEnergy<=63,newlyVisible);
    require(cv::countNonZero(newlyVisible)>0, "Boost retains weak pixels excluded by the common sensitivity threshold");
    cv::Mat invisible; cv::bitwise_and(weak.kept,weak.scoredEnergy==0,invisible);
    require(cv::countNonZero(invisible)==0, "Every scored pixel remains visible in the heatmap at its actual local threshold");
    settings.minBlobArea = 200;
    require(cv::countNonZero(cas::inspectFrame(reference,changed,settings,true).kept)==0,
            "Upper corners obey the common minimum defect area");
    settings.minBlobArea = 20;
    auto middle = image.clone(); cv::rectangle(middle,{340,40,8,10},cv::Scalar::all(135),cv::FILLED);
    const auto center = cas::inspectFrame(reference,middle,settings,true);
    require(center.upperCornerExtraScore==0 && center.anomalyScore==center.mainRoiScore, "Upper middle does not gain sensitivity");
    auto lower = image.clone(); cv::rectangle(lower,{65,105,8,10},cv::Scalar::all(135),cv::FILLED);
    const auto low = cas::inspectFrame(reference,lower,settings,true);
    require(low.upperCornerExtraScore==0, "Lower side does not gain sensitivity");
    for (int relaxation : {0,1,3,5,10}) {
        settings.shiftToleranceX = relaxation; settings.shiftToleranceY = relaxation+1;
        const auto out = cas::inspectFrame(reference,changed,settings,true);
        require(out.envelopeKernel==cv::Size(2*relaxation+1,2*(relaxation+1)+1), "Shared X/Y relaxation remains configurable");
        require(out.shiftLimit==cv::Point2d(12,12), "Boost does not change global alignment limits");
    }
    settings.sensitivity = 0;
    const auto disabled = cas::inspectFrame(reference,changed,settings,true);
    require(!disabled.reject && disabled.anomalyScore==0 && cv::countNonZero(disabled.kept)==0,
            "Zero global sensitivity disables all inspection including boost");
    settings.sensitivity = 1;
    require(cas::inspectFrame(reference,image,settings,false).reject, "Global full sensitivity retains endpoint behavior");
}

void learnedCorners() {
    const auto image = fixture();
    auto normal = image.clone();
    cv::rectangle(normal, {65,40,10,14}, cv::Scalar::all(120), cv::FILLED);
    const cv::Rect box(55,30,35,35);
    for (bool robust : {false, true}) {
        cas::Reference reference(image.clone()); reference.setRoi(polygon);
        cas::Settings settings; settings.threshold = .01; settings.learningRobustEnabled = robust;
        const auto before = cas::inspectFrame(reference, normal, settings, true);
        require(before.mainRoiScore == 0 && before.reject, "Fixture is visible only through corner boost");
        require(near(before.rawAnomalyScore, before.anomalyScore), "Raw score includes corner boost without learning");
        reference.setLearned({{"weak-corner", box, normal(box).clone()}});
        const auto after = cas::inspectFrame(reference, normal, settings, true);
        require(!after.reject && after.anomalyScore == 0, "Both matchers learn weak corner-only anomalies");
        require(!after.matchedCaseIds.empty(), "Corner-only match reports its accepted case");
        require(near(after.rawAnomalyScore, before.anomalyScore), "Learning preserves the full original corner score");
        auto defect = normal.clone();
        cv::rectangle(defect, {65,40,10,14}, cv::Scalar::all(0), cv::FILLED);
        require(cas::inspectFrame(reference, defect, settings, true).reject, "New defect in learned corner still rejects");
    }
}

void concurrentInspections() {
    const auto image = fixture(); cas::Reference reference(image.clone()); reference.setRoi(polygon);
    const auto cached = reference.prepared(1); const auto original = cached->upperCornerMask.clone();
    auto changed = image.clone(); cv::rectangle(changed,{65,40,8,10},cv::Scalar::all(135),cv::FILLED);
    const cas::Settings settings; const auto expected = cas::inspectFrame(reference,changed,settings,true);
    std::vector<std::future<bool>> workers;
    for (int i=0;i<4;++i) workers.push_back(std::async(std::launch::async,[&]{
        for(int j=0;j<15;++j){const auto out=cas::inspectFrame(reference,changed,settings,true);
            if(out.anomalyScore!=expected.anomalyScore || cv::norm(out.kept,expected.kept,cv::NORM_INF)!=0)return false;}
        return true;
    }));
    for(auto& worker:workers)require(worker.get(),"Shared reference inspections remain deterministic");
    require(cv::norm(original,cached->upperCornerMask,cv::NORM_INF)==0,"Cached corner geometry stays immutable");
}
}
int main(){try{cv::setNumThreads(1);geometry();sensitivityAndMask();learnedCorners();concurrentInspections();
    std::cout<<"upper corner geometry, relative sensitivity, shared relaxation, score, heatmap and concurrency passed\n";
    return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
