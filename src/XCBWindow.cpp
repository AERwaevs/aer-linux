#include <Graphics/XCBWindow.h>
#include <Events/WindowEvents.h>
#include <Events/MouseEvents.h>

#include <cstring>
#include <thread>

namespace aer::gfx
{
    ref_ptr<Window> Window::create( const WindowProperties& props )
    {
        return ref_ptr<linux::XCBWindow>( new linux::XCBWindow( { props } ) );
    }

    template<>
    linux::Window_t Window::native()
    {
        return static_cast<linux::XCBWindow*>( this )->native();
    };

} // namespace aer::gfx

namespace aer::linux::xcb
{

enum atom_size : uint8_t
{
    ATOM_SIZE_8     = 8,
    ATOM_SIZE_16    = 16,
    ATOM_SIZE_32    = 32
};

struct atom_request_t
{
    xcb_connection_t*        _connection = nullptr;
    xcb_intern_atom_cookie_t _cookie{};
    xcb_atom_t               _atom{};
    
    atom_request_t( xcb_connection_t* in_connection, const char* atom_name )
    :   _connection( in_connection ), 
        _cookie( xcb_intern_atom( _connection, false, strlen( atom_name ), atom_name ) )
    {}

    operator xcb_atom_t()
    {
        if( _connection )
        {
            if( auto reply = xcb_intern_atom_reply( _connection, _cookie, nullptr ) )
            {
                _atom = reply->atom;
                free( reply );
            } 
            _connection = nullptr;
        }
        return _atom;
    }
};

struct motif_hints_t
{
    static constexpr uint32_t num_fields = 5;

    uint32_t flags{};
    uint32_t functions{};
    uint32_t decorations{};
    int32_t  input_mode{};
    uint32_t status{};

    enum Flags : uint32_t
    {
        FLAGS_FUNCTIONS     = 0b0001,
        FLAGS_DECORATIONS   = 0b0010,
        FLAGS_INPUT_MODE    = 0b0100,
        FLAGS_STATUS        = 0b1000
    };

    enum Functions : uint32_t
    {
        FUNC_ALL            = 0b000001,
        FUNC_RESIZE         = 0b000010,
        FUNC_MOVE           = 0b000100,
        FUNC_MINIMUMSIZE    = 0b001000,
        FUNC_MAXIMUMSIZE    = 0b010000,
        FUNC_CLOSE          = 0b100000
    };

    enum Decorations : uint32_t
    {
        DECOR_ALL           = 0b0000001,
        DECOR_BORDER        = 0b0000010,
        DECOR_RESIZE        = 0b0000100,
        DECOR_TITLE         = 0b0001000,
        DECOR_MENU          = 0b0010000,
        DECOR_MINIMUMSIZE   = 0b0100000,
        DECOR_MAXIMUMSIZE   = 0b1000000
    };

    static inline auto borderless() { return motif_hints_t{ FLAGS_DECORATIONS }; }
    static inline auto window( bool resize = true, bool move = true, bool close = true, bool minimize = true, bool maximize = true )
    {
        return motif_hints_t
        {
            Flags{ FLAGS_DECORATIONS | FLAGS_FUNCTIONS },
            Functions{
                (resize & FUNC_RESIZE) | (move & FUNC_MOVE) | (close & FUNC_CLOSE) |
                (minimize & FUNC_MINIMUMSIZE) |
                (maximize & FUNC_MAXIMUMSIZE)
            },
            Decorations{ DECOR_ALL }
        };
    }
};

bool getWindowGeometry( xcb_connection_t* connection, xcb_window_t window, int& x, int& y, uint32_t& width, uint32_t& height )
{
    const auto geometry_cookie = xcb_get_geometry( connection, window );
    if( auto geometry_reply = xcb_get_geometry_reply( connection, geometry_cookie, nullptr ) )
    {
        x = geometry_reply->x;
        y = geometry_reply->y;
        width = geometry_reply->width;
        height = geometry_reply->height;

        const auto tree_cookie = xcb_query_tree( connection, window );
        if( auto tree_reply = xcb_query_tree_reply( connection, tree_cookie, nullptr ) )
        {
            const auto trans_cookie = xcb_translate_coordinates( connection, window, tree_reply->parent, x, y );
            if( auto trans_reply = xcb_translate_coordinates_reply( connection, trans_cookie, nullptr ) )
            {
                x = trans_reply->dst_x;
                y = trans_reply->dst_y;
                free( trans_reply );
            }
            free( tree_reply );
        }
        free( geometry_reply );
        return true;
    }
    return false;
}

} // namespace aer::linux::xcb

namespace aer::linux
{

enum : uint8_t { SERVER_USER_MASK = 0x80 };

XCBWindow::XCBWindow( const WindowProperties& props )
:   _properties( props ),
    _first_xcb_timestamp( 0 ),
    _first_xcb_time_point( clock::now() )
{
    using namespace aer::linux::xcb;

    int screenNum( props.screenNum );
    const auto display = !_properties.display.empty()
                       ? _properties.display.c_str()
                       : nullptr;

    _connection = props.systemConnection.has_value()
                ? std::any_cast<xcb_connection_t*>( props.systemConnection )
                : xcb_connect( display, &screenNum );

    if( xcb_connection_has_error( _connection ) )
    {
        xcb_disconnect( _connection );
        AE_FATAL( "Failed to establish xcb connection" );
    };

    _wmProtocols = atom_request_t( _connection, "WM_PROTOCOLS" );
    _wmDeleteWindow = atom_request_t( _connection, "WM_DELETE_WINDOW" );

    _window = props.nativeWindow.has_value()
            ? std::any_cast<xcb_window_t>( props.nativeWindow )
            : xcb_generate_id( _connection );

    const auto setup = xcb_get_setup( _connection );
    const auto screenCount = xcb_setup_roots_length( setup );

    AE_WARN_IF( props.screenNum >= screenCount, "Requested screen %d, only %d screens available", props.screenNum, screenCount );
    screenNum = props.screenNum < screenCount ? screenNum : 0;
    
    auto screen_iterator = xcb_setup_roots_iterator( setup );
    for( int i = 0; i < screenNum; ++i ) xcb_screen_next( &screen_iterator );
    _screen = screen_iterator.data;

    const xcb_window_t   parent         = _screen->root;
    const uint8_t        depth          = XCB_COPY_FROM_PARENT;
    const xcb_visualid_t visual         = XCB_COPY_FROM_PARENT;
    const uint16_t       border_width   = 0;
    const uint16_t       window_class   = XCB_WINDOW_CLASS_INPUT_OUTPUT;
    const uint32_t       value_mask     = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_BIT_GRAVITY | XCB_CW_OVERRIDE_REDIRECT;
    const uint32_t       event_mask     = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
                                        | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE
                                        | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
                                        | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
    const uint32_t       value_list[]   = { _screen->black_pixel, XCB_GRAVITY_NORTH_WEST, 0, event_mask };

    xcb_create_window
    ( 
        _connection, XCB_COPY_FROM_PARENT, _window, _screen->root,
        props.fullscreen ? 0 : props.posx,
        props.fullscreen ? 0 : props.posy,
        props.fullscreen ? _screen->width_in_pixels : props.width,
        props.fullscreen ? _screen->height_in_pixels : props.height,
        border_width, XCB_WINDOW_CLASS_INPUT_OUTPUT, _screen->root_visual, value_mask, value_list
    );

    const auto atom_request = [&]( const char* atom_name ) { return atom_request_t( _connection, atom_name ); };
    const auto change_property = [&]( xcb_atom_t atom, xcb_atom_enum_t type, uint8_t format, uint32_t data_len, const void* data )
    {
        return xcb_change_property( _connection, XCB_PROP_MODE_REPLACE, _window, atom, type, format, data_len, data );
    };

    // window class, title, protocols
    change_property( XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, ATOM_SIZE_8, props.windowClass.size(), props.windowClass.c_str() );
    change_property( XCB_ATOM_WM_NAME, XCB_ATOM_STRING, ATOM_SIZE_8, props.name.size(), props.name.c_str() );
    change_property( _wmProtocols, XCB_ATOM_ATOM, ATOM_SIZE_32, 1, &_wmDeleteWindow );

    // window manager hints
    if( props.fullscreen )
    {
        xcb_atom_t atoms[]{ atom_request( "_NET_WM_STATE_FULLSCREEN" ) };
        change_property( atom_request( "_NET_WM_STATE" ), XCB_ATOM_ATOM, ATOM_SIZE_32, 1, atoms );
    }

    // window decorations
    auto hints = props.borderless ? motif_hints_t::borderless() : motif_hints_t::window();
    change_property( atom_request( "_MOTIF_WM_HINTS" ), XCB_ATOM_WM_HINTS, ATOM_SIZE_32, motif_hints_t::num_fields, &hints );

    while( auto event = xcb_wait_for_event( _connection ) )
    {
        if( _first_xcb_timestamp != 0 ) { free( event ); break; }

        if( ( event->response_type & ~SERVER_USER_MASK ) == XCB_PROPERTY_NOTIFY )
        {
            auto property_notify    = reinterpret_cast<xcb_property_notify_event_t*>( event );
            _first_xcb_timestamp    = property_notify->time;
            _first_xcb_time_point   = clock::now();
        }
        free( event );
    }

    xcb_map_window( _connection, _window );
    xcb_flush( _connection );

    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );

    if( auto geometry_reply = xcb_get_geometry_reply( _connection, xcb_get_geometry( _connection, _window ), nullptr ) )
    {
        _properties.posx = geometry_reply->x;
        _properties.posy = geometry_reply->y;
        _properties.width = geometry_reply->width;
        _properties.height = geometry_reply->height;
        free( geometry_reply );
    }
}

XCBWindow::~XCBWindow()
{
    if( _connection )
    {
        if( _window != 0 ) xcb_destroy_window( _connection, _window );
        xcb_flush( _connection );
        xcb_disconnect( _connection );
    }
}
    
bool XCBWindow::PollEvents( Events& events, bool clear_unhandled )
{
    xcb_generic_event_t* event = nullptr;
    int i = 0;
    while( ( event = xcb_poll_for_event( _connection ) ) )
    {
        ++i;
        switch( auto response_type = event->response_type & ~SERVER_USER_MASK )
        {
            //-----------------------------------------------------------------------------------//
            //                                   WINDOW                                          //
            //-----------------------------------------------------------------------------------//
            case XCB_DESTROY_NOTIFY: { _events.emplace_back( new WindowCloseEvent( this ) ); break; }
            case XCB_CLIENT_MESSAGE:
            {
                auto client_message = reinterpret_cast<xcb_client_message_event_t*>( event );
                if( client_message->data.data32[0] == _wmDeleteWindow )
                {
                    _events.emplace_back( new WindowCloseEvent( this ) );
                }
                break;
            }
            case XCB_CONFIGURE_NOTIFY:
            {
                auto configure = reinterpret_cast<xcb_configure_notify_event_t*>( event );
                int32_t x = configure->x, 
                        y = configure->y;

                uint32_t width = configure->width, 
                         height = configure->height;

                xcb::getWindowGeometry( _connection, _window, x, y, width, height );

                bool previousResizeEventIsEqual = false;
                for( auto prev : events ) if( auto wre = dynamic_cast<WindowResizeEvent*>( prev.get() ) )
                {
                    previousResizeEventIsEqual = ( wre->width() == width && wre->height() == height );
                }

                if( !previousResizeEventIsEqual )
                {
                    _events.emplace_back( new WindowResizeEvent( this, width, height ) );
                    _properties.width = width;
                    _properties.height = height;
                }

                break;
            }
            case XCB_MOTION_NOTIFY:
            {
                auto motion = reinterpret_cast<xcb_motion_notify_event_t*>( event );
                if( motion->same_screen ) _events.emplace_back( new MouseMoveEvent( this, motion->event_x, motion->event_y ) );
                break;
            }
            case XCB_FOCUS_IN: { _events.emplace_back( new WindowFocusEvent( this ) ); break; }
            case XCB_FOCUS_OUT: { _events.emplace_back( new WindowUnfocusEvent( this ) ); break; }
        default:
            AE_WARN( "Unhandled event: %d", static_cast<int>( response_type ) );
            break;
        }

    }

    return aer::gfx::Window::PollEvents( events, clear_unhandled );
}

} // namespace aer::linux