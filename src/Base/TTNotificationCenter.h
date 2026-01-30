#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

class TTNotificationCenter {
public:
    template<typename PayloadType>
    void subscribe(const char* name, void* observer, std::function<void(const PayloadType&)> callback);

    template<typename PayloadType>
    void post(const char* name, const PayloadType& payload);

    void unsubscribeByObserver(void* observer);

private:
    typedef std::function<void(const void*)> Handler;
    std::map<std::string, std::vector<std::pair<void*, Handler>>> _handlers;
};

template<typename PayloadType>
void TTNotificationCenter::subscribe(
    const char* name, void* observer, std::function<void(const PayloadType&)> callback) {
    Handler h = [callback](const void* p) {
        callback(*static_cast<const PayloadType*>(p));
    };
    _handlers[name].push_back(std::make_pair(observer, h));
}

template<typename PayloadType>
void TTNotificationCenter::post(const char* name, const PayloadType& payload) {
    std::map<std::string, std::vector<std::pair<void*, Handler>>>::iterator it = _handlers.find(name);
    if (it == _handlers.end()) return;
    const void* p = &payload;
    for (size_t i = 0; i < it->second.size(); i++) {
        it->second[i].second(p);
    }
}
