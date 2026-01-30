#include "TTNotificationCenter.h"

void TTNotificationCenter::unsubscribeByObserver(void* observer) {
    for (std::map<std::string, std::vector<std::pair<void*, Handler>>>::iterator it = _handlers.begin();
         it != _handlers.end(); ++it) {
        std::vector<std::pair<void*, Handler>>& vec = it->second;
        for (size_t i = vec.size(); i > 0; i--) {
            if (vec[i - 1].first == observer) {
                vec.erase(vec.begin() + (int)(i - 1));
            }
        }
    }
}
