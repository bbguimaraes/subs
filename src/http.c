#include "http.h"

#include <string.h>

#include <curl/curl.h>

#include "buffer.h"
#include "log.h"

static size_t write_function(char *p, size_t size, size_t n, void *data) {
    n *= size;
    buffer_append(data, p, n);
    return n;
}

static CURL *init_curl(const char *url, struct buffer *buffer, bool verbose) {
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "machinatrix");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_function);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
    if(verbose)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    return curl;
}

static bool request(
    CURL *curl, const char *url, struct buffer *buffer, bool verbose)
{
    if(!curl)
        curl = init_curl(url, buffer, verbose);
    char err[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err);
    if(verbose)
        printf("Request: GET %s\n", url);
    const CURLcode ret = curl_easy_perform(curl);
    if(ret != CURLE_OK)
        log_err("%d: %s: %s\n", ret, err, *err ? err : curl_easy_strerror(ret));
    else if(verbose)
        printf("Response:\n%s\n", (const char*)buffer->p);
    curl_easy_cleanup(curl);
    return ret == CURLE_OK;
}

bool http_get(void *p, const char *url, struct buffer *buffer) {
    struct http_client *const http = p;
    return request(NULL, url, buffer, http->flags & HTTP_VERBOSE);
}

bool http_post(
    void *p, const char *url, const char *post_data, struct buffer *buffer)
{
    struct http_client *const http = p;
    const bool verbose = http->flags & HTTP_VERBOSE;
    CURL *const curl = init_curl(url, buffer, verbose);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, /*XXX*/strlen(post_data));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    return request(curl, url, buffer, false);
}
