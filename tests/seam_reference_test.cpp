#include "inspect/seam_reference.h"
#include "inspect/inspector.h"
#include "app/app.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
using nlohmann::json;
namespace {
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
cd::SeamParams settings(){cd::SeamParams p;p.enabled=true;p.referenceSearch=true;p.pts={{180,30},{180,450}};p.bandHalf=20;p.referenceTolerance=1;return p;}
cv::Mat fixture(){cv::Mat image(480,360,CV_8UC3,cv::Scalar(70,70,70));cv::line(image,{180,20},{180,460},{110,110,110},4);cv::rectangle(image,{187,210,22,50},{15,15,15},-1);return image;}
void synthetic(){
    auto p=settings();const auto ref=fixture();const auto model=cd::prepareSeamReference(ref,p,"fixture");require(model->ready,"reference seam captured");p.referenceSignature=model->signature;
    auto same=cd::inspectReferenceSeam(ref,model.get(),p);require(same.controlComplete&&!same.reject&&same.maxDev<.01,"reference shape compares with itself without re-fitting");
    auto marker=ref.clone();cv::rectangle(marker,{187,210,22,50},{70,70,70},-1);cv::rectangle(marker,{188,190,26,80},{0,0,0},-1);
    auto moved=cd::inspectReferenceSeam(marker,model.get(),p);require(moved.controlComplete&&!moved.reject&&moved.maxDev<1,"nearby moved black square cannot replace the seam");
    cv::Mat brighter;cv::add(ref,cv::Scalar(20,20,20),brighter);auto light=cd::inspectReferenceSeam(brighter,model.get(),p);require(light.controlComplete&&!light.reject&&light.maxDev<.25,"uniform brightness change does not become seam displacement");
    auto curved=cv::Mat(480,360,CV_8UC3,cv::Scalar(70,70,70));std::vector<cv::Point> points;
    for(int y=20;y<461;++y)points.emplace_back(cvRound(180+3*std::sin(y*.035)),y);
    cv::polylines(curved,points,false,{110,110,110},4);const auto curvedModel=cd::prepareSeamReference(curved,p,"curved");require(curvedModel->ready,"non-quadratic reference curve can be learned");
    auto curvedSettings=p;curvedSettings.referenceSignature=curvedModel->signature;auto curvedSame=cd::inspectReferenceSeam(curved,curvedModel.get(),curvedSettings);require(curvedSame.controlComplete&&!curvedSame.reject&&curvedSame.maxDev<.3,"measured reference form is retained instead of straightened");
    auto kink=ref.clone();cv::rectangle(kink,{170,310,18,26},{70,70,70},-1);cv::line(kink,{185,310},{185,335},{110,110,110},4);
    auto bad=cd::inspectReferenceSeam(kink,model.get(),p);require(bad.controlComplete&&bad.reject&&bad.maxDev>3,"local seam displacement is measured against frozen reference");
    p.referenceTolerance=7;auto loose=cd::inspectReferenceSeam(kink,model.get(),p);require(loose.controlComplete&&!loose.reject,"one larger tolerance accepts the same measured displacement");
    require(loose.rowState==bad.rowState&&loose.rowEdge==bad.rowEdge&&loose.maxDev==bad.maxDev,"changing tolerance never changes search or measured shape");
    for(float t:loose.rowTol)require(std::abs(t-7)<1e-6,"same tolerance at every sample, including both ends");
    for(int y:{60,410}){auto end=ref.clone();cv::rectangle(end,{170,y,18,20},{70,70,70},-1);cv::line(end,{185,y},{185,y+19},{110,110,110},4);p.referenceTolerance=1;auto a=cd::inspectReferenceSeam(end,model.get(),p);p.referenceTolerance=7;auto b=cd::inspectReferenceSeam(end,model.get(),p);require(a.reject&&!b.reject,"uniform tolerance applies near either end as well as middle");}
    auto missing=ref.clone();cv::rectangle(missing,{165,140,21,180},{70,70,70},-1);auto gap=cd::inspectReferenceSeam(missing,model.get(),p);require(!gap.controlComplete,"missing seam cannot be replaced by the neighboring square");
    cv::Mat shifted;const cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,6,0,1,-4);cv::warpAffine(ref,shifted,transform,ref.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT,{70,70,70});p.referenceTolerance=1;auto pose=cd::inspectReferenceSeam(shifted,model.get(),p,{6,-4});require(pose.controlComplete&&!pose.reject&&pose.maxDev<.05,"whole-frame pose compensation keeps seam-reference coordinates consistent");
    require(!cd::inspectReferenceSeam(ref,nullptr,p).controlComplete,"unconfirmed reference cannot pass");
    auto changed=p;changed.pts[0].x+=2;require(!cd::inspectReferenceSeam(ref,model.get(),changed).controlComplete,"changed endpoints cannot reuse the old model");
    cv::Mat border;const cv::Mat nearBorder=(cv::Mat_<double>(2,3)<<1,0,140,0,1,0);cv::warpAffine(ref,border,nearBorder,ref.size(),cv::INTER_LINEAR,cv::BORDER_CONSTANT,{70,70,70});
    auto atBorder=settings();atBorder.pts={{320,30},{320,450}};atBorder.referenceTolerance=50;const auto borderModel=cd::prepareSeamReference(border,atBorder,"border");require(borderModel->ready,"near-border reference can be prepared");atBorder.referenceSignature=borderModel->signature;
    require(!cd::inspectReferenceSeam(border,borderModel.get(),atBorder,{50,0}).controlComplete,"invisible expected seam cannot be replaced by another visible edge even at maximum tolerance");
    std::cout<<"Reference seam: square isolation, fixed shape, uniform tolerance, true kink, gaps and pose passed\n";
}
void persistence(){
    namespace fs=std::filesystem;const auto base=fs::weakly_canonical(fs::temp_directory_path());const auto root=base/("seam-reference-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto frame=root/"frames/camera_0/ref/frame.jpg";fs::create_directories(frame.parent_path());cv::imwrite(frame.string(),fixture());const auto config=root/"config/settings.json";
    std::string signature;
    {cd::App app(root/"frames",config,root/"logs");std::string error;json response;
        auto image=cv::imread(frame.string());
        require(app.setCamera(0,{{"seam",{{"search_method","reference"},{"enabled",true}}}},error),"reference mode may be selected before points exist");
        require(app.inspectUpload(0,image,"no-line",false,error)["verdict"]=="NOT_INSPECTED","missing endpoints cannot bypass the reference guard");
        require(app.setCamera(0,{{"seam",{{"search_method","reference"},{"enabled",true},{"pts",{{180,30},{180,450}}},{"reference_tolerance",1}}}},error),"reference mode configured");
        require(app.inspectUpload(0,image,"unconfirmed",false,error)["verdict"]=="NOT_INSPECTED","unconfirmed mode cannot pass HTTP adapter");
        require(app.confirmSeamReference(0,response,error),"reference confirmation accepted");signature=response["seam"]["reference_signature"];
        require(app.state()["cameras_info"][0]["seam_reference"]["ready"].get<bool>(),"ready state exposed");
        require(!app.setCamera(0,{{"seam",{{"reference_signature","spoof"}}}},error),"clients cannot forge confirmation");
        require(app.setCamera(0,{{"seam",{{"reference_tolerance",5}}}},error),"uniform tolerance update accepted");
        require(app.state()["cameras_info"][0]["seam"]["reference_signature"]==signature,"tolerance retains reference identity");
        const auto echo=app.state()["cameras_info"][0]["seam"];require(app.setCamera(0,{{"seam",echo}},error),"read-modify-write clients may echo the unchanged read-only signature");
        require(app.state()["cameras_info"][0]["seam_reference"]["ready"].get<bool>(),"echo retains confirmation");
    }
    {cd::App app(root/"frames",config,root/"logs");std::string error;json response;
        const auto c=app.state()["cameras_info"][0];require(c["seam_reference"]["ready"].get<bool>()&&c["seam"]["reference_tolerance"]==5,"confirmed reference and tolerance survive restart");
        require(app.setCamera(0,{{"seam",{{"pts",{{181,30},{181,450}}}}}},error),"endpoint edit accepted");require(!app.state()["cameras_info"][0]["seam_reference"]["ready"].get<bool>(),"endpoint edit invalidates confirmation");
        require(app.confirmSeamReference(0,response,error),"edited endpoints can be confirmed");
    }
    auto changed=fixture();changed.at<cv::Vec3b>(100,100)={0,0,0};cv::imwrite(frame.string(),changed);
    {cd::App app(root/"frames",config,root/"logs");std::string error;json response;
        auto info=app.state()["cameras_info"][0]["seam_reference"];require(!info["ready"].get<bool>()&&info["reason"]=="seam_reference_changed","changed pixels at same filename invalidate the reference");
        require(app.confirmSeamReference(0,response,error),"updated reference can be confirmed explicitly");
        require(app.setReferenceFromImage(0,changed,error),"primary reference can be replaced");require(!app.state()["cameras_info"][0]["seam_reference"]["ready"].get<bool>(),"primary reference replacement always requires confirmation");
    }
    const auto resolved=fs::weakly_canonical(root);require(resolved.parent_path()==base&&resolved.filename().string().rfind("seam-reference-test-",0)==0,"safe temporary cleanup");fs::remove_all(resolved);
    std::cout<<"Reference seam: confirmation, persistence, invalidation and signature guards passed\n";
}
void photos(const std::filesystem::path& file,const std::filesystem::path& output){
    std::ifstream input(file);json state;input>>state;const auto c=state["cameras_info"][6],s=c["seam"];cd::SeamParams p;p.enabled=true;p.referenceSearch=true;p.referenceTolerance=s["tol_mid"];p.bandHalf=s["band"];
    for(const auto& q:s["pts"])p.pts.emplace_back(q[0].get<double>(),q[1].get<double>());
    const auto root=std::filesystem::path("D:/Pj/CppDetector/frames36/camera_6");const auto image=cv::imread((root/c["reference"].get<std::string>()/"frame.jpg").string());
    const auto model=cd::prepareSeamReference(image,p,"camera6");require(model->ready,"actual camera 6 reference can be prepared");p.referenceSignature=model->signature;
    std::filesystem::create_directories(output);json result={{"reference_coverage",model->coverage},{"sign",model->sign},{"frames",json::array()}};
    for(const auto& entry:c["frames"]){const std::string name=entry;auto current=cv::imread((root/name/"frame.jpg").string());auto r=cd::inspectReferenceSeam(current,model.get(),p);
        if(name==c["reference"].get<std::string>())require(r.controlComplete&&r.maxDev<.01,"actual reference self-comparison is zero on runtime sampling grid");
        result["frames"].push_back({{"frame",name},{"coverage",r.coverage},{"complete",r.controlComplete},{"max_dev",r.maxDev},{"reject",r.reject},{"ms",r.ms}});
        if(name=="f_0000079"||name=="f_0000080"||name=="f_0000083")cv::imwrite((output/(name+".png")).string(),r.maskPreview);
    }
    std::ofstream(output/"photos.json")<<result.dump(2);std::cout<<result.dump()<<'\n';
}
}
int main(int argc,char**argv){try{cv::setNumThreads(1);synthetic();persistence();if(argc>2)photos(argv[1],argv[2]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
