// Объявления OCCT, без которых не компилируется Format/STEP.hpp (его
// подключает Model.hpp). Только типы и сигнатуры: чтение STEP в этой сборке
// заменено заглушкой stubs/Format/STEP.cpp, и ни один метод здесь
// по-настоящему не вызывается.
#pragma once

#include <memory>

namespace opencascade {
template<typename T>
class handle
{
public:
    handle() = default;
    T *get() const { return m_ptr.get(); }
    T *operator->() const { return m_ptr.get(); }
    bool IsNull() const { return !m_ptr; }

private:
    std::shared_ptr<T> m_ptr;
};
} // namespace opencascade

#ifndef Handle
#define Handle(Class) opencascade::handle<Class>
#endif

typedef int    Standard_Integer;
typedef double Standard_Real;
typedef bool   Standard_Boolean;

class TopoDS_Shape {};
class TDocStd_Document {};
class XCAFDoc_ShapeTool {};

class XCAFApp_Application
{
public:
    static Handle(XCAFApp_Application) GetApplication() { return {}; }
};

class Message_ProgressScope {};

class Message_ProgressIndicator
{
public:
    virtual ~Message_ProgressIndicator() = default;
    virtual bool UserBreak() { return false; }
    virtual void Show(const Message_ProgressScope &, const bool) {}
    double GetPosition() const { return 0.0; }
};
