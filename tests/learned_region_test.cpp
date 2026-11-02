#include "service/service.h"
#include <opencv2/imgproc.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    const auto dir = std::filesystem::temp_directory_path() / ("cas-learning-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        cv::Mat base(320, 384, CV_8UC3);
        cv::RNG rng(42); rng.fill(base, cv::RNG::UNIFORM, 50, 180);
        cv::GaussianBlur(base, base, {5, 5}, 0);
        cv::Mat normal = base.clone();
        cv::rectangle(normal, {100, 100, 60, 60}, {240, 240, 240}, -1);
        const cv::Rect bbox(85, 85, 90, 90);
        cas::InspectJob job; job.product = "bucket#cam=1";
        job.patch = cas::SettingsPatch::fromJson({{"inspect_scale", 1.0}, {"sensitivity", 0.5}, {"threshold", 0.1}, {"min_blob_area", 3}});
        std::string id;
        {
            cas::Service svc(dir, 2); svc.setReference(job.product, base);
            require(svc.inspectMat(job, normal, true).out.reject, "unlearned anomaly must reject");
            auto item = svc.addLearnedRegion(job.product, normal(bbox).clone(), bbox, base.size(), "17");
            id = item.at("id");
            auto accepted = svc.inspectMat(job, normal, true).out;
            require(!accepted.reject && !accepted.matchedCaseIds.empty(), "learned normal must pass");
            require(accepted.rawAnomalyScore > accepted.anomalyScore, "must report raw score and adjustment");
            require(svc.addLearnedRegion(job.product, normal(bbox).clone(), bbox, base.size(), "17").at("id") == id,
                "retry must be idempotent");
            for (double scale : {0.5, 0.7}) {
                auto scaled = job;
                scaled.patch = cas::SettingsPatch::fromJson({{"inspect_scale", scale}, {"threshold", 0.1}});
                require(!svc.inspectMat(scaled, normal, true).out.reject, "learning must work at analysis scales");
            }
            cv::Mat shifted;
            const cv::Mat translation = (cv::Mat_<double>(2, 3) << 1, 0, 7, 0, 1, -4);
            cv::warpAffine(normal, shifted, translation, normal.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT);
            // Restrict ROI to avoid artificial border differences caused by shifting the synthetic image.
            auto shiftedJob = job;
            shiftedJob.roi = cas::NormPolygon{{0.05, 0.05}, {0.95, 0.05}, {0.95, 0.95}, {0.05, 0.95}};
            require(!svc.inspectMat(shiftedJob, shifted, true).out.reject, "learned patch must tolerate frame translation");
            cv::Mat defect = normal.clone(); cv::rectangle(defect, {115, 115, 30, 30}, {0, 0, 0}, -1);
            require(svc.inspectMat(job, defect, true).out.reject, "new anomaly inside learned region must reject");
            defect = normal.clone(); cv::rectangle(defect, {250, 200, 30, 30}, {0, 0, 0}, -1);
            require(svc.inspectMat(job, defect, true).out.reject, "anomaly outside learned region must reject");
            auto other = job; other.product = "bucket#cam=2"; svc.setReference(other.product, base);
            require(svc.inspectMat(other, normal, false).out.reject, "learning must be camera scoped");
        }
        {
            cas::Service svc(dir, 1); svc.setReference(job.product, base);
            require(!svc.inspectMat(job, normal, false).out.reject, "learning must survive restart");
            cv::Mat replacement = base.clone(); replacement.at<cv::Vec3b>(0, 0)[0] ^= 1;
            svc.setReference(job.product, replacement);
            require(svc.inspectMat(job, normal, false).out.reject, "new reference must not inherit learning");
            svc.setReference(job.product, base);
            require(!svc.inspectMat(job, normal, false).out.reject, "restoring reference must restore learning");
            require(svc.deleteLearned(id), "delete must succeed");
            require(svc.inspectMat(job, normal, false).out.reject, "deleting learning must restore anomaly");
        }
        std::filesystem::remove_all(dir); std::cout << "learned region checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n"; std::filesystem::remove_all(dir); return 1;
    }
}
