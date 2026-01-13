#pragma once

template<typename T>
T& TTInstanceOf()
{
    static T instance;
    return instance;
}
