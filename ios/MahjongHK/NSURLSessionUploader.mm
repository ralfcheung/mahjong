#include "NSURLSessionUploader.h"
#import <Foundation/Foundation.h>

void NSURLSessionUploader::uploadJson(const std::string& url, const std::string& jsonBody,
                                      std::function<void(bool success, int statusCode, const std::string& response)> callback) {
    @autoreleasepool {
        NSString* urlStr = [NSString stringWithUTF8String:url.c_str()];
        NSURL* nsUrl = [NSURL URLWithString:urlStr];
        if (!nsUrl) {
            if (callback) callback(false, 0, "Invalid URL");
            return;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsUrl];
        request.HTTPMethod = @"POST";
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];

        NSData* bodyData = [NSData dataWithBytes:jsonBody.c_str() length:jsonBody.size()];
        request.HTTPBody = bodyData;

        // Capture callback for async completion
        auto callbackCopy = callback;

        NSURLSession* session = [NSURLSession sharedSession];
        NSURLSessionDataTask* task = [session dataTaskWithRequest:request
            completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                if (error || !response) {
                    if (callbackCopy) {
                        std::string errMsg = error ? [[error localizedDescription] UTF8String] : "No response";
                        callbackCopy(false, 0, errMsg);
                    }
                    return;
                }

                NSHTTPURLResponse* httpResponse = (NSHTTPURLResponse*)response;
                int statusCode = (int)httpResponse.statusCode;

                std::string body;
                if (data) {
                    body = std::string((const char*)data.bytes, data.length);
                }

                bool success = (statusCode >= 200 && statusCode < 300);
                if (callbackCopy) callbackCopy(success, statusCode, body);
            }];
        [task resume];
    }
}
