#pragma once

#include <Base/Event.h>

#include <Graphics/API.h>
#include <Graphics/Window.h>

#include <xcb/xcb.h>

namespace aer::linux
{

struct Window_t : private std::pair<xcb_connection_t*, xcb_window_t>
{
    auto connection(){ return first; };
    auto window()    { return second; };
};

struct WindowProperties : gfx::WindowProperties
{
    using Base = gfx::WindowProperties;
    using API = gfx::API;

    API  api{   API::Vulkan };
    bool borderless{ false  };
    bool vsync{      false  };
};

class XCBWindow : public gfx::Window
{
public:

                    XCBWindow(     const WindowProperties& props = WindowProperties() );
    Window_t        native()         const            { return _window; }
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
    Window_t            _window;
    WindowProperties    _properties;
};

} // namespace aer::linux