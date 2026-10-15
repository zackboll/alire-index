// IMGPROC-ALIRE-003 disposable probe: cv::getRectSubPix border semantics on
// the Imgproc 0.2.0 subpixel test ramp, across memory layouts, IPP selection
// and the three IPP-dispatched depth combinations.
//
// Every native call uses memory that is allocated well beyond any address the
// border paths can form: no tight standalone Mat is ever passed at a border.
#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const int N = 4;                       // ramp is N x N
inline double ramp_value(int r, int c) { return r * 40 + c * 8; }

enum Combo { U8_U8, U8_F32, F32_F32 };
const char *combo_name(Combo k) {
   return k == U8_U8 ? "u8>u8" : k == U8_F32 ? "u8>f32" : "f32>f32";
}
int src_type(Combo k) { return k == F32_F32 ? CV_32FC1 : CV_8UC1; }
int dst_depth(Combo k) { return k == U8_U8 ? CV_8U : CV_32F; }

// Independent oracle: bilinear interpolation with nearest-edge (replicate)
// extension, in double precision, at the documented sample coordinates.
double oracle(int w, int h, double cx, double cy, int r, int c) {
   const double x = cx - (w - 1) * 0.5 + c, y = cy - (h - 1) * 0.5 + r;
   const double fx = std::floor(x), fy = std::floor(y);
   const double ax = x - fx, ay = y - fy;
   auto px = [](double v) { return std::min(std::max(v, 0.0), double(N - 1)); };
   const int x0 = int(px(fx)), x1 = int(px(fx + 1)), y0 = int(px(fy)), y1 = int(px(fy + 1));
   return (1 - ay) * ((1 - ax) * ramp_value(y0, x0) + ax * ramp_value(y0, x1)) +
          ay * ((1 - ax) * ramp_value(y1, x0) + ax * ramp_value(y1, x1));
}

// Fill a parent buffer: logical ramp at (top, left), guard either replicated
// from the ramp edge or a sentinel value.
void fill(cv::Mat &parent, int top, int left, bool replicate, double sentinel) {
   for (int r = 0; r < parent.rows; ++r)
      for (int c = 0; c < parent.cols; ++c) {
         const int lr = r - top, lc = c - left;
         double v;
         if (lr >= 0 && lr < N && lc >= 0 && lc < N) v = ramp_value(lr, lc);
         else if (replicate)
            v = ramp_value(std::min(std::max(lr, 0), N - 1), std::min(std::max(lc, 0), N - 1));
         else v = sentinel;
         if (parent.depth() == CV_8U) parent.at<uchar>(r, c) = uchar(v);
         else parent.at<float>(r, c) = float(v);
      }
}

struct Layout {
   std::string name;
   // Owns the memory; `view` is the logical N x N image passed natively
   // (except for the interior layout, which passes the whole padded image).
   cv::Mat parent;
   std::vector<unsigned char> raw;     // external storage for custom strides
   cv::Mat view;
   int shift = 0;                      // centre translation (interior only)
};

// Production guard extent (cpp/subpixel_patch_guard.hpp plan at tag 0.2.0).
void production_guard(int w, int h, float cx, float cy, int &left, int &right,
                      int &top, int &bottom, bool &direct) {
   const float ax = cx - (w - 1) * 0.5f, ay = cy - (h - 1) * 0.5f;
   const int gx = cvFloor(ax), gy = cvFloor(ay);
   const long end_x = long(gx) + w, end_y = long(gy) + h;
   direct = gx >= 0 && gy >= 0 && end_x < N && end_y < N;
   left = std::max(0, -gx); top = std::max(0, -gy);
   right = int(std::max(0L, end_x - (N - 1)));
   bottom = int(std::max(0L, end_y - (N - 1)));
}

Layout make_layout(const std::string &kind, Combo k, int w, int h, float cx,
                   float cy, int offset = 0, int extra_step = 0) {
   Layout L;
   L.name = kind;
   const int type = src_type(k);
   const size_t esz = CV_ELEM_SIZE(type);
   const double sentinel = k == F32_F32 ? 1.0e6 : 250.0;
   if (kind == "interior") {
      const int g = 24;                // far larger than any tested window
      L.parent.create(N + 2 * g, N + 2 * g, type);
      fill(L.parent, g, g, true, 0);
      L.view = L.parent;
      L.shift = g;
   } else if (kind == "production" || kind == "sentinel-min") {
      int left, right, top, bottom; bool direct;
      production_guard(w, h, cx, cy, left, right, top, bottom, direct);
      if (direct && kind == "production") {
         L.parent.create(N, N, type);  // production passes the source itself
         fill(L.parent, 0, 0, true, 0);
         L.view = L.parent;
         L.name = "production(direct)";
         return L;
      }
      L.parent.create(N + top + bottom + 1, N + left + right, type);
      fill(L.parent, top, left, kind == "production", sentinel);
      L.view = L.parent(cv::Rect(left, top, N, N));
   } else if (kind == "replicated-large" || kind == "sentinel-large") {
      const int g = 24;
      L.parent.create(N + 2 * g, N + 2 * g, type);
      fill(L.parent, g, g, kind == "replicated-large", sentinel);
      L.view = L.parent(cv::Rect(g, g, N, N));
   } else if (kind == "continuous-in-sentinel" || kind == "stride") {
      // Rows of the logical image laid out in an external buffer with a
      // chosen step and base offset, surrounded by >= 4 KiB of sentinel.
      const size_t step = kind == "stride" ? N * esz + size_t(extra_step) : N * esz;
      const size_t margin = 4096;
      const size_t bytes = 2 * margin + step * (N + 64);
      L.raw.assign(bytes, 0);
      cv::Mat whole(int(bytes / esz), 1, type, L.raw.data());
      whole.setTo(sentinel);
      unsigned char *base = L.raw.data() + margin + size_t(offset);
      L.view = cv::Mat(N, N, type, base, step);
      for (int r = 0; r < N; ++r)
         for (int c = 0; c < N; ++c) {
            if (type == CV_8UC1) L.view.at<uchar>(r, c) = uchar(ramp_value(r, c));
            else L.view.at<float>(r, c) = float(ramp_value(r, c));
         }
      L.name = kind == "stride" ? "stride(off=" + std::to_string(offset) + ",step=" +
                                      std::to_string(step) + ")"
                                : kind;
   }
   return L;
}

struct Result {
   int differ = 0;                     // pixels differing from the oracle
   double max_abs = 0;
   std::string first;
};

// Compares against the oracle. For u8 output OpenCV uses fixed-point weights,
// so a fractional sample may round to either neighbouring integer; an integer
// sample coordinate (exact source pixel) must match exactly.
Result run(const Layout &L, Combo k, int w, int h, float cx, float cy, cv::Mat &patch) {
   cv::getRectSubPix(L.view, cv::Size(w, h), cv::Point2f(cx + L.shift, cy + L.shift),
                     patch, dst_depth(k));
   Result res;
   for (int r = 0; r < h; ++r)
      for (int c = 0; c < w; ++c) {
         const double e = oracle(w, h, cx, cy, r, c);
         const double a = dst_depth(k) == CV_8U ? patch.at<uchar>(r, c) : patch.at<float>(r, c);
         const double d = std::fabs(a - e);
         const bool ok = dst_depth(k) == CV_8U ? d < 1.0 : d <= 1e-3;
         if (!ok) {
            if (res.differ == 0) {
               char buf[256];
               std::snprintf(buf, sizeof buf, "dst(r=%d,c=%d) sample(x=%.2f,y=%.2f) expected=%.4f actual=%.4f diff=%+.4f",
                             r, c, cx - (w - 1) * 0.5 + c, cy - (h - 1) * 0.5 + r, e, a, a - e);
               res.first = buf;
            }
            ++res.differ;
         }
         res.max_abs = std::max(res.max_abs, d);
      }
   return res;
}

std::string dump(const cv::Mat &p) {
   std::string s;
   for (int r = 0; r < p.rows; ++r) {
      for (int c = 0; c < p.cols; ++c) {
         char buf[32];
         if (p.depth() == CV_8U) std::snprintf(buf, sizeof buf, "%d", p.at<uchar>(r, c));
         else std::snprintf(buf, sizeof buf, "%.3f", p.at<float>(r, c));
         s += buf;
         s += c + 1 == p.cols ? (r + 1 == p.rows ? "" : " | ") : ",";
      }
   }
   return s;
}

// Exact hex dump used for byte-for-byte comparison with the Ada binding.
std::string hexdump(const cv::Mat &p) {
   std::string s;
   for (int r = 0; r < p.rows; ++r)
      for (int c = 0; c < p.cols; ++c) {
         char buf[16];
         if (p.depth() == CV_8U) std::snprintf(buf, sizeof buf, " %02X", p.at<uchar>(r, c));
         else {
            uint32_t bits;
            const float f = p.at<float>(r, c);
            std::memcpy(&bits, &f, 4);
            std::snprintf(buf, sizeof buf, " %08X", bits);
         }
         s += buf;
      }
   return s;
}

} // namespace

int main(int argc, char **argv) {
   const std::string mode = argc > 1 ? argv[1] : "matrix";
   std::printf("OpenCV %s\n", CV_VERSION);
   const std::string info = cv::getBuildInformation();
   for (const char *key : {"Intel IPP", "IPP IW", "Baseline:", "Dispatched code", "requested:", "C++ Compiler:", "Platform:"}) {
      size_t at = info.find(key);
      if (at != std::string::npos) {
         const size_t sol = info.rfind('\n', at) + 1, eol = info.find('\n', at);
         std::printf("build-info: %s\n", info.substr(sol, eol - sol).c_str());
      }
   }
   std::printf("cpu-features: %s\n", cv::getCPUFeaturesLine().c_str());
   std::printf("startup useIPP=%d useOptimized=%d ippVersion=%s\n", cv::ipp::useIPP() ? 1 : 0,
               cv::useOptimized() ? 1 : 0, cv::ipp::getIppVersion().c_str());

   const float coords[] = {0.0f, 0.25f, 0.5f, 1.0f, 1.5f, 2.5f, 2.75f, 3.0f, 3.5f, 3.75f};
   const int sizes[][2] = {{1, 1}, {2, 2}, {3, 3}, {5, 5}, {7, 3}, {3, 7}, {9, 9}, {4, 1}, {1, 4}};
   const Combo combos[] = {U8_U8, U8_F32, F32_F32};

   if (mode == "canonical") {
      // Fixed case list shared with the Ada probe; production layout only,
      // under whatever IPP state the environment selected.
      const float cx_list[] = {3.0f, 0.0f, 3.0f, 0.0f, 1.0f, 2.75f, 3.0f, 2.5f, 1.25f};
      const float cy_list[] = {0.0f, 0.0f, 3.0f, 3.0f, 1.0f, 0.25f, 0.5f, 0.0f, 2.75f};
      const int csz[][2] = {{3, 3}, {2, 2}, {5, 5}, {1, 1}};
      for (Combo k : combos)
         for (auto &s : csz)
            for (int i = 0; i < 9; ++i) {
               Layout L = make_layout("production", k, s[0], s[1], cx_list[i], cy_list[i]);
               cv::Mat p;
               run(L, k, s[0], s[1], cx_list[i], cy_list[i], p);
               std::printf("CANON %s %dx%d %.2f,%.2f:%s\n", combo_name(k), s[0], s[1],
                           cx_list[i], cy_list[i], hexdump(p).c_str());
            }
      return 0;
   }

   const char *layouts[] = {"interior", "production", "sentinel-min", "replicated-large",
                            "sentinel-large", "continuous-in-sentinel"};
   for (int ipp = 1; ipp >= 0; --ipp) {
      cv::ipp::setUseIPP(ipp != 0);
      std::printf("\n##### IPP requested=%d effective useIPP=%d\n", ipp, cv::ipp::useIPP() ? 1 : 0);
      for (Combo k : combos)
         for (const char *kind : layouts) {
            int cases = 0, bad_cases = 0, bad_px = 0;
            double worst = 0;
            std::vector<std::string> examples;
            for (auto &s : sizes)
               for (float cx : coords)
                  for (float cy : coords) {
                     if (cx >= N || cy >= N) continue;   // center must be inside
                     Layout L = make_layout(kind, k, s[0], s[1], cx, cy);
                     cv::Mat p;
                     Result res = run(L, k, s[0], s[1], cx, cy, p);
                     ++cases;
                     if (res.differ) {
                        ++bad_cases; bad_px += res.differ;
                        if (examples.size() < 12) {
                           char buf[512];
                           std::snprintf(buf, sizeof buf, "    %dx%d @(%.2f,%.2f) %d px: %s  [%s]",
                                         s[0], s[1], cx, cy, res.differ, res.first.c_str(), dump(p).c_str());
                           examples.push_back(buf);
                        }
                     }
                     worst = std::max(worst, res.max_abs);
                  }
            std::printf("ipp=%d %-7s %-24s cases=%d mismatching_cases=%d mismatching_px=%d max_abs=%.4f\n",
                        ipp, combo_name(k), kind, cases, bad_cases, bad_px, worst);
            for (auto &e : examples) std::printf("%s\n", e.c_str());
         }

      // Layout dependence of the failing test case: alignment and stride sweep.
      for (Combo k : combos) {
         std::string reference;
         int variants = 0, distinct = 0;
         for (int off : {0, 1, 3, 4, 8, 15, 16, 31, 32, 63})
            for (int extra : {0, 1, 12, 60, 124, 1020}) {
               if (k == F32_F32 && (off % 4 || extra % 4)) continue;
               Layout L = make_layout("stride", k, 3, 3, 3.0f, 0.0f, off, extra);
               cv::Mat p;
               run(L, k, 3, 3, 3.0f, 0.0f, p);
               const std::string d = dump(p);
               if (reference.empty()) reference = d;
               ++variants;
               if (d != reference) {
                  ++distinct;
                  std::printf("    stride variant differs: %s [%s]\n", L.name.c_str(), d.c_str());
               }
            }
         std::printf("ipp=%d %-7s Top_Right 3x3@(3,0) alignment/stride sweep: variants=%d differing_from_first=%d first=[%s]\n",
                     ipp, combo_name(k), variants, distinct, reference.c_str());
      }

      // The exact failing test case, every layout, full matrices.
      for (Combo k : combos)
         for (const char *kind : layouts) {
            Layout L = make_layout(kind, k, 3, 3, 3.0f, 0.0f);
            cv::Mat p;
            Result res = run(L, k, 3, 3, 3.0f, 0.0f, p);
            std::printf("TOPRIGHT ipp=%d %-7s %-24s %s [%s]%s%s\n", ipp, combo_name(k), L.name.c_str(),
                        res.differ ? "DIFFER" : "MATCH ", dump(p).c_str(),
                        res.differ ? "  first: " : "", res.first.c_str());
         }
   }

   // IPP versus portable on a non-linear image (fixed pseudo-random values),
   // production layout, separating interior from border-touching windows:
   // rounding differences are at most one UInt8 step; semantic differences
   // are not.
   {
      uint32_t seed = 12345u;
      double rnd[N][N];
      for (auto &row : rnd)
         for (double &v : row) {
            seed = seed * 1103515245u + 12345u;
            v = double((seed >> 16) % 256u);
         }
      for (Combo k : combos) {
         int interior_cases = 0, border_cases = 0, interior_diff = 0, border_diff = 0;
         int border_large = 0;
         double interior_max = 0, border_max = 0;
         std::vector<std::string> examples;
         for (auto &s : sizes)
            for (float cx : coords)
               for (float cy : coords) {
                  int left, right, top, bottom; bool direct;
                  production_guard(s[0], s[1], cx, cy, left, right, top, bottom, direct);
                  const int type = src_type(k);
                  cv::Mat parent(N + top + bottom + 1, N + left + right, type);
                  for (int r = 0; r < parent.rows; ++r)
                     for (int c = 0; c < parent.cols; ++c) {
                        const double v = rnd[std::min(std::max(r - top, 0), N - 1)]
                                            [std::min(std::max(c - left, 0), N - 1)];
                        if (type == CV_8UC1) parent.at<uchar>(r, c) = uchar(v);
                        else parent.at<float>(r, c) = float(v);
                     }
                  const cv::Mat view = parent(cv::Rect(left, top, N, N));
                  cv::Mat a, b;
                  cv::ipp::setUseIPP(true);
                  cv::getRectSubPix(view, cv::Size(s[0], s[1]), cv::Point2f(cx, cy), a, dst_depth(k));
                  cv::ipp::setUseIPP(false);
                  cv::getRectSubPix(view, cv::Size(s[0], s[1]), cv::Point2f(cx, cy), b, dst_depth(k));
                  cv::Mat a64, b64;
                  a.convertTo(a64, CV_64F);
                  b.convertTo(b64, CV_64F);
                  const double d = cv::norm(a64, b64, cv::NORM_INF);
                  if (direct) {
                     ++interior_cases;
                     if (d > 0) ++interior_diff;
                     interior_max = std::max(interior_max, d);
                  } else {
                     ++border_cases;
                     if (d > 0) ++border_diff;
                     if (d > (dst_depth(k) == CV_8U ? 1.0 : 1e-3)) {
                        ++border_large;
                        if (examples.size() < 20) {
                           char buf[400];
                           std::snprintf(buf, sizeof buf, "    %dx%d @(%.2f,%.2f) |ipp-portable|max=%.3f ipp=[%s] portable=[%s]",
                                         s[0], s[1], cx, cy, d, dump(a).c_str(), dump(b).c_str());
                           examples.push_back(buf);
                        }
                     }
                     border_max = std::max(border_max, d);
                  }
               }
         std::printf("IPPDIFF %-7s interior: cases=%d differing=%d max=%.4f | border: cases=%d differing=%d beyond_rounding=%d max=%.4f\n",
                     combo_name(k), interior_cases, interior_diff, interior_max, border_cases,
                     border_diff, border_large, border_max);
         for (auto &e : examples) std::printf("%s\n", e.c_str());
      }
   }

   // useOptimized(false) with IPP requested.
   cv::setUseOptimized(false);
   cv::ipp::setUseIPP(true);
   {
      Layout L = make_layout("production", U8_U8, 3, 3, 3.0f, 0.0f);
      cv::Mat p;
      Result res = run(L, U8_U8, 3, 3, 3.0f, 0.0f, p);
      std::printf("TOPRIGHT useOptimized=0 setUseIPP(1) effective useIPP=%d u8>u8 production %s [%s]\n",
                  cv::ipp::useIPP() ? 1 : 0, res.differ ? "DIFFER" : "MATCH ", dump(p).c_str());
   }
   return 0;
}
