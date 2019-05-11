#pragma once

#include <iostream>
#include <utility>
#include <vector>
#include <curl/curl.h>
#include "models/models.hpp"
#include "json.hpp"
#include "utils.hpp"

class Client
{
  public:
    Client() : m_Curl(curl_easy_init())
    {
    }

    Client(const Client &) = delete;            // disable copying
    Client &operator=(const Client &) = delete; // disable assignment

    ~Client()
    {
        if (m_Curl != nullptr)
        {
            curl_easy_cleanup(m_Curl);
        }
        m_Curl = nullptr;
    }

    std::vector<Enumerate> enumerate() const
    {
        return perform<std::vector<Enumerate>>("/enumerate").first;
    }

    Session acquire(std::string path, std::string previousSession = "null") const
    {
        return perform<Session>("/acquire/" + path + "/" + previousSession).first;
    }

    Session release(std::string session) const
    {
        return perform<Session>("/release/" + session).first;
    }

    Call call(std::string session, std::string hex) const
    {
        Call response;
        auto result = perform<Error>("/call/" + session, hex.c_str());
        if (result.first.error.empty())
        {
            auto raw = result.second;
            if (raw.size() > 0)
            {
                std::unique_ptr<unsigned char[]> bytes(new unsigned char[raw.length() / 2]);
                hex2bin(raw.c_str(), raw.length(), bytes.get());
                response.from_bytes(bytes.get());
            }
            else
            {
                response.type = INTERNAL_ERROR;
                response.error = "response is empty";
            }
        }
        else
        {
            response.error = result.first.error;
        }

        return response;
    }

  protected:
    template <typename T>
    std::pair<T, std::string> perform(std::string url, const char *body = nullptr) const
    {
        auto result = perform(url, body);
        T response;

        try
        {
            response = nlohmann::json::parse(result).get<T>();
        }
        catch (nlohmann::detail::parse_error e)
        {
            // std::cout << "NOT JSON: " << result << std::endl;
        }
        catch (nlohmann::detail::exception e)
        {
            std::cout << "CATCHED: " << e.what() << std::endl
                      << "WAS: " << result << std::endl;
        }

        return std::make_pair(response, result);
    }

    std::string perform(std::string url, const char *body = nullptr) const
    {
        if (!m_Curl)
            return {};

        std::string buffer;
        struct curl_slist *chunk = NULL;
        chunk = curl_slist_append(chunk, TREZORD_ORIGIN_HEADER);

        curl_easy_setopt(m_Curl, CURLOPT_HTTPHEADER, chunk);
        curl_easy_setopt(m_Curl, CURLOPT_URL, (static_cast<std::string>(TREZORD_HOST) + url).c_str());
        curl_easy_setopt(m_Curl, CURLOPT_POST, 1L);
        curl_easy_setopt(m_Curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(m_Curl, CURLOPT_WRITEDATA, &buffer);

        if (body != nullptr)
        {
            curl_easy_setopt(m_Curl, CURLOPT_POSTFIELDS, body);
        }

        CURLcode res = curl_easy_perform(m_Curl);
        curl_slist_free_all(chunk);

        if (res == CURLE_OK)
            return buffer;

        return {};
    }

  private:
    CURL *m_Curl = nullptr;

    static constexpr const char *TREZORD_HOST = "http://127.0.0.1:21325";
    static constexpr const char *TREZORD_ORIGIN_HEADER = "Origin: https://beam.trezor.io";

    static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        static_cast<std::string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
        return size * nmemb;
    }
};
