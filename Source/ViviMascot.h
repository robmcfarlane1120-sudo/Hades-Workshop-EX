#pragma once
#include <wx/scrolwin.h>
#include <wx/weakref.h>
#include <wx/timer.h>
#include <vector>
#include <windows.h>

// One native alpha overlay outside every scrolling ancestor. No wx paint DC,
// background rectangle, sizer, or copied backing-store pixels are involved.
class ViviViewportMascot : public wxEvtHandler {
public:
    ViviViewportMascot(wxWindow* root, const wxImage& image, wxWindow* excluded, wxWindow* fieldPage, wxWindow* fieldPreview)
        : m_root(root), m_fieldPage(fieldPage), m_fieldPreview(fieldPreview), m_image(image), m_timer(this) {
        Collect(root, excluded);
        m_overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            L"STATIC", L"HWEX_ViviMascot", WS_CHILD | WS_DISABLED,
            0, 0, 0, 0, (HWND)root->GetHandle(), nullptr, GetModuleHandleW(nullptr), nullptr);
        root->Bind(wxEVT_DESTROY, &ViviViewportMascot::OnRootDestroy, this);
        Bind(wxEVT_TIMER, &ViviViewportMascot::OnTick, this);
        m_timer.Start(30);
    }
    ~ViviViewportMascot() override {
        m_timer.Stop();
        if (m_overlay && IsWindow(m_overlay)) DestroyWindow(m_overlay);
    }
private:
    void Collect(wxWindow* window, wxWindow* excluded) {
        if (window == excluded) return;
        auto* scroll = wxDynamicCast(window, wxScrolledWindow);
        if (scroll) {
            if (scroll->GetSizer()) m_pages.emplace_back(scroll);
            return;
        }
        for (auto* child : window->GetChildren()) Collect(child, excluded);
    }
    bool UploadBitmap(int height) {
        const int width = wxMax(1, height * m_image.GetWidth() / m_image.GetHeight());
        const wxImage scaled = m_image.Scale(width, height, wxIMAGE_QUALITY_HIGH);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr);
        HDC memory = CreateCompatibleDC(screen);
        void* pixels = nullptr;
        HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!memory || !dib) {
            if (dib) DeleteObject(dib);
            if (memory) DeleteDC(memory);
            ReleaseDC(nullptr, screen);
            return false;
        }
        const unsigned char* rgb = scaled.GetData();
        const unsigned char* alpha = scaled.HasAlpha() ? scaled.GetAlpha() : nullptr;
        auto* bgra = static_cast<unsigned char*>(pixels);
        for (int i = 0; i < width * height; ++i) {
            const unsigned int a = alpha ? alpha[i] : 255;
            bgra[4*i] = (rgb[3*i+2] * a + 127) / 255;
            bgra[4*i+1] = (rgb[3*i+1] * a + 127) / 255;
            bgra[4*i+2] = (rgb[3*i] * a + 127) / 255;
            bgra[4*i+3] = a;
        }
        HGDIOBJ previous = SelectObject(memory, dib);
        POINT source = {};
        SIZE size = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const bool ok = UpdateLayeredWindow(m_overlay, screen, nullptr, &size,
            memory, &source, 0, &blend, ULW_ALPHA) != FALSE;
        SelectObject(memory, previous);
        DeleteObject(dib);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        if (ok) m_size = wxSize(width, height);
        return ok;
    }
    void OnTick(wxTimerEvent&) {
        auto* root = m_root.get();
        if (!root || !m_overlay) return;
        wxScrolledWindow* selected = nullptr;
        wxRect viewport;
        const wxRect rootBounds(root->ClientToScreen(wxPoint(0, 0)), root->GetClientSize());
        if (root->IsShownOnScreen()) {
            for (const auto& reference : m_pages) {
                auto* page = reference.get();
                if (!page || !page->IsShownOnScreen()) continue;
                wxRect bounds(page->ClientToScreen(wxPoint(0, 0)), page->GetClientSize());
                bounds.Intersect(rootBounds);
                // Stay out of narrow startup layouts and selector panes.
                if (bounds.width < root->FromDIP(420) || bounds.height < root->FromDIP(240)) continue;
                if (!selected || bounds.GetRight() > viewport.GetRight() ||
                    (bounds.GetRight() == viewport.GetRight() && bounds.width > viewport.width)) {
                    selected = page;
                    viewport = bounds;
                }
            }
        }
        if (!selected) { ShowWindow(m_overlay, SW_HIDE); return; }
        // Fields alone: keep Vivi above the preview border, clear of the controls below.
        if (selected == m_fieldPage.get()) {
            auto* preview = m_fieldPreview.get();
            if (!preview || !preview->IsShownOnScreen()) { ShowWindow(m_overlay, SW_HIDE); return; }
            wxRect previewBounds(preview->ClientToScreen(wxPoint(0, 0)), preview->GetClientSize());
            viewport.Intersect(previewBounds);
        }
        const int height = root->FromDIP(180);
        if (height != m_size.y && !UploadBitmap(height)) { ShowWindow(m_overlay, SW_HIDE); return; }
        const int margin = root->FromDIP(12);
        if (selected == m_fieldPage.get() && (viewport.width < m_size.x + 2 * margin || viewport.height < m_size.y + 2 * margin)) {
            ShowWindow(m_overlay, SW_HIDE); return;
        }
        const wxPoint position = root->ScreenToClient(wxPoint(
            viewport.x + viewport.width - m_size.x - margin,
            viewport.y + viewport.height - m_size.y - margin));
        if (position != m_position || !IsWindowVisible(m_overlay)) {
            SetWindowPos(m_overlay, HWND_TOP, position.x, position.y, m_size.x, m_size.y,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
            m_position = position;
        }
    }
    void OnRootDestroy(wxWindowDestroyEvent& event) {
        event.Skip();
        if (event.GetEventObject() == m_root.get()) delete this;
    }
    wxWeakRef<wxWindow> m_root;
    wxWeakRef<wxWindow> m_fieldPage;
    wxWeakRef<wxWindow> m_fieldPreview;
    wxImage m_image;
    wxTimer m_timer;
    std::vector<wxWeakRef<wxScrolledWindow>> m_pages;
    HWND m_overlay = nullptr;
    wxSize m_size;
    wxPoint m_position = wxPoint(-1, -1);
};
inline void AddViviMascots(wxWindow* window, const wxImage& image, wxWindow* excluded, wxWindow* fieldPage, wxWindow* fieldPreview) {
    new ViviViewportMascot(window, image, excluded, fieldPage, fieldPreview);
}