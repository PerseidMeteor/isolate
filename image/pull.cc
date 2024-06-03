#include "../include/log.hpp"
#include "../include/rapidjson/document.h"
#include <vector>
#include <algorithm>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <sstream>

// 使用libcurl的回调函数来收集HTTP响应数据
static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            std::string *s) {
  size_t newLength = size * nmemb;
  try {
    s->append((char *)contents, newLength);
    return newLength;
  } catch (std::bad_alloc &e) {
    // 处理内存不足问题
    return 0;
  }
}

// 发送HTTP GET请求
bool http_get(const std::string &url, std::string &response) {
  CURL *curl;
  CURLcode res;
  response.clear();

  curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
  }
  return false;
}

///@brief download image blob
bool download_blob(const std::string &registry, const std::string &repository,
                   const std::string &digest, std::string& path) {
  std::string url = registry + "/v2/" + repository + "/blobs/" + digest;
  std::string blob_data;
  if (http_get(url, blob_data)) {
    
    std::string filename =
        "image_data/" + digest.substr(7); // 假设去掉“sha256:”

    path = filename;
    std::ofstream file(filename, std::ios::binary);
    file.write(blob_data.c_str(), blob_data.size());
    file.close();

    return true;
  }
  return false;
}

int fetch_image(std::string image, std::vector<std::string>& layers_path) {
  Logger log;
  log.Log(INFO, "Fetch image " + image);

  // 解析 image 字符串，获取仓库和标签
  std::size_t colon_pos = image.find_last_of(':');
  if (colon_pos == std::string::npos) {
    log.Log(ERROR, "Invalid image format. Image must contain a tag.");
    return -1;
  }

  std::string repository = image.substr(0, colon_pos);
  std::string tag = image.substr(colon_pos + 1);

  std::string manifest;
  std::string registry = "http://10.68.49.26:8000";

  std::string manifest_url =
      registry + "/v2/" + repository + "/manifests/" + tag;
  log.Log(INFO, "Requesting manifest from URL: " + manifest_url);

  if (http_get(manifest_url, manifest)) {
    log.Log(INFO, "Received manifest response: " + manifest);
    rapidjson::Document doc;
    if (doc.Parse(manifest.c_str()).HasParseError()) {
      log.Log(ERROR, "JSON parsing error");
      return -1;
    }

    // fetch config from remote registry
    if (doc.HasMember("config")) {
      const rapidjson::Value &config = doc["config"];
      if (config.HasMember("digest") && config["digest"].IsString()) {
        std::string digest = config["digest"].GetString();
        log.Log(INFO, "Download config: " + digest);
        std::string config_path;
        if (!download_blob(registry, repository, digest, config_path)) {
          log.Log(ERROR, "Failed to download config: " + digest);
          return 1;
        }
      }
    }else {
      log.Log(ERROR,
              "Manifest does not contain 'config' or 'config' is not an object");
      return -1;
    }

    // fetch layers from remote registry
    if (doc.HasMember("layers") && doc["layers"].IsArray()) {
      const rapidjson::Value &layers = doc["layers"];
      for (rapidjson::SizeType i = 0; i < layers.Size(); i++) {
        const rapidjson::Value &layer = layers[i];
        if (layer.HasMember("digest") && layer["digest"].IsString()) {
          std::string digest = layer["digest"].GetString();
          log.Log(INFO, "Download Layer: " + digest);
          std::string layer_path;
          if (!download_blob(registry, repository, digest, layer_path)) {
            log.Log(ERROR, "Failed to download blob: " + digest);
            return -1;
          }
          layers_path.emplace_back(layer_path);
        }
      }
    } else {
      log.Log(ERROR,
              "Manifest does not contain 'layers' or 'layers' is not an array");
      return -1;
    }

    // save manifest to manifest file
    std::string reponame = repository;
    std::replace(reponame.begin(), reponame.end(), '/', '@');
    std::string manifest_filename = "image_data/" + reponame +":"+ tag + ".json";

    std::ofstream file(manifest_filename);
    file << manifest;
    file.close();

  } else {
    log.Log(ERROR, "HTTP GET request failed for URL: " + manifest_url);
    return -1;
  }

  log.Log(INFO, "Image fetch and blob download completed successfully");
  return 0;
}

int obtain_layer_from_manifest(const std::string& path, std::vector<std::string>& layers_path) {
    Logger log;

    const std::string base_path = "image_data/";
    std::ifstream file(base_path + path);
    if (!file.is_open()) {
        log.Log(ERROR, "Failed to open manifest file: " + base_path + path);
        return -1;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    file.close();
    std::string json = ss.str();

    rapidjson::Document document;
    document.Parse(json.c_str());
    if (document.HasParseError()) {
        log.Log(ERROR, "Failed to parse the JSON manifest in file: " + path);
        return -1;
    }

    if (!document.HasMember("layers") || !document["layers"].IsArray() || document["layers"].Empty()) {
        log.Log(WARNING, "No layers found or 'layers' is not an array in file: " + path);
        return -1;
    }

    const rapidjson::Value& layers = document["layers"];
    for (rapidjson::SizeType i = 0; i < layers.Size(); ++i) {
        const rapidjson::Value& layer = layers[i];
        if (layer.HasMember("digest") && layer["digest"].IsString()) {
            std::string digest = layer["digest"].GetString();
            layers_path.emplace_back(base_path + digest.substr(7));
        }
    }

    log.Log(INFO, "Successfully obtained layer paths from manifest: " + path);
    return 0;
}

int lookup_image(const std::string& image, std::vector<std::string>& layers_path) {
    Logger log;
    log.Log(INFO, "Lookup image " + image);
    
    std::ifstream file("image_data/images.json");
    if (!file.is_open()) {
        log.Log(ERROR, "Failed to open images.json file.");
        return -1;
    }
    
    std::ostringstream ss;
    ss << file.rdbuf();
    file.close();
    std::string json = ss.str();

    rapidjson::Document document;
    document.Parse(json.c_str());
    if (document.HasParseError()) {
        log.Log(ERROR, "Failed to parse JSON.");
        return -1;
    }

    if (document.IsArray()) {
        for (const auto& obj : document.GetArray()) {
            for (auto itr = obj.MemberBegin(); itr != obj.MemberEnd(); ++itr) {
                if (itr->name.GetString() == image) {
                    log.Log(INFO, "Found image: " + image + ", File: " + itr->value.GetString());
                    return obtain_layer_from_manifest(itr->value.GetString(), layers_path);
                }
            }
        }
        log.Log(WARNING, "Image not found: " + image);
        return -1;
    } else {
        log.Log(ERROR, "JSON is not an array or failed to parse.");
        return -1;
    }
}
