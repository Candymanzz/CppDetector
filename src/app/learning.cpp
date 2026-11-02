#include "app.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>

namespace cd {
json App::learnRegion(int cam, const json& body, std::string& err) {
    if (!validCam(cam)) { err = "bad camera"; return {}; }
    Cam& c = *cams_[cam];
    std::lock_guard<std::mutex> lock(c.m);
    try {
        if (!body.is_object() || !body.contains("version") || !body["version"].is_number_unsigned() ||
            body.at("version").get<uint64_t>() != c.ver || c.inspected.empty() || c.last.value("ver", uint64_t(0)) != c.ver)
            throw std::invalid_argument("Inspection changed; inspect the frame again");
        if (c.last.value("defect_injected", false)) throw std::invalid_argument("Cannot learn an injected defect");
        if (c.last.value("verdict", "") == "NO_REFERENCE") throw std::invalid_argument("No matching reference");
        const auto& b = body.at("bbox");
        for (auto k : {"x", "y", "width", "height"})
            if (!b.at(k).is_number_integer()) throw std::invalid_argument("Region coordinates must be integers");
        const int x=b.at("x"), y=b.at("y"), w=b.at("width"), h=b.at("height");
        if (x<0 || y<0 || w<=0 || h<=0 || x>=c.inspected.cols || y>=c.inspected.rows ||
            w>c.inspected.cols-x || h>c.inspected.rows-y) throw std::invalid_argument("Region outside frame");
        const cv::Rect box(x,y,w,h);
        // Durable source identity: retry of the same frame/rectangle is idempotent, including after restart.
        const auto source = cas::Reference(c.inspected).fingerprint() + ":" + b.dump();
        auto item = learning_.addLearnedRegion(c.product, c.inspected(box).clone(), box, c.inspected.size(), source);
        c.insp.setLearned(learning_.reference(c.product)->learned());
        return {{"saved",true},{"accepted_cases",json::array({item})}};
    } catch (const std::exception& e) { err=e.what(); return {}; }
}
json App::learnedCases(int cam) {
    if (!validCam(cam)) return json::array();
    Cam& c=*cams_[cam]; std::lock_guard<std::mutex> lock(c.m);
    auto cases=learning_.learnedCases(c.product);
    const auto fingerprint=c.ref.empty()?std::string{}:cas::Reference(c.ref).fingerprint();
    for(auto& item:cases) item["active"]=item.at("reference_fingerprint")==fingerprint;
    return cases;
}
bool App::deleteLearned(int cam,const std::string& id,std::string& err) {
    if (!validCam(cam) || id.empty()) { err="bad camera or case id"; return false; }
    Cam& c=*cams_[cam]; std::lock_guard<std::mutex> lock(c.m);
    try {
        bool found=false;
        for(const auto& item:learning_.learnedCases(c.product)) if(item.at("id")==id) found=true;
        if(!found) {err="case not found";return false;}
        learning_.deleteLearned(id);
        c.insp.setLearned(learning_.reference(c.product)->learned());
        return true;
    } catch(const std::exception& e) {err=e.what();return false;}
}
bool App::learnedImage(int cam,const std::string& id,std::vector<uchar>& out) {
    if(!validCam(cam)) return false;
    Cam& c=*cams_[cam]; std::lock_guard<std::mutex> lock(c.m);
    for(const auto& item:learning_.learnedCases(c.product)) if(item.at("id")==id) {
        auto img=learning_.learnedImage(id); return !img.empty() && cv::imencode(".jpg",img,out);
    }
    return false;
}
bool App::inspectedJpeg(int cam,uint64_t version,std::vector<uchar>& out) {
    if(!validCam(cam)) return false;
    Cam& c=*cams_[cam];std::lock_guard<std::mutex> lock(c.m);
    return version==c.ver && !c.inspected.empty() && cv::imencode(".jpg",c.inspected,out);
}
}
