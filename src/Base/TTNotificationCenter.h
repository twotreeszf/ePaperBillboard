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
    void sendNotification(const char* name, const PayloadType& payload);

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
void TTNotificationCenter::sendNotification(const char* name, const PayloadType& payload) {
    std::map<std::string, std::vector<std::pair<void*, Handler>>>::iterator it = _handlers.find(name);
    if (it == _handlers.end()) return;
    std::vector<std::pair<void*, Handler>> snapshot = it->second;
    const void* p = &payload;
    for (size_t i = 0; i < snapshot.size(); i++) {
        it = _handlers.find(name);
        if (it == _handlers.end()) {
            return;
        }
        bool still = false;
        for (size_t j = 0; j < it->second.size(); j++) {
            if (it->second[j].first == snapshot[i].first) {
                still = true;
                break;
            }
        }
        if (still) {
            snapshot[i].second(p);
        }
    }
}
