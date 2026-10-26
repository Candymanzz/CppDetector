#include "app/app.h"
#include "inspect/inspector.h"
#include "production/core/seam.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool ok, const char* what) { ++checks; if (!ok) throw std::runtime_error(what); }

cas::Settings nativeSettings(const cd::InspectParams& p, const cd::SeamParams* seam, cv::Size size) {
    cas::Settings s;
    s.sensitivity = p.sensitivity; s.threshold = p.rejectThreshold / 100;
    s.shiftToleranceX = p.shiftToleranceX; s.shiftToleranceY = p.shiftToleranceY;
    s.minBlobArea = p.minBlobArea; s.inspectScale = p.downscale;
    if (seam && seam->pts.size() >= 2) {
        s.seam.enabled = seam->enabled;
        s.seam.p0x = seam->pts.front().x / size.width; s.seam.p0y = seam->pts.front().y / size.height;
        s.seam.p1x = seam->pts.back().x / size.width; s.seam.p1y = seam->pts.back().y / size.height;
        s.seam.band = seam->bandHalf; s.seam.tolMid = seam->tolMid; s.seam.tolEdge = seam->tolEdge; s.seam.edgeFrac = seam->edgeFrac;
    }
    return s;
}
cas::NormPolygon nativeRoi(const std::vector<cv::Point>& roi, cv::Size size) {
    cas::NormPolygon out;
    for (auto p : roi) out.emplace_back(p.x / double(size.width - 1), p.y / double(size.height - 1));
    return out;
}
void applyNativeSeam(const cv::Mat& frame, const cas::SeamSettings& s, cas::InspectOutcome& out) {
    if (!s.enabled || !s.hasLine() || out.reason == "size_mismatch") return;
    auto sr = cas::measureSeam(frame, s);
    out.seamEnabled = true; out.seamFound = sr.found; out.seamReject = sr.reject;
    out.seamRatio = sr.ratio; out.seamMaxDev = sr.maxDev; out.seamWorstT = sr.worstT; out.seamCoverage = sr.coverage;
    if (!sr.reject) return;
    out.reject = true; if (out.reason.empty()) out.reason = "seam_deviation";
    if (!sr.badPts.empty()) {
        auto bb = cv::boundingRect(sr.badPts);
        bb.x -= 12; bb.y -= 12; bb.width += 24; bb.height += 24;
        bb &= cv::Rect(0, 0, frame.cols, frame.rows);
        if (bb.area() > 0) out.zones.push_back(bb);
    }
}
void compare(const cd::InspectResult& a, const cas::InspectOutcome& b, const cas::Reference& reference) {
    require(a.score == b.anomalyScore * 100 && a.reject == b.reject && a.reason == b.reason, "score/verdict mismatch");
    require(a.shift == b.shift && a.shiftLimit == b.shiftLimit && a.native.rawShift == b.rawShift
            && a.native.shiftResponse == b.shiftResponse, "positioning mismatch");
    require(a.zones == b.zones && a.envelopeKernel == b.envelopeKernel, "zones/kernel mismatch");
    require(a.native.areaPercent == b.areaPercent && a.native.roiArea == b.roiArea, "area mismatch");
    require(cv::norm(a.native.diff, b.diff, cv::NORM_INF) == 0, "difference map mismatch");
    require(cv::norm(a.native.kept, b.kept, cv::NORM_INF) == 0, "accepted blobs mismatch");
    require(cv::norm(a.heatmap, cas::buildHeatmap(b, reference.roi(), 512), cv::NORM_INF) == 0, "native heatmap mismatch");
    require(a.native.seamEnabled == b.seamEnabled && a.native.seamFound == b.seamFound
            && a.native.seamReject == b.seamReject && a.native.seamRatio == b.seamRatio, "seam mismatch");
}
void one(const cv::Mat& ref, const cv::Mat& frame, const cd::InspectParams& p,
         const std::vector<cv::Point>& roi = {}, const cd::SeamParams* seam = nullptr) {
    cd::Inspector local; local.setReference(ref, roi, p);
    cas::Reference production(ref.clone()); production.setRoi(nativeRoi(roi, ref.size()));
    auto s = nativeSettings(p, seam, ref.size());
    auto expected = cas::inspectFrame(production, frame, s, true);
    applyNativeSeam(frame, s.seam, expected);
    const auto result = local.inspect(frame, p, seam);
    compare(result, expected, production);
}

void syntheticTests() {
    cv::Mat ref(320, 384, CV_8UC3); cv::RNG rng(12345); rng.fill(ref, cv::RNG::UNIFORM, 0, 255);
    cv::putText(ref, "LABEL", {50, 130}, cv::FONT_HERSHEY_SIMPLEX, 2, {255, 255, 255}, 4);
    auto changed = ref.clone(); cv::rectangle(changed, {120, 140, 30, 20}, {0, 0, 0}, -1);
    for (double scale : {1.0, 0.7, 0.45}) for (auto tolerance : {cv::Point(0, 0), cv::Point(3, 3), cv::Point(6, 1), cv::Point(1, 6)}) {
        cd::InspectParams p; p.downscale = scale; p.shiftToleranceX = tolerance.x; p.shiftToleranceY = tolerance.y;
        for (const auto& roi : {std::vector<cv::Point>{}, std::vector<cv::Point>{{60,40},{320,50},{310,270},{70,280}}}) {
            one(ref, changed, p, roi);
            const cv::Mat m = (cv::Mat_<double>(2, 3) << 1, 0, 10, 0, 1, -6);
            cv::Mat moved; cv::warpAffine(ref, moved, m, ref.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
            one(ref, moved, p, roi);
        }
    }
    cd::InspectParams p; p.rejectThreshold = 100; p.sensitivity = 1; p.shiftToleranceX = p.shiftToleranceY = 0; p.minBlobArea = 1;
    auto destroyed = ref.clone(); cv::rectangle(destroyed, {50, 40, 250, 220}, {0, 0, 0}, -1);
    one(ref, destroyed, p);
    cd::Inspector boundary; boundary.setReference(ref, {}, p);
    auto edge = boundary.inspect(destroyed, p);
    require(edge.native.anomalyScore == 1 && edge.reject, "100% equality must reject, just like native >= threshold");

    cv::Mat line(320,384,CV_8UC3,cv::Scalar(80,80,80));
    cv::line(line, {180,35}, {180,285}, {250,250,250}, 4);
    cd::SeamParams seam; seam.enabled = true; seam.pts = {{180,40},{180,280}}; seam.bandHalf = 20;
    p = cd::InspectParams{};
    one(line, line, p, {}, &seam);
    auto bent = line.clone(); cv::line(bent, {180,120}, {190,160}, {250,250,250}, 4);
    one(line, bent, p, {}, &seam);
    // Repeated square-ROI inspections must preserve parity, results and the shared cache.
    const std::vector<cv::Point> square{{40,20},{320,20},{320,300},{40,300}};
    cd::Inspector local; local.setReference(ref, square, p);
    cas::Reference production(ref.clone()); production.setRoi(nativeRoi(square, ref.size()));
    auto s = nativeSettings(p, nullptr, ref.size());
    auto prepared = production.prepared(1);
    auto before = prepared->refSmall.clone();
    const auto expected = cas::inspectFrame(production,ref,s,true);
    for (int i=0; i<60; ++i) {
        const auto result = local.inspect(ref,p);
        compare(result, cas::inspectFrame(production,ref,s,true), production);
        compare(result, expected, production);
    }
    require(cv::norm(before,prepared->refSmall,cv::NORM_INF) == 0,
            "repeated inspections must not change the cached reference thumbnail");
}

void profileTests(const std::filesystem::path& dir) {
    const auto oldFile = dir / "legacy.json", newFile = dir / "native.json";
    cd::json old = {{"defect",true},{"cameras", {{{"reference","upload"},{"roi",{{50,40},{300,40},{300,270},{50,270}}},
        {"sensitivity",1},{"reject_threshold",0},{"shift_tolerance_x",9},{"shift_tolerance_y",7},{"min_blob_area",1},
        {"downscale",0.45},{"edge",{{"enabled",true}}}}}}};
    const std::string bytes = old.dump(2);
    { std::ofstream out(oldFile); out << bytes; }
    cv::Mat photo(320,384,CV_8UC3,cv::Scalar(80,80,80));
    cv::imwrite((dir/"ref_legacy_cam0.png").string(),photo);
    {
        cd::App app(dir/"frames", newFile, dir/"logs",oldFile);
        auto state=app.state(); auto cam=state["cameras_info"][0];
        require(cam["reject_threshold"] == 25 && cam["sensitivity"] == 0.5 && cam["downscale"] == 1
                && cam["shift_tolerance_x"] == 3 && cam["min_blob_area"] == 20, "new profile must start with source defaults");
        require(cam["roi"] == old["cameras"][0]["roi"] && cam["reference"] == "upload" && cam["width"] == 384,
                "reference and ROI must be imported");
        require(cam["edge"]["enabled"] == false && state["defect"] == false, "legacy extras must not alter native test verdicts");
        std::string err;
        require(!app.setCamera(0,{{"edge",{{"enabled",true}}}},err), "edge API must not silently enable a different detector");
        require(app.setCamera(0,{{"shift_tolerance_x",8},{"reject_threshold",12.5}},err), "new profile must save native settings");
        const auto result=app.inspectUpload(0,photo,"same-frame",false,err);
        require(result["inspection_engine"] == "position-x-y" && result["native_settings"]["threshold"] == 0.125,
                "inspection must expose native settings and use percent/100");
        require(result["score"] == 0 && result["reject"] == false, "same image must pass");
    }
    std::ifstream in(oldFile); const std::string after((std::istreambuf_iterator<char>(in)), {});
    require(after == bytes, "legacy user config must not be overwritten");
    {
        cd::App restarted(dir/"frames",newFile,dir/"logs",oldFile);
        const auto cam=restarted.state()["cameras_info"][0];
        require(cam["shift_tolerance_x"] == 8 && cam["reject_threshold"] == 12.5, "native settings must survive restart");
    }
}
void realFrames(const std::filesystem::path& root) {
    int frames=0;
    for(int camera=0;camera<10;++camera) {
        std::vector<std::filesystem::path> files;
        for(const auto& file:std::filesystem::recursive_directory_iterator(root/("camera_"+std::to_string(camera))))
            if(file.path().filename()=="frame.jpg")files.push_back(file.path());
        std::sort(files.begin(),files.end()); require(!files.empty(),"camera has no frames");
        auto ref=cv::imread(files.front().string()); cd::InspectParams p;
        cd::Inspector local; local.setReference(ref,{},p); cas::Reference native(ref.clone());
        auto settings=nativeSettings(p,nullptr,ref.size());
        for(const auto& path:files) {
            auto img=cv::imread(path.string()); require(!img.empty(),"cannot decode frame");
            compare(local.inspect(img,p),cas::inspectFrame(native,img,settings,true),native); ++frames;
        }
        std::cout<<"PARITY camera "<<camera<<": "<<files.size()<<" frames"<<std::endl;
    }
    std::cout<<"real frames: "<<frames<<"\n";
}
}
int main(int argc,char**argv) {
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/("native-parity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        cv::setNumThreads(1); fs::create_directories(dir);
        syntheticTests(); profileTests(dir);
        if(argc>1)realFrames(argv[1]);
        const auto resolved=fs::weakly_canonical(dir), parent=fs::weakly_canonical(fs::temp_directory_path());
        if(resolved.parent_path()!=parent || resolved.filename().string().rfind("native-parity-",0)!=0)
            throw std::runtime_error("unsafe temporary cleanup path");
        fs::remove_all(resolved); // only this test's uniquely created directory, validated above
        std::cout<<"production parity passed: "<<checks<<" checks\n"; return 0;
    } catch(const std::exception&e) {std::cerr<<e.what()<<" (test data: "<<dir.string()<<")\n";return 1;}
}
