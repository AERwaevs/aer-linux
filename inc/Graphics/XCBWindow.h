#pragma once

#include <Base/Event.h>
#include <Graphics/Window.h>

#include <xcb/xcb.h>

namespace aer::linux
{

class XCBWindow : public Window
{
    using clock = std::chrono::steady_clock;
public:
                    XCBWindow( const WindowProperties& = WindowProperties() );
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
};

} // namespace aer::linux

namespace aer
{
    ref_ptr<Window> createWindow( const WindowProperties& props )
    {
        return ref_ptr<Window>( new linux::XCBWindow( { props } ) );
    }

} // namespace aer
