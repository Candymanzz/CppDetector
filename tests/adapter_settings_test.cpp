#include "app/app.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool near(double a, double b) { return std::abs(a-b) < 1e-8; }
}

int main() {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / ("adapter-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        cv::setNumThreads(1);
        std::string err;
        cv::Mat image(1024,1224,CV_8UC3,cv::Scalar::all(100));
        cv::putText(image,"LABEL",{350,500},cv::FONT_HERSHEY_SIMPLEX,2,{240,240,240},4);
        {
            cd::App app(dir/"frames",dir/"config"/"test.json",dir/"logs");
            require(app.setReferenceFromImage(4,image,err),"reference");
            auto cam = app.state()["cameras_info"][4];
            require(near(cam["horizontal_shift_tolerance_mm"],3.24) && near(cam["vertical_shift_tolerance_mm"],4.08),"camera-specific mm defaults");
            require(app.setCamera(4,{{"horizontal_shift_tolerance_mm",.54},{"vertical_shift_tolerance_mm",1.02},{"downscale",.73}},err),"set mm and 1 percent scale");
            auto result = app.inspectUpload(4,image,"metric",false,err);
            require(near(result["shift_limit"][0],2) && near(result["shift_limit"][1],3),"mm converts using camera 4 scale");
            require(near(result["shift_limit_mm"][0],.54) && near(result["analysis_pixel_mm"][0],.27/.73),"metric diagnostics");
            require(result["native_settings"]["inspect_scale"] == .73,"scale retained");
            const auto before = app.state()["cameras_info"][4];
            require(!app.setCamera(4,{{"horizontal_shift_tolerance_mm",1},{"vertical_shift_tolerance_mm",30.01}},err),"invalid mm rejected");
            require(app.state()["cameras_info"][4] == before,"failed update is atomic"); err.clear();
            require(app.setCamera(4,{{"max_alignment_shift_x",7}},err),"legacy pixel update");
            cam=app.state()["cameras_info"][4];
            require(near(cam["horizontal_shift_tolerance_mm"],1.89) && near(cam["vertical_shift_tolerance_mm"],1.02),"pixel override affects one metric axis");
            const cd::json calibration = {{"horizontal",{{"p0",{0,0}},{"p1",{1,0}},{"length_mm",122.4}}},
                                          {"vertical",{{"p0",{0,0}},{"p1",{0,1}},{"length_mm",204.8}}}};
            require(app.setCamera(4,{{"calibration",calibration},{"horizontal_shift_tolerance_mm",.5},{"vertical_shift_tolerance_mm",1}},err),"calibration API");
            result=app.inspectUpload(4,image,"calibrated",false,err);
            require(result["metric_scale_source"] == "calibrated" && near(result["shift_limit"][0],5) && near(result["shift_limit"][1],5),"calibration applied to inspection");
            require(app.setCamera(4,{{"horizontal_shift_tolerance_mm",0},{"learning_robust_enabled",false},{"learning_shadow_enabled",false},{"learning_search_radius",0}},err),"zero and false settings");
        }
        {
            cd::App app(dir/"frames",dir/"config"/"test.json",dir/"logs");
            const auto cam=app.state()["cameras_info"][4];
            require(cam["horizontal_shift_tolerance_mm"] == 0 && cam["learning_robust_enabled"] == false && cam["learning_shadow_enabled"] == false && cam["learning_search_radius"] == 0,"zero and false survive restart");
            require(cam["metric_scale_source"] == "calibrated" && cam["downscale"] == .73 && cam["reference"] == "upload","calibration, scale and reference survive restart");
        }
        const auto resolved = fs::weakly_canonical(dir), parent = fs::weakly_canonical(fs::temp_directory_path());
        require(resolved.parent_path() == parent && resolved.filename().string().rfind("adapter-settings-",0)==0,"safe cleanup path");
        fs::remove_all(resolved);
        std::cout << "adapter metric/persistence checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<" (data: "<<dir.string()<<")\n"; return 1; }
}
