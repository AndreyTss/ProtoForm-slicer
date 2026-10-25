// Вместо <wx/colordlg.h> для src/slic3r/Utils/ColorSpaceConvert.cpp.
// libslic3r берёт из этого файла интерфейса только RGB2HSV (FlushVolCalc.cpp),
// но файл компилируется целиком, и ему нужен wxColour с четырьмя каналами.
#pragma once

class wxColour
{
public:
    wxColour() = default;
    wxColour(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
        : m_r(r), m_g(g), m_b(b), m_a(a)
    {}

    unsigned char Red() const { return m_r; }
    unsigned char Green() const { return m_g; }
    unsigned char Blue() const { return m_b; }
    unsigned char Alpha() const { return m_a; }

private:
    unsigned char m_r = 0, m_g = 0, m_b = 0, m_a = 255;
};
