#include <stdexcept>
#include <string>
extern "C" int cxx_probe(int n) {
   std::string s(static_cast<std::size_t>(n), 'x');
   try {
      if (s.size() == 3) throw std::runtime_error("probe");
   } catch (const std::exception &e) {
      return static_cast<int>(s.size() + std::string(e.what()).size());
   }
   return -1;
}
