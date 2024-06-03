#pragma once

#include <string>
#include <vector>

///@brief fetch image from registry and return the image layer path
///@param image the image name
///@param layer_path 
///@return 0 if success
int fetch_image(std::string image, std::vector<std::string>& layer_path);

int obtain_layer_from_manifest(const std::string& path, std::vector<std::string>& layers_path);

int lookup_image(const std::string& image, std::vector<std::string>& layers_path);

