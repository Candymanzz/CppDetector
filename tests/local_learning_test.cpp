#include "app/app.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/("local-learning-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        cv::setNumThreads(1);
        cv::Mat base(320,384,CV_8UC3); cv::RNG rng(42); rng.fill(base,cv::RNG::UNIFORM,50,180);
        cv::GaussianBlur(base,base,{5,5},0);
        auto normal=base.clone(); cv::rectangle(normal,{100,100,60,60},{240,240,240},-1);
        std::string id,err;
        {
            cd::App app(dir/"frames",dir/"config"/"test.json",dir/"logs");
            require(app.setReferenceFromImage(0,base,err),"reference upload");
            require(app.setReferenceFromImage(1,base,err),"second camera reference");
            require(app.setCamera(0,{{"reject_threshold",10},{"min_blob_area",3},{"max_alignment_shift_x",20},{"learning_search_radius",22}},err),"settings");
            auto r=app.inspectUpload(0,normal,"normal",false,err); require(r.at("reject"),"unlearned anomaly");
            cd::json body={{"version",r.at("ver")},{"bbox",{{"x",85},{"y",85},{"width",90},{"height",90}}}};
            auto item=app.learnRegion(0,body,err);
            require(err.empty() && item.at("saved"),"save selected region");
            id=item.at("accepted_cases")[0].at("id");
            require(app.learnRegion(0,body,err).at("accepted_cases")[0].at("id")==id,"retry idempotency");
            r=app.inspectUpload(0,normal,"normal",false,err);
            require(!r.at("reject").get<bool>() && r.at("raw_anomaly_score")>r.at("anomaly_score"),"local learning affects score");
            require(r.at("matched_accepted_case_ids")[0]==id,"matched ids exposed");
            require(r.at("learning_engine")=="local-robust-v2" && r.at("learning_diagnostics")[0]["status"]=="matched", "diagnostics exposed");
            require(r.at("learning_ms").get<double>()>=0,"learning timing");
            require(!app.setCamera(0,{{"learning_search_radius",65}},err),"radius range enforced");err.clear();
            require(!app.setCamera(0,{{"learning_shadow_enabled",2}},err),"boolean type enforced");err.clear();
            require(app.setCamera(0,{{"learning_robust_enabled",false},{"learning_shadow_enabled",false}},err),"native fallback settings");
            require(app.inspectUpload(0,normal,"legacy",false,err).at("learning_engine")=="native-52aa31ea","native fallback mode");
            require(app.setCamera(0,{{"learning_robust_enabled",true},{"learning_shadow_enabled",true}},err),"restore robust mode");
            r=app.inspectUpload(0,normal,"normal",false,err);
            std::vector<uchar> image;
            require(app.inspectedJpeg(0,r.at("ver"),image),"untouched inspection image");
            require(app.learnedImage(0,id,image) && !app.learnedImage(1,id,image),"camera-scoped previews");
            app.learnRegion(0,body,err); require(!err.empty(),"stale selection rejected"); err.clear();
            body["version"]=r.at("ver"); body["bbox"]["width"]=9999;
            app.learnRegion(0,body,err); require(!err.empty(),"invalid bounds rejected"); err.clear();
            body["bbox"]["width"]=90;
            auto injected=app.inspectUpload(0,normal,"injected",true,err); body["version"]=injected.at("ver");
            app.learnRegion(0,body,err); require(!err.empty(),"injected defect must not be learned"); err.clear();
            auto defect=normal.clone();cv::rectangle(defect,{115,115,30,30},{0,0,0},-1);
            require(app.inspectUpload(0,defect,"new-defect",false,err).at("reject"),"new defect inside learned region");
            require(app.inspectUpload(1,normal,"other-camera",false,err).at("reject"),"camera isolation");
        }
        {
            cd::App app(dir/"frames",dir/"config"/"test.json",dir/"logs");
            require(app.state()["cameras_info"][0]["max_alignment_shift_x"]==20,"independent limits persist");
            require(app.state()["cameras_info"][0]["learning_search_radius"]==22 && app.state()["cameras_info"][0]["learning_robust_enabled"]==true,"learning settings persist");
            require(!app.inspectUpload(0,normal,"restart",false,err).at("reject").get<bool>(),"restart preserves learning");
            auto replacement=base.clone();replacement.at<cv::Vec3b>(0,0)[0]^=1;
            require(app.setReferenceFromImage(0,replacement,err),"replace reference");
            require(!app.learnedCases(0)[0].at("active").get<bool>(),"different reference deactivates cases");
            require(app.inspectUpload(0,normal,"changed-ref",false,err).at("reject"),"reference isolation");
            require(app.setReferenceFromImage(0,base,err),"restore reference");
            require(!app.inspectUpload(0,normal,"restored",false,err).at("reject").get<bool>(),"restored reference restores cases");
            require(!app.deleteLearned(1,id,err),"cannot delete another camera's case");err.clear();
            require(app.deleteLearned(0,id,err),"delete case");
            require(app.inspectUpload(0,normal,"deleted",false,err).at("reject"),"delete restores anomaly");
        }
        const auto resolved=fs::weakly_canonical(dir),parent=fs::weakly_canonical(fs::temp_directory_path());
        require(resolved.parent_path()==parent && resolved.filename().string().rfind("local-learning-",0)==0,"safe cleanup path");
        fs::remove_all(resolved);
        std::cout<<"local learning checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<" (data: "<<dir.string()<<")\n";return 1;}
}
