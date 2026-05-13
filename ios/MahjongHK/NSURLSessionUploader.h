#pragma once
#include "DataUploader.h"

class NSURLSessionUploader : public DataUploader {
public:
    void uploadJson(const std::string& url, const std::string& jsonBody,
                   std::function<void(bool success, int statusCode, const std::string& response)> callback) override;
};
