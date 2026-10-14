// Disposable probe: cv::getRectSubPix on the Imgproc 0.2.0 subpixel test ramp,
// using the binding's guarded-ROI layout, with IPP enabled and disabled.
#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

static cv::Mat ramp() {
   cv::Mat m(4, 4, CV_8UC1);
   for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
         m.at<uchar>(r, c) = static_cast<uchar>(r * 40 + c * 8);
   return m;
}

// Same replicated guard geometry as cpp/subpixel_patch_guard.hpp at tag 0.2.0
// (byte-replicated padding plus one extra bottom row, logical ROI passed on).
static cv::Mat guarded(const cv::Mat &src, int w, int h, float cx, float cy,
                       cv::Mat &storage) {
   const float ax = cx - (w - 1) * 0.5f, ay = cy - (h - 1) * 0.5f;
   const int gx = cvFloor(ax), gy = cvFloor(ay);
   const int end_x = gx + w, end_y = gy + h;
   const bool direct = gx >= 0 && gy >= 0 && end_x < src.cols && end_y < src.rows;
   if (direct) return src;
   const int left = std::max(0, -gx), top = std::max(0, -gy);
   const int right = std::max(0, end_x - (src.cols - 1));
   const int bottom = std::max(0, end_y - (src.rows - 1));
   cv::copyMakeBorder(src, storage, top, bottom + 1, left, right,
                      cv::BORDER_REPLICATE);
   return storage(cv::Rect(left, top, src.cols, src.rows));
}

static bool run_case(const char *name, int w, int h, float x, float y) {
   const cv::Mat src = ramp();
   cv::Mat storage;
   const cv::Mat input = guarded(src, w, h, x, y, storage);
   bool ok = true;
   std::string got;
   for (int ipp = 1; ipp >= 0; --ipp) {
      cv::ipp::setUseIPP(ipp != 0);
      cv::Mat patch;
      cv::getRectSubPix(input, cv::Size(w, h), cv::Point2f(x, y), patch, CV_8U);
      std::string line;
      bool match = true;
      for (int r = 0; r < h; ++r)
         for (int c = 0; c < w; ++c) {
            const float sx = std::max(0.0f, std::min(3.0f, x - (w - 1) * 0.5f + c));
            const float sy = std::max(0.0f, std::min(3.0f, y - (h - 1) * 0.5f + r));
            // Ada's UInt8_Value (Float) conversion rounds to nearest, ties away.
            const int expected = static_cast<int>(std::lround(sy * 40.0f + sx * 8.0f));
            const int actual = patch.at<uchar>(r, c);
            line += std::to_string(actual) + (actual == expected ? "" : "*") +
                    (c + 1 == w ? (r + 1 == h ? "" : " | ") : ",");
            if (actual != expected) match = false;
         }
      std::printf("%-16s %dx%d @(%.2f,%.2f) ipp=%d useIPP=%d %s  [%s]\n", name, w,
                  h, x, y, ipp, cv::ipp::useIPP() ? 1 : 0,
                  match ? "MATCH " : "DIFFER", line.c_str());
      if (ipp == 0 && !match) ok = false;
      if (ipp == 1 && !match) got = "ipp-differs";
   }
   return ok;
}

int main() {
   std::printf("OpenCV %s\n", CV_VERSION);
   const std::string info = cv::getBuildInformation();
   size_t at = info.find("IPP");
   while (at != std::string::npos) {
      const size_t sol = info.rfind('\n', at) + 1;
      const size_t eol = info.find('\n', at);
      std::printf("build-info: %s\n", info.substr(sol, eol - sol).c_str());
      at = info.find("IPP", eol);
   }
   std::printf("haveIPP=%d ippVersion=%s\n", cv::ipp::useIPP() ? 1 : 0,
               cv::ipp::getIppVersion().c_str());
   struct C { const char *n; int w, h; float x, y; } cases[] = {
      {"Odd", 3, 3, 1, 1}, {"One_Row", 3, 1, 1, 1}, {"One_Column", 1, 3, 1, 1},
      {"Single", 1, 1, 1, 1}, {"Fraction_U8", 1, 1, 1.25f, 1.25f},
      {"Top", 3, 3, 1, 0}, {"Bottom", 3, 3, 1, 3}, {"Left", 3, 3, 0, 1},
      {"Right", 3, 3, 3, 1}, {"Top_Right", 3, 3, 3, 0}, {"Bottom_Left", 3, 3, 0, 3},
      {"Bottom_Corner", 3, 3, 3, 3}, {"Wider", 7, 3, 1, 1}, {"Taller", 3, 7, 1, 1},
      {"Fraction_Top", 3, 3, 1.25f, 0.25f}, {"Fraction_Bottom", 3, 3, 1.25f, 2.75f},
      {"Fraction_Left", 3, 3, 0.25f, 1.25f}, {"Fraction_Right", 3, 3, 2.75f, 1.25f},
      {"Top_Left", 3, 3, 0, 0}};
   int portable_failures = 0;
   for (const C &c : cases)
      if (!run_case(c.n, c.w, c.h, c.x, c.y)) ++portable_failures;
   std::printf("portable (useIPP=0) oracle mismatches: %d\n", portable_failures);
   return 0;
}
