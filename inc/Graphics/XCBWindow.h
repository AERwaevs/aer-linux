#pragma once

#include <Base/Event.h>

#include <Graphics/API.h>
#include <Graphics/Window.h>

#include <xcb/xcb.h>

namespace aer::linux
{

struct Window_t : private std::tuple<xcb_connection_t*, xcb_screen_t*, xcb_window_t>
{
    using underlying_t = std::tuple<xcb_connection_t*, xcb_screen_t*, xcb_window_t>;

    Window_t( xcb_connection_t* in_connection, xcb_screen_t* in_screen, xcb_window_t in_window )
    :   underlying_t( in_connection, in_screen, in_window )
    {};

    auto connection() const { return std::get<0>(*this); };
    auto screen()     const { return std::get<1>(*this); };
    auto window()     const { return std::get<2>(*this); };
};

struct WindowProperties : gfx::WindowProperties
{
    using Base = gfx::WindowProperties;
    using API = gfx::API;

    API         api{   API::Vulkan };
    bool        borderless{ false  };
    bool        vsync{      false  };
    int         screenNum{  -1     };
    std::string display;
    std::string windowClass;
};

class XCBWindow : public gfx::Window
{
    using clock = std::chrono::steady_clock;
public:
                    XCBWindow(     const WindowProperties& props = WindowProperties() );
    Window_t        native()         const            { return { _connection, _screen, _window }; }
    bool            borderless()     const            { return _properties.borderless; }
    bool            vsync()          const            { return _properties.vsync; }
    std::string     name()           const   override { return _properties.name; }
    uint32_t        width()          const   override { return _properties.width; }
    uint32_t        height()         const   override { return _properties.height; }
    bool            minimized()      const   override { return _properties.minimized; }

    void SetName( const std::string& name )  override { _properties.name = name; }
    void SetVsync( bool enabled )                     { _properties.vsync = enabled; }

    bool            PollEvents( Events& events_list, bool clear_unhandled = true ) override;
protected:
    virtual         ~XCBWindow();
protected:
    xcb_connection_t*   _connection = nullptr;
    xcb_screen_t*       _screen     = nullptr;
    xcb_window_t        _window{};
    xcb_atom_t          _wmProtocols{};
    xcb_atom_t          _wmDeleteWindow{};
    xcb_timestamp_t     _first_xcb_timestamp = 0;
    clock::time_point   _first_xcb_time_point;
    WindowProperties    _properties{};
};

} // namespace aer::linux