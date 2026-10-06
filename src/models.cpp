#include "blurcam/vision.hpp"
#include <cerrno>
#include <curl/curl.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <openssl/evp.h>
#include <sstream>
#include <unistd.h>
namespace blurcam {
void exclusive_write(const std::filesystem::path &p, const std::string &text) {
  int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0)
    throw std::runtime_error("Refusing overwrite or unable to create: " + p.string());
  size_t n = 0;
  while (n < text.size()) {
    auto r = write(fd, text.data() + n, text.size() - n);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0) {
      if (fd >= 0)
        close(fd);
      unlink(p.c_str());
      throw std::runtime_error("Write failed");
    }
    n += size_t(r);
  }
  close(fd);
}
std::string model_digest(const std::filesystem::path &p) {
  std::ifstream file(p, std::ios::binary);
  if (!file)
    throw std::runtime_error("Cannot read model");
  auto *ctx = EVP_MD_CTX_new();
  if (!ctx)
    throw std::bad_alloc();
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  char buf[8192];
  while (file) {
    file.read(buf, sizeof(buf));
    EVP_DigestUpdate(ctx, buf, size_t(file.gcount()));
  }
  if (!file.eof()) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("Model read error");
  }
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int len;
  EVP_DigestFinal_ex(ctx, md, &len);
  EVP_MD_CTX_free(ctx);
  std::ostringstream out;
  for (unsigned i = 0; i < len; i++)
    out << std::hex << std::setw(2) << std::setfill('0') << unsigned(md[i]);
  return out.str();
}
std::filesystem::path default_model() { return data_path() / "yunet-2023mar.onnx"; }
void list_models() {
  auto p = default_model();
  std::cout << "YuNet 2023mar (MIT)\nPath: " << p << "\nSHA256: " << MODEL_SHA << "\nStatus: "
            << (std::filesystem::exists(p)
                    ? (model_digest(p) == MODEL_SHA ? "verified" : "CHECKSUM MISMATCH")
                    : "not installed")
            << '\n';
}
void install_model() {
  auto p = default_model();
  if (std::filesystem::exists(p)) {
    if (model_digest(p) == MODEL_SHA) {
      std::cout << "Model already verified: " << p << '\n';
      return;
    }
    throw std::runtime_error(
        "Existing model has wrong checksum; remove it explicitly before reinstalling");
  }
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    throw std::runtime_error("Cannot initialize HTTPS");
  struct Curl {
    CURL *h = curl_easy_init();
    ~Curl() {
      if (h)
        curl_easy_cleanup(h);
      curl_global_cleanup();
    }
  } curl;
  if (!curl.h)
    throw std::bad_alloc();
  std::string data;
  auto receive = +[](char *p, size_t a, size_t b, void *o) -> size_t {
    auto *out = static_cast<std::string *>(o);
    size_t n = a * b;
    if (n > 300000 || out->size() > 300000 - n)
      return 0;
    try {
      out->append(p, n);
    } catch (...) {
      return 0;
    }
    return n;
  };
  curl_easy_setopt(curl.h, CURLOPT_URL,
                   "https://media.githubusercontent.com/media/opencv/opencv_zoo/"
                   "47534e27c9851bb1128ccc0102f1145e27f23f98/models/face_detection_yunet/"
                   "face_detection_yunet_2023mar.onnx");
  curl_easy_setopt(curl.h, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl.h, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(curl.h, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.h, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl.h, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(curl.h, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl.h, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(curl.h, CURLOPT_WRITEDATA, &data);
  auto result = curl_easy_perform(curl.h);
  if (result != CURLE_OK)
    throw std::runtime_error(std::string("Model download failed: ") + curl_easy_strerror(result));
  std::filesystem::create_directories(p.parent_path());
  std::string name = (p.parent_path() / ".yunet-XXXXXX").string();
  std::vector<char> temp(name.begin(), name.end());
  temp.push_back(0);
  int fd = mkstemp(temp.data());
  if (fd < 0)
    throw std::runtime_error("Cannot create model temporary file");
  struct Temp {
    std::string p;
    int fd = -1;
    ~Temp() {
      if (fd >= 0)
        close(fd);
      unlink(p.c_str());
    }
  } cleanup{temp.data(), fd};
  size_t pos = 0;
  while (pos < data.size()) {
    auto r = write(fd, data.data() + pos, data.size() - pos);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0)
      throw std::runtime_error("Cannot write downloaded model");
    pos += size_t(r);
  }
  if (model_digest(cleanup.p) != MODEL_SHA)
    throw std::runtime_error("Downloaded model checksum mismatch");
  if (link(cleanup.p.c_str(), p.c_str()) != 0)
    throw std::runtime_error("Cannot install model without overwriting existing file");
  std::cout << "Installed verified MIT YuNet model: " << p << '\n';
}
} // namespace blurcam
