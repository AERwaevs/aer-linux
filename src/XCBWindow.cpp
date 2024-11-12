#include <Graphics/XCBWindow.h>

#include <Input/MouseCodes.h>
#include <Input/KeyCodes.h>

#include <Events/WindowEvents.h>
#include <Events/KeyEvents.h>
#include <Events/MouseEvents.h>

#include <cstring>
#include <thread>

namespace aer::xcb
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

struct keyboard_map : Object
{
    using code_mod_pair = std::pair<uint16_t, uint16_t>;
    using code_map      = std::map<code_mod_pair, key::symbol>;
    

    keyboard_map( xcb_connection_t* connection )
    {
        auto setup = xcb_get_setup( connection );
        auto min_keycode = setup->min_keycode;
        auto max_keycode = setup->max_keycode;
        auto cookie = xcb_get_keyboard_mapping( connection, min_keycode, max_keycode - min_keycode + 1 );

        if( auto reply = xcb_get_keyboard_mapping_reply( connection, cookie, nullptr ) )
        {
            auto keysyms = xcb_get_keyboard_mapping_keysyms( reply );
            auto length = xcb_get_keyboard_mapping_keysyms_length( reply );
            auto keysyms_per_keycode = reply->keysyms_per_keycode;

            for( int i = 0; i < length; i += keysyms_per_keycode )
            {
                auto keysym = &keysyms[i];
                auto keycode = min_keycode + i / keysyms_per_keycode;
                for( int j = 0; j < keysyms_per_keycode; ++j )
                {
                    if( keysym[j] != 0 ) _keymap[{ keycode, j }] = keysym[j];
                }
            }
            free( reply );
        } 
    }

    key_symbol symbol( uint16_t keycode, uint16_t modifier = 0 )
    {
        auto itr = _keymap.find( { keycode, modifier } );
        if( itr == _keymap.end() ) return KEY_Undefined;
        
        auto base_key = itr->second;
        if( modifier == 0 ) return static_cast<key_symbol>( base_key );

        uint16_t index = 0;
        bool shift = (modifier & key::MOD_Shift) != 0;
        bool numpad = base_key >= KEY_KP_Space && base_key <= KEY_KP_Divide;

        if( numpad )
        {
            bool numlock = (modifier & key::MOD_NumLock) != 0;
            index = numlock && !shift ? 1 : 0;
        }
        else
        {
            bool capslock = (modifier & key::MOD_CapsLock) != 0;
            index = capslock && shift ? 1 : 0;
        }
        if( index == 0 ) return static_cast<key_symbol>( base_key );
        if( itr = _keymap.find( { keycode, index } ); itr != _keymap.end() ) return static_cast<key_symbol>( itr->second );
        else return KEY_Undefined;
    }

    key::mod mod( key::symbol symbol, uint16_t modifier, bool pressed )
    {
        uint16_t mask{ 0 };
        if( symbol >= KEY_Shift_L && symbol <= KEY_Hyper_R )
        {
            switch( symbol )
            {
                case KEY_Shift_L:
                case KEY_Shift_R:   mask = XCB_KEY_BUT_MASK_SHIFT; break;
                case KEY_Control_L:
                case KEY_Control_R: mask = XCB_KEY_BUT_MASK_CONTROL; break;
                case KEY_Alt_L:
                case KEY_Alt_R:     mask = XCB_KEY_BUT_MASK_MOD_1; break;
                case KEY_Meta_L:
                case KEY_Meta_R:    mask = XCB_KEY_BUT_MASK_MOD_2; break;
                case KEY_Hyper_L:
                case KEY_Hyper_R:   mask = XCB_KEY_BUT_MASK_MOD_3; break;
                case KEY_Super_L:
                case KEY_Super_R:   mask = XCB_KEY_BUT_MASK_MOD_4; break;
                default: break;
            }
        }
        pressed ? modifier |= mask : modifier &= ~mask;
        return key::mod{ modifier };
    }

protected:
    code_map _keymap;
    uint16_t _modmask{ 0XFF };
};

} // namespace aer::xcb

namespace aer
{
using namespace aer::xcb;

enum : uint8_t { SERVER_USER_MASK = 0x80 };

XCBWindow::XCBWindow( const WindowProperties& props )
:   Window( props ),
    _first_xcb_timestamp( 0 ),
    _first_xcb_time_point( clock::now() ),
    _connection( [&] -> xcb_connection_t*
    {
        const auto display = !_properties.display.empty()
                           ? _properties.display.c_str()
                           : nullptr;

        auto connection = props.systemConnection.has_value()
                        ? std::any_cast<xcb_connection_t*>( props.systemConnection )
                        : xcb_connect( display, &_properties.screenNum );

        if( xcb_connection_has_error( connection ) == 0 ) return connection;
        else xcb_disconnect( connection );
        AE_FATAL( "Failed to establish xcb connection" );
    }())
{
//    int screenNum( props.screenNum );
//    const auto display = !_properties.display.empty()
//                       ? _properties.display.c_str()
//                       : nullptr;
//
//    _connection = props.systemConnection.has_value()
//                ? std::any_cast<xcb_connection_t*>( props.systemConnection )
//                : xcb_connect( display, &screenNum );
//
//    if( xcb_connection_has_error( _connection ) )
//    {
//        xcb_disconnect( _connection );
//        AE_FATAL( "Failed to establish xcb connection" );
//    };
    int screenNum( props.screenNum );

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
    static keyboard_map keymap( _connection );

    while( auto event = xcb_poll_for_event( _connection ) )
    {
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
            case XCB_KEY_PRESS:
            {
                auto key_press    = reinterpret_cast<xcb_key_press_event_t*>( event );
                auto key          = keymap.symbol( key_press->detail );
                auto modified_key = keymap.symbol( key_press->detail, key_press->state );
                auto mod          = keymap.mod( key, key_press->state, true );
                _events.emplace_back( new KeyDownEvent( this, key, modified_key, mod ) );
                break;
            }
            case XCB_KEY_RELEASE:
            {
                auto key_release  = reinterpret_cast<xcb_key_release_event_t*>( event );
                auto key          = keymap.symbol( key_release->detail );
                auto modified_key = keymap.symbol( key_release->detail, key_release->state );
                auto mod          = keymap.mod( key, key_release->state, false );
                _events.emplace_back( new KeyUpEvent( this, key, modified_key, mod ) );
                break;
            }
        default:
            AE_WARN( "Unhandled event: %d", static_cast<int>( response_type ) );
            break;
        }

    }

    return aer::Window::PollEvents( events, clear_unhandled );
}

} // namespace aer