#include "app/app.h"
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    const auto root=std::filesystem::temp_directory_path()/
        ("cppdetector-port-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root/"frames");
        cv::Mat image(513,613,CV_8UC3); cv::RNG rng(9); rng.fill(image,cv::RNG::UNIFORM,20,230);
        std::string error;
        {
            cd::App app(root/"frames",root/"config/settings.json",root/"logs");
            require(app.setReferenceFromImage(4,image,error),"set reference");
            require(app.setCamera(4,{{"sensitivity",.5},{"horizontal_shift_tolerance_mm",30},
                {"vertical_shift_tolerance_mm",29.95},{"downscale",.5}},error),"set new limits");
            require(!app.setCamera(4,{{"horizontal_shift_tolerance_mm",30.01}},error),"reject over 30mm");
            error.clear();
            auto result=app.inspectUpload(4,image,"self",false,error);
            require(error.empty(),"inspect upload error");
            require(result["score"]==0 && result["shift"][0]==0 && result["shift"][1]==0,"self inspection");
            require(result.contains("shift_mm") && result.contains("shift_response"),"position diagnostics");
            require(app.setCamera(4,{{"label_outline",{{50,50},{550,50},{550,450},{50,450}}}},error),"save label outline");
            const auto before=app.state()["cameras_info"][4];
            require(!app.setCamera(4,{{"label_tracking_enabled","false"}},error),"reject non-boolean toggle");
            require(app.state()["cameras_info"][4]==before,"invalid toggle update is atomic");
            require(!app.setCamera(4,{{"label_outline",{{-1,0},{10,0},{10,10}}}},error),"invalid label rejected");
            require(app.state()["cameras_info"][4]==before,"invalid contour update is atomic");
            error.clear();
            require(app.setCamera(4,{{"label_tracking_enabled",false}},error),"disable label tracking");
            require(app.state()["cameras_info"][4]["label_outline"]==before["label_outline"],"disable preserves contour");
            result=app.inspectUpload(4,image,"disabled",false,error);
            require(!result["label_tracking"]["enabled"].get<bool>(),"disabled contour not used in inspection");
        }
        {
            cd::App app(root/"frames",root/"config/settings.json",root/"logs");
            auto camera=app.state()["cameras_info"][4];
            require(camera["horizontal_shift_tolerance_mm"]==30 && camera["vertical_shift_tolerance_mm"]==29.95,
                "metric settings restart");
            require(camera["label_outline"].size()==4,"label contour survives restart");
            require(camera["label_tracking_enabled"]==false,"disabled setting survives restart");
            require(app.state()["cameras_info"][3]["label_outline"].empty(),"label is camera scoped");
            require(app.state()["cameras_info"][3]["label_tracking_enabled"]==true,"toggle is camera scoped");
            require(app.setCamera(4,{{"label_tracking_enabled",true}},error),"re-enable label tracking");
            const auto enabled=app.inspectUpload(4,image,"enabled",false,error);
            require(enabled["label_tracking"]["enabled"].get<bool>(),"restored contour is used in inspection");
            require(app.setReferenceFromImage(4,image,error),"replace reference");
            require(app.state()["cameras_info"][4]["label_outline"].empty(),"new reference requires contour confirmation");
        }
        const auto resolved=std::filesystem::weakly_canonical(root);
        if (resolved.parent_path()==std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) &&
            resolved.filename().string().rfind("cppdetector-port-",0)==0) std::filesystem::remove_all(resolved);
        std::cout<<"application inspection and settings restart passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<" fixtures: "<<root<<'\n'; return 1; }
}
