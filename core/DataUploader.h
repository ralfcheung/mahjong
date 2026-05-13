#pragma once
#include <string>
#include <functional>

class DataUploader {
public:
    virtual ~DataUploader() = default;
    virtual void uploadJson(const std::string& url, const std::string& jsonBody,
                           std::function<void(bool success, int statusCode, const std::string& response)> callback) = 0;
};
